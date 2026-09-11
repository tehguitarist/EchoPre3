// PerfBenchmark -- CPU cost of the real chain as a percentage of realtime, plus the reported
// latency, per oversampling factor x MODE (build.md "Performance & fidelity probes"). Feeds the
// README performance table and the HQ/Eco decision in dsp.md.
//
// Registered with add_test() as a FINITE-ONLY probe. It deliberately does NOT gate on a CPU
// percentage: build.md is explicit that CI machine speed varies, so a threshold here would fail on
// a slow runner and pass on a fast one regardless of whether the DSP got cheaper or dearer. The
// ratios between rows are the durable part; the absolute figures are this machine's.

#include <cmath>
#include <cstdio>

#include "ProbeHarness.h"

using namespace pedal::probe;

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PedalAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);

    bool finite = true;
    constexpr double kSeconds = 4.0;

    std::printf("Echo Pre 3 -- CPU as %% of realtime, stereo, %g s render at %g Hz, %d-sample blocks\n", kSeconds, kFs,
                kBlock);
    std::printf("Timed through processBlock, so trims, metering and the bypass crossfade are all in it.\n\n");
    std::printf("  %-8s %-10s %10s %10s %10s %12s\n", "factor", "mode", "shipped", "iter-8", "iter-20",
                "latency smp");

    for (int osIdx = 0; osIdx < 4; ++osIdx)
        for (int m = 0; m < 3; ++m)
        {
            Setup s;
            s.osIndex = osIdx;
            s.modeIndex = m;

            // The two extra columns bracket what the CLOSED-FORM solve replaced: the safeguarded
            // Newton at its shipped count and at a fully-converged one. The closed form is exact, so
            // this is a pure CPU comparison with no accuracy axis to trade against.
            s.closedForm = true;
            s.solveIters = 0;
            const double shipped = cpuPercent(proc, s, kSeconds, &finite);
            const int latency = proc.getLatencySamples();
            s.closedForm = false;
            s.solveIters = 8;
            const double off = cpuPercent(proc, s, kSeconds, &finite);
            s.solveIters = 20;
            const double on = cpuPercent(proc, s, kSeconds, &finite);
            s.closedForm = true;

            std::printf("  %6dx %-10s %9.2f%% %9.2f%% %9.2f%% %12d\n", kOsFactors[osIdx], kModeNames[m], shipped, off,
                        on, latency);
        }

    // Bypass is a second reading worth having, and it is the one that found the skip was missing.
    // architecture.md specifies the DSP is skipped while bypassed; processBlock used to run the
    // whole chain and crossfade it against the dry copy regardless of the mix, so this row read the
    // same as the active row (3.47% against 3.51% at 8x). It now reads a flat ~0.11% at every
    // factor. Keep the row: if the two ever converge again, the skip has regressed.
    std::printf("\n  Bypassed (architecture.md: the DSP is skipped here -- these rows must diverge):\n");
    std::printf("  %-8s %-10s %10s %10s\n", "factor", "mode", "active", "bypassed");
    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup s;
        s.osIndex = osIdx;
        const double active = cpuPercent(proc, s, kSeconds, &finite);
        s.bypass = true;
        const double bypassed = cpuPercent(proc, s, kSeconds, &finite);
        std::printf("  %6dx %-10s %9.2f%% %9.2f%%\n", kOsFactors[osIdx], "Dark", active, bypassed);
    }

    // ⚠⚠ DOES THE STIMULUS MATTER? The rows above use a swept sine, which is the BEST case for the
    // branch predictor -- and this solve branches every sample (cutoff early-return,
    // saturation-versus-triode, Halley's denominator fallback, two clamps). A tone makes all of
    // those perfectly predicted, so the headline figure could be flattering real programme
    // material. All five programmes below are normalised to the SAME RMS, which is what separates
    // complexity from level: the drive decides which branch a sample takes, and only the
    // predictability differs. The Chord row is the guitar proxy (six plucked strings, six harmonics
    // each, decaying envelopes) and is the one to read as "real playing".
    std::printf("\n  Does the STIMULUS matter? All at the same RMS (-9 dBFS), 4x, Dark:\n");
    std::printf("  %-14s %10s %10s %14s\n", "programme", "4x CPU", "8x CPU", "vs swept sine");
    {
        double base4 = 0.0;
        struct Row { const char* name; Programme p; };
        const Row rows[] = { { "swept sine", Programme::SweptSine },
                             { "1 kHz tone", Programme::Tone1k },
                             { "pink noise", Programme::PinkNoise },
                             { "white noise", Programme::WhiteNoise },
                             { "guitar chord", Programme::Chord } };
        for (const auto& r : rows)
        {
            Setup s4;
            s4.osIndex = 2; // 4x
            const double c4 = cpuPercent(proc, s4, kSeconds, &finite, r.p);
            Setup s8;
            s8.osIndex = 3;
            const double c8 = cpuPercent(proc, s8, kSeconds, &finite, r.p);
            if (base4 == 0.0)
                base4 = c4;
            std::printf("  %-14s %9.2f%% %9.2f%% %13.2f%%\n", r.name, c4, c8, c4 - base4);
        }
    }

    // And at a realistic tracking level rather than a hot one: -18 dBFS RMS is ordinary DI guitar,
    // where the stage sits in saturation for nearly every sample and never reaches triode.
    std::printf("\n  Guitar chord at three levels (4x, Dark) -- the triode branch is what a hot\n"
                "  signal pays for, so CPU is expected to RISE with level:\n");
    std::printf("  %-16s %10s\n", "RMS", "4x CPU");
    for (const double rms : { 0.0398, 0.1259, 0.3536 }) // -28, -18, -9 dBFS
    {
        Setup s4;
        s4.osIndex = 2;
        const double c = cpuPercent(proc, s4, kSeconds, &finite, Programme::Chord, rms);
        std::printf("  %-16.1f %9.2f%%\n", 20.0 * std::log10(rms), c);
    }

    if (! finite)
    {
        std::printf("\nFAILED: non-finite sample in a render\n");
        return 1;
    }
    std::printf("\nPASS: PerfBenchmark (finite output at every factor x mode)\n");
    return 0;
}
