// PerfBenchmark -- CPU cost of the real chain as a percentage of realtime, plus the reported
// latency, per oversampling factor x MODE (build.md "Performance & fidelity probes"). Feeds the
// README performance table and the HQ/Eco decision in dsp.md.
//
// Registered with add_test() as a FINITE-ONLY probe. It deliberately does NOT gate on a CPU
// percentage: build.md is explicit that CI machine speed varies, so a threshold here would fail on
// a slow runner and pass on a fast one regardless of whether the DSP got cheaper or dearer. The
// ratios between rows are the durable part; the absolute figures are this machine's.

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
    std::printf("  %-8s %-10s %10s %10s %10s %12s\n", "factor", "mode", "shipped", "2 iters", "20 iters",
                "latency smp");

    for (int osIdx = 0; osIdx < 4; ++osIdx)
        for (int m = 0; m < 3; ++m)
        {
            Setup s;
            s.osIndex = osIdx;
            s.modeIndex = m;

            // The two extra columns used to be ADAA off/on. ADAA went with the device-model
            // rewrite (PluginProcessor.h has why), and what replaced it as the CPU lever is the
            // solve's iteration count -- so these bracket the shipped count instead.
            s.solveIters = 0; // shipped kSolveIters
            const double shipped = cpuPercent(proc, s, kSeconds, &finite);
            const int latency = proc.getLatencySamples();
            s.solveIters = 2;
            const double off = cpuPercent(proc, s, kSeconds, &finite);
            s.solveIters = 20;
            const double on = cpuPercent(proc, s, kSeconds, &finite);

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

    if (! finite)
    {
        std::printf("\nFAILED: non-finite sample in a render\n");
        return 1;
    }
    std::printf("\nPASS: PerfBenchmark (finite output at every factor x mode)\n");
    return 0;
}
