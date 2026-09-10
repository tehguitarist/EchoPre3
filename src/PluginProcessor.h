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

    // ========================= CALIBRATION -- BOTH ANCHORED 2026-09-10 =========================
    // ⭐ kInputRef: the play side of the capture rig, CONFIRMED by the owner rather than assumed.
    // Its interface is set to 1.7745 V RMS at -5 dBFS / 220 Hz, i.e. input_level_dbu = +12.20 dBu,
    // so a full-scale sine is 3.156 V RMS = 4.4626 V peak. (220 Hz rather than 1 kHz because the
    // meter is specified only to 400 Hz; accuracy there is +-0.135 dB.) It is also the level a
    // well-recorded guitar metering -12 dBFS RMS implies, so the plugin's own full scale means the
    // same thing a tracked DI does. Every capture in analysis/captures/ was made at this setting.
    static constexpr double kInputRef = 4.4626; // volts per full scale -- MEASURED

    // ⭐⭐ kOutputMakeup: MEASURED 2026-09-10, and this is the FIRST anchor it has ever had. It was
    // exactly 1.0 and unanchored since the project began, because every earlier reference was a NAM
    // model carrying an unknown rig gain (build-plan limit L2) -- there was no possible route to it
    // and there will not be another. calibration doc §2 is explicit that this is a LEVEL MATCH and
    // not a headroom pad, and the measured value duly exceeds 1.0.
    //
    // What makes it possible is that BOTH ends of the capture rig were written down and they are
    // NOT equal: play +12.20 dBu (4.4626 V/FS), record +14.29 dBu (5.6767 V/FS). So a capture's
    // digital gain is 2.090 dB away from the pedal's actual VOLTAGE gain, which is the quantity the
    // plugin computes. ⚠ Getting that backwards is a 4.2 dB error and it happened on the first
    // attempt. The check that catches it is free: a loop is a wire and this pedal's bypass is true
    // bypass (nulls at -70.8 dB, 0.008 dB insertion loss), so both must read exactly 0.000 dB of
    // voltage gain. They read -0.026 and -0.015.
    //
    //   +1.261 dB = x1.1562,  sd 0.175 dB, spread 0.424 dB, six knob positions 9:00 -> 17:00
    //
    // ⚠⚠ FITTED FROM **DARK** CAPTURES ONLY. P4's own JFET is ~25 % weaker than this model's
    // (K0 = 5.06-5.19 against the shipped 6.59) and the recorded decision is to voice to P1/P2, so
    // that gap is deliberate. It lives in the mode shelf, whose 1.9 kHz zero is inside the top of
    // the 200-2000 Hz fit band; in DARK the shelf does not exist at all (Zs = R5, flat everywhere).
    // Bright duly reads 0.09-0.18 dB lower at every knob position, mean +1.147 dB. Pooling the modes
    // would fold a voicing decision into a level constant.
    //
    // ⚠ COUPLED TO gm, and only to gm: both are level scalars in the DARK path. What separates them
    // is that gm ALSO sets the mode differential, which is rig-free and independently measured -- so
    // gm is fitted from the differential FIRST and this absorbs the remainder. ➡ IF gm MOVES,
    // RE-RUN analysis/absolute_gain.py. (gm was re-checked 2026-09-10 and did not move.)
    // ⚠ Also coupled to kVolumeTaperP, which is why it was re-measured after the taper went 2.30:
    // at the old 2.0 the per-position scatter was 2.73 dB, at 2.30 it is 0.42 dB.
    static constexpr double kOutputMakeup = 1.1562; // MEASURED -- analysis/absolute_gain.py

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
