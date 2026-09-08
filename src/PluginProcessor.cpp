#include "PluginProcessor.h"

#include "PluginEditor.h"

using namespace juce;

namespace
{
const StringArray kOsChoices { "1x", "2x", "4x", "8x" };
constexpr double kBypassRampSeconds = 0.005;
} // namespace

AudioProcessorValueTreeState::ParameterLayout PedalAudioProcessor::createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    // The only pot control on this pedal (circuit.md): 500 kA VOLUME, taper applied in DSP.
    layout.add(std::make_unique<AudioParameterFloat>("volume", "Volume", NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // 3-position ON-OFF-ON MODE switch: source-bypass network (circuit.md stage 2b). Ordered by
    // physical lever position (up/middle/down), not by brightness -- see circuit.md note #2.
    layout.add(std::make_unique<AudioParameterChoice>("mode", "Mode", StringArray { "Bright", "Dark", "Mid" }, 1));

    // Trims, in dB, distinct from the pedal controls.
    layout.add(std::make_unique<AudioParameterFloat>("input_trim",  "Input Trim",  NormalisableRange<float>(-12.0f, 12.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>("output_trim", "Output Trim", NormalisableRange<float>(-12.0f, 12.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterBool>("trim_link", "Trim Link", false));

    // Oversampling: realtime + offline-render factors.
    layout.add(std::make_unique<AudioParameterChoice>("oversampling",        "Oversampling",        kOsChoices, 2)); // 4x
    layout.add(std::make_unique<AudioParameterChoice>("render_oversampling", "Render Oversampling", kOsChoices, 3)); // 8x

    // NO `hq` PARAMETER, deliberately. The template's HQ toggle gated the diode solve's omega
    // approximation -- and this pedal has no diodes, no omega solver, and nothing else whose cost
    // and accuracy trade off against each other today. dsp.md is explicit: don't add an HQ button
    // reflexively, let FeatureProfile decide. Removing it now is free; after release it would break
    // saved sessions. The editor's toggle is param-guarded, so it simply never appears.
    // Revisit at build step 6: ADAA is the one plausible lever this pedal may actually acquire.

    layout.add(std::make_unique<AudioParameterBool>("bypass", "Bypass", false));

    return layout;
}

PedalAudioProcessor::PedalAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::stereo(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
{
    pVolume             = apvts.getRawParameterValue("volume");
    pMode               = apvts.getRawParameterValue("mode");
    pInputTrim          = apvts.getRawParameterValue("input_trim");
    pOutputTrim         = apvts.getRawParameterValue("output_trim");
    pOversampling       = apvts.getRawParameterValue("oversampling");
    pRenderOversampling = apvts.getRawParameterValue("render_oversampling");
    pBypass             = apvts.getRawParameterValue("bypass");
    pTrimLink           = apvts.getRawParameterValue("trim_link");

    inputTrimParam  = dynamic_cast<AudioParameterFloat*>(apvts.getParameter("input_trim"));
    outputTrimParam = dynamic_cast<AudioParameterFloat*>(apvts.getParameter("output_trim"));
    lastInputTrim  = inputTrimParam != nullptr ? inputTrimParam->get() : 0.0f;
    lastOutputTrim = outputTrimParam != nullptr ? outputTrimParam->get() : 0.0f;

    apvts.addParameterListener("input_trim", this);
    apvts.addParameterListener("output_trim", this);
}

PedalAudioProcessor::~PedalAudioProcessor()
{
    apvts.removeParameterListener("input_trim", this);
    apvts.removeParameterListener("output_trim", this);
}

void PedalAudioProcessor::parameterChanged(const String& parameterID, float newValue)
{
    const bool isInput = (parameterID == "input_trim");
    if (! isInput && parameterID != "output_trim")
        return;

    // The mirrored write re-enters here; let it through only as far as the bookkeeping below.
    if (isSyncingTrim.load())
        return;

    // Track the last value even while the link is DISENGAGED, so that engaging it mid-session
    // measures the next move against where the knob actually is rather than a stale value.
    float& last = isInput ? lastInputTrim : lastOutputTrim;
    const float delta = newValue - last;
    last = newValue;

    if (pTrimLink == nullptr || pTrimLink->load() <= 0.5f)
        return;

    auto* other = isInput ? outputTrimParam : inputTrimParam;
    if (other == nullptr)
        return;

    // Clamping at the compensating side's stop means the two can drift out of exact mirror symmetry
    // at the extremes. That is expected -- a real compensating pair does the same once one side
    // bottoms out -- not a bug to chase.
    const float target = jlimit(-12.0f, 12.0f, other->get() - delta);

    isSyncingTrim = true;
    other->setValueNotifyingHost(other->convertTo0to1(target));
    (isInput ? lastOutputTrim : lastInputTrim) = target;
    isSyncingTrim = false;
}

void PedalAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    scratch.setSize(2, samplesPerBlock);

    // One oversampler per factor, all prepared here. juce::dsp::Oversampling fixes its factor at
    // construction, so switching by rebuilding would allocate on the audio thread.
    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        // LINEAR-PHASE FIR, deliberately, not the cheaper polyphase IIR. dsp.md treats the IIR as an
        // optimisation to be justified by a measured cost, and there is a specific reason not to
        // reach for it here: the IIR is non-linear-phase, and step 9 validates this model with a
        // SUB-SAMPLE NULL against the reference renders. Phase smeared by the resampler is
        // indistinguishable from phase the circuit model got wrong, so it would corrupt the very
        // measurement the whole phase-testing pass exists to protect. Revisit only if PerfBenchmark
        // shows the FIR is a real cost (build step 6).
        oversamplers[i] = std::make_unique<dsp::Oversampling<double>>(
            2, (size_t) i, dsp::Oversampling<double>::filterHalfBandFIREquiripple, true, false); // [PROBE]
        oversamplers[i]->initProcessing((size_t) samplesPerBlock);
        oversamplers[i]->reset();
    }

    currentOsIndex = -1;
    updateOversamplingFactor(pOversampling != nullptr ? (int) pOversampling->load() : 2);

    volumeSmooth.reset(sampleRate, 0.02);
    volumeSmooth.setCurrentAndTargetValue(pVolume != nullptr ? (double) pVolume->load() : 0.5);
    bypassMix.reset(sampleRate, kBypassRampSeconds);
    bypassMix.setCurrentAndTargetValue((pBypass != nullptr && pBypass->load() > 0.5f) ? 1.0 : 0.0);

    // updateOversamplingFactor() above has just reset the chain, so it is already in the state the
    // skip path resets it to -- but the flag must not survive a re-prepare, or a processor prepared
    // while bypassed would never take the reset branch again after leaving and re-entering bypass.
    dspIsIdle = false;

    for (auto& l : inputLevel)  l.store(0.0f);
    for (auto& l : outputLevel) l.store(0.0f);
}

void PedalAudioProcessor::updateOversamplingFactor(int factorIndex)
{
    factorIndex = jlimit(0, (int) oversamplers.size() - 1, factorIndex);
    if (factorIndex == currentOsIndex)
        return;

    currentOsIndex = factorIndex;
    auto& os = *oversamplers[(size_t) factorIndex];
    os.reset();

    // The stages straddle the oversampling boundary, so only the oversampled half is re-prepared
    // against the new rate; the output network always runs at base rate.
    const double osRate = baseSampleRate * (double) (1 << factorIndex);
    for (auto& d : dsp)
    {
        d.prepare(baseSampleRate, osRate);
        d.reset();
    }

    applyAdaaPolicy();

    // ROUND, don't truncate. The equiripple FIR's latency is fractional (about 65.9 samples at 8x),
    // and truncating threw away nearly a whole sample of it -- which the host's delay compensation
    // then never puts back, and which showed up directly as a 1-sample lag in the OfflineRender
    // output against the source. Rounding halves the worst-case residual to 0.5 samples; the rest is
    // sub-sample and is what analyze.py's frac_align exists to remove.
    setLatencySamples(roundToInt(os.getLatencyInSamples()));
}

void PedalAudioProcessor::applyAdaaPolicy()
{
    adaaActive = (adaaOverride == AdaaOverride::useOsGate) ? (currentOsIndex <= kAdaaMaxOsIndex)
                                                           : (adaaOverride == AdaaOverride::forceOn);
    for (auto& d : dsp)
        d.setAdaa(adaaActive);
}

void PedalAudioProcessor::setAdaaOverride(AdaaOverride m)
{
    adaaOverride = m;
    applyAdaaPolicy();
}

bool PedalAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PedalAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numSmp = buffer.getNumSamples();
    const int numCh = jmin(buffer.getNumChannels(), 2);

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSmp);

    // Offline bounces get their own (higher) factor automatically -- no extra UI action needed.
    const int wantOs = isNonRealtime() ? (pRenderOversampling != nullptr ? (int) pRenderOversampling->load() : 3)
                                       : (pOversampling != nullptr ? (int) pOversampling->load() : 2);
    updateOversamplingFactor(wantOs);

    volumeSmooth.setTargetValue((double) pVolume->load());
    const double volume = volumeSmooth.skip(numSmp);
    bypassMix.setTargetValue(pBypass->load() > 0.5f ? 1.0 : 0.0);

    // ===================== FULLY BYPASSED: skip the DSP (architecture.md) =====================
    // The gate is "the crossfade has FINISHED", not "the bypass parameter is set": during the ~5 ms
    // transition both paths are genuinely needed, and skipping on the parameter alone would replace
    // the fade with a hard cut. Hence mix parked at its 1.0 target with no ramp outstanding.
    if (! bypassMix.isSmoothing() && bypassMix.getCurrentValue() >= 1.0)
    {
        // ---- RESETTING THE OVERSAMPLER IS NOT OPTIONAL. It stops being fed here, so its FIR delay
        // line still holds pre-bypass audio; re-engaging without clearing it splices that stale tail
        // onto the live signal, and the splice lands at about the reported latency after the toggle.
        // Measured (BypassClickTest, oversampler left unreset): the worst sample-to-sample step
        // reaches 1.09x / 1.54x / 1.63x the crossfade's own bound at 2x / 4x / 8x -- an audible cut
        // inside a fade that is otherwise doing its job. It is within bound at 1x only because a 1x
        // "oversampler" has no filter state to go stale. This is a click the fade cannot cover.
        //
        // ---- RESETTING THE CHAIN'S OWN STATE IS A JUDGEMENT CALL, AND THE MEASUREMENT DID NOT MAKE
        // IT. While skipped the WDF caps and the JFET shelf's x1/y1 freeze, so re-engaging either
        // resumes from that stale state or starts from a cleared one. Neither is what the hardware
        // does: a real pedal keeps its input connected and its capacitors keep tracking, which is
        // precisely the work being removed here. Both were measured, over four factors x eight
        // toggle phases, with the bypass HOLD LENGTH swept as well as the toggle instants -- a fixed
        // hold of 0.2 s is exactly 200 periods of the 1 kHz probe tone and hands a resumed state a
        // perfect phase match it would never get in use. Peak re-engage excursion, FS:
        //
        //        factor    reset    resume
        //          1x     0.0148    0.0187
        //          2x     0.0167    0.0154
        //          4x     0.0174    0.0129
        //          8x     0.0176    0.0143
        //
        // That is a wash -- neither leads consistently, and the spread is scatter between phase
        // draws, not a difference between the two policies. The reason is the paragraph above: with
        // the oversampler cleared, its FIR ramps the chain's input up from zero over its own
        // impulse response instead of stepping it, so the high-passes are barely kicked either way
        // and the frozen charge has little left to matter. Do not re-derive this expecting the
        // clean-vs-stale argument to show up in the audio; it does not.
        //
        // RESET, then, chosen on DETERMINISM rather than on sound: with a cleared chain the output
        // after re-engaging depends only on the input and the parameters, not on how long ago the
        // pedal was switched off or what was playing at the time. That matters concretely here --
        // OfflineRender drives this same processor, and step 9 validates the model with a sub-sample
        // null against the reference renders. A chain carrying unrecorded history into a render is a
        // nondeterminism in the instrument the remaining calibration constants are read off.
        if (! dspIsIdle)
        {
            for (auto& d : dsp)
                d.reset();
            oversamplers[(size_t) currentOsIndex]->reset();
            dspIsIdle = true;
        }

        // Meters keep running, or the VU bars freeze whenever the pedal is off. Both read the DRY
        // signal: it is what leaves the plugin, and the input trim is genuinely out of circuit here,
        // so metering the trimmed level would show a control that is doing nothing.
        for (int ch = 0; ch < numCh; ++ch)
        {
            const auto* dry = buffer.getReadPointer(ch);
            float peak = 0.0f;
            for (int n = 0; n < numSmp; ++n)
                peak = jmax(peak, std::abs(dry[n]));
            inputLevel[(size_t) ch].store(peak);
            outputLevel[(size_t) ch].store(peak);
        }

        // buffer already holds the dry signal untouched -- true bypass needs no copy.
        return;
    }
    dspIsIdle = false;

    const auto mode = (pedal::dsp::Mode) jlimit(0, 2, (int) pMode->load());
    for (auto& d : dsp)
    {
        d.setMode(mode);
        d.setVolume(volume);
    }

    const double inTrim = Decibels::decibelsToGain((double) pInputTrim->load());
    const double outTrim = Decibels::decibelsToGain((double) pOutputTrim->load());

    // Volts back to full scale, plus the (still uncalibrated) makeup. VOLUME is NOT here: it lives
    // inside the output network's solve, which is what makes its control law non-monotonic.
    const double outputGain = kOutputMakeup * outTrim / kInputRef;

    scratch.setSize(2, numSmp, false, false, true);

    // Input trim into the DAW-domain wet path, meter it, then scale to real volts for the circuit.
    // The float buffer is left untouched so it still holds the dry signal for the bypass crossfade.
    for (int ch = 0; ch < numCh; ++ch)
    {
        const auto* src = buffer.getReadPointer(ch);
        auto* dst = scratch.getWritePointer(ch);
        float inPeak = 0.0f;
        for (int n = 0; n < numSmp; ++n)
        {
            const double wet = (double) src[n] * inTrim;
            inPeak = jmax(inPeak, (float) std::abs(wet));
            dst[n] = wet * kInputRef;
        }
        inputLevel[(size_t) ch].store(inPeak);
    }
    if (numCh == 1)
        scratch.clear(1, 0, numSmp);

    dsp::AudioBlock<double> block(scratch.getArrayOfWritePointers(), 2, (size_t) numSmp);

    // Oversampled half: input network -> JFET stage. What comes back down is a drain CURRENT.
    auto& os = *oversamplers[(size_t) currentOsIndex];
    auto osBlock = os.processSamplesUp(block);
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = osBlock.getChannelPointer((size_t) ch);
        const int n = (int) osBlock.getNumSamples();
        for (int i = 0; i < n; ++i)
            d[i] = dsp[(size_t) ch].processOversampled(d[i]);
    }
    os.processSamplesDown(block);

    // Base-rate half: the output / VOLUME network turns that current into output volts.
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* wet = scratch.getWritePointer(ch);
        auto* dry = buffer.getWritePointer(ch);
        auto mix = bypassMix;
        float outPeak = 0.0f;

        for (int n = 0; n < numSmp; ++n)
        {
            const double volts = dsp[(size_t) ch].processBase(wet[n]);
            const double m = mix.getNextValue();
            const double y = (1.0 - m) * (volts * outputGain) + m * (double) dry[n];

            dry[n] = (float) y;
            outPeak = jmax(outPeak, (float) std::abs(y));
        }
        outputLevel[(size_t) ch].store(outPeak);
    }
    bypassMix.skip(numSmp);
}

AudioProcessorEditor* PedalAudioProcessor::createEditor()
{
    return new PedalAudioProcessorEditor(*this);
}

void PedalAudioProcessor::getStateInformation(MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void PedalAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(ValueTree::fromXml(*xml));
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PedalAudioProcessor();
}
