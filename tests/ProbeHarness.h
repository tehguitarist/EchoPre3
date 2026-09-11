#pragma once

// Shared measurement instruments for the three fidelity/performance probes (build.md). One copy,
// because OSFidelity and FeatureProfile must measure the SAME quantity the same way -- FeatureProfile
// pairs each accuracy delta with a CPU delta, and a probe that measured "alias floor" slightly
// differently from the probe it is being read alongside would produce a verdict about the
// instruments rather than about the feature.
//
// Everything here drives the REAL PedalAudioProcessor through processBlock, so the CPU figures
// include the trims, metering and bypass crossfade a host actually pays for, and the accuracy
// figures come from the same code path the plugin ships.

#include <chrono>
#include <cmath>
#include <random>
#include <complex>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "PluginProcessor.h"

namespace pedal::probe
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;
constexpr int kFftOrder = 15;
constexpr int kFftSize = 1 << kFftOrder; // 32768 -> 1.46 Hz bins at 48 kHz

inline const char* const kModeNames[] = {"Bright", "Dark", "Mid"};
inline constexpr int kOsFactors[] = {1, 2, 4, 8};

inline double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1.0e-300));
}

/** Settings for one measurement. Every field is set explicitly on every run: a probe that inherits
 *  a leftover parameter from the previous configuration reports a difference that is not the one it
 *  claims to be measuring. */
struct Setup
{
    int osIndex = 3;
    int modeIndex = 1; // Dark
    double volume = 0.5;
    double inputTrimDb = 0.0;
    double outputTrimDb = 0.0;
    // ⚠ The output LOAD is part of the model (it moves the level, C10's corner and the drain-node
    // impedance -- circuit.md note #31), so it belongs here for the same reason every other field
    // does. It was missing when the control was added, which meant every probe silently inherited
    // the plugin's default: harmless while no probe changed it, and a leak waiting to happen the
    // moment one did. 0 = the shipped 68k default.
    int loadIndex = 0;
    bool bypass = false;
    int solveIters = 0;        // 0 = the shipped JfetStage::kSolveIters (restored, not inherited)
    bool closedForm = true;    // false selects the iterative reference solve
};

inline void configure(PedalAudioProcessor& proc, const Setup& s)
{
    auto setChoice = [&proc](const char* id, int idx)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(id)))
            p->setValueNotifyingHost(p->convertTo0to1((float)idx));
    };
    auto setPlain = [&proc](const char* id, float v)
    {
        if (auto* p = proc.apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    };

    setPlain("trim_link", 0.0f);
    setPlain("volume", (float)s.volume);
    setPlain("bypass", s.bypass ? 1.0f : 0.0f);
    setPlain("input_trim", (float)s.inputTrimDb);
    setPlain("output_trim", (float)s.outputTrimDb);
    setChoice("mode", s.modeIndex);
    setChoice("output_load", s.loadIndex);
    // Both OS parameters, so the render never depends on whether isNonRealtime() happens to be set.
    setChoice("oversampling", s.osIndex);
    setChoice("render_oversampling", s.osIndex);

    proc.prepareToPlay(kFs, kBlock);
    // ALWAYS set it, never conditionally: 0 means "the shipped count" and the processor restores it.
    proc.setSolveIters(s.solveIters);
    proc.setUseClosedForm(s.closedForm);
}

/** Run `n` samples of `gen` through the processor in blocks after discarding `discard` samples of
 *  warm-up, returning channel 0. Sets `finite` false if any sample is not finite. */
template <typename Gen>
std::vector<double> render(PedalAudioProcessor& proc, const Setup& s, int discard, int n, Gen gen,
                           bool* finite = nullptr)
{
    configure(proc, s);

    const int total = discard + n;
    juce::AudioBuffer<float> buf(2, kBlock);
    std::vector<double> out;
    out.reserve((size_t)n);

    juce::MidiBuffer midi;
    for (int start = 0; start < total; start += kBlock)
    {
        const int m = juce::jmin(kBlock, total - start);
        buf.clear();
        for (int i = 0; i < m; ++i)
        {
            const float v = (float)gen(start + i);
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        juce::AudioBuffer<float> sub(buf.getArrayOfWritePointers(), 2, 0, m);
        midi.clear();
        proc.processBlock(sub, midi);

        for (int i = 0; i < m; ++i)
        {
            const double y = (double)sub.getSample(0, i);
            if (finite != nullptr && ! std::isfinite(y))
                *finite = false;
            if (start + i >= discard && (int)out.size() < n)
                out.push_back(y);
        }
    }
    return out;
}

/** Steady-state complex response at `freq`, by quadrature correlation -- the same instrument the
 *  per-stage tests use. Peak picking is phase dependent and worth up to 0.5 dB near Nyquist, which
 *  is precisely the band these probes exist to measure. */
inline std::complex<double> responseAt(PedalAudioProcessor& proc, Setup s, double freq, double amp)
{
    const int discard = (int)(0.5 * kFs);
    const int n = (int)std::ceil(std::max(0.5 * kFs, 200.0 * kFs / freq));
    auto y =
        render(proc, s, discard, n, [freq, amp](int i) { return amp * std::sin(2.0 * M_PI * freq * (double)i / kFs); });

    double re = 0.0, im = 0.0;
    for (int i = 0; i < (int)y.size(); ++i)
    {
        // The phase reference must run on the generator's clock, hence the discard offset.
        const double th = 2.0 * M_PI * freq * (double)(discard + i) / kFs;
        re += y[(size_t)i] * std::sin(th);
        im += y[(size_t)i] * std::cos(th);
    }
    const double sc = 2.0 / ((double)y.size() * amp);
    return {re * sc, im * sc};
}

/** Magnitude response normalised at `refHz`, so the result is SHAPE and not level. Comparing two
 *  configurations without this normalisation conflates a top-octave droop with a broadband gain
 *  difference, and they have completely different causes. */
inline double relativeResponseDb(PedalAudioProcessor& proc, const Setup& s, double freq, double refHz, double amp)
{
    return db(std::abs(responseAt(proc, s, freq, amp))) - db(std::abs(responseAt(proc, s, refHz, amp)));
}

struct Spectrum
{
    double h2Dbc = 0.0;
    double wantedThdDbc = 0.0; // harmonics still under Nyquist -- the distortion that belongs here
    double aliasDbc = 0.0;     // every other bin -- what oversampling and ADAA are for
};

/** Single-tone spectrum with the tone on an exact FFT bin, so a rectangular window is leak free:
 *  harmonics and folds land on bins too, and "wanted" separates from "alias" cleanly rather than
 *  smearing into it. */
inline Spectrum toneSpectrum(PedalAudioProcessor& proc, const Setup& s, double amp, int binOfTone,
                             bool* finite = nullptr)
{
    const double f0 = (double)binOfTone * kFs / (double)kFftSize;
    auto y = render(
        proc, s, (int)(0.5 * kFs), kFftSize,
        [f0, amp](int i) { return amp * std::sin(2.0 * M_PI * f0 * (double)i / kFs); }, finite);

    std::vector<float> fft((size_t)kFftSize * 2, 0.0f);
    for (int i = 0; i < kFftSize; ++i)
        fft[(size_t)i] = (float)y[(size_t)i];
    juce::dsp::FFT(kFftOrder).performFrequencyOnlyForwardTransform(fft.data());

    std::vector<double> power((size_t)kFftSize / 2, 0.0);
    for (int i = 0; i < kFftSize / 2; ++i)
        power[(size_t)i] = (double)fft[(size_t)i] * (double)fft[(size_t)i];

    std::vector<bool> wanted((size_t)kFftSize / 2, false);
    auto markGuarded = [&wanted](int bin)
    {
        for (int d = -2; d <= 2; ++d)
            if (bin + d >= 0 && bin + d < (int)wanted.size())
                wanted[(size_t)(bin + d)] = true;
    };

    Spectrum out;
    const double fund = std::max(power[(size_t)binOfTone], 1.0e-300);
    markGuarded(binOfTone);

    double wantedHarm = 0.0;
    for (int k = 2; k * binOfTone < kFftSize / 2; ++k)
    {
        wantedHarm += power[(size_t)(k * binOfTone)];
        if (k == 2)
            out.h2Dbc = 10.0 * std::log10(std::max(power[(size_t)(2 * binOfTone)], 1.0e-300) / fund);
        markGuarded(k * binOfTone);
    }

    // The even bump rectifies, so the stage makes genuine low-frequency content and C10's high-pass
    // removes most of it. What survives is circuit, not folding -- count it as alias and the probe
    // reports a DC offset as an alias floor.
    const int lfGuard = (int)std::ceil(30.0 * (double)kFftSize / kFs);
    double alias = 0.0;
    for (int i = lfGuard; i < kFftSize / 2; ++i)
        if (! wanted[(size_t)i])
            alias += power[(size_t)i];

    out.wantedThdDbc = 10.0 * std::log10(std::max(wantedHarm, 1.0e-300) / fund);
    out.aliasDbc = 10.0 * std::log10(std::max(alias, 1.0e-300) / fund);
    return out;
}

/** What to feed cpuPercent(). ⚠⚠ A SWEPT SINE IS THE BEST CASE FOR THE BRANCH PREDICTOR, and the
 *  JFET solve branches every sample (cutoff early-return, saturation-versus-triode, Halley's
 *  denominator fallback, two clamps). A periodic stimulus makes every one of those perfectly
 *  predicted, so a tone-only benchmark can flatter the figure that ends up in the README. These
 *  exist so that claim is MEASURED rather than assumed -- see PerfBenchmark's programme section.
 *
 *  All of them are normalised to the same RMS, which is what isolates *complexity* from *level*:
 *  the drive sets which branch each sample takes, and only the predictability should differ. */
enum class Programme
{
    SweptSine,  // 80 Hz - 5 kHz log sweep: the historical stimulus, and the most predictable
    Tone1k,     // a fixed tone: even more predictable, the floor of the effect
    WhiteNoise, // maximally unpredictable sample to sample
    PinkNoise,  // broadband but spectrally realistic
    Chord       // six plucked strings with harmonics and decaying envelopes -- a guitar proxy
};

/** Fill `dst` with `n` samples of the chosen programme, scaled to `targetRms` (full-scale units).
 *  Generated OUTSIDE any timed region -- see cpuPercent(). */
inline void fillProgramme(float* dst, int n, Programme p, double targetRms, double fs = kFs)
{
    std::mt19937 rng(12345u);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    double phase = 0.0, b0 = 0.0, b1 = 0.0, b2 = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        double y = 0.0;
        switch (p)
        {
            case Programme::SweptSine:
            {
                const double f = 80.0 * std::pow(62.5, (double) i / (double) n);
                phase += 2.0 * M_PI * f / fs;
                y = std::sin(phase);
                break;
            }
            case Programme::Tone1k:
                y = std::sin(2.0 * M_PI * 1000.0 * t);
                break;
            case Programme::WhiteNoise:
                y = uni(rng);
                break;
            case Programme::PinkNoise:
            {
                // three one-pole sections: a standard -3 dB/octave approximation
                const double w = uni(rng);
                b0 = 0.99765 * b0 + w * 0.0990460;
                b1 = 0.96300 * b1 + w * 0.2965164;
                b2 = 0.57000 * b2 + w * 1.0526913;
                y = b0 + b1 + b2 + w * 0.1848;
                break;
            }
            case Programme::Chord:
            {
                // E2 A2 D3 G3 B3 E4, six harmonics each, staggered plucks, 1.6 s repeat
                static const double kOpen[6] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
                const double cyc = std::fmod(t, 1.6);
                for (int st = 0; st < 6; ++st)
                {
                    const double onset = 0.012 * (double) st;
                    const double age = cyc - onset;
                    if (age < 0.0)
                        continue;
                    const double env = std::exp(-age * 2.2);
                    for (int h = 1; h <= 6; ++h)
                        y += env * std::sin(2.0 * M_PI * kOpen[st] * h * age + 0.7 * st + 0.3 * h) / (double) (h * h);
                }
                break;
            }
        }
        dst[i] = (float) y;
    }

    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) dst[i] * (double) dst[i];
    const double rms = std::sqrt(sum / (double) std::max(1, n));
    const float g = (float) (targetRms / std::max(rms, 1.0e-12));
    for (int i = 0; i < n; ++i)
        dst[i] *= g;
}

/** CPU as a percentage of realtime for one configuration: wall clock over audio duration.
 *  The stimulus is real audio at a realistic level, not silence -- a denormal-flushed silent block
 *  is far cheaper than real audio, and timing one measures the wrong thing entirely.
 *
 *  📌 The stimulus is PRE-GENERATED. It used to be computed inside the timed loop, which charged
 *  every reading a std::pow and a std::sin per sample (~0.06 points of the total -- small, but it
 *  is the harness's cost, not the plugin's). */
inline double cpuPercent(PedalAudioProcessor& proc, const Setup& s, double seconds,
                         bool* finite = nullptr, Programme prog = Programme::SweptSine,
                         double targetRms = 0.3536)
{
    configure(proc, s);

    const int totalSamples = (int)(seconds * kFs);
    std::vector<float> stim((size_t) totalSamples);
    fillProgramme(stim.data(), totalSamples, prog, targetRms);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;

    // Warm-up outside the timed region: the first blocks touch cold pages and resolve the
    // oversampler's first-use work, which a running host has long since paid for.
    for (int i = 0; i < 8; ++i)
    {
        buf.clear();
        midi.clear();
        proc.processBlock(buf, midi);
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int start = 0; start < totalSamples; start += kBlock)
    {
        const int m = juce::jmin(kBlock, totalSamples - start);
        for (int i = 0; i < m; ++i)
        {
            const float v = stim[(size_t)(start + i)];
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        juce::AudioBuffer<float> sub(buf.getArrayOfWritePointers(), 2, 0, m);
        midi.clear();
        proc.processBlock(sub, midi);
        if (finite != nullptr)
            for (int i = 0; i < m; ++i)
                if (! std::isfinite(sub.getSample(0, i)))
                    *finite = false;
    }
    const auto t1 = std::chrono::steady_clock::now();
    return 100.0 * std::chrono::duration<double>(t1 - t0).count() / seconds;
}
} // namespace pedal::probe
