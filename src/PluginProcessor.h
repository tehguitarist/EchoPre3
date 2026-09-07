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
    // ⚠ REVISED 2026-09-08, and the reason changed completely. The step-4b refit moved ADAA from
    // the shaper's whole map to its nonlinear EXCESS only (JfetStage.h: the loop suppresses only
    // what it generates, so only the excess belongs in the second shelf). ADAA1 is linear in the
    // map, so ADAA[g - id] = ADAA[g] - ADAA[id], and the two-point average on the linear path --
    // ADAA's entire cost in the numbers below -- is now exactly cancelled. Re-measured, Dark, on a
    // hot tone (full scale at +12 dB input trim, far past any guitar):
    //
    //     factor   alias floor removed   12 kHz cost   wanted H2 lost   CPU cost
    //       1x           8.41 dB           0.00 dB        1.18 dB        ~0 pp
    //       2x          -0.52 dB           0.00 dB        0.29 dB        ~0 pp
    //       4x          -0.18 dB           0.00 dB        0.07 dB       ~0.13 pp
    //       8x          -0.04 dB           0.00 dB        0.01 dB       ~0.25 pp
    //
    // The old middle column read 2.99 dB at 1x and 0.68 dB at 2x; it is zero at every factor now,
    // and FeatureProfile's verdict for 1x flipped from a rejection to "FREE WIN -- keep always on".
    //
    // IT IS STILL OFF, on a different column. The cost moved rather than vanishing: averaging the
    // map also averages away some of the harmonic the device is SUPPOSED to make, and at 1x that is
    // 1.18 dB of wanted H2 (1.29 dB at a realistic -6 dBFS). OSFidelity's own premise is that the
    // wanted distortion must not move with the factor -- the OS control is a quality knob, not a
    // voicing knob -- and 1.2 dB of H2 is a voicing change. What it buys is 8.4 dB off an alias
    // floor already at -79 dBc under that absurd drive, and -122 dBc at a realistic level.
    //
    // And the argument that needs no threshold at all still stands, unchanged in direction and
    // larger in size: 1x + ADAA is beaten outright by plain 2x, by 18.0 dB of alias floor, for 0.83
    // percentage points of CPU. Anyone who cares about that floor should spend the CPU on the factor
    // the user already has.
    //
    // !! The 2x row is no longer marginal, and that is worth stating because the previous version of
    // this comment warned at length that it was. ADAA at 2x now makes the floor slightly WORSE
    // (-0.52 dB), because at 2x and above the floor is the decimation FIR's own stopband
    // (-105.8/-105.5/-105.4 dBc at 2x/4x/8x, flat) rather than fold-back, so there is nothing left
    // for ADAA to remove and only its own smoothing remains. The judgement call that comment
    // documented has been dissolved by a measurement rather than re-argued.
    //
    // The gate stays a <= threshold on the OS INDEX (0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x) rather than
    // being deleted: dsp.md warns the benefit is not monotone in rate, and this refit is the second
    // time the picture has moved. -1 means "at no factor", which is where the measurement puts it
    // today. If the 1x wanted-H2 loss ever becomes acceptable -- say the shelf restore of
    // build-plan.md 9.3 lands and 1x becomes a supported setting rather than a fallback -- this is
    // a one-line change with three probes already standing to justify it.
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
