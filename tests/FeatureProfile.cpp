// FeatureProfile -- classifies each performance-affecting feature by measuring its CPU cost and its
// accuracy delta TOGETHER (build.md "Performance & fidelity probes", dsp.md "HQ / Eco mode").
//
// The point of measuring them together is that neither number decides anything alone. A feature
// that is nearly free but makes the model no better is clutter, not a win; one that helps but costs
// real CPU is an HQ candidate; one that costs nothing and still makes the model WORSE has to be off
// however cheap it is. dsp.md's rule is not to add an HQ button reflexively but to let this probe
// decide, and on this pedal it decides against: there is no lever worth a switch.
//
// PerfBenchmark and OSFidelity report the raw tables. This one is the verdict, and it exists so the
// verdict is reproducible rather than remembered.
//
// Finite-only as a test, for the same reason as the other two probes: the CPU figures are
// machine-specific and the accuracy figures are what is being reported, so gating on either would
// make the probe circular.

#include <cstdio>

#include "ProbeHarness.h"

using namespace pedal::probe;

namespace
{
// A feature is "free" below this. 0.25 percentage points of realtime is well under the run-to-run
// scatter of the timer on a loaded machine, so anything under it cannot be called a cost honestly.
constexpr double kFreeCpuPp = 0.25;
// Accuracy deltas under this are inside the measurement's own repeatability; calling one an
// improvement or a regression would be reading noise.
constexpr double kNeutralDb = 1.0;

constexpr int kToneBin = 3413; // 4999.6 Hz, exactly on an FFT bin
constexpr double kSeconds = 3.0;
constexpr double kSmall = 0.02;

/** One configuration, measured on every axis that matters, so the axes are never collapsed into a
 *  single score. An earlier version of this probe DID collapse them -- it added the alias-floor
 *  improvement to the top-octave magnitude change and called the sum "net dB" -- and it scored ADAA
 *  at 1x a "free win" on a +2.64 dB total. That verdict was an artefact of the arithmetic: a dB of
 *  alias floor sitting at -63 dBc and a dB of frequency response at 12 kHz are not the same
 *  quantity, and nothing justifies exchanging them one for one. There is no honest weighting to
 *  reach for either, so this probe does not invent one. It compares whole configurations instead. */
struct Point
{
    double cpuPp = 0.0;
    double aliasDbc = 0.0; // hot single tone; lower is a quieter fold-back floor
    double hf12kDb = 0.0;  // 12 kHz relative to 1 kHz; lower is a darker top octave
};

Point measure(PedalAudioProcessor& proc, const Setup& base, bool* finite)
{
    Point p;
    p.cpuPp = cpuPercent(proc, base, kSeconds, finite);
    Setup hot = base;
    hot.inputTrimDb = 12.0;
    p.aliasDbc = toneSpectrum(proc, hot, 1.0, kToneBin, finite).aliasDbc;
    p.hf12kDb = relativeResponseDb(proc, base, 12000.0, 1000.0, kSmall);
    return p;
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PedalAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);

    bool finite = true;

    std::printf("Echo Pre 3 -- feature profile. CPU in percentage points of realtime; accuracy in dB.\n");
    std::printf("Free below %.2f pp; accuracy deltas within +-%.1f dB are read as no change.\n", kFreeCpuPp,
                kNeutralDb);

    // ---- THE ONE REMAINING LEVER: how many Newton iterations the JFET solve is given.
    //
    // ⚠ This test used to profile ADAA, which was the chain's only CPU-versus-accuracy knob. ADAA
    // went with the device-model rewrite -- the map is 2-D now (overdrive and drain-source voltage),
    // so ADAA1's derivation does not apply; PluginProcessor.h has the reasoning and the measurements
    // that say nothing audible was lost. What replaced it as the lever is the solve itself: the
    // square law with its load line needs a safeguarded Newton, and the iteration count is a
    // straight trade of CPU against how far the solve converges.
    //
    // The accuracy axis here is the same pair OSFidelity uses -- the alias floor and the wanted
    // harmonics -- because an under-converged solve shows up as BOTH: the residual is a per-sample
    // error, so it is broadband, and it is signal-dependent, so it also moves the wanted content.
    std::printf("\n========== FEATURE: Newton iterations in the JFET solve ==========\n");
    std::printf("  Shipped count is JfetStage::kSolveIters. Fewer is cheaper; the question is where\n");
    std::printf("  it stops being free.\n");
    std::printf("  ⚠⚠ THIS TABLE UNDER-DISCRIMINATES, AND THE COUNT WAS NOT CHOSEN FROM IT. Every\n");
    std::printf("  column here is a CHAIN-level measurement, and all three are largely blind to how far\n");
    std::printf("  the solve converged: the alias and 12 kHz figures are taken at +12 dB of trim where\n");
    std::printf("  the output is clipped hard enough to swamp it, and even H2 moves under 0.2 dB from 2\n");
    std::printf("  iterations to 20. An earlier cut of this test therefore read 2 iterations as\n");
    std::printf("  'converged -- buys nothing' when a stage-level probe put its H2 14.5 dB out.\n");
    std::printf("  ➡ JfetStageTest section 8c is the authority: it drives the STAGE at a known gate\n");
    std::printf("  voltage, so the load-line region is reached squarely instead of marginally. What\n");
    std::printf("  this table is good for is the CPU column, which is exact and is the whole trade.\n\n");
    std::printf("  %-16s %9s %11s %11s %11s\n", "configuration", "CPU %", "alias dBc", "12 kHz dB", "H2 err dB");

    constexpr int kIterCounts[] = {2, 4, 6, 10, 20};
    constexpr int kNumIter = (int) (sizeof(kIterCounts) / sizeof(kIterCounts[0]));
    Point byIter[kNumIter];
    double h2[kNumIter];
    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        for (int k = 0; k < kNumIter; ++k)
        {
            Setup s;
            s.osIndex = osIdx;
            s.solveIters = kIterCounts[k];
            byIter[k] = measure(proc, s, &finite);
            Setup h = s;
            h.inputTrimDb = 0.0;
            h2[k] = toneSpectrum(proc, h, 0.5, kToneBin, &finite).h2Dbc;
            std::printf("  %2dx %-12s %8.2f%% %11.2f %11.2f %11.2f\n", kOsFactors[osIdx],
                        (std::string(std::to_string(kIterCounts[k])) + " iters").c_str(),
                        byIter[k].cpuPp, byIter[k].aliasDbc, byIter[k].hf12kDb,
                        h2[k] - h2[kNumIter - 1]);
        }

        // The verdict is per factor and against the CONVERGED answer, not against the neighbouring
        // count: what matters is whether the shipped count has arrived, and a count-to-count delta
        // can be small while both are still far from the root.
        for (int k = 0; k < kNumIter - 1; ++k)
        {
            const double h2Err = h2[k] - h2[kNumIter - 1];
            const double saved = byIter[kNumIter - 1].cpuPp - byIter[k].cpuPp;
            const char* v = (std::abs(h2Err) < kNeutralDb) ? "converged -- the extra iterations buy nothing"
                                                          : "NOT converged";
            std::printf("      %2dx %2d iters vs converged: H2 %+7.2f dB, saves %5.2f pp   %s\n",
                        kOsFactors[osIdx], kIterCounts[k], h2Err, saved, v);
        }
        std::printf("\n");
    }

    std::printf("Read the table as whole configurations, not as a score. The count is a threshold,\n");
    std::printf("never an interpolation, and any single-number summary of two error axes is a\n");
    std::printf("weighting decision wearing a measurement's clothes.\n");


    if (! finite)
    {
        std::printf("\nFAILED: non-finite sample in a render\n");
        return 1;
    }
    std::printf("\nPASS: FeatureProfile (finite; verdicts printed above)\n");
    return 0;
}
