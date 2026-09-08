#pragma once

#include <algorithm>
#include <cmath>

#include "InputNetwork.h"

namespace pedal::dsp
{
/**
 * dsp.md's low-oversampling top-octave restore: one first-order high shelf, undoing what the
 * oversampled region's own discretisation takes off the top.
 *
 * ============================= WHAT IT IS CORRECTING, EXACTLY =============================
 * After the mode shelf's discretisation was fixed (build-plan.md §11) the residual 1x droop is
 * mode-independent and comes from one place: the input network's trapezoidal caps. A trapezoidal-cap
 * WDF is the bilinear transform of its own prototype, so the error is known in closed form --
 * InputNetwork::discretisationGain() -- with no filter to run and nothing to measure. Checked
 * against OSFidelity's measured droop at 1x/2x/4x: it predicts every cell to 0.01 dB.
 *
 * ⭐ That is what makes this a DERIVATION rather than a compensator fitted to a residual, which is
 * the distinction dsp.md's own wording ("a single fixed-shape high-shelf, gain set PER OS factor")
 * leaves open. The coefficients fall out of the network's transfer function at whatever
 * (base rate, oversampled rate) the host hands over, so it self-scales to every rate and shrinks as
 * the factor rises -- 0.22 dB of correction at 4x, 0.05 dB at 8x.
 *
 * ============== ⚠ WHERE IT SITS: BEFORE THE JFET, NOT AFTER. MEASURED, NOT ASSUMED. ==============
 * It runs at the OVERSAMPLED rate, immediately after the input network, so it PRE-compensates the
 * drive to the shaper. Putting it at base rate after the whole chain -- which is what "one biquad at
 * base rate" suggests, and what was built first -- corrects the linear path just as well but also
 * boosts the HARMONICS, which never carried the droop in the first place: they are generated
 * downstream of the input network, so nothing has attenuated them. Measured at 1x, 48 kHz:
 *
 *                        no restore    after the chain    before the JFET
 *     droop @18 kHz        -5.03 dB        -1.69 dB          -1.68 dB
 *     wanted H2 vs 8x      +0.07 dB        +0.63 dB          +0.16 dB   <-- the OS factor as voicing
 *     alias floor         -79.39 dBc      -77.86 dBc        -79.31 dBc
 *
 * So the post-chain placement bought its response fix by moving the distortion 0.6 dB and costing
 * 1.5 dB of alias floor; pre-compensation gets the same response for 0.09 dB and 0.08 dB. The one
 * thing it gives up is a little linear accuracy at 2x -- measured droop at 18 kHz is -0.37 dB here
 * against -0.19 dB post-chain -- because the shelf's plateau is pinned at its own Nyquist, and that
 * constraint weakens as the running rate rises above the base rate.
 *
 * ================== ⚠⚠ WHY IT IS DELIBERATELY BOUNDED AND STOPS SHORT ==================
 * The droop runs to -infinity at Nyquist (the bilinear zero sits exactly there), so a restore that
 * chases the top octave has to invert a near-Nyquist zero, which means placing a pole beside it.
 * That was built and measured first: matching the target at 0.325*fs and 0.470*fs tracks the droop
 * to 0.44 dB all the way to 20 kHz -- and does it with +29 to +37 dB of gain at Nyquist, precisely
 * where 1x (which has no decimation filter at all) puts its alias products. A restore that
 * amplifies the aliasing by 30 dB to flatten a response is a net loss however good the plot looks.
 *
 * So the plateau is CAPPED at the droop's own value at kTopFraction*baseRate -- it corrects toward
 * the top of the band and then stops, rather than following the droop into the corner. The cap is
 * what bounds the boost; it is not a tuning knob, it is the declared edge of the correction band.
 *
 *     peak boost   <= 6.6 dB across every base rate and factor  (was 29-37 dB unbounded)
 *
 * ⛔ Requiring an EXACT match at both frequencies instead of capping the plateau is INFEASIBLE for a
 * first-order section at almost every rate in the pre-compensation position (20 of 24 cells tested),
 * and where it is feasible it needs 15-45 dB of boost. The cap is not a compromise forced by
 * laziness; the unbounded design does not exist.
 *
 * The price is the top octave, exactly as dsp.md predicts it must be. Worst |error| against the
 * analog input network over 20 Hz - 20 kHz, at a 48 kHz base rate:
 *
 *     factor            1x       2x       4x       8x
 *     uncorrected     7.941    1.088    0.245    0.060  dB
 *     with restore    3.268    0.604    0.141    0.035  dB
 *
 * and in the part of the band that is actually being corrected (48 kHz base, 1x, dB vs analog):
 *
 *     freq          8k     12k     14k     16k     18k     20k
 *     uncorrected  -0.10  -1.07   -1.94   -3.20   -5.03   -7.95
 *     corrected    +0.33  +0.03   -0.30   -0.80   -1.62   -3.21
 *
 * ================================== THE TWO DESIGN CHOICES ==================================
 * kMatchFraction and kTopFraction are FRACTIONS OF THE BASE RATE, and they are choices, not
 * derivations -- the only two in this file, stated plainly so they are not later mistaken for
 * measured constants. Everything else follows from the circuit. They were picked by scanning both
 * against the worst residual over every base rate in {44.1, 48, 88.2, 96, 176.4, 192} kHz crossed
 * with every factor, subject to the peak boost staying under 8 dB.
 */
class OsDroopRestore
{
public:
    /** Where the correction is made EXACT (12.0 kHz at a 48 kHz base rate). Below this the droop is
     *  under ~1 dB even at 1x, above it the shelf is already past its knee. */
    static constexpr double kMatchFraction = 0.25;

    /** The top of the correction band (19.2 kHz at 48 kHz base). The shelf's plateau is pinned to the
     *  droop's value HERE, which is what bounds the boost -- see the ⚠⚠ note above. */
    static constexpr double kTopFraction = 0.40;

    /** ⚠ Both fractions are CAPPED in absolute terms, at their value at a 48 kHz base rate. Without
     *  the cap a 192 kHz session designs the shelf around 48 and 76.8 kHz -- entirely above the
     *  audible band -- and then slightly overshoots inside it, which the "never worse than doing
     *  nothing" check in DroopRestoreTest catches at 8x. Hearing does not scale with the sample
     *  rate, so above 48 kHz the design stops following it. */
    static constexpr double kMatchHzMax = 12000.0;
    static constexpr double kTopHzMax = 19200.0;

    /** `baseRate` is the host's rate, which sets WHERE the audible band ends and so where the two
     *  design frequencies fall. `osRate` is the rate the input network is discretised at, which sets
     *  HOW MUCH droop there is to undo. `runRate` is the rate this filter itself runs at -- equal to
     *  osRate in the shipped pre-compensation position, and a separate argument only so the
     *  placement stays visible at the call site rather than buried in an assumption here. */
    void prepare(double baseRate, double osRate, double runRate)
    {
        reset();
        makeIdentity();

        // The gains the restore would have to supply at the two design frequencies. Clamped at unity
        // because above the audio band the droop can briefly go the other way, and a restore must
        // never CUT -- it exists to give back what discretisation took.
        const double matchHz = std::min(kMatchFraction * baseRate, kMatchHzMax);
        const double topHz = std::min(kTopFraction * baseRate, kTopHzMax);
        const double g1 = std::max(1.0, 1.0 / InputNetwork::discretisationGain(matchHz, osRate));
        const double gTop = std::max(1.0, 1.0 / InputNetwork::discretisationGain(topHz, osRate));

        // Nothing worth correcting -- a high factor, or a base rate so high the droop never develops.
        // Bypassing outright here is not just an optimisation: it is what keeps the solve below away
        // from its degenerate corner (g1 -> 1 sends the pole to Nyquist).
        if (g1 <= 1.0 + kNegligible || gTop <= g1 * (1.0 + kNegligible))
            return;

        // Unity DC gain, plateau pinned to gTop, exact match at kMatchFraction. With p = b0 + b1 and
        // q = b0 - b1 the magnitude separates as in JfetStage::updateShelf(); imposing |H(0)| = 1
        // gives p = 1 + a1 and |H(Nyquist)| = gTop gives q = gTop*(1 - a1), and the remaining
        // constraint collapses to a ratio with no iteration:
        //
        //     ((1+a1)/(1-a1))^2 = (gTop^2 - g1^2) / (cot^2(pi*kMatchFraction) * (g1^2 - 1))
        //
        // Both sides are positive whenever gTop > g1 > 1, which the guard above has just ensured --
        // so unlike a two-point match (which has no such guarantee and does go infeasible at some
        // rates) this form is always solvable.
        const double cotSq = 1.0 / std::pow(std::tan(M_PI * matchHz / runRate), 2.0);
        const double w = (gTop * gTop - g1 * g1) / (cotSq * (g1 * g1 - 1.0));
        const double r = std::sqrt(w);
        const double a1 = (r - 1.0) / (r + 1.0);

        if (! (std::abs(a1) < kMaxPole))
            return; // degenerate -- stay bypassed rather than ship a filter ringing at Nyquist

        const double p = 1.0 + a1;
        const double q = gTop * (1.0 - a1);
        b0 = 0.5 * (p + q);
        b1 = 0.5 * (p - q);
        pole = a1;
    }

    void reset() noexcept { x1 = 0.0; y1 = 0.0; }

    inline double processSample(double x) noexcept
    {
        const double y = b0 * x + b1 * x1 - pole * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    /** True when prepare() found nothing worth correcting at this rate and factor. Reported by the
     *  probes so "the restore is off" is visible rather than inferred. */
    bool isBypassed() const noexcept { return b0 == 1.0 && b1 == 0.0 && pole == 0.0; }

    /** Plateau gain in dB -- the peak boost this filter applies. Bounded by construction; the probes
     *  print it so the bound stays a measurement. */
    double plateauDb() const noexcept { return 20.0 * std::log10((b0 - b1) / (1.0 - pole)); }

private:
    void makeIdentity() noexcept
    {
        b0 = 1.0;
        b1 = 0.0;
        pole = 0.0;
    }

    // Below this much droop the correction is not worth a filter, and the solve is ill-conditioned
    // (g1 -> 1 sends the pole to Nyquist). 1.0006 in gain is 0.005 dB. It is what bypasses the
    // restore outright at high base rates crossed with high factors, where the whole droop is
    // 0.004 dB and there is nothing left to correct.
    static constexpr double kNegligible = 6.0e-4;
    static constexpr double kMaxPole = 0.995;

    double b0 = 1.0, b1 = 0.0, pole = 0.0;
    double x1 = 0.0, y1 = 0.0;
};
} // namespace pedal::dsp
