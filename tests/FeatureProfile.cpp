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

    // ---- ADAA, at every factor, measured on all three axes at once. No score, no weighting: the
    //      table is read by comparing whole configurations, which is the only comparison the
    //      measurement actually supports.
    std::printf("\n========== FEATURE: ADAA on the JFET shaper ==========\n");
    std::printf("  %-16s %9s %11s %11s\n", "configuration", "CPU %", "alias dBc", "12 kHz dB");

    Point plain[4], withAdaa[4];
    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup off;
        off.osIndex = osIdx;
        off.adaa = Adaa::forceOff;
        Setup on = off;
        on.adaa = Adaa::forceOn;
        plain[osIdx] = measure(proc, off, &finite);
        withAdaa[osIdx] = measure(proc, on, &finite);

        std::printf("  %2dx %-12s %8.2f%% %11.2f %11.2f\n", kOsFactors[osIdx], "ADAA off", plain[osIdx].cpuPp,
                    plain[osIdx].aliasDbc, plain[osIdx].hf12kDb);
        std::printf("  %2dx %-12s %8.2f%% %11.2f %11.2f\n", kOsFactors[osIdx], "ADAA on", withAdaa[osIdx].cpuPp,
                    withAdaa[osIdx].aliasDbc, withAdaa[osIdx].hf12kDb);
    }

    // ---- THE VERDICT. The question the plugin actually has to answer is per-factor: the user
    //      picks the oversampling factor, and given that choice, should ADAA be on? So the decision
    //      is ADAA on against ADAA off AT THAT FACTOR, and the two error axes really do trade there.
    //
    //      A budget is needed to trade them, and inventing a dB-for-dB exchange rate would be a
    //      weighting decision disguised as a measurement. So the budget is stated as what it is: a
    //      frequency-response error at 12 kHz is a broadband, always-present coloration, while the
    //      alias floor is inharmonic content sitting far below the fundamental only under a drive
    //      no guitar produces. ADAA therefore has to keep its top-octave cost inside the response
    //      budget before its alias gain counts for anything at all.
    //
    //      !! kHfBudgetDb is PROVISIONAL. The project's real pass/fail band is meant to come from
    //      M6, the measured spread between two physical units (docs/build-plan.md), and that has not
    //      been measured yet. 1 dB is a placeholder chosen to be defensible, not derived. If M6
    //      shows two real Secret Preamps differ by several dB in the top octave, widen it here and
    //      re-read this table -- the verdict below is only as good as this number.
    constexpr double kHfBudgetDb = 1.0;

    std::printf("\n========== VERDICT: at each factor, should ADAA be on? ==========\n");
    std::printf("  ADAA must keep its top-octave cost inside %.1f dB at 12 kHz before its alias\n", kHfBudgetDb);
    std::printf("  gain counts. That budget is a stated assumption, not a measurement -- see the\n");
    std::printf("  source comment and build-plan.md M6.\n\n");
    std::printf("  %-8s %11s %11s %11s   %s\n", "factor", "alias dB", "12 kHz dB", "CPU pp", "verdict");

    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        // Lower dBc is a quieter floor, so a positive gain means ADAA removed fold-back.
        const double aliasGain = plain[osIdx].aliasDbc - withAdaa[osIdx].aliasDbc;
        const double hfCost = withAdaa[osIdx].hf12kDb - plain[osIdx].hf12kDb; // negative = darker
        const double cpuCost = withAdaa[osIdx].cpuPp - plain[osIdx].cpuPp;

        const char* v;
        if (-hfCost > kHfBudgetDb)
            v = "OFF -- breaks the response budget";
        else if (aliasGain <= kNeutralDb)
            v = "NO-OP -- nothing measurable to remove";
        else
            v = cpuCost <= kFreeCpuPp ? "FREE WIN -- keep always on" : "HQ LEVER -- worth a switch";

        std::printf("  %6dx %+11.2f %+11.2f %+11.2f   %s\n", kOsFactors[osIdx], aliasGain, hfCost, cpuCost, v);
    }

    // ---- Corroboration, on a comparison that needs no budget at all: ADAA competes for the same
    //      job as the next step of the factor, which the user already has. Where doubling the factor
    //      wins on BOTH axes, ADAA is dominated outright and no weighting is required to say so.
    std::printf("\n  Corroboration -- ADAA against simply doubling the factor (no budget needed):\n");
    std::printf("  %-24s %11s %11s %11s   %s\n", "comparison", "alias dB", "12 kHz dB", "CPU pp", "verdict");

    for (int osIdx = 0; osIdx < 3; ++osIdx)
    {
        const double aliasLead = plain[osIdx + 1].aliasDbc - withAdaa[osIdx].aliasDbc;
        const double hfLead = withAdaa[osIdx].hf12kDb - plain[osIdx + 1].hf12kDb;
        const double cpuLead = plain[osIdx + 1].cpuPp - withAdaa[osIdx].cpuPp;

        char label[64];
        std::snprintf(label, sizeof(label), "%dx + ADAA  vs  %dx plain", kOsFactors[osIdx], kOsFactors[osIdx + 1]);

        const char* v;
        if (std::abs(aliasLead) <= kNeutralDb && std::abs(hfLead) <= kNeutralDb)
            v = "TOSS-UP -- neither is better";
        else if (aliasLead > kNeutralDb && hfLead > -kNeutralDb)
            v = "ADAA wins";
        else
            v = "DOMINATED -- spend the CPU on the factor";

        std::printf("  %-24s %+11.2f %+11.2f %+11.2f   %s\n", label, aliasLead, hfLead, cpuLead, v);
    }

    std::printf("\nRead the tables as whole configurations, not as a score. dsp.md warns the ADAA\n");
    std::printf("benefit is not monotone in rate, so the gate is a threshold on the factor and never\n");
    std::printf("an interpolation -- and any single-number summary of two different error axes is a\n");
    std::printf("weighting decision wearing a measurement's clothes.\n");

    if (! finite)
    {
        std::printf("\nFAILED: non-finite sample in a render\n");
        return 1;
    }
    std::printf("\nPASS: FeatureProfile (finite; verdicts printed above)\n");
    return 0;
}
