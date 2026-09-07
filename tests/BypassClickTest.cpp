// BypassClickTest -- does skipping the DSP while bypassed introduce a click, and does it change the
// audio once the pedal is switched back on?
//
// architecture.md specifies "DSP skipped when bypassed" and a ~5 ms crossfade on the transition.
// Skipping is what makes the transition risky: while skipped the WDF capacitors and the JFET shelf's
// state stop tracking the signal, and the oversampler stops being fed, so re-engaging restarts a
// chain that is no longer in step with its input. That is a correctness question with an audible
// answer, so it is measured here rather than reasoned about.
//
// Four things are checked at every oversampling factor, over eight toggle phases spread across one
// period of the test tone -- toggling only at a zero crossing, or only on the test's own block grid,
// would test the easiest case and call it the general one:
//
//   1. TRUE BYPASS IS EXACT. Once settled to dry, the output is the input sample for sample. This is
//      what proves the skip path is a passthrough and not a very quiet wet path.
//   2. NO DISCONTINUITY, in either direction. The per-sample step is bounded by what the crossfade
//      itself can produce; anything above that is a cut, not a fade.
//   3. THE RE-ENGAGE TRANSIENT IS SMALL. Restarting the chain against a live signal steps its
//      high-passes, and that settles over ~22 ms -- longer than the 5 ms fade covers. It is bounded
//      here rather than certified: the figure the table reports is small (under 2% of the wet peak)
//      because the oversampler is reset alongside, so its FIR ramps the chain's input up from zero
//      instead of stepping it. NOTE for anyone reading processBlock's state comment: this measure
//      did NOT choose reset over resume -- the two came out a wash and determinism decided it.
//   4. THE SKIP DOES NOT CHANGE THE AUDIO. Once settled, the toggled render matches a render that
//      was never bypassed. A performance optimisation that alters the steady state is a voicing
//      change wearing an optimisation's clothes.
//
// The discontinuity instrument carries its own mutation guard (section 5): the same measurement is
// run against a synthetic HARD CUT between the two paths, and must reject it. Without that, "no
// click found" is equally consistent with an instrument that cannot see one. It has also been
// checked against a real defect rather than only a synthetic one -- with the oversampler left
// unreset on entering the skip, section 2 fails at 2x, 4x and 8x (1.09x, 1.54x and 1.63x the bound),
// which is what established that the oversampler reset is load-bearing.

#include <cmath>
#include <cstdio>
#include <vector>

#include "ProbeHarness.h"

using namespace pedal::probe;

namespace
{
constexpr double kToneHz = 1000.0; // 48 samples per period at 48 kHz -- real slew, and short enough
constexpr double kToneAmp = 0.3;   // that eight toggle phases genuinely span the waveform
constexpr int kTotal = (int) (1.0 * kFs);
constexpr int kFadeSamples = (int) (0.005 * kFs); // architecture.md's ~5 ms bypass crossfade
constexpr int kEngageAt = 9600;                   // 0.20 s, plus a per-trial phase offset
constexpr int kBypassFor = 9600; // 0.20 s bypassed, plus a per-trial offset. 9600 samples is
constexpr int kHoldStep = 6;     // EXACTLY 200 periods of the test tone, so a fixed hold would put
                                 // any frozen state back perfectly in phase with the live signal --
                                 // an accident that makes "resume the stale state" look free. The
                                 // per-trial offset sweeps the held phase across the period so the
                                 // staleness is real.
constexpr int kSettleSamples = (int) (0.30 * kFs);
constexpr int kNumPhases = 8;
constexpr int kPhaseStep = 7; // 7 samples x 8 trials mod a 48-sample period = 8 distinct phases

/** The stimulus, already rounded to the float the buffer will actually carry -- comparing a true
 *  bypass against the unrounded double would fail by one float ulp and prove nothing. */
inline double tone(int i)
{
    return (double) (float) (kToneAmp * std::sin(2.0 * M_PI * kToneHz * (double) i / kFs));
}

/** Render `kTotal` samples, flipping the bypass parameter at each sample index in `flips`. Blocks
 *  are split exactly at the flip points, so a toggle lands mid-block at an arbitrary phase of the
 *  waveform rather than wherever the test's own block grid happened to fall. */
std::vector<double> renderToggling(PedalAudioProcessor& proc, const Setup& s, const std::vector<int>& flips,
                                   bool* finite)
{
    configure(proc, s);
    auto* bypassParam = proc.apvts.getParameter("bypass");

    std::vector<double> out((size_t) kTotal, 0.0);
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;

    bool state = s.bypass;
    size_t nextFlip = 0;
    int pos = 0;

    while (pos < kTotal)
    {
        while (nextFlip < flips.size() && flips[nextFlip] <= pos)
        {
            state = ! state;
            if (bypassParam != nullptr)
                bypassParam->setValueNotifyingHost(state ? 1.0f : 0.0f);
            ++nextFlip;
        }

        int limit = kTotal;
        if (nextFlip < flips.size())
            limit = juce::jmin(limit, flips[nextFlip]);
        const int m = juce::jmin(kBlock, limit - pos);

        buf.clear();
        for (int i = 0; i < m; ++i)
        {
            const float v = (float) tone(pos + i);
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        juce::AudioBuffer<float> sub(buf.getArrayOfWritePointers(), 2, 0, m);
        midi.clear();
        proc.processBlock(sub, midi);

        for (int i = 0; i < m; ++i)
        {
            const double y = (double) sub.getSample(0, i);
            if (! std::isfinite(y))
                *finite = false;
            out[(size_t) (pos + i)] = y;
        }
        pos += m;
    }
    return out;
}

/** Largest sample-to-sample step over [from, to). This is the whole click instrument: a crossfade
 *  bounds it, a cut does not. */
double maxStep(const std::vector<double>& y, int from, int to, int* whereOut = nullptr)
{
    double worst = 0.0;
    int where = -1;
    for (int n = juce::jmax(1, from); n < to; ++n)
    {
        const double d = std::abs(y[(size_t) n] - y[(size_t) (n - 1)]);
        if (d > worst)
        {
            worst = d;
            where = n;
        }
    }
    if (whereOut != nullptr)
        *whereOut = where;
    return worst;
}

/** y minus the never-bypassed render: everything the toggle added and nothing else. */
std::vector<double> residual(const std::vector<double>& y, const std::vector<double>& ref)
{
    std::vector<double> r((size_t) kTotal, 0.0);
    for (int n = 0; n < kTotal; ++n)
        r[(size_t) n] = y[(size_t) n] - ref[(size_t) n];
    return r;
}

double maxDiff(const std::vector<double>& a, const std::vector<double>& b, int from, int to)
{
    double worst = 0.0;
    for (int n = from; n < to; ++n)
        worst = std::max(worst, std::abs(a[(size_t) n] - b[(size_t) n]));
    return worst;
}

double peakAbs(const std::vector<double>& y)
{
    double p = 0.0;
    for (double v : y)
        p = std::max(p, std::abs(v));
    return p;
}
} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PedalAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);

    bool finite = true, ok = true;

    std::printf("Echo Pre 3 -- bypass skip: click, passthrough exactness, and settled equivalence\n");
    std::printf("%g Hz tone at %.2f FS, %d toggle phases per factor, %d-sample crossfade.\n\n", kToneHz, kToneAmp,
                kNumPhases, kFadeSamples);

    // ---- 1/2. Per factor: the two reference renders, then every toggle phase against them.
    std::printf("========== 1-4. TOGGLE BEHAVIOUR PER OVERSAMPLING FACTOR ==========\n");
    std::printf("  step      = largest sample-to-sample jump anywhere in the toggling render\n");
    std::printf("  bound     = what the crossfade alone can produce: max|dW| + max|dD| + max|D-W|/%d\n",
                kFadeSamples);
    std::printf("  dry err   = |output - input| while bypassed; true bypass makes this exactly zero\n");
    std::printf("  transient = peak departure from the never-bypassed render after the fade completes\n");
    std::printf("  settled   = the same, %.2f s later, in dB relative to the wet peak\n\n", kSettleSamples / kFs);
    std::printf("  %-8s %10s %10s %8s %10s %10s %10s\n", "factor", "step", "bound", "step/bd", "dry err",
                "transient", "settled dB");

    std::vector<double> keptWet, keptDry, keptToggled; // one representative case for section 5
    int keptEngageAt = 0;

    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup s;
        s.osIndex = osIdx;

        s.bypass = false;
        const auto wetRef = renderToggling(proc, s, {}, &finite);
        s.bypass = true;
        const auto dryRef = renderToggling(proc, s, {}, &finite);
        s.bypass = false;

        // The dry reference must be the input itself, or every comparison below is against the
        // wrong thing. Checked before it is used, not assumed.
        double dryRefErr = 0.0;
        for (int n = 0; n < kTotal; ++n)
            dryRefErr = std::max(dryRefErr, std::abs(dryRef[(size_t) n] - tone(n)));
        if (dryRefErr > 1.0e-9)
        {
            std::printf("  FAIL: steady bypassed render is not the input (max err %.3e)\n", dryRefErr);
            ok = false;
        }

        const double wetPeak = peakAbs(wetRef);
        const double bound =
            maxStep(wetRef, 1, kTotal) + maxStep(dryRef, 1, kTotal) + maxDiff(wetRef, dryRef, 0, kTotal) / kFadeSamples;

        double worstStep = 0.0, worstDryErr = 0.0, worstTransient = 0.0, worstSettled = 0.0;
        double worstResidualStep = 0.0;
        int worstStepWhere = -1, worstStepRelease = -1, worstStepEngage = -1;

        for (int k = 0; k < kNumPhases; ++k)
        {
            const int engageAt = kEngageAt + k * kPhaseStep;
            const int releaseAt = engageAt + kBypassFor + k * kHoldStep;
            const auto y = renderToggling(proc, s, {engageAt, releaseAt}, &finite);

            int where = -1;
            const double st = maxStep(y, 1, kTotal, &where);
            if (st > worstStep)
            {
                worstStep = st;
                worstStepWhere = where;
                worstStepEngage = engageAt;
                worstStepRelease = releaseAt;
            }
            // The restart's own contribution, isolated: the residual AFTER the fade has finished,
            // so what is left is the chain settling and not the crossfade doing its job.
            worstResidualStep = std::max(
                worstResidualStep,
                maxStep(residual(y, wetRef), releaseAt + kFadeSamples, releaseAt + kSettleSamples));
            // Bypassed and settled: the output must be the input, bit for bit.
            worstDryErr = std::max(worstDryErr, maxDiff(y, dryRef, engageAt + kFadeSamples, releaseAt));
            // Re-engaged, fade over: everything left is the restarted chain settling.
            worstTransient =
                std::max(worstTransient, maxDiff(y, wetRef, releaseAt + kFadeSamples, releaseAt + kSettleSamples));
            worstSettled = std::max(worstSettled, maxDiff(y, wetRef, releaseAt + kSettleSamples, kTotal));

            if (osIdx == 3 && k == 0)
            {
                keptWet = wetRef;
                keptDry = dryRef;
                keptToggled = y;
                keptEngageAt = engageAt;
            }
        }

        const double settledDb = db(worstSettled / std::max(wetPeak, 1.0e-300));
        std::printf("  %6dx %10.5f %10.5f %8.2f %10.2e %10.5f %10.1f\n", kOsFactors[osIdx], worstStep, bound,
                    worstStep / bound, worstDryErr, worstTransient, settledDb);
        // Where the worst step lands is the diagnostic, not just how big it is: at 4x and 8x it
        // sits at the reported latency after the toggle, which is the oversampler's FIR
        // delivering the wet path's first real content into a fade already part way through.
        std::printf("           worst step %+d smp after re-engage (latency %d); post-fade restart slew %.5f\n",
                    worstStepWhere - worstStepRelease, proc.getLatencySamples(), worstResidualStep);
        juce::ignoreUnused(worstStepEngage);

        if (worstStep > bound)
        {
            std::printf("    FAIL: step %.5f exceeds the crossfade bound %.5f -- that is a cut, not a fade\n",
                        worstStep, bound);
            ok = false;
        }
        if (worstDryErr > 1.0e-9)
        {
            std::printf("    FAIL: bypassed output differs from the input by %.3e -- not true bypass\n", worstDryErr);
            ok = false;
        }
        // A tenth of the signal is a thump anyone would hear. The measured figures sit orders of
        // magnitude under it; this gate exists to catch a future change that reintroduces one, not
        // to certify the present number, which the table reports rather than asserts.
        if (worstTransient > 0.1 * wetPeak)
        {
            std::printf("    FAIL: re-engage transient %.5f is over a tenth of the wet peak %.5f\n", worstTransient,
                        wetPeak);
            ok = false;
        }
        if (settledDb > -80.0)
        {
            std::printf("    FAIL: settled output is %.1f dB from the never-bypassed render -- the skip is\n"
                        "          not transparent, so it has changed the pedal rather than saved CPU\n",
                        settledDb);
            ok = false;
        }
    }

    // ---- 5. Mutation guard. The instrument above found no click; this shows it is capable of
    //         finding one. A hard cut between the same two paths, at the same instant, must be
    //         rejected by the same bound -- otherwise section 2 proves nothing.
    std::printf("\n========== 5. MUTATION GUARD: the step instrument must reject a hard cut ==========\n");
    if (keptWet.empty())
    {
        std::printf("  FAIL: no representative case was kept\n");
        ok = false;
    }
    else
    {
        std::vector<double> hardCut = keptWet;
        for (int n = keptEngageAt; n < kTotal; ++n)
            hardCut[(size_t) n] = keptDry[(size_t) n];

        const double bound = maxStep(keptWet, 1, kTotal) + maxStep(keptDry, 1, kTotal)
                             + maxDiff(keptWet, keptDry, 0, kTotal) / kFadeSamples;
        const double cutStep = maxStep(hardCut, 1, kTotal);
        const double realStep = maxStep(keptToggled, 1, kTotal);

        std::printf("  hard cut step %.5f  vs  bound %.5f  (x%.1f)\n", cutStep, bound, cutStep / bound);
        std::printf("  shipped  step %.5f  vs  bound %.5f  (x%.2f)\n", realStep, bound, realStep / bound);
        if (cutStep <= bound)
        {
            std::printf("  FAIL: the instrument passes a hard cut, so it cannot detect a click at all\n");
            ok = false;
        }
        else
        {
            std::printf("  PASS: the bound separates a cut from the shipped crossfade\n");
        }
    }

    if (! finite)
    {
        std::printf("\nFAILED: non-finite sample in a render\n");
        return 1;
    }
    if (! ok)
    {
        std::printf("\nFAILED: BypassClickTest\n");
        return 1;
    }
    std::printf("\nPASS: BypassClickTest (true bypass exact, no discontinuity, settled output unchanged)\n");
    return 0;
}
