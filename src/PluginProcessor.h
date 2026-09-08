#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/EchoPreDsp.h"

/**
 * Echo Pre 3 -- circuit-modelled Echoplex EP-3 preamp (Chase Tone Secret Preamp topology).
 *
 * Signal chain per channel, mirroring circuit.md: input network -> 2N5457 common-source stage
 * (oversampled) -> output/VOLUME network. VOLUME is NOT a scalar gain here: it is two arms of a real
 * pot inside the output network's solve, which is what makes the control deliberately non-monotonic.
 */
class PedalAudioProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    PedalAudioProcessor();
    ~PedalAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    /** Test hook: Newton iterations per sample in the JFET solve. Production leaves this at
     *  JfetStage::kSolveIters; FeatureProfile A/Bs it. */
    void setSolveIters(int n);

    /** Test hook: closed-form solve vs the iterative reference. Production is always closed form. */
    void setUseClosedForm(bool b);

    /** Measurement hook: the JFET stage's one amplitude parameter, Vov. Sweeping it is how it gets
     *  fitted against a calibrated capture; production leaves it at JfetParams' shipped value. */
    void setVov(double v);

    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Echo Pre 3"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int sizeInBytes) override;

    // Peak meters read by the editor's timer (DAW domain, per channel). Written on the audio thread.
    float getInputLevel(int ch) const  { return inputLevel[(size_t) juce::jlimit(0, 1, ch)].load(); }
    float getOutputLevel(int ch) const { return outputLevel[(size_t) juce::jlimit(0, 1, ch)].load(); }


    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOversamplingFactor(int factorIndex);

    // ======================== ADAA IS GONE, AND IT WILL NOT COME BACK EASILY ========================
    // The stage no longer implements antiderivative anti-aliasing, and the removal is structural
    // rather than a policy change. ADAA1 needs a MEMORYLESS 1-D map whose argument is near-linear
    // between samples. The device model is now Shichman-Hodges with its load line, so the map is
    // I(Vov_i, Vds_i) -- TWO arguments, the second of which is itself a function of the output
    // current. The derivation does not apply, and no closed-form antiderivative of the composite
    // exists to substitute (dsp.md is explicit that quadrature is measured-wrong here, not merely
    // inelegant).
    //
    // 📌 Nothing audible was lost, and that is measured rather than assumed. ADAA was already OFF at
    // every shipped factor. FeatureProfile's last run under the previous structure put it at +16.7 dB
    // of alias improvement at 1x -- its best case -- and STILL found 1x + ADAA beaten outright by
    // plain 2x, by 10.3 dB of floor for 1.3 pp of CPU, with 2x/4x/8x + ADAA making the floor WORSE.
    // Above 2x the floor is the decimation FIR's stopband, which ADAA can only add to.
    //
    // ➡ If it is ever wanted back, the place it can live is the SATURATION branch alone, where
    // I = beta*(Vov + w)^2 is 1-D in w with the trivial antiderivative beta*(Vov + w)^3/3. Triode
    // would have to fall back to plain evaluation, which makes the behaviour signal-dependent -- so
    // let FeatureProfile decide whether that trade is worth making before building it.


    // Trim link (architecture.md): while engaged, nudging one trim by d dB nudges the other by -d,
    // so pushing the circuit harder doesn't change overall loudness. Implemented as a listener pair
    // with a re-entrancy guard rather than derived in processBlock, which would fight the host's own
    // automation and recall of both parameters.
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    juce::AudioParameterFloat* inputTrimParam = nullptr;
    juce::AudioParameterFloat* outputTrimParam = nullptr;
    std::atomic<float>* pTrimLink = nullptr;
    std::atomic<bool> isSyncingTrim { false };
    // Plain floats, not a String-keyed map: parameterChanged can be called from the audio thread.
    float lastInputTrim = 0.0f, lastOutputTrim = 0.0f;

    // ============================ CALIBRATION -- BOTH UNCALIBRATED ============================
    // kInputRef is an ASSUMPTION, not a measurement. Calibrating it needs a bypass/unity capture,
    // and the reference data is seven NAM models, which cannot be bypassed -- so no anchor exists
    // (docs/build-plan.md L2). This is the template's starting value, carried forward deliberately.
    // Nothing downstream may be written as if this were anchored.
    static constexpr double kInputRef = 4.4626; // volts per full scale -- see below

    // Output makeup is to be level-matched to unit P1 (build-plan.md §6), which needs the renders.
    // Until then it is exactly unity so the model's own gain is visible and unmasked. Do NOT pad
    // this for headroom -- calibration doc §2 is explicit that it is a level match, and the final
    // value may exceed 1.0.
    static constexpr double kOutputMakeup = 1.0; // UNCALIBRATED

    std::array<pedal::dsp::EchoPreDsp, 2> dsp;
    juce::AudioBuffer<double> scratch;

    // One oversampler per factor, all prepared up front: juce::dsp::Oversampling fixes its factor at
    // construction, so switching by rebuilding one would allocate on the audio thread.
    std::array<std::unique_ptr<juce::dsp::Oversampling<double>>, 4> oversamplers;
    int currentOsIndex = -1;
    double baseSampleRate = 48000.0;

    // Trims are read per block as plain gains; only VOLUME (which re-solves the WDF network) and the
    // bypass crossfade need smoothing.
    juce::SmoothedValue<double> volumeSmooth, bypassMix;

    // True while the DSP is being skipped because the bypass crossfade has fully settled to dry
    // (architecture.md "Bypass"). It exists only so the chain and the oversampler are reset ONCE on
    // entering that state rather than on every skipped block. See processBlock for why reset rather
    // than resume, and BypassClickTest for the measurement that decided it.
    bool dspIsIdle = false;

    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pInputTrim = nullptr;
    std::atomic<float>* pOutputTrim = nullptr;
    std::atomic<float>* pOversampling = nullptr;
    std::atomic<float>* pRenderOversampling = nullptr;
    std::atomic<float>* pBypass = nullptr;

    std::array<std::atomic<float>, 2> inputLevel { 0.0f, 0.0f };
    std::array<std::atomic<float>, 2> outputLevel { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalAudioProcessor)
};
