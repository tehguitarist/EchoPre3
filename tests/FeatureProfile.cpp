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

    // ---- THE SOLVE: closed form against the safeguarded Newton it replaced.
    //
    // ⚠ This test used to profile ADAA, which went with the device-model rewrite. What replaced it
    // is not a trade at all: Shichman-Hodges is piecewise quadratic and both networks around it are
    // linear in the drain current, so every branch has an exact algebraic root. The closed form is
    // not an approximation of the iterative solve, it is the same answer -- JfetStageTest 8c pins
    // them together at 1e-17 A. So the only axis here is CPU, and the accuracy columns are printed
    // to demonstrate that they do NOT move rather than to weigh anything against each other.
    bool equivalent = true;
    std::printf("\n========== FEATURE: closed-form solve vs iterative ==========\n");
    std::printf("  %-18s %9s %11s %11s\n", "configuration", "CPU %", "alias dBc", "12 kHz dB");

    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup cf;
        cf.osIndex = osIdx;
        cf.closedForm = true;
        Setup it = cf;
        it.closedForm = false;
        it.solveIters = 8; // the count the iterative path shipped at

        const Point pc = measure(proc, cf, &finite);
        const Point pi = measure(proc, it, &finite);
        std::printf("  %2dx %-14s %8.2f%% %11.2f %11.2f\n", kOsFactors[osIdx], "closed form", pc.cpuPp,
                    pc.aliasDbc, pc.hf12kDb);
        std::printf("  %2dx %-14s %8.2f%% %11.2f %11.2f\n", kOsFactors[osIdx], "iterative x8", pi.cpuPp,
                    pi.aliasDbc, pi.hf12kDb);

        const double aliasErr = pc.aliasDbc - pi.aliasDbc;
        const double hfErr = pc.hf12kDb - pi.hf12kDb;
        std::printf("      %2dx: %.2f pp cheaper, alias %+.3f dB, 12 kHz %+.3f dB   %s\n", kOsFactors[osIdx],
                    pi.cpuPp - pc.cpuPp, aliasErr, hfErr,
                    (std::abs(aliasErr) < kNeutralDb && std::abs(hfErr) < kNeutralDb)
                        ? "same answer, less CPU"
                        : "DIFFERENT ANSWER -- the closed form is not equivalent");
        if (std::abs(aliasErr) > kNeutralDb || std::abs(hfErr) > kNeutralDb)
            equivalent = false;
    }

    std::printf("\nThe closed form is exact, so there is nothing to weigh: it is the same equations\n");
    std::printf("solved algebraically instead of by iteration. The iterative path is retained only as\n");
    std::printf("the independent oracle JfetStageTest 8c checks against.\n");

    if (! finite)
    {
        std::printf("\nFAILED: non-finite sample in a render\n");
        return 1;
    }
    if (! equivalent)
    {
        std::printf("\nFAILED: the closed-form solve is not equivalent to the iterative one\n");
        return 1;
    }
    std::printf("\nPASS: FeatureProfile (finite; verdicts printed above)\n");
    return 0;
}
