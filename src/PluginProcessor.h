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

    /** ADAA is normally chosen by the oversampling factor (see kAdaaMaxOsIndex). The measurement
     *  probes need to A/B it at a FIXED factor, and dsp.md is explicit that a gate hardcoded beyond
     *  reach makes the gate's own validation measure the gate rather than the mechanism -- so this
     *  override exists for PerfBenchmark / FeatureProfile / OSFidelity. Nothing in the plugin's own
     *  signal path or UI ever calls it, and there is no parameter behind it. */
    enum class AdaaOverride { useOsGate, forceOn, forceOff };
    void setAdaaOverride(AdaaOverride m);
    bool adaaIsActive() const { return adaaActive; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateOversamplingFactor(int factorIndex);
    void applyAdaaPolicy();

    // ============================ ADAA IS MEASURED AND SWITCHED OFF ============================
    // Not "not implemented" -- implemented, proven exact (JfetStageTest section 6 checks the
    // antiderivative against an independent Simpson integral to 1e-15), measured at every factor by
    // OSFidelity and FeatureProfile, then switched off on the numbers. Recorded here because the
    // next reader's instinct will be to reach for ADAA, and reading is cheaper than rebuilding it.
    //
    // ADAA1 is a two-point average of the shaper: it lowers the alias floor and costs a
    // cos(pi*f/fs_os) magnitude rolloff plus half an oversampled sample of delay. Measured on a hot
    // tone (full scale at +12 dB input trim, far past any guitar), Dark:
    //
    //     factor   alias floor removed   12 kHz cost   CPU cost
    //       1x           5.63 dB           2.99 dB      ~0 pp
    //       2x           1.18 dB           0.68 dB      ~0 pp
    //       4x           0.29 dB           0.17 dB      ~0 pp
    //       8x           0.07 dB           0.04 dB     ~0.15 pp
    //
    // CPU was never the question -- ADAA is free here. It is off because of the second column.
    //
    // Two things decide it. First, at 1x -- the only factor where either effect exceeds a dB -- ADAA
    // buys 5.6 dB of a fold-back floor already sitting at -63 dBc under that absurd drive (-128 dBc
    // at a realistic level) by TRIPLING the top-octave droop, -1.07 dB to -4.06 dB at 12 kHz. A
    // broadband, always-present response error is not a fair trade for inharmonic content that far
    // down. Second, and needing no such judgement: 1x + ADAA is beaten outright by plain 2x on BOTH
    // axes -- 10.6 dB quieter and 3.9 dB less dark -- for 0.86 percentage points of CPU. Anyone who
    // cares about that floor should spend the CPU on the factor the user already has.
    //
    // !! One row is genuinely marginal and must not be misremembered as clear-cut. At 2x, ADAA
    // clears a 1 dB alias-gain threshold and a 1 dB response budget -- 1.18 dB against 0.68 dB --
    // so the verdict there flips on where those thresholds sit, and both are stated assumptions
    // rather than measurements (FeatureProfile's kHfBudgetDb is provisional until M6 measures the
    // real unit-to-unit spread). It is off at 2x too, on the grounds that a sub-dB effect whose sign
    // depends on a placeholder threshold is not a basis for shipping a behaviour.
    //
    // Note also that the floor stops improving past 2x: -79.3 dBc at 2x, 4x and 8x alike. That is no
    // longer fold-back but the decimation FIR's own stopband, which is why ADAA's benefit column
    // collapses rather than merely shrinking. It also means the "alias gain" at those factors is
    // largely ADAA's own high-frequency attenuation measured a second time -- gain and cost track at
    // roughly 1.8:1 at EVERY factor, which is what a broadband rolloff looks like rather than what
    // selective antialiasing looks like.
    //
    // The gate stays a <= threshold on the OS INDEX (0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x) rather than
    // being deleted: dsp.md warns the benefit is not monotone in rate, so if a later refit moves the
    // shaper's curvature far enough to change the picture, this is a one-line change with three
    // probes already standing to justify it. -1 means "at no factor", which is where the
    // measurement puts it today.
    static constexpr int kAdaaMaxOsIndex = -1;

    AdaaOverride adaaOverride = AdaaOverride::useOsGate;
    bool adaaActive = false;

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
    static constexpr double kInputRef = 0.87; // volts per full scale -- ASSUMED

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
