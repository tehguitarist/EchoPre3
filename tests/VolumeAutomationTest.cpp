// VolumeAutomationTest -- does automating VOLUME zipper?
//
// CLAUDE.md has carried this as a known residual since the chain was built: "VOLUME updates per
// block, not per sample (it re-solves WDF impedances). Fast automation may zipper. Normal WDF
// practice; revisit only if it is audible." This is the measurement that decides it, because
// "may zipper" is not a finding and the reasoning cuts both ways -- there IS a 20 ms SmoothedValue
// on the control, but processBlock advances it with skip(numSmp) and applies the single resulting
// value to the whole block, so the ramp is quantised to the host's block grid. Whether that is
// audible depends on the step size, and the step size depends on the control law's slope, which on
// THIS pedal is not a constant: the VOLUME network is non-monotonic (circuit.md validation note #1)
// so dGain/dx runs from very steep near full CCW to zero at the volume peak.
//
// What is measured, at four block sizes spanning 32..2048 samples:
//
//   1. THE ZIPPER ITSELF. A steady tone, a step change in the volume parameter, and the peak
//      sample-to-sample discontinuity in the output that the tone itself cannot account for. The
//      tone's own slew is subtracted by measuring against the SAME render with no parameter change
//      -- not against an analytic slew bound, which would fold the pedal's own phase shift into the
//      residual.
//   2. THE BLOCK-RATE SIGNATURE. A per-block-quantised ramp produces a step proportional to the
//      block length; a per-sample ramp does not. So the artefact must FALL as the block size falls.
//      If it does not, the cause is not the update granularity and this whole framing is wrong.
//   3. THE RAMP IS ACTUALLY RAMPING. A 20 ms SmoothedValue over a 10.7 ms block is only ~2 steps,
//      which is a coarse enough quantisation that it is worth asserting the smoother is engaged at
//      all rather than assuming it: with it defeated the transition would be ONE step of the whole
//      gain change, and the test prints that bound so the two cannot be confused.
//   4. THE WORST PLACE ON THE KNOB, not a convenient one. The sweep is run at the steepest part of
//      the control law as well as across the peak, because a test that automates 0.4 -> 0.6 would
//      measure the one region where the law is flat and report that automation is free.
//
// ⚠ The instrument carries a mutation guard (section 5), the same way BypassClickTest does: the
// same measurement is run against a deliberately DEFEATED smoother, and must report a materially
// larger artefact. Without that, "no zipper found" is equally consistent with an instrument that
// cannot see one.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ProbeHarness.h"

using namespace pedal::probe;

namespace
{
// 2 kHz: 24 samples per period at 48 kHz, so ONE period fits inside even the smallest block under
// test. That is a requirement, not a preference -- the envelope is a one-period matched filter, so
// if a period straddled a block boundary the filter would smear the very step being measured and
// report it as smaller than it is. (The first version used 1 kHz and 32-sample blocks, i.e. two
// thirds of a period per block, and read 0.000 dB for a step that lands entirely in one block.)
constexpr double kToneHz = 2000.0;
constexpr int kBlockSizes[] = {64, 128, 512, 2048};

// Where on the knob to automate. The control law's slope is what sets the step size, and it is
// strongly position-dependent on this pedal, so both the steep end and the flat peak are measured.
struct Move
{
    const char* name;
    double from, to;
};
constexpr Move kMoves[] = {
    {"0.15 -> 0.35  (steep: the bottom of the law)", 0.15, 0.35},
    {"0.35 -> 0.75  (across the volume peak)", 0.35, 0.75},
    {"0.05 -> 0.95  (full sweep, worst case)", 0.05, 0.95},
};

/** Render a steady tone, stepping the `volume` parameter at `jumpAt` samples. Returns channel 0.
 *  Own block loop rather than ProbeHarness::render, because the parameter has to move BETWEEN
 *  blocks and the block size is the independent variable. */
std::vector<double> renderWithJump(PedalAudioProcessor& proc, const Setup& s, int blockSize,
                                   int discard, int n, int jumpAt, double volFrom, double volTo,
                                   bool* finite)
{
    Setup init = s;
    init.volume = volFrom;
    configure(proc, init);
    proc.prepareToPlay(kFs, blockSize); // configure() prepared at kBlock; the block size is the variable

    auto* volParam = proc.apvts.getParameter("volume");

    const int total = discard + n;
    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;
    std::vector<double> out;
    out.reserve((size_t)n);

    bool jumped = false;
    for (int start = 0; start < total; start += blockSize)
    {
        // The step lands on a block boundary, which is where a host's automation lands too.
        if (! jumped && start >= jumpAt)
        {
            if (volParam != nullptr)
                volParam->setValueNotifyingHost(volParam->convertTo0to1((float)volTo));
            jumped = true;
        }

        const int m = juce::jmin(blockSize, total - start);
        buf.clear();
        for (int i = 0; i < m; ++i)
        {
            const float v = (float)(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * kToneHz
                                                    * (double)(start + i) / kFs));
            buf.setSample(0, i, v);
            buf.setSample(1, i, v);
        }
        juce::AudioBuffer<float> sub(buf.getArrayOfWritePointers(), 2, 0, m);
        midi.clear();
        proc.processBlock(sub, midi);

        for (int i = 0; i < m; ++i)
        {
            const double y = (double)sub.getSample(0, i);
            if (! std::isfinite(y))
                *finite = false;
            if (start + i >= discard && (int)out.size() < n)
                out.push_back(y);
        }
    }
    return out;
}

/** The tone's amplitude at each sample, by matched filter over one period. Amplitude-independent
 *  and phase-independent, so it tracks the GAIN the chain is applying and nothing else.
 *
 *  ⚠⚠ THE OBVIOUS METRIC IS WRONG AND THE FIRST VERSION OF THIS TEST USED IT. "Peak per-sample
 *  jump, minus the same render with no parameter move" looks like it cancels the tone's own slew,
 *  and it does not: the moved render ends at a DIFFERENT amplitude, so its legitimate steady-state
 *  slew is larger too, and the difference reports that as a step. It read 0.126 on a full sweep at
 *  a 32-sample block, against the 0.131 a steady 1 kHz tone's own slew gives at that amplitude --
 *  i.e. essentially all of it was the tone. Same asymmetric-comparison trap as circuit.md's shelf
 *  and droop-restore findings: the reference has to differ from the signal in ONLY the thing being
 *  measured.
 *
 *  A zipper IS a staircase in the applied gain, so measure the gain. Smoothed over one period
 *  (1 ms at 1 kHz), which is far shorter than any block under test, so a per-block step stays a
 *  step. */
std::vector<double> envelope(const std::vector<double>& y, double toneHz)
{
    const int N = (int)std::lround(kFs / toneHz); // exactly one period
    const double w = 2.0 * juce::MathConstants<double>::pi * toneHz / kFs;
    std::vector<double> a(y.size(), 0.0);
    for (size_t n = (size_t)N; n < y.size(); ++n)
    {
        double si = 0.0, co = 0.0;
        for (int k = 0; k < N; ++k)
        {
            const double t = (double)(n - (size_t)k);
            si += y[n - (size_t)k] * std::sin(w * t);
            co += y[n - (size_t)k] * std::cos(w * t);
        }
        a[n] = 2.0 * std::sqrt(si * si + co * co) / (double)N;
    }
    return a;
}

/** The largest single-step change in the applied gain, in dB, read on a ONE-PERIOD grid.
 *
 *  ⚠⚠ THE GRID MUST NOT BE THE HOST BLOCK, AND USING IT MADE THIS TEST BLIND TO ITS OWN FIX.
 *  Reading once per host block reports the gain change ACROSS that block, which is the same number
 *  whether the gain moved in one step or in sixty-four -- so when processBlock started slicing the
 *  block into 32-sample chunks, this function returned byte-identical figures and the fix looked
 *  like a no-op. (It was not: the same build nulls bit-for-bit against the previous one on a static
 *  render, so the code had definitely changed.) The grid has to be finer than the thing being
 *  measured, and the finest grid the envelope supports is its own window -- one period.
 *
 *  A read at n covers [n-period+1, n], so two reads one period apart straddle any single step
 *  completely and see its full height. That makes the measurement independent of where the steps
 *  actually fall, which is the property a test of an implementation detail needs.
 *
 *  ⚠ It also picks up the LEGITIMATE gain change across one period during a continuous ramp. That
 *  is real and should be counted: at 0.5 ms per period even a 50 ms full-range sweep contributes
 *  only ~0.3 dB, so it cannot mask a staircase, and a metric that excluded it would be measuring a
 *  model of the automation rather than the automation. */
double worstGainStepDb(const std::vector<double>& y, double toneHz, int /*blockSize*/, int jumpIdx)
{
    const auto a = envelope(y, toneHz);
    const int period = (int)std::lround(kFs / toneHz);

    // ⚠ GATED ON ABSOLUTE LEVEL, and without that the metric over-weights silence. A relative step
    // is only audible if the signal it sits on is audible: this pedal's control law runs to -inf at
    // full CCW, so the bottom of a full sweep passes through -50 dBFS and below, where a 9 dB
    // relative step is a change of 0.0002 in amplitude and is masked by anything at all. Steps more
    // than kAudibleRangeDb below the transition's own peak are therefore not counted. ⛔ This is a
    // gate on the SIGNAL's level, not on the measurement's disagreement with anything -- it cannot
    // hide a defect at a level a listener could hear.
    constexpr double kAudibleRangeDb = 40.0;
    double peak = 0.0;
    for (size_t n = 0; n < a.size(); ++n)
        peak = juce::jmax(peak, a[n]);
    const double floorAmp = peak * std::pow(10.0, -kAudibleRangeDb / 20.0);

    double worst = 0.0, prev = 0.0;
    bool have = false;
    for (int end = juce::jmax(period, jumpIdx - period); end < (int)a.size(); end += period)
    {
        if (a[(size_t)end] <= 0.0)
            continue;
        const double g = db(a[(size_t)end]);
        if (have && a[(size_t)end] >= floorAmp)
            worst = juce::jmax(worst, std::abs(g - prev));
        prev = g;
        have = true;
    }
    return worst;
}

} // namespace

int main()
{
    PedalAudioProcessor proc;
    bool ok = true, finite = true;

    std::printf("VolumeAutomationTest -- is the per-block VOLUME update audible?\n");
    std::printf("  tone %.0f Hz, volume stepped on a block boundary, 8x OS, Dark.\n", (double)kToneHz);
    std::printf("  Metric: the largest single-period step in the APPLIED GAIN, in dB. A zipper is a\n");
    std::printf("  staircase in the gain, so the staircase height is the quantity -- independent of\n");
    std::printf("  how large the overall move was.\n\n");

    struct Row { double shipped, defeated; };
    std::vector<Row> rows;
    double worst = 0.0, worstDefeated = 0.0;

    for (const auto& mv : kMoves)
    {
        std::printf("  %s\n", mv.name);
        std::printf("    %8s %16s %16s\n", "block", "gain step dB", "smoother off");
        for (int bs : kBlockSizes)
        {
            Setup s;
            s.osIndex = 3;
            s.modeIndex = 1;

            // The jump lands on a block boundary and a whole number of blocks into the window, so
            // the per-block read grid lines up with the blocks the processor actually saw.
            const int jumpAt = juce::jmax(16384, 8 * bs);
            const int discard = jumpAt - 4 * bs;
            const int n = 32768;
            const int jumpIdx = jumpAt - discard;   // where the jump sits in the returned window

            auto moved = renderWithJump(proc, s, bs, discard, n, jumpAt, mv.from, mv.to, &finite);
            const double gs = worstGainStepDb(moved, kToneHz, bs, jumpIdx);

            proc.setVolumeSmoothingEnabled(false);
            auto movedNs = renderWithJump(proc, s, bs, discard, n, jumpAt, mv.from, mv.to, &finite);
            proc.setVolumeSmoothingEnabled(true);
            const double gsNs = worstGainStepDb(movedNs, kToneHz, bs, jumpIdx);

            worst = juce::jmax(worst, gs);
            rows.push_back({gs, gsNs});
            // ⚠ The guard compares at the SMALLEST block only. At 2048 samples (42.7 ms) one block
            // is longer than the 20 ms ramp, so the smoother cannot do anything and shipped ==
            // defeated BY CONSTRUCTION -- taking the worst over all sizes picks exactly that case
            // and reports a working instrument as blind.
            if (bs == kBlockSizes[0])
                worstDefeated = juce::jmax(worstDefeated, gsNs);
            std::printf("    %8d %16.3f %16.3f\n", bs, gs, gsNs);
        }
        std::printf("\n");
    }

    // ---- MUTATION GUARD: the instrument must see the known-bad configuration ------------------
    std::printf("  MUTATION GUARD -- with the 20 ms ramp defeated the whole move lands in one\n");
    std::printf("  block, so the gain step should be the full move. At the smallest block:\n");
    std::printf("  %.3f dB defeated against %.3f dB shipped.\n", worstDefeated, rows[0].shipped);
    if (worstDefeated < rows[0].shipped * 1.5)
    {
        std::printf("  FAIL: defeating the smoother did not materially increase the measured step,\n"
                    "        so the instrument cannot see a zipper and the figures above mean\n"
                    "        nothing.\n");
        ok = false;
    }
    else
    {
        std::printf("  OK: the guard fires, so the instrument does detect a gain staircase.\n");
    }

    // ---- BLOCK-SIZE INDEPENDENCE ---------------------------------------------------------------
    // ⭐ This is the assertion that the fix is actually in effect, and it is a stronger one than
    // "the number got smaller". VOLUME is applied every kVolumeChunk base-rate samples, which is
    // finer than any block size under test -- so the staircase height must NOT depend on the host's
    // block size at all. Before the fix it scaled with it (0.626 -> 8.628 dB across 64..2048 on the
    // first move); if that dependence ever comes back, the slicing has stopped engaging.
    std::printf("\n  BLOCK-SIZE INDEPENDENCE: VOLUME updates every %d samples, finer than any block\n",
                PedalAudioProcessor::kVolumeChunkDefault);
    std::printf("  here, so the step must not depend on the block size.\n");
    bool independent = true;
    for (size_t m = 0; m < (size_t)(sizeof(kMoves) / sizeof(kMoves[0])); ++m)
    {
        const size_t base = m * (sizeof(kBlockSizes) / sizeof(kBlockSizes[0]));
        double lo = 1.0e9, hi = 0.0;
        for (size_t k = 0; k < sizeof(kBlockSizes) / sizeof(kBlockSizes[0]); ++k)
        {
            lo = juce::jmin(lo, rows[base + k].shipped);
            hi = juce::jmax(hi, rows[base + k].shipped);
        }
        std::printf("    %-46s  spread %.3f dB over 64..2048\n", kMoves[m].name, hi - lo);
        if (hi - lo > 0.05)
            independent = false;
    }
    if (! independent)
    {
        std::printf("    FAIL: the step still depends on the host block size, so VOLUME is being\n"
                    "          applied per block somewhere -- the slicing is not engaging.\n");
        ok = false;
    }

    // ---- THE CASE A HOST ACTUALLY PRODUCES -----------------------------------------------------
    // The jumps above are the pathological case (a preset recall, or a parameter snapped in one
    // block). Ordinary automation and a knob drag send a SEQUENCE of small moves, one per block, so
    // the step is set by the automation's own rate and the 20 ms ramp contributes almost nothing.
    // That is the case a user hears, so it is measured rather than argued about.
    std::printf("\n  CONTINUOUS AUTOMATION -- a full 0.05 -> 0.95 sweep spread over a ramp time,\n");
    std::printf("  the parameter moved once per block as a host would. This is the realistic case.\n");
    std::printf("    %8s %10s %16s\n", "block", "ramp ms", "gain step dB");
    double worstRealistic = 0.0, worstFast = 0.0;
    for (const double rampMs : {50.0, 200.0, 1000.0})
    {
        for (int bs : kBlockSizes)
        {
            Setup s;
            s.osIndex = 3;
            s.modeIndex = 1;
            s.volume = 0.05;
            configure(proc, s);
            proc.prepareToPlay(kFs, bs);

            auto* volParam = proc.apvts.getParameter("volume");
            const int rampSmp = (int)(rampMs * 1.0e-3 * kFs);
            const int warm = 8 * bs;
            const int total = warm + rampSmp + 8 * bs;

            juce::AudioBuffer<float> buf(2, bs);
            juce::MidiBuffer midi;
            std::vector<double> out;
            out.reserve((size_t)total);

            for (int start = 0; start < total; start += bs)
            {
                const double t = juce::jlimit(0.0, 1.0, (double)(start - warm) / (double)rampSmp);
                if (volParam != nullptr && start >= warm)
                    volParam->setValueNotifyingHost(
                        volParam->convertTo0to1((float)(0.05 + 0.90 * t)));

                const int m = juce::jmin(bs, total - start);
                buf.clear();
                for (int i = 0; i < m; ++i)
                {
                    const float v = (float)(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi
                                                            * kToneHz * (double)(start + i) / kFs));
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
                juce::AudioBuffer<float> sub(buf.getArrayOfWritePointers(), 2, 0, m);
                midi.clear();
                proc.processBlock(sub, midi);
                for (int i = 0; i < m; ++i)
                {
                    const double y = (double)sub.getSample(0, i);
                    if (! std::isfinite(y))
                        finite = false;
                    out.push_back(y);
                }
            }
            const double gs = worstGainStepDb(out, kToneHz, bs, warm);
            if (rampMs >= 200.0)
                worstRealistic = juce::jmax(worstRealistic, gs);
            else
                worstFast = juce::jmax(worstFast, gs);
            std::printf("    %8d %10.0f %16.3f\n", bs, rampMs, gs);
        }
    }
    std::printf("  worst at >=200 ms (realistic): %.3f dB | worst at 50 ms: %.3f dB\n",
                worstRealistic, worstFast);

    // ---- THE VERDICT: A CONVERGENCE TEST, NOT A dB THRESHOLD -----------------------------------
    // ⭐⭐ The question is not "is the step small" but "is the step the QUANTISATION or the gain
    // TRAJECTORY". Those want opposite responses: quantisation is a defect to chunk away,
    // trajectory is the control moving fast and there is nothing to fix. A fixed dB bound cannot
    // tell them apart -- a 26 dB move completed in 100 ms legitimately changes the gain by ~1.8 dB
    // in every one-period window, so any bound tight enough to catch a staircase would also fail
    // an ideal per-sample implementation of the same move.
    //
    // So the criterion is convergence: re-measure with the chunk QUARTERED. If the step does not
    // fall, chunking harder buys nothing and what remains is the trajectory. This is the same shape
    // as the solve's iteration-count argument in dsp.md -- compare against a more-converged version
    // of yourself rather than against a number somebody chose.
    std::printf("\n  CONVERGENCE -- is the residual step the quantisation, or the trajectory?\n");
    std::printf("  Re-measured with the VOLUME chunk quartered. A step that does not fall is the\n");
    std::printf("  gain genuinely moving, and no amount of finer chunking would help it.\n");
    char shippedLabel[24], fineLabel[24];
    std::snprintf(shippedLabel, sizeof shippedLabel, "chunk %d",
                  PedalAudioProcessor::kVolumeChunkDefault);
    std::snprintf(fineLabel, sizeof fineLabel, "chunk %d",
                  PedalAudioProcessor::kVolumeChunkDefault / 4);
    std::printf("    %-46s %11s %11s %9s\n", "move", shippedLabel, fineLabel, "ratio");
    bool converged = true;
    // ⚠⚠ THE CONVERGENCE CHECK GOES VACUOUS IF THE CHUNK EXCEEDS THE BLOCK, and that was verified
    // the hard way: with the fix deliberately reverted (chunk 4096) this section reported ratios of
    // exactly 1.00 and PASSED, because neither 4096 nor 1024 slices a 512-sample block so the two
    // renders were identical. The other two sections caught the reversion (block-size independence
    // read a 22 dB spread, and the realistic-automation bound read 15 dB) -- but a section that
    // passes by doing nothing is worse than no section, so the precondition is asserted.
    if (PedalAudioProcessor::kVolumeChunkDefault >= 512)
    {
        std::printf("    FAIL: the chunk (%d) is not smaller than this section's 512-sample block,\n"
                    "          so neither render slices and the comparison is vacuous.\n",
                    PedalAudioProcessor::kVolumeChunkDefault);
        ok = false;
    }
    for (const auto& mv : kMoves)
    {
        Setup s;
        s.osIndex = 3;
        s.modeIndex = 1;
        const int bs = 512;
        const int jumpAt = 16384, discard = jumpAt - 4 * bs, n = 32768;
        const int jumpIdx = jumpAt - discard;

        proc.setVolumeChunk(0); // 0 = restore the shipped default, never inherit
        auto a32 = renderWithJump(proc, s, bs, discard, n, jumpAt, mv.from, mv.to, &finite);
        const double g32 = worstGainStepDb(a32, kToneHz, bs, jumpIdx);

        proc.setVolumeChunk(PedalAudioProcessor::kVolumeChunkDefault / 4);
        auto a8 = renderWithJump(proc, s, bs, discard, n, jumpAt, mv.from, mv.to, &finite);
        const double g8 = worstGainStepDb(a8, kToneHz, bs, jumpIdx);
        proc.setVolumeChunk(0);

        const double ratio = g8 > 1.0e-9 ? g32 / g8 : 1.0;
        std::printf("    %-46s %11.3f %11.3f %9.2f\n", mv.name, g32, g8, ratio);
        // A quartered chunk that barely moves the step (ratio near 1) means the step is the
        // trajectory. A ratio well above 1 means quantisation is still dominating and the shipped
        // chunk is too coarse.
        if (ratio > 1.25)
            converged = false;
    }
    if (! converged)
    {
        std::printf("    FAIL: quartering the chunk materially reduced the step, so the shipped\n"
                    "          chunk is still the dominant term -- it is too coarse.\n");
        ok = false;
    }
    else
    {
        std::printf("    OK: the residual step is the gain trajectory, not the update rate.\n");
    }

    // A backstop on the REALISTIC cases only -- an automated fade at a musically plausible rate.
    // The pathological rows (a full-range move completed instantly or in 50 ms) are excluded and
    // reported instead: 26 dB in 50 ms is a gesture whose own trajectory exceeds this bound, which
    // is exactly what the convergence test above exists to distinguish.
    constexpr double kRealisticBoundDb = 0.5;
    std::printf("\n  realistic automation (>=200 ms for a full-range sweep): worst %.3f dB, "
                "bound %.3f dB\n", worstRealistic, kRealisticBoundDb);
    if (worstRealistic > kRealisticBoundDb)
    {
        std::printf("  FAIL: ordinary VOLUME automation steps audibly.\n");
        ok = false;
    }
    std::printf("  pathological cases, reported not gated: instantaneous full-range jump %.3f dB, "
                "50 ms full-range sweep %.3f dB\n", rows[8].shipped, worstFast);
    if (! finite)
    {
        std::printf("  FAIL: non-finite output.\n");
        ok = false;
    }

    std::printf("\n%s\n", ok ? "VolumeAutomationTest PASSED" : "VolumeAutomationTest FAILED");
    return ok ? 0 : 1;
}
