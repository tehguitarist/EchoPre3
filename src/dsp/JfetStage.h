#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "CircuitValues.h"

namespace pedal::dsp
{
/**
 * Stage 2 -- the 2N5457 common-source gain stage, the ONLY nonlinear element in the signal path.
 * Implements Path B of docs/nonlinear-component-modeling.md §2:
 *
 *   Vg --[ 1/k(s) ]--+--------------------------------------+--*(-gm)--> drain Norton current
 *                     |                                      |
 *                     +--[ g(.) - identity ]--[ 1/k(s) ]-----+
 *
 * ============================ THE STRUCTURAL TRAP (worth ~20 dB) ============================
 * A degenerated CS stage is a CURRENT source, not a voltage source. With Zs = R5 || (bypass cap):
 *
 *     k(s)    = 1 + gm*Zs(s)     falls with frequency as the cap shorts R5
 *     Gm(s)   = gm / k(s)        transconductance RISES with frequency
 *     Rout(s) = ro * k(s)        drain output resistance FALLS with frequency
 *     => open-circuit gain Gm*Rout = gm*ro, FLAT and independent of the degeneration
 *
 * So the MODE switch's HF lift is NOT an unconditional gain shelf -- it exists only to the extent the
 * stage is loaded. This class therefore emits a CURRENT, and OutputNetwork stamps the drain impedance
 * into its own solve. Applying the 1/k(s) lift here AND driving the output network from an ideal
 * voltage source would double-count it, which on this pedal is the whole audible job of MODE.
 *
 * ==================================== MODE, restructured ====================================
 * Because the source network is folded analytically into k(s), it never appears as a WDF tree, and
 * the MODE switch does not change any WDF topology. It selects one of three first-order shelf
 * coefficient sets. There are no per-mode scattering matrices to precompute -- build-plan.md step 5
 * anticipated some, and Path B removes the need.
 *
 * Modelled as a first-order shelf, taking Zs = R5 || (1/sC) for the engaged branch only:
 *
 *     1/k(s) = (1 + s*tau) / (K0 + s*tau),   K0 = 1 + gm*R5,  tau MEASURED (see JfetParams)
 *
 * zero at the bypass corner 1/(2*pi*tau), pole K0x above it; DARK (no cap engaged) is the constant
 * 1/K0. The un-engaged branch's 1 MOhm pulldown is treated as fully off: including it would shift the
 * HF degeneration from R5 to R5||500k, i.e. 0.7% / 0.06 dB (circuit.md sanctions this).
 *
 * ============ THE SECOND TRAP: DEGENERATION SUPPRESSES DISTORTION TWICE, NOT ONCE ============
 * ⚠⚠ This one is worth 16.4 dB = 20*log10(K0), it is invisible to every linear test, and the
 * obvious structure gets it wrong. Feeding the shelf output through the shaper models the drive to
 * the nonlinearity correctly -- and then stops. Local feedback ALSO suppresses whatever the device
 * generates inside the loop. Expanding id = gm*u + c*u^2 with u = vg - id*Zs to second order:
 *
 *     order 1:   u1  = vg / k(s)                       (the shelf, as before)
 *     order 2:   id2 = c * (u1^2) / k(s)               <-- the same shelf again, on the PRODUCT
 *
 *     so  H2/H1 = A / (4*Vov*k^2),  NOT  A / (4*Vov*k)
 *
 * Hence the second filter instance above: the shaper's nonlinear EXCESS (g(w) - w, i.e. everything
 * the device adds beyond its own small-signal slope) is fed back through an identical 1/k(s) while
 * the linear path is left untouched. That is exact to second order and structurally right at higher
 * orders, since every term generated inside the loop is suppressed by it.
 *
 * Checked against an exact per-sample implicit solve of id = gm*g(vg - id*R5), which is the ground
 * truth this approximation is answerable to (JfetStageTest section 7 runs it):
 *
 *     gate amplitude:   0.2 V     0.5 V     0.8 V     1.5 V
 *     shelf-only:      +16.3 dB  +16.1 dB  +15.7 dB  +13.3 dB   of H2 error  (the bug)
 *     this model:       -0.04     -0.24     -0.66     -3.08                  (Volterra truncation)
 *
 * Two consequences worth keeping. MODE now moves distortion as k^2: fully bypassed, H2 rises 32.7 dB
 * relative to the fundamental versus DARK, because the drive rises by K0 AND the suppression is
 * gone. And ADAA is now applied to the EXCESS ONLY, which is what makes it free -- see shapeAdaa().
 *
 * ================================ APPROXIMATION, DECLARED ================================
 * This is a Wiener-Hammerstein approximation with the second-order feedback term restored. True
 * degeneration is nonlinear feedback (vgs = vg - i_d*Zs, an implicit solve); linearising the
 * degeneration into 1/k(s) and putting the curvature on vgs is a deliberate modelling CHOICE, not an
 * oversight. Path A (the per-sample implicit solve above, with the source network's state carried
 * through it) is the escalation, and the table above is its criterion in hand: it buys under 0.7 dB
 * of H2 at full scale and 3.1 dB at +5 dB of input trim.
 */
struct JfetParams
{
    // ============ MEASURED 2026-09-07 (build-plan.md M1/M2), no longer placeholders ============
    // gm comes out of a RATIO of two captures, so it needed no level calibration: the mode-versus-
    // DARK plateau is K0 = 1 + gm*R5, measured 6.5912 across two units (P1 6.46, P2 6.72). At
    // R5 = 3.6 k that is gm = 1553 uS -- about TWICE the datasheet-typical self-bias solve's 813 uS,
    // and ~9x the ~180 uS the maker's published control points implied. Both of those priors are
    // refuted; circuit.md note #7 has the arithmetic.
    //
    // !! Use the raw K0 - 1 here, NOT circuit.md's ro-corrected 1.58-1.71 mS. That correction is for
    // a model whose drain resistance rises as ro*k(s); this one folds Rout into the CONSTANT R6||ro
    // (see outputImpedance()), and under that structure the mode differential this model produces is
    // exactly K0 = 1 + gm*R5. Taking the corrected value would make the model MISS the measurement
    // it was fitted to. ChainTest asserts the plateau lands on K0.
    //
    // ⛔ THIS VALUE IS A RECORDED VOICING DECISION, not merely a fit -- circuit.md note #28.
    // The model is voiced to the NEWER three-position units (P1/P2); the owner's own two-position
    // unit P4 measures K0 = 5.06-5.19, i.e. a ~25% weaker JFET (gm ~ 1146 uS). The consequences are
    // known, quantified and ACCEPTED: against P4 the model runs up to +1.51 dB bright at 6.5-10 kHz
    // and makes ~1.26 dB less H2. Do NOT "fix" either by moving gm. Reversing the decision is this
    // one constant plus a re-run of analysis/absolute_gain.py (gm and kOutputMakeup are both level
    // scalars in the DARK path). The device law below (mExp, vp) came from P4 either way.
    double gm = 1.5531069e-3; // S -- (K0 - 1)/R5 with K0 = 6.5912 measured. M2.

    // The mode shelf's time constants, MEASURED rather than computed as R5*C. Both branches' fitted
    // zeros sit ~7% below the drawn 1/(2*pi*R5*C), by a common factor: the two branches imply the
    // same R5 to within 1.6%, and the cap RATIO is confirmed (2.24 measured vs 2.20 drawn). So it is
    // one offset -- either R5 measures ~3.85 k or both caps run ~7% high -- and schematic.png was
    // re-read at high zoom to rule out a transcription error ("3k6" is unambiguous). This dataset
    // cannot separate R5 from C, and the DSP only ever consumes (tau, K0), so the pair is what is
    // stored. Means of the two units' fits; per-unit spread is 1.2% (bright) and 7.7% (mid).
    double tauBright = 85.369e-6; // s -- 22 nF branch, zero at 1864 Hz (drawn R5*C = 79.2 us)
    double tauMid    = 38.263e-6; // s -- 10 nF branch, zero at 4160 Hz (drawn R5*C = 36.0 us)

    // ================== THE AMPLITUDE PARAMETERS BELOW ARE STILL NOT MEASURED ==================
    // They are DERIVED from the measured gm plus one declared choice, because the harmonic data
    // cannot pin them on its own -- see "the degeneracy" below.
    //
    // With gm measured, the square-law self-bias solve collapses to a ONE-parameter family. From
    // Id = IDSS*(Vov/|Vp|)^2, gm = 2*Id/Vov and Vgs = -Id*R5:
    //
    //     |Vp|/Vov = 1 + gm*R5/2 = 3.7956      IDSS/Id = (|Vp|/Vov)^2 = 14.407
    //
    // so choosing any one of (IDSS, |Vp|, Id, Vov) fixes the rest. The datasheet's IDSS <= 5 mA caps
    // the family at Vov = 0.447 V; its Vgs(off) >= 0.5 V floors it at Vov = 0.131 V; the load line
    // (Vds > Vov with R6 = 22 k off a 22 V rail) would allow up to Vov = 1.053 V, so the datasheet
    // binds first. This ships the IDSS = 5 mA end:
    //
    //     Id = 347 uA   Vov = 0.4469 V   |Vp| = 1.696 V   Vs = 1.249 V   Vd = 14.36 V   Vds = 13.1 V
    //
    // Two reasons for that end rather than the middle: the maker states Q1 is "cherry picked to
    // cream-of-the-crop specs", which means high IDSS for a given pinch-off, and it is the
    // MINIMUM-curvature admissible point (aEven = 1/Vov is smallest there), which is the
    // conservative choice for a pedal whose whole claim is headroom. Note the drain lands at 14.4 V,
    // not the ~11 V mid-rail circuit.md estimated from a nominal part -- high gm at this Id needs a
    // small Vov, which needs a small Id.
    //
    // !! THE DEGENERACY -- ⭐ BROKEN 2026-09-08 FOR P1, BY AN EXTERNAL CALIBRATION FACT. Read this
    // whole block before touching aEven; the constant below has NOT been changed yet.
    //
    // The degeneracy, as it stood: the reference renders' H2 pins only the PRODUCT aEven x (the
    // trainers' reamp level in V/FS), because both enter the harmonic amplitude linearly and the
    // dataset has no absolute level anchor (build-plan.md L1/L2). At the shipped Vov the P2 ladder's
    // top three cells imply the trainers reamped at 0.41-0.64 V/FS; at the other end of the family
    // (Vov = 0.131) they imply 0.12-0.19 V/FS. Nothing IN THE DATASET chooses between them, and the
    // datasheet cap bought only a one-sided bound: the renders were driven at <= 0.41 V/FS, i.e.
    // >= 6.6 dB below kInputRef = 0.87, so an A/B at matched DIGITAL level compares the plugin's
    // distortion at a drive the reference never saw. Match the DRIVE, then null.
    //
    // ⭐ WHAT CHANGED: the owner reports P1 (thelamehorse) was captured with NAM's input calibrated
    // to -12 dBu. NAM calibrates by playing a 1 kHz sine at 0 dBFS and measuring RMS volts at the
    // jack into the gear, so that fixes the trainer's level outright:
    //
    //     V/FS = 0.7746 * 10^(-12/20) * sqrt(2) = 0.2752 V per full scale
    //
    // (the sqrt(2) is because the calibration measures the RMS of a full-scale SINE, while V/FS is
    // the volts a sample of 1.0 represents). ✅ That is INSIDE the <= 0.41 V/FS bound this block
    // derived independently from H2 -- two unrelated routes agreeing, which is why it is believed.
    // It is 24.20 dB below kInputRef = 4.4626, so matched-drive A/B against P1 means feeding the
    // plugin -24.20 dB (analysis/harmonic_audit.py --dbu-capture -12 does exactly this).
    // ⚠ That offset was -10.00 dB while kInputRef was 0.87 and this comment said so for a while
    // after the calibration moved. It is the same trap OfflineRender's --input-trim guard exists
    // for: a drive offset is a DIFFERENCE between two calibrations, so it changes whenever either
    // end does.
    //
    // ⚠⚠ AND THE MODEL IS ~10 dB SHORT OF H2 AT THAT DRIVE. Over P1's 24 usable cells (125-800 Hz,
    // top two levels, all three modes) the deficit is mean -9.47 dB, median -9.65, sd 2.98. Since
    // H2/H1 = A/(4*Vov*k^2) is exactly inverse in Vov, that implies
    //
    //     Vov ~= 0.150 V   (|Vp| = 0.570 V, Id = 117 uA, IDSS = 1.68 mA, Vd = 19.4 V, Vds = 19.0 V)
    //
    // against the shipped 0.447 V. VERIFIED end-to-end, not just algebraically: a throwaway probe
    // build at Vov = 0.1502 moved the same 24 cells to mean -0.17 dB, median -0.44, sd 2.92.
    // Both endpoints are datasheet-admissible (IDSS 1.68 mA in 1-5 mA, |Vp| 0.570 V just inside the
    // 0.5 V floor), and 0.150 sits near the bottom of this block's own [0.131, 0.447] bracket.
    //
    // ⛔ NOT APPLIED, and here is the honest reason rather than caution for its own sake:
    //   1. The -12 dBu figure is the owner's recollection. It is NOT in the .nam metadata -- none of
    //      the seven files carries input_level_dbu or output_level_dbu (checked).
    //      ⭐ PARTLY DISCHARGED 2026-09-08: the circuit now brackets it independently. Solving
    //      H2/H1 = A_gate/(4*Vov*k^2) for the rig's V/FS at P1's measured H2, over the datasheet's
    //      admissible Vov range, gives V/FS in [0.103, 0.999] V = [-20.6, -0.8] dBu. That is a wide
    //      bracket -- it does NOT pin -12 dBu specifically, and is equally consistent with -5 -- but
    //      it fixes the ORDER OF MAGNITUDE from measurement alone, which is the part that was
    //      resting purely on memory.
    //      ⚠⚠ AND IT SETTLED A REAL AMBIGUITY THAT WAS WORTH 24 dB. The owner separately quotes
    //      +12.2 dBu = 3.156 V RMS = 4.46 V peak at 0 dBFS. Both numbers are correct and they
    //      describe DIFFERENT POINTS IN THE CHAIN: +12.2 dBu is the INTERFACE's output at full
    //      scale, while NAM's input_level_dbu is measured AT THE GEAR'S INPUT JACK, i.e. after the
    //      reamp box. The difference is 24.2 dB, an entirely ordinary reamp attenuation, and
    //      4.4626 V / 10^(24.2/20) = 0.2752 V, which is the -12 dBu figure exactly. The bracket
    //      above independently rules the interface-side number out by 13 dB: 4.46 V/FS at the
    //      pedal would need Vov far outside the datasheet.
    //      ➡ Whenever a level is written down here, say WHERE in the chain it is measured.
    //   2. The sd of 2.98 dB is not the fit's precision, it is the CAPTURE's floor. A known-answer
    //      probe (below the shelf zero every mode has Zs = R5, so H2 in dBc must be IDENTICAL across
    //      modes) reads 1.5-21.3 dB of spread on P1, typically 4-9. So Vov is pinned to about a
    //      factor of 1.4, i.e. roughly [0.11, 0.21], whose low half the datasheet forbids.
    //   3. It moves the OPERATING POINT a long way: the drain goes 14.4 -> 19.4 V, only 2.6 V under
    //      the rail, which changes the clipping asymmetry and invalidates the load-line arithmetic
    //      below (that "g = +0.370 V" figure is computed at Vds = 13.1 V).
    //   4. It contradicts the reading of the maker's "cherry picked to cream-of-the-crop specs" that
    //      justified the IDSS = 5 mA end: the implied part is LOW-IDSS, LOW-pinchoff (1.68 mA,
    //      0.57 V), not a hot one.
    // ➡ Apply it together with a re-derived load line, or wait for a second calibrated capture.
    //
    // ============ FITTED 2026-09-09, TWICE, AND STILL NOT APPLIED -- analysis/vov_fit.py ============
    // The fit the block above asked for now exists, with the cross-check it asked for. Two
    // observables of DIFFERENT ORDER, off different signals, with different floors:
    //
    //     route A   H2 in dBc, 125-800 Hz, all three modes, 60 cells    Vov = 0.126  (0.133-0.136
    //               (near-inverse in Vov)                                     per mode; bright lower)
    //     route B   compression GROWTH over the top 10 dB, 5-8 kHz      Vov = 0.165  (0.127-0.182
    //               (third order, so ~1/Vov^2)                                per cell)
    //
    // They agree to a factor of 1.31, inside the factor of ~1.5 the floor allows, and they bracket
    // note #10's independently-derived 0.150. ⭐ The estimator is validated by construction first:
    // `--self-test` replaces the capture with a plugin render at an off-grid Vov = 0.2200 and both
    // routes recover it (A exactly, B at 0.2215-0.2217).
    //
    // ⚠⚠ REASON 3 ABOVE IS DISCHARGED, AND A BETTER ONE REPLACES IT. The load line does NOT move:
    // triodeOnsetGateVolts() reads 1.691 V at the shipped Vov and 1.737 V at 0.150, because
    // lowering Vov raises Vds_q by almost exactly as much as it lowers the current. What moves is
    // CUTOFF, and it overtakes triode:
    //
    //     Vov      Vds_q      cutoff        triode      first to clip
    //     0.4469   13.12 V    -2.7 dBFS     -7.5 dBFS   triode   (the stage as shipped)
    //     0.2050   17.92 V    -9.5 dBFS     -6.9 dBFS   cutoff   (the crossover is near here)
    //     0.1500   19.02 V   -12.2 dBFS     -7.3 dBFS   cutoff, by 4.9 dB
    //
    // So applying the fit does not invalidate the load line -- it makes the load line nearly
    // unreachable, and swaps the stage's clipping MECHANISM from triode-first to cutoff-first. That
    // is a qualitative voicing change, decided by a parameter pinned only to a factor of 1.3-1.5,
    // from the one capture whose harmonic data sits just 4.7 dB above its own error floor. It is a
    // stronger reason to wait than any of the four above, and it did not exist until the load line
    // was built and the fit was done.
    //
    // 📌 Reasons 1, 2 and 4 are unchanged. ➡ The +12.2 dBu capture session settles this: it puts the
    // reference's digital levels 1:1 onto the plugin's and reaches the load-line region, so it
    // measures the clipping mechanism directly instead of inferring it from a parameter.
    // Ohm -- 1/(lambda*Id) at lambda = 2 mV/V, with the fitted Id0 = 419 uA (was 1.4407e6 at the
    // old structure's Id0 = 347 uA). lambda is an assumption, not a measurement; only the Id0 it is
    // divided by moved. 📌 It is worth 0.3 % of R6 || ro (21.67 k -> 21.60 k), i.e. 0.03 dB, so it
    // does not disturb the output impedance this was validated against (measured 99.8 k / 60.8 k
    // against the model's 101.5 / 59.4 -- circuit.md note #22). Re-derived rather than left stale
    // because leaving it would make the operating point internally inconsistent for no benefit.
    double ro = 1.1921e6;


    // ============================ THE DEVICE, AS THE DEVICE (2026-09-08) ============================
    // The fitted shaper -- a bounded odd core plus a tanh^2 even bump, with limitPos/limitNeg/beta --
    // is GONE, replaced by the square-law JFET's own equations plus the load line it actually works
    // against. Three separately-recorded deferrals collapse into one change here, because they were
    // never really three things:
    //
    //   * the tanh^2 bump saturated where the true parabola does not (4.5 % of H2 at 0 dBFS, 23 % of
    //     the compression at a low Vov). The parabola IS the device law; nothing is approximated now.
    //   * `beta`, the cubic, is gone rather than zero. A square-law device has no cubic term; the odd
    //     content this stage produces comes from the feedback loop and from the triode region, which
    //     is where it comes from in the circuit.
    //   * ⭐⭐ THE LOAD LINE, which is why the shaper had to go rather than be adjusted. Its ceiling is
    //     ~542 uA of extra drain current, against the channel ceiling's 4.65 mA -- 8.6x tighter. The
    //     old structure could not express it: the even bump ALONE asymptotes at Vov/2 = 347 uA, which
    //     is already 64 % of the load-line limit, leaving the core nowhere to go. That was recorded as
    //     a known impossibility and it was correct.
    //
    // ⚠⚠ AND THE LOAD LINE STOPPED BEING A CORNER CASE WHEN kInputRef MOVED. At kInputRef = 0.87 the
    // drain entered triode only at +7.5 dBFS, i.e. with input trim. At the calibrated 4.4626 V/FS --
    // a well-recorded guitar metering -12 dBFS RMS at ~0.78 V -- it is entered at -6.7 dBFS, so the
    // top 6.7 dB of every normally-tracked take is in it. Shipping the calibration without this would
    // have returned a smooth ~2 dB of compression exactly where the real drain slams into triode.
    //
    // THE MODEL. Standard Shichman-Hodges, in the current domain, with the source and drain networks
    // both stamped into one per-sample solve (see solveDrain()):
    //
    //     Vov_i = Vov + w                     instantaneous overdrive, w = effective AC vgs
    //     Vds_i = Vds_q - i*zLoad - vs        the LOAD LINE: drain falls, source rises, with i
    //
    //     I = 0                               Vov_i <= 0          cutoff
    //     I = beta*Vov_i^2                    Vds_i >= Vov_i      saturation
    //     I = beta*(2*Vov_i*Vds_i - Vds_i^2)  Vds_i <  Vov_i      triode
    //
    // ✅ The map is C1 everywhere, which is what keeps it well behaved under oversampling. At cutoff
    // the parabola meets zero WITH zero slope, so there is no corner. At the triode boundary both the
    // value and both partials agree (Vds_i = Vov_i makes 2*Vov_i*Vds_i - Vds_i^2 = Vov_i^2, and
    // dI/dVds = 2*beta*(Vov_i - Vds_i) = 0). Neither was arranged; both fall out of the algebra.
    //
    // ✅ The SMALL-SIGNAL response is unchanged, exactly. I ~= beta*(Vov + w)^2 = Id0 + 2*beta*Vov*w
    // and 2*beta*Vov = gm by construction, so i = gm*w and the loop closes to the same 1/k(s) the
    // shelf realises. Vds_q is 13 V against an overdrive of 0.45, so small signals are always in
    // saturation and the drain never enters the linear path. JfetStageTest 8a asserts it to 1e-11 dB.
    //
    // ONE amplitude parameter now, not five. Everything else is derived from it and the measured gm.
    // ⭐⭐ MEASURED 2026-09-10 against P4's probe captures -- analysis/onset_fit.py, circuit.md
    // note #26. These two replace the single `vov`, and BOTH the parameterisation and the exponent
    // changed for the same reason: no member of the square-law family can describe this pedal.
    //
    // THE OBSERVATION. In DARK the source one-port is the bare resistor with no state, so the whole
    // stage is MEMORYLESS and one period of gate sine through the solve IS the exact steady-state
    // spectrum -- an oracle that reproduces this plugin at 8x oversampling to 0.01 dB. Against P4's
    // DARK tone ladder the shipped square law leaves a residual that no `vov` removes:
    //
    //     cells below cutoff (curvature)       want Vov ~ 0.90 at P4's own gm
    //     cells past cutoff  (clipping onset)  want Vov ~ 0.63 at P4's own gm
    //
    // A factor of 1.5 apart. That IS the "H2-versus-drive curve has the wrong SHAPE, not the wrong
    // scale" that circuit.md note #24 recorded and could not localise.
    //
    // ⭐ WHY AN EXPONENT, AND WHY THE FIT IS WELL POSED RATHER THAN TWO KNOBS ON A CURVE. The three
    // parameters are each pinned by a different, independent observable:
    //
    //   * CUTOFF ONSET IS EXACTLY |Vp| IN GATE VOLTS, whatever gm and m are. At cutoff the device
    //     passes nothing, so the source sits at -R5*Id0 and the gate must reach -(Vov + R5*Id0),
    //     which is -|Vp| by definition. Every subset of the captures puts it at 1.90-2.02 V.
    //   * SMALL-SIGNAL CURVATURE then pins m: H2/H1 goes as (m-1)*(m + gm*R5) / (m*|Vp|*K0^2).
    //   * gm is already measured, from the MODE differential, which is rig-free and linear.
    //
    // ⛔ AND THE SQUARE LAW CANNOT BE RESCUED BY MOVING gm. Freeing gm with m pinned at 2 leaves the
    // residual 36 % worse than freeing m instead (RMS 1.36 dB against 1.00) AND demands K0 = 6.79
    // against P4's measured 5.126 -- a 2.4 dB error in the mode plateau, seven times M6's whole
    // unit-to-unit band, from a fit whose own residual is 0.03 dB. With m free, gm wants to stay
    // within 0.04 dB of where it was measured.
    //
    // ⭐⭐ THE EVIDENCE THAT THIS IS STRUCTURE AND NOT A SPENT DEGREE OF FREEDOM. A free parameter
    // always improves the observable it was fitted to, and this project has been caught by exactly
    // that before (note #22's `Vov` fit off a floor, note #21's C10 at three knob positions). Two
    // things separate them here:
    //
    //   1. FITTED ON H2 AT 220 Hz ONLY, then scored against five observables that were not fitted.
    //      All five improve, and their OFFSETS go to zero rather than merely shrinking:
    //
    //        observable          shipped SH      m = 1.60      (offset / shape-RMS, dB)
    //        H2 @220  (fitted)   -8.28 / 2.55   +0.15 / 0.63
    //        H3 @220            -11.04 / 6.29   +0.66 / 1.35    a different ORDER
    //        H2 @3150            -9.06 / 2.52   -0.36 / 0.62    a 14x different FREQUENCY
    //        H3 @3150           -11.10 / 6.39   -0.21 / 1.47
    //        compression @220    +0.61 / 0.42   +0.10 / 0.09    read on the FUNDAMENTAL
    //        compression @3150   +0.74 / 0.44   +0.21 / 0.11
    //
    //   2. IT MAKES THE OTHER PARAMETER AGREE ACROSS DISJOINT SUBSETS, which a spent degree of
    //      freedom does not do. Fitting the pad-0 captures and the padded ones separately -- no
    //      shared cells, different knob positions, different drives:
    //
    //        m pinned at 2.0:  |Vp| = 2.237 (pad-0)  vs  2.960 (padded)   -- 32 % apart
    //        m free:           |Vp| = 2.023          vs  1.904            --  6 % apart (m 1.65/1.53)
    //
    //      Across all nine subsets tried -- per frequency, per pad, per volume half, per drive
    //      region, and H3 alone -- m returns 1.46-1.71 and |Vp| 1.70-2.02.
    //
    // 📌 The residual profiled against a PINNED m has a real minimum, not a plateau (which is what
    //    dsp.md says to check before believing a fitted parameter): 1.4 -> 1.96 dB RMS, 1.5 -> 1.25,
    //    1.55 -> 1.07, 1.60 -> 1.00, 1.65 -> 1.04, 1.7 -> 1.13, 1.8 -> 1.40, 2.0 -> 1.98. So m is
    //    identified to about +-0.1, and Shichman-Hodges is refuted at twice the residual.
    //
    // ⚠ WHAT THIS IS NOT. `m` is an empirical TRANSFER-LAW exponent, not a derived physics constant.
    // Shichman-Hodges' m = 2 is itself an approximation to the gradual-channel solution, and real
    // JFET transfer curves are routinely fitted with exponents from ~1.5 to ~2.5; this unit lands at
    // the low end. Nothing here claims to know why, and the triode branch below is the unique
    // generalisation that keeps the solve's guarantees, not a physical derivation either.
    double mExp = 1.60; // transfer-law exponent. 2.0 would be Shichman-Hodges.

    // ⭐⭐ |Vp| IS THE PARAMETER NOW, NOT Vov, AND THE SWAP REMOVES A TRAP RATHER THAN RENAMING ONE.
    // Self-bias gives Vov = |Vp| / (1 + gm*R5/m), hence Id0 = gm*|Vp| / (m + gm*R5) -- which is
    // BOUNDED ABOVE by |Vp|/R5 however large gm gets. Under the old (gm, Vov) parameterisation
    // Id0 = gm*Vov/m grew without limit, and that is precisely how a Vov sweep at a fixed gm walked
    // the quiescent drain to a NEGATIVE voltage and had an hour of renders read as a curvature
    // measurement (circuit.md note #22a). OfflineRender grew a guard for it. Parameterised on |Vp|
    // the bias point CANNOT collapse, so that guard is now belt-and-braces rather than load-bearing.
    //
    // ⚠⚠ MEASURED ON P4, SHIPPED AT P1/P2's gm -- a deliberate MIXING, per CLAUDE.md's two-position
    // block, and this is what it costs. P4 is the only unit whose harmonic data clears its own
    // floor; the recorded decision voices the model to the newer P1/P2 units, whose gm is 36 %
    // higher. Transplanting the device's own (m, |Vp|) onto their gm re-solves the bias to
    // Vov = 0.4328 V, Id0 = 420 uA, IDSS = 4.65 mA, Vds_q = 11.25 V -- every one inside the
    // datasheet. ⭐ It is coherent as a PARTS BIN: one pinch-off and one exponent, with IDSS the only
    // thing that differs between units, which is exactly what "cherry picked to cream-of-the-crop
    // specs" selects on. P1 implies 4.31 mA, the shipped mean 4.65, P2 5.03.
    // ➡ The consequence is a PREDICTION, and it has been MEASURED: the model makes about 2.4 dB LESS
    // H2 than the owner's own P4 at the same small-signal drive. Small-signal H2/H1 goes as
    // (m-1) / (Vov * K0^2), and moving to P1/P2's gm raises K0 from 5.126 to 6.591 while LOWERING
    // Vov from 0.5427 to 0.4321 -- the two partly cancel, leaving 20*log10(0.7597) = -2.39 dB.
    // Measured against P4's DARK ladder at the sub-cutoff cells: +1.9 to +2.8 dB, mean +2.3.
    // ⚠ NOT 4.4 dB, which is what K0^2 alone gives and what an earlier draft of this comment said.
    // That figure belongs to a different transplant -- carrying P4's Vov across instead of its |Vp|
    // -- and it is wrong for the one actually implemented, because Vov is DERIVED from gm here.
    // Same class as BRIGHT's 6.5-10 kHz miss (circuit.md note #25), same cause: it is the recorded
    // decision to voice the model to the newer units, not a defect.
    double vp = 1.942; // V -- pinch-off magnitude. THE amplitude parameter.
                       // ⭐ Cutoff onset in GATE VOLTS is exactly this number, by construction.

    // AC impedance at the DRAIN NODE, which is what turns drain current into drain volts and
    // therefore what sets the load line's slope.
    //
    // 📌 A CONSTANT, and deliberately so. The true value is (R6 || ro) in parallel with the C10 ->
    // output-network branch, which moves with VOLUME -- but the stage runs INSIDE the oversampled
    // region while OutputNetwork runs at base rate, so the real drain voltage is not available here
    // per sample even in principle. EchoPreDsp sets this from the output network's own computed
    // value once per block (setDrainLoad), which is the same cadence VOLUME already updates at.
    double zLoad = 20.6e3; // Ohm

    /** Quiescent overdrive, from the self-bias solve Vov = |Vp| - Id0*R5 with Id0 = gm*Vov/m. */
    double vov() const { return vp / (1.0 + gm * circuit::kR5 / mExp); }
    /** Quiescent drain current, Id0 = gm*Vov/m. Bounded above by |Vp|/R5 for ANY gm -- see `vp`. */
    double id0() const { return gm * vov() / mExp; }
    /** Transfer-law scale, beta = Id0/Vov^m. (The name predates the exponent; the role is the same.) */
    double betaSq() const { return id0() / std::pow(vov(), mExp); }
    /** Quiescent drain-source voltage. The drain sinks Id0 through R6 and the source rises through R5. */
    double vdsQuiescent() const { return circuit::kVA - id0() * (circuit::kR6 + circuit::kR5); }
    /** Pinch-off magnitude. The stored parameter now, not a derived one. */
    double vpMagnitude() const { return vp; }
    /** IDSS implied by the family -- the datasheet sanity check (2N5457: 1-5 mA). */
    double idss() const { return id0() * std::pow(vp / vov(), mExp); }

};
/** MODE positions, ordered by physical lever position top-to-bottom to match the APVTS choice list
 *  and the hardware toggle (circuit.md note #2). Deliberately NOT ordered by brightness.
 *  RESOLVED 2026-09-07 by build-plan.md M1, and it DID swap Bright with Mid: the mode-differential
 *  shelf zero measures 1.86 kHz on the BRIGHT captures and 4.17 kHz on the MID captures, in both
 *  units, so BRIGHT engages C1 (22 nF) and MID engages C2 (10 nF) -- the opposite of what
 *  circuit.md inferred from the labels' plain meaning. The enum ORDER is untouched (it is the APVTS
 *  choice index; architecture.md forbids reordering it); only capForMode() below changed. */
enum class Mode
{
    Bright = 0, // C1 22 nF engaged -> bypass corner ~2.01 kHz (measured 1.86 kHz)
    Dark,       // neither engaged  -> R5 fully degenerating, lowest gain, flat
    Mid         // C2 10 nF engaged -> bypass corner ~4.42 kHz (measured 4.17 kHz)
};

class JfetStage
{
public:
    void setParams(const JfetParams& p)
    {
        params = p;
        updateShelf();
    }

    const JfetParams& getParams() const noexcept { return params; }

    void setMode(Mode m)
    {
        if (m != mode)
        {
            mode = m;
            updateShelf();
        }
    }

    /** Newton residual at the last solved sample, in AMPS of drain current. Exposed so a test can
     *  assert the fixed iteration count actually converges on real signal rather than assume it. */
    double lastSolveResidual() const noexcept { return solveResidual; }

    /** Test/probe hooks. Production never calls these. */
    void setSolveIters(int n) noexcept { solveIters = (n > 0) ? n : kSolveIters; }
    static constexpr int shippedSolveIters() noexcept { return kSolveIters; }

    /** Which of the three solves to run. Production is always Shipped; the other two exist so a test
     *  can check the shipped one against an implementation that shares none of its structure.
     *
     *  ⚠ THE ROLES SWAPPED WHEN THE EXPONENT LANDED. The closed form is exact only for the SQUARE
     *  law -- substituting the source one-port and the load line into it leaves a quadratic in the
     *  drain current -- and the stage ships m = 1.60, where the same substitution leaves a
     *  transcendental equation. So the closed form is now the m = 2 reference and the fast
     *  power-law solve is production. GenericNewton is the safeguarded Newton over the composite
     *  piecewise map, kept as the converged oracle both are checked against. */
    enum class Solver
    {
        Shipped,             // closed form at m == 2, otherwise solvePowerLaw()
        ClosedFormSquareLaw, // force the closed form; only meaningful at m == 2
        GenericNewton        // solveIterative(), run at setSolveIters()
    };

    void setSolver(Solver s) noexcept { solver = s; }
    Solver getSolver() const noexcept { return solver; }

    /** Back-compatible spelling: false selects the generic Newton oracle, true the shipped path. */
    void setUseClosedForm(bool shouldUseClosedForm) noexcept
    {
        solver = shouldUseClosedForm ? Solver::Shipped : Solver::GenericNewton;
    }
    bool closedFormIsEnabled() const noexcept { return solver != Solver::GenericNewton; }
    void sourcePortCoeffs(double& rd, double& c1, double& c2) const noexcept
    {
        rd = srcRd; c1 = srcC1; c2 = srcC2;
    }

    /** AC impedance at the drain node, in ohms -- the load line's slope. EchoPreDsp sets it from the
     *  output network once per block; see JfetParams::zLoad for why it is a per-block constant and
     *  not a per-sample quantity. */
    void setDrainLoad(double ohms)
    {
        params.zLoad = ohms;
    }

    /** Call at the rate this stage actually runs at -- the OVERSAMPLED rate. The shelf's pole sits at
     *  K0 x the bypass corner: 12.3 kHz in Bright and 27.4 kHz in Mid, the second ABOVE Nyquist at
     *  48 kHz. That is why this stage lives inside the oversampled region, and also why its
     *  discretisation is a three-point magnitude match rather than a bilinear transform or a prewarp
     *  -- see updateShelf(), which measures all three. */
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        updateShelf();
        reset();
    }

    void reset()
    {
        driveShelf.reset();
        srcIdPrev = 0.0;
        srcVsPrev = 0.0;
        solveResidual = 0.0;
        lastVds = params.vdsQuiescent();
        lastW = 0.0;
    }

    /** Gate volts -> drain Norton current, in amps, signed as the current INJECTED INTO node D.
     *  Negative small-signal gain: a rising gate pulls more drain current and drags the drain DOWN,
     *  so this stage INVERTS, as the real single-stage pedal does. Keep it -- polarity is audible in
     *  a null test against the reference renders even though it is inaudible solo. */
    inline double processSample(double vGate) noexcept
    {
        return -solveDrain(vGate);
    }

    /** Solve the whole stage for this sample and return the AC drain current, in amps, signed as the
     *  current the device SINKS. processSample() negates it, because the Norton current injected into
     *  node D is the opposite sign: a rising gate pulls more drain current and drags the drain DOWN,
     *  so this stage inverts, as the real single-stage pedal does.
     *
     *  ONE unknown, i, with everything else following from it:
     *
     *      vs    = Rd*i + vOff        the source one-port (updateSourcePort)
     *      w     = vGate - vs         effective AC gate-source drive
     *      Vov_i = Vov + w            instantaneous overdrive
     *      Vds_i = Vds_q - i*zLoad - vs   the LOAD LINE: drain falls and source rises together
     *      F(i)  = i - (I(Vov_i, Vds_i) - Id0)
     *
     *  ⭐ NEWTON IS UNCONDITIONALLY SAFE HERE, and now for a structural reason rather than a tuned
     *  one. Differentiating,
     *
     *      dF/di = 1 + Rd*(dI/dVov) + (zLoad + Rd)*(dI/dVds)
     *
     *  and BOTH partials of the Shichman-Hodges law are non-negative everywhere (2*beta*Vov_i and 0
     *  in saturation, 2*beta*Vds_i and 2*beta*(Vov_i - Vds_i) in triode, 0 and 0 in cutoff). So
     *  dF/di >= 1 always, F is strictly increasing, and it has exactly one root.
     *  📌 That replaces the old guarantee, which leaned on limitNeg being set 1.15x past cutoff to
     *  stop the fitted shaper folding back. The device's own equations cannot fold, so the coupling
     *  between the monotonicity margin and the solve's convergence is gone with the shaper.
     *
     *  The start is the closed-form LINEAR root, for the reason measured under the previous
     *  structure: it already inverts the dominant linear term exactly, and a warm start from the
     *  previous sample is far worse. */
    inline double solveDrain(double vGate) noexcept
    {
        // ⚠⚠ THE CLOSED FORM IS EXACT ONLY FOR m == 2, AND THE STAGE NO LONGER SHIPS m == 2.
        // Substituting the source one-port and the load line into a SQUARE law leaves a quadratic in
        // i with an algebraic root; at any other exponent it leaves a transcendental equation and
        // there is nothing to solve in closed form. So the roles of the two solvers have swapped:
        // the safeguarded Newton is production, and solveClosedForm() is retained as the independent
        // oracle JfetStageTest checks it against at m = 2 -- which is still worth keeping, because
        // it is the same equations reached by a completely different method.
        const double i = (solver == Solver::GenericNewton)      ? solveIterative(vGate)
                       : (solver == Solver::ClosedFormSquareLaw
                          || params.mExp == 2.0)                  ? solveClosedForm(vGate)
                                                                  : solvePowerLaw(vGate);

        const double vs = srcRd * i + srcOffset();
        lastVds = params.vdsQuiescent() - i * params.zLoad - vs;
        lastW = vGate - vs;
        double dV = 0.0, dD = 0.0;
        const double resid = i - (deviceCurrent(params.vov() + lastW, lastVds, params.betaSq(),
                                                params.mExp, dV, dD)
                                  - params.id0());
        // Reported in AMPS OF CURRENT ERROR, not as the raw residual: dF/di runs to ~40 here, so the
        // bare residual overstates the error by that factor and reads alarming when the answer is exact.
        solveResidual = resid / (1.0 + srcRd * dV + (params.zLoad + srcRd) * dD);
        advanceSource(i, vs);
        return i;
    }

    /** ⭐⭐ THE SOLVE IS CLOSED FORM. There is no iteration, and there never needed to be.
     *
     *  Shichman-Hodges is piecewise QUADRATIC, and both the source one-port and the load line are
     *  LINEAR in the drain current -- so substituting them into the device law leaves a quadratic in
     *  i on every branch, with an exact root. The safeguarded Newton that shipped first was solving
     *  by iteration something that has an algebraic answer.
     *
     *  With  A = Vov + vGate - vOff   (so Vov_i = A - Rd*i)
     *        B = Vds_q - vOff         (so Vds_i = B - R*i,  R = zLoad + Rd):
     *
     *    saturation   I = beta*Vov_i^2
     *                 -> beta*Rd^2 * i^2  -  (2*beta*A*Rd + 1) * i  +  (beta*A^2 - Id0) = 0
     *    triode       I = beta*Vds_i*(2*Vov_i - Vds_i),  and 2*Vov_i - Vds_i = C + D*i
     *                 with C = 2A - B and D = zLoad - Rd
     *                 -> beta*R*D * i^2  +  (1 - beta*(B*D - R*C)) * i  +  (Id0 - beta*B*C) = 0
     *    cutoff       Vov_i <= 0, or the drain bottomed -> I = 0 -> i = -Id0, no solve at all
     *
     *  The branches are tried in the order they occur in normal use, so the common case costs one
     *  square root. Because the composite map is monotone and C1, exactly one branch's root satisfies
     *  its own validity condition, which is what the checks below select on -- no tolerance tuning,
     *  and no possibility of two branches both claiming the sample.
     *
     *  ⚠ The roots are taken in the CANCELLATION-STABLE form q = -(b + sign(b)*sqrt(D))/2, root =
     *  c/q, not the schoolbook (-b +- sqrt(D))/2a. In saturation a = beta*Rd^2 falls to 2.7e-6 at
     *  384 kHz in Bright while b is ~-1, so the schoolbook small root is the difference of two nearly
     *  equal numbers and loses most of its significant digits exactly where the stage is quietest.
     *
     *  ✅ Verified against the safeguarded Newton it replaces, run to 200 iterations, over
     *  -20..+20 V of gate drive at every mode's Rd and a range of port offsets: worst relative
     *  disagreement 1.4e-10, and that case sits at 3e-19 A, i.e. numerical zero. JfetStageTest
     *  section 8c asserts it continuously. */
    inline double solveClosedForm(double vGate) const noexcept
    {
        const double rd = srcRd;
        const double off = srcOffset();
        const double vov = params.vov();
        const double beta = params.betaSq();
        const double id0 = params.id0();
        const double A = vov + vGate - off;
        const double B = params.vdsQuiescent() - off;
        const double R = params.zLoad + rd;

        // BOTH roots of a*x^2 + b*x + c, in the cancellation-stable pair q/a and c/q, with NaN for
        // a root that does not exist. Written into r[0] and r[1].
        //
        // ⚠⚠ BOTH, NOT THE SMALLER ONE. The first port of this returned only c/q, on the reasoning
        // that the physical root is the small one -- which is true only while b < 0. Here
        // b = -(2*beta*A*Rd + 1) flips sign at A = -1/(2*beta*Rd), i.e. at about -0.53 V of gate
        // drive, and below that the PHYSICAL root is the other one. The stage then fell through to
        // the cutoff branch and returned -Id0 for every sample below that, an 0.9 mA error on a
        // 0.35 mA quiescent current. The prototype tested both roots and the port dropped one.
        // ➡ Root SELECTION here is by the branch's own physical validity condition, never by
        // magnitude: it is the only criterion that cannot be wrong, and the map being monotone and
        // C1 guarantees exactly one root satisfies it.
        const auto roots = [](double a, double b, double c, double* r) noexcept {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            r[0] = r[1] = nan;
            const double disc = b * b - 4.0 * a * c;
            if (disc < 0.0)
                return;
            const double sq = std::sqrt(disc);
            const double q = -0.5 * (b + ((b >= 0.0) ? sq : -sq));
            r[0] = (q != 0.0) ? (c / q) : nan;
            r[1] = (a != 0.0) ? (q / a) : nan;
        };
        double r[2];

        // --- saturation, the common case -----------------------------------------------------
        {
            roots(beta * rd * rd, -(2.0 * beta * A * rd + 1.0), beta * A * A - id0, r);
            for (const double i : { r[0], r[1] })
            {
                if (i != i) // NaN
                    continue;
                const double vovI = A - rd * i;
                if (vovI >= 0.0 && (B - R * i) >= vovI)
                    return i;
            }
        }
        // --- triode --------------------------------------------------------------------------
        {
            const double C = 2.0 * A - B;
            const double D = params.zLoad - rd;
            roots(beta * R * D, 1.0 - beta * (B * D - R * C), id0 - beta * B * C, r);
            for (const double i : { r[0], r[1] })
            {
                if (i != i)
                    continue;
                const double vovI = A - rd * i;
                const double vdsI = B - R * i;
                if (vovI >= 0.0 && vdsI >= 0.0 && vdsI < vovI)
                    return i;
            }
        }
        // --- cutoff, or the drain bottomed: the device passes nothing -------------------------
        return -id0;
    }

    /** ⭐⭐ THE PRODUCTION SOLVE at the shipped exponent: ONE unknown in the common case, and it is
     *  the CHANGE OF VARIABLE that makes it cheap rather than any tuning.
     *
     *  Newton on the drain current over the composite piecewise map -- which is what solveIterative()
     *  below does, and what shipped first here -- needs eight iterations, because dF/di runs from 1
     *  in cutoff to ~40 in strong saturation and the safeguarded step has to bisect its way across
     *  that. But in SATURATION the whole stage collapses to a single scalar equation in the
     *  instantaneous overdrive u = Vov_i. Substituting the source one-port vs = Rd*i + vOff and
     *  i = beta*u^m - Id0 into u = Vov + vGate - vs gives
     *
     *      u + c*u^m = P,      c = Rd*beta,     P = Vov + vGate - vOff + Rd*Id0
     *
     *  and then i = (A - u)/Rd with A = Vov + vGate - vOff, which needs NO further pow.
     *
     *  ⭐ That function is convex and strictly increasing for m > 1, and the bracket (0, P] is free
     *  -- h(0) = -P < 0 and h(P) = c*P^m > 0. Measured from the linear-root start, worst relative
     *  error over gate drives from 0.05 V to 4.016 V and over Rd from 3600 ohm (Dark) down to
     *  55 ohm (Bright at 384 kHz):
     *
     *  Measured against a 300-step bisection of the same equations, over gate drives from -14 V to
     *  +14 V at five values of Rd from 3600 ohm (Dark) down to 55 ohm (Bright at 384 kHz), worst
     *  absolute error in the drain current:
     *
     *      iterations       2          3          4
     *      Newton           9.6e-08    8.9e-11    7.6e-17
     *      Halley           6.9e-11    1.6e-18    1.6e-18
     *
     *  ⭐ THREE HALLEY STEPS IS EXACT -- 1.6e-18 A is rounding -- against the generic solve's eight
     *  Newton steps on a harder problem. Both methods cost one std::pow per iteration, and Halley
     *  gets h, h' and h'' out of that same pow, so its extra order of convergence is free.
     *
     *  📌 Two Halley steps were measured and rejected: 6.9e-11 A is 135 dB below the quiescent
     *  current and inaudible by any measure, but it costs 1 percentage point of chain CPU at the 4x
     *  default (5.3 % against 6.3 %) to buy an EXACT solve, and every measurement this project makes
     *  compares the model against something else. A model that answers its own equations exactly can
     *  be asserted at machine precision, so a future structural error shows up immediately instead
     *  of hiding inside a tolerance that had to be justified.
     *
     *  📌 CPU, measured, so it does not have to be re-derived: this solve costs 6.7-7.5 % of
     *  realtime at the 4x default and 12.8-14.4 % at 8x, against 2.3 % for the square law's closed
     *  form. It is all std::pow -- three per sample. Replacing them with exp2((m-1)*log2(u)) was
     *  benchmarked at 6.3 ns against pow's 9.2 ns, i.e. about 1.4 points of chain CPU, and NOT
     *  taken: it costs a digit or two of the machine-precision agreement above, and build.md's rule
     *  is that FeatureProfile decides what to optimise. At 7 % it does not flag this.
     *
     *  ⚠⚠ AND THE WORST CASE IS AT THE CUTOFF KNEE, NOT AT FULL DRIVE. Every iteration count above
     *  fails worst at a gate of about -1.78 V, just inside cutoff (|Vp| = 1.942 V), where P is small,
     *  the linear-root start is 2.3x above the root, and h'' goes as u^(m-2) -> infinity. A sweep
     *  that probes only loud inputs will not find it: the first version of this solve was checked at
     *  0.05-4.016 V and read exact.
     *
     *  ⚠ P <= 0 IS CUTOFF AND MUST BE TESTED FIRST. It is not an edge case -- it is what every loud
     *  negative half-cycle does, and the scalar equation has no positive root there at all.
     *
     *  Triode falls back to a safeguarded Newton on i, because there the drain voltage is a second
     *  coupled unknown and the collapse above does not happen. It is worth leaving generic: triode
     *  is empty at ordinary playing levels and is entered for at most ~20 % of a period even at
     *  0 dBFS into the largest drain load. */
    inline double solvePowerLaw(double vGate) noexcept
    {
        const double rd = srcRd;
        const double off = srcOffset();
        const double vov = params.vov();
        const double id0 = params.id0();
        const double beta = params.betaSq();
        const double m = params.mExp;
        const double zl = params.zLoad;

        const double A = vov + vGate - off;
        const double P = A + rd * id0;
        if (P <= 0.0) // cutoff: the device passes nothing, whatever the drain is doing
            return -id0;

        const double B = params.vdsQuiescent() - off;
        const double R = zl + rd;

        // --- saturation, the common case: one unknown, u + c*u^m = P ---------------------------
        const double c = rd * beta;
        double u = A - rd * (params.gm * (vGate - off) / (1.0 + params.gm * rd)); // linear root
        if (!(u > 0.0) || u > P)
            u = 0.5 * P; // only reachable from a pathological start; the bracket is (0, P]
        for (int n = 0; n < kSatIters; ++n)
        {
            // ⭐ HALLEY, not Newton, and it is free: h, h' and h'' all come out of the SAME single
            // std::pow, so a cubically-convergent step costs exactly what a quadratic one does.
            // Two Halley steps reach the same accuracy as three Newton steps (worst relative error
            // 1.2e-12 against 7.3e-12 over the whole drive range and both extremes of Rd), and the
            // pow count is what the solve costs: measured 6.29 % -> 5.29 % of realtime at the 4x
            // default, 13.33 % -> 11.61 % at 8x.
            const double up = std::pow(u, m - 1.0);
            const double h = u + c * u * up - P;
            const double hp = 1.0 + c * m * up;
            const double hpp = c * m * (m - 1.0) * up / u;
            const double den = 2.0 * hp * hp - h * hpp;
            // ⚠ Halley's denominator is not sign-definite the way Newton's h' >= 1 is, so fall back
            // to the Newton step rather than dividing by something near zero. h'' > 0 and h' >= 1,
            // so this can only trigger for a large positive h, i.e. a start far above the root.
            u -= (den > 0.0) ? (2.0 * h * hp / den) : (h / hp);
            if (!(u > 0.0))
                u = 0.5 * P;
            else if (u > P)
                u = P;
        }
        const double iSat = (A - u) / rd;
        if (B - R * iSat >= u) // Vds_i >= Vov_i: saturation is the valid branch
            return iSat;

        // --- triode ---------------------------------------------------------------------------
        // F(i) = i + Id0 - beta*(u^m - w^m),  u = A - Rd*i,  w = u - Vds_i = (A - B) + zLoad*i.
        // F is strictly increasing (both partials of the law are non-negative), so the root is
        // unique and the bracket is free.
        //
        // ⚠⚠ BOTH ENDS OF THE BRACKET ARE UPPER BOUNDS AND THE SATURATION ROOT IS ONE OF THEM. In
        // triode the device passes LESS than the saturation law would at the same overdrive, so the
        // true root lies BELOW iSat -- and below B/R, where the drain has bottomed and the current
        // is zero. Taking [iSat, B/R] as the bracket, which is the shape the saturation branch
        // suggests, is inverted whenever iSat > B/R, which is exactly what a loud half-cycle does:
        // it returned iSat and the stage read 980 uA against the oracle's 458 at a 0 dBFS peak.
        // Caught by section 6, which solves the same circuit by bisection.
        double lo = -id0, hi = (iSat < B / R) ? iSat : B / R;
        if (!(hi > lo))
            hi = lo + 1.0e-9;
        double i = 0.5 * (lo + hi);
        for (int n = 0; n < kTriodeIters; ++n)
        {
            const double uu = A - rd * i;
            const double w = (A - B) + zl * i;
            if (!(uu > 0.0))
                return -id0;
            const double wc = (w > 0.0) ? w : 0.0;
            const double upow = std::pow(uu, m - 1.0);
            const double wpow = (wc > 0.0) ? std::pow(wc, m - 1.0) : 0.0;
            const double f = i + id0 - beta * (uu * upow - wc * wpow);
            (f > 0.0 ? hi : lo) = i;
            const double next = i - f / (1.0 + beta * m * (rd * upow + zl * wpow));
            // ⚠ NON-STRICT, for the reason solveIterative() records: the root sits exactly ON a
            // bracket end whenever the drain bottoms, which is what loud half-cycles do, and strict
            // tests reject a converged iterate and bisect away from the answer.
            i = (next >= lo && next <= hi) ? next : 0.5 * (lo + hi);
        }
        return i;
    }

    inline double solveIterative(double vGate) noexcept
    {
        const double rd = srcRd;
        const double off = srcOffset();
        const double gm = params.gm;
        const double vov = params.vov();
        const double beta = params.betaSq();
        const double id0 = params.id0();
        const double vdsQ = params.vdsQuiescent();
        const double zl = params.zLoad;

        // ⚠⚠ SAFEGUARDED NEWTON, NOT PLAIN NEWTON -- and the difference is the whole solve.
        // F is strictly increasing (dF/di = 1 + Rd*dI/dVov + (zLoad+Rd)*dI/dVds, both partials of the
        // square law being non-negative), so it has exactly one root. That is NOT enough for plain
        // Newton: F' varies from 1 in cutoff to ~40 in strong saturation, so a step taken in the flat
        // region lands deep in the steep one and the iteration CYCLES rather than converging. It was
        // measured doing exactly that -- a period-3 orbit with the residual stuck near 1e-2 A no
        // matter how many iterations were spent, worst in BRIGHT where Rd falls to 112 ohm at 192 kHz
        // and the loop barely damps anything. The previous fitted shaper was bounded and never
        // provoked it; the square law does, on ordinary signal.
        //
        // The fix is the textbook one, and it is cheap here because the bracket is FREE and TIGHT:
        //   lo = -Id0                     the device in cutoff, I = 0, so F = -I <= 0
        //   hi = (Vds_q - vOff)/(zLoad+Rd)  the drain bottomed, Vds = 0 so I = 0 and F = i + Id0 > 0
        // That is about 980 uA wide at the shipped operating point. Each iteration keeps the bracket,
        // takes the Newton step when it stays inside, and bisects when it does not -- so convergence
        // is guaranteed in a fixed iteration count instead of hoped for.
        const double lo0 = -id0;
        const double hi0 = (vdsQ - off) / (zl + rd);
        double lo = lo0, hi = (hi0 > lo0) ? hi0 : lo0 + 1.0e-9;

        double i = gm * (vGate - off) / (1.0 + gm * rd); // the linear root: the best cheap start
        i = (i < lo) ? lo : ((i > hi) ? hi : i);
        double vs = 0.0;
        for (int n = 0; n < solveIters; ++n)
        {
            vs = rd * i + off;
            const double vovI = vov + (vGate - vs);
            const double vdsI = vdsQ - i * zl - vs;
            double dIdVov = 0.0, dIdVds = 0.0;
            const double I = deviceCurrent(vovI, vdsI, beta, params.mExp, dIdVov, dIdVds);
            const double f = i - (I - id0);

            (f > 0.0 ? hi : lo) = i; // F increases, so a positive residual puts the root to the left
            const double next = i - f / (1.0 + rd * dIdVov + (zl + rd) * dIdVds);
            // ⚠ NON-STRICT. The root sits exactly ON a bracket end whenever the device is in
            // cutoff (i = -Id0 = lo) or the drain is bottomed (Vds = 0 = hi), which are not corner
            // cases here -- they are what the loud half-cycles do. With strict inequalities a
            // converged iterate is rejected as "outside", the solve bisects away from the answer,
            // and MORE iterations make it WORSE. Measured doing exactly that before this was fixed.
            i = (next >= lo && next <= hi) ? next : 0.5 * (lo + hi);
        }

        // ⚠⚠ TAKE THE NEWTON ITERATE. Do NOT "improve" it with a final i = I(...) - Id0, which is
        // what the previous structure did and which is a FIXED-POINT step -- and this map is not a
        // contraction. |dI/di| = Rd*2*beta*Vov_i reaches ~8.8 in the normal operating range, so that
        // step MULTIPLIES the error by 8.8 instead of reducing it. Under the old fitted shaper it was
        // mild enough to look harmless; against the square law it sent a 3 V input to 9.5 mA of drain
        // current, past IDSS, with an internally inconsistent vs. Newton converges here because F is
        // monotone; the fixed-point iteration does not converge at all.
        return i;
    }

    /** The device itself: Shichman-Hodges square law with its cutoff and triode regions, plus both
     *  partials for the Newton step. Total drain current in amps, NOT referenced to the quiescent
     *  point -- solveDrain subtracts Id0.
     *
     *  ⚠ Vds is clamped at zero. The load line prevents the drain ever getting there in normal
     *  operation (the solve settles where the two curves meet, which is above it), but the clamp
     *  keeps the triode parabola on its monotone side during Newton's intermediate iterates, where
     *  nothing physical constrains the trial value. */
    static inline double deviceCurrent(double vovInst, double vdsInst, double beta, double m,
                                       double& dIdVov, double& dIdVds) noexcept
    {
        if (vovInst <= 0.0) // cutoff -- the law meets zero WITH zero slope for m > 1, so this is C1
        {
            dIdVov = 0.0;
            dIdVds = 0.0;
            return 0.0;
        }
        const double vds = (vdsInst > 0.0) ? vdsInst : 0.0;
        const double uPow = std::pow(vovInst, m - 1.0); // one pow; u^m is u * u^(m-1)
        if (vds >= vovInst)                             // saturation
        {
            dIdVov = m * beta * uPow;
            dIdVds = 0.0;
            return beta * vovInst * uPow;
        }
        // Triode. I = beta*(Vov_i^m - (Vov_i - Vds_i)^m).
        //
        // ⭐ This is the ONLY generalisation of Shichman-Hodges' triode branch that keeps every
        // property the solve leans on, and it is forced rather than chosen:
        //   * at m = 2 it expands to exactly beta*(2*Vov_i*Vds_i - Vds_i^2), the shipped expression;
        //   * at the boundary Vds_i = Vov_i it meets the saturation value with dI/dVds = 0, so the
        //     composite map stays C1 and there is no corner for oversampling to alias off;
        //   * both partials stay non-negative (w = Vov_i - Vds_i <= Vov_i, so u^(m-1) >= w^(m-1)),
        //     which is what makes F strictly increasing and the root unique -- the guarantee the
        //     safeguarded Newton below is built on.
        // It is not a smooth-looking interpolation picked to fit.
        const double w = vovInst - vds;
        const double wPow = std::pow(w, m - 1.0);
        dIdVov = m * beta * (uPow - wPow);
        dIdVds = (vdsInst > 0.0) ? m * beta * wPow : 0.0;
        return beta * (vovInst * uPow - w * wPow);
    }

    /** Instantaneous drain-source voltage and effective gate-source drive at the last solved sample.
     *  Exposed so a test can assert WHERE on the load line the stage is operating rather than infer
     *  it from the output, which is the only way to check the triode region is being entered at the
     *  drive the circuit says it should be. */
    double lastDrainSourceVolts() const noexcept { return lastVds; }
    double lastGateDrive() const noexcept { return lastW; }

    /** Gate swing at which the drain enters TRIODE -- where the load line meets the device curve.
     *
     *  ⚠⚠ THIS USED TO REPORT THE ANSWER IN THE WRONG UNITS, and the wrong number is on the record.
     *  It solved for the gate-SOURCE drive w and then converted to gate volts by multiplying by the
     *  SMALL-SIGNAL K0 -- but at triode onset the stage is nowhere near small signal, and the real
     *  conversion is vGate = w + R5*i with i the large-signal current. The old form read 1.691 V
     *  where the true onset at the shipped drain load is about 2.4 V.
     *  📌 And 1.691 V is very nearly the CUTOFF onset (1.696 V under the old constants), so the
     *  recorded claim "triode is entered first, at -7.5 dBFS" was a mis-converted triode figure that
     *  coincided with the correct cutoff figure. Cutoff is entered first, at every VOLUME position.
     *  Bisected on the real pair, and returned in GATE VOLTS. */
    double triodeOnsetGateVolts() const
    {
        const double id0 = params.id0(), beta = params.betaSq(), m = params.mExp;
        auto slack = [&](double w) {
            const double vovI = params.vov() + w;
            const double i = beta * std::pow(vovI, m) - id0;
            const double vs = circuit::kR5 * i;
            return (params.vdsQuiescent() - i * params.zLoad - vs) - vovI;
        };
        // Vds - Vov_i decreases monotonically in w. Report infinity when the drain never gets there,
        // which is what a small drain load (VOLUME near its stop) actually does.
        if (slack(20.0) > 0.0)
            return std::numeric_limits<double>::infinity();
        double lo = 0.0, hi = 20.0;
        for (int n = 0; n < 200; ++n)
        {
            const double mid = 0.5 * (lo + hi);
            (slack(mid) > 0.0 ? lo : hi) = mid;
        }
        const double w = 0.5 * (lo + hi);
        const double i = beta * std::pow(params.vov() + w, m) - id0;
        return w + circuit::kR5 * i;
    }

    /** Gate swing at which the device reaches CUTOFF, in gate volts.
     *
     *  ⭐ It is exactly |Vp|, with no solve and no dependence on gm, on m, or on the drain load. At
     *  cutoff the device passes nothing, so the source sits at -R5*Id0 and the gate has to reach
     *  -(Vov + R5*Id0) = -|Vp| by the definition of the self-bias point. That is what makes |Vp| the
     *  parameter the captures measure directly rather than infer (see JfetParams::vp). */
    double cutoffOnsetGateVolts() const { return params.vp; }

    /** Drain Norton impedance, R6 || ro. Stamped into OutputNetwork, never applied here.
     *  ro*k(s) is frequency dependent in principle, but ro (1.44 MOhm at the fitted Id) is 65x R6, so
     *  the parallel combination moves by under 1% (0.06 dB) across the whole k range -- constant. */
    double outputImpedance() const
    {
        return (circuit::kR6 * params.ro) / (circuit::kR6 + params.ro);
    }

    /** DC degeneration factor K0 = 1 + gm*R5. This IS the mode plateau ratio M2 measures, and the one
     *  number in the model that a ratio of two captures hands over with no level calibration. */
    double degenerationDC() const { return 1.0 + params.gm * circuit::kR5; }

private:
    /** The source one-port's Thevenin offset for THIS sample: everything in vs[n] that does not
     *  depend on id[n]. Zero in DARK, where the port is the bare resistor R5. */
    inline double srcOffset() const noexcept { return srcC1 * srcIdPrev + srcC2 * srcVsPrev; }

    inline void advanceSource(double id, double vs) noexcept
    {
        srcIdPrev = id;
        srcVsPrev = vs;
    }

    /** Holder for the discretised 1/k(s) coefficients. ⚠ The stage no longer RUNS this filter on
     *  the signal -- the source one-port derived from these coefficients (updateSourcePort) is what
     *  carries the degeneration now, inside the solve. It is kept as the place the three-point
     *  magnitude match writes its answer, because that design and its per-rate error budget are what
     *  the mode differential and OsDroopRestore were validated against. */
    struct Shelf
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0;
        double x1 = 0.0, y1 = 0.0;

        inline double process(double x) noexcept
        {
            const double y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x;
            y1 = y;
            return y;
        }

        void reset() noexcept { x1 = 0.0; y1 = 0.0; }
    };

    /** Source-bypass time constant engaged by the current MODE position; 0 means no branch grounded
     *  (DARK). MEASURED, not computed as R5*C -- see JfetParams for why the two differ by 7%.
     *  The lever->cap mapping itself was also measured and came out reversed (M1). */
    double bypassTau() const
    {
        switch (mode)
        {
            case Mode::Bright: return params.tauBright; // 22 nF branch, zero 1864 Hz
            case Mode::Mid:    return params.tauMid;    // 10 nF branch, zero 4160 Hz
            case Mode::Dark:   break;
        }
        return 0.0;
    }

    // Discretisation of 1/k(s) = (1 + s*tau) / (K0 + s*tau).
    //
    // ⚠ NOT a plain bilinear transform, and at the base rate the difference is worth up to 3.5 dB.
    // Bilinear warps every corner down by a factor tan(theta/2)/(theta/2), and this shelf is
    // unusually exposed to that because its pole sits K0 = 6.6x above its zero: 12.3 kHz in Bright
    // and 27.4 kHz in Mid, the second ABOVE Nyquist at 48 kHz. Warping a pole that is already past
    // Nyquist back down into the audio band makes the shelf reach its plateau early, which reads as
    // a top-octave LIFT. Measured against the analog shelf at 48 kHz, plain bilinear costs +1.17 dB
    // (Bright) and +3.53 dB (Mid) -- and that is ALL of the mode-to-mode spread OSFidelity reports
    // in the 1x droop, to within 0.05 dB. None of it is physical; it is this line of code.
    //
    // ⛔ Prewarping does not fix it. Prewarp pins ONE corner, and pinning BOTH (a separate prewarp
    // constant for the numerator and the denominator, which a first-order section has room for)
    // measures WORSE than plain bilinear -- 3.06 dB at 18 kHz against 1.17 -- because pinning the
    // two ends of a transition lets the curve between them bow out. Measured before it was believed.
    //
    // Instead, match the analog MAGNITUDE exactly at three frequencies: DC, Nyquist, and the shelf's
    // own log-midpoint sqrt(fz*fp) = fz*sqrt(K0). A first-order section has exactly three degrees of
    // freedom, so three constraints determine it with nothing left over to fit -- and all three
    // frequencies come from the circuit, not from a residual. A scan for the minimax-optimal third
    // frequency lands within 6% of the log-midpoint's error, so the principled choice gives up
    // nothing measurable to a tuned one.
    //
    // Result, worst |error| over 20 Hz - 20 kHz against the analog shelf:
    //
    //     rate         48 kHz (1x)   96 kHz (2x)   192 kHz (4x)   384 kHz (8x)
    //     bilinear     3.529 dB      0.801 dB      0.192 dB       0.048 dB
    //     this         0.488 dB      0.203 dB      0.053 dB       0.013 dB
    //
    // ⚠ Phase was checked, not assumed, because a magnitude-only design is exactly where phase gets
    // quietly traded away and this project has been bitten by that before. RAW phase error does get
    // worse (Mid at 18 kHz: -22.7 deg against bilinear's -13.5). It is a near-CONSTANT fractional
    // sample of extra delay -- remove the best-fit pure delay, which is what a sub-sample null aligns
    // out anyway, and the residual phase error is also roughly HALVED at every rate (Mid at 48 kHz:
    // 5.7 deg against 12.8). So this is better on both axes, not a trade.
    void updateShelf()
    {
        const double k0 = degenerationDC();
        const double tau = bypassTau();

        if (tau <= 0.0)
        {
            // DARK: k is frequency-independent, so this degenerates to a plain gain -- exact at every
            // rate, with no discretisation question to answer. Taking the tau -> 0 limit of a
            // bilinear form instead would give a1 = 1, a marginally stable filter rather than a
            // constant.
            driveShelf.b0 = 1.0 / k0;
            driveShelf.b1 = 0.0;
            driveShelf.a1 = 0.0;
        }
        else
        {
            const auto anaMagSq = [k0, tau](double f) {
                const double wTau = 2.0 * M_PI * f * tau;
                return (1.0 + wTau * wTau) / (k0 * k0 + wTau * wTau);
            };

            const double fZero = 1.0 / (2.0 * M_PI * tau);
            // The third match frequency must sit strictly inside (0, Nyquist). At the base rate the
            // Mid shelf's log-midpoint (10.7 kHz) is comfortably inside; the clamp is here for a
            // host running below ~24 kHz, and it only ever moves the point DOWN, which keeps the
            // solve well-posed rather than merely safe.
            const double fMatch = std::min(fZero * std::sqrt(k0), 0.45 * fs);

            // With p = b0 + b1 and q = b0 - b1, the magnitude response separates cleanly:
            //
            //     |H|^2 = [p^2*cos^2(th/2) + q^2*sin^2(th/2)]
            //           / [(1+a1)^2*cos^2(th/2) + (1-a1)^2*sin^2(th/2)]
            //
            // At DC (th = 0) that is p/(1+a1) and at Nyquist (th = pi) it is q/(1-a1), so the first
            // two constraints give p and q outright in terms of a1. Substituting them into the third
            // collapses it to a single ratio, with no iteration:
            //
            //     (1+a1)/(1-a1) = tan(th3/2) * sqrt((Mn^2 - T3) / (T3 - 1/K0^2))
            //
            // Both radicands are strictly positive: |1/k(j*2*pi*f)| rises monotonically from 1/K0 at
            // DC toward 1, so 1/K0^2 < T3 < Mn^2 for any 0 < fMatch < Nyquist. That is why no
            // clamping or fallback branch is needed here -- the geometry guarantees it.
            const double mnSq = anaMagSq(0.5 * fs);
            const double t3 = anaMagSq(fMatch);
            const double ratio =
                std::tan(M_PI * fMatch / fs) * std::sqrt((mnSq - t3) / (t3 - 1.0 / (k0 * k0)));

            const double a1 = (ratio - 1.0) / (ratio + 1.0);
            const double p = (1.0 + a1) / k0;                // b0 + b1 -- exact DC gain 1/K0
            const double q = std::sqrt(mnSq) * (1.0 - a1);   // b0 - b1 -- exact magnitude at Nyquist

            driveShelf.b0 = 0.5 * (p + q);
            driveShelf.b1 = 0.5 * (p - q);
            driveShelf.a1 = a1;
        }

        updateSourcePort();
    }

    /** Derive the DISCRETE source one-port Zs(z) that the implicit solve feeds back through, FROM
     *  the shelf coefficients computed above rather than from a fresh discretisation of R5 || C.
     *
     *  ⭐⭐ THIS IS THE WHOLE DESIGN, and it is the reason Path A costs nothing in linear accuracy.
     *  Linearised, the solve gives W(z)/Vg(z) = 1 / (1 + gm*Zs(z)). Demanding that this equal the
     *  shelf H(z) = (b0 + b1 z^-1)/(1 + a1 z^-1) that updateShelf() just designed inverts to
     *
     *      gm*Zs(z) = 1/H(z) - 1 = [ (1-b0) + (a1-b1) z^-1 ] / [ b0 + b1 z^-1 ]
     *
     *  which is a first-order one-port, realised as the difference equation
     *
     *      vs[n] = Rd*id[n] + C1*id[n-1] + C2*vs[n-1]
     *      Rd = (1-b0)/(gm*b0)   C1 = (a1-b1)/(gm*b0)   C2 = -b1/b0
     *
     *  So the stage's SMALL-SIGNAL RESPONSE IS BIT-IDENTICAL to the shipped shelf, at every mode and
     *  every sample rate, and everything that was validated against it stands untouched: the mode
     *  differential, the three-point magnitude match that fixed the 3.09 dB bilinear spread, the
     *  phase residuals, OsDroopRestore's premise. ⛔ Discretising R5 || C directly instead -- the
     *  obvious thing to write -- would have quietly reintroduced plain bilinear on a shelf whose pole
     *  sits ABOVE Nyquist at the base rate, i.e. the exact 3.5 dB error the previous session removed.
     *  The nonlinearity is the only thing this path is allowed to change.
     *
     *  ✅ Two facts fall OUT of the algebra rather than being imposed, which is what says it is right:
     *    - Zs(z=1) = (K0-1)/gm = R5 EXACTLY, in every mode and at every rate. Physically it must be:
     *      the bypass caps block DC, so the DC feedback path is the bare resistor. That is also why
     *      this path produces frequency-INDEPENDENT self-bias compression while the harmonic
     *      suppression stays frequency-dependent -- two behaviours from one port.
     *    - DARK collapses to Rd = R5, C1 = C2 = 0: a memoryless resistor, no state, no filter.
     *
     *  Stability: the port's pole is at z = -b1/b0, which is the shelf's own zero (the bypass corner
     *  at 1.86 / 4.17 kHz). Measured across modes and rates it lands at 0.55-0.97, well inside the
     *  unit circle, and Rd stays positive (55-3600 ohm). DroopRestoreTest-style asserts on both are
     *  in JfetStageTest section 8. */
    void updateSourcePort()
    {
        const double b0 = driveShelf.b0, b1 = driveShelf.b1, a1 = driveShelf.a1;
        srcRd = (1.0 - b0) / (params.gm * b0);
        srcC1 = (a1 - b1) / (params.gm * b0);
        srcC2 = -b1 / b0;
    }

    JfetParams params {};
    Mode mode = Mode::Dark;
    double fs = 48000.0;

    Shelf driveShelf {};

    // The implicit solve's source one-port (updateSourcePort()) and its state.
    double srcRd = circuit::kR5, srcC1 = 0.0, srcC2 = 0.0;
    double srcIdPrev = 0.0, srcVsPrev = 0.0;
    double solveResidual = 0.0;

    // Newton iterations per sample. FIXED, for the same reason as before: a convergence loop whose
    // length depends on the signal makes CPU depend on programme material, and a per-sample early-out
    // branch is mispredicted exactly where the signal is busiest.
    //
    // 📌 TEN, against the previous structure's two, AND IT COSTS NOTHING. deviceCurrent() has no
    // transcendentals at all -- the square law is a few multiplies and two comparisons, where the old
    // shaper needed a tanh and a sqrt per evaluation. Measured at the 4x default: 4.84 % of realtime
    // at 3 iterations, 4.80 % at 12, i.e. indistinguishable. So the count is set by accuracy alone.
    // Harmonic error against a 40-iteration reference, worst over the three modes:
    //
    //     gate amp    3 iters              5              6             10
    //     <= 1.7 V    0.0000 dB            0.0000         0.0000        0.0000
    //     4.016 V     10.59 dB of H2       1.61           0.70          0.0000
    //
    // 4.016 V is a 0 dBFS peak at the shipped kInputRef, i.e. the loudest ordinary signal, and it is
    // deep in triode -- which is exactly where the solve is hardest and where a cheaper count would
    // have been wrong by 10 dB while looking fine everywhere else.
    static constexpr int kSolveIters = 8;

    // Iterations for the production (power-law) solve. Both are set on MEASURED convergence, not on
    // a residual: see solvePowerLaw()'s table for saturation, and JfetStageTest 8c(b), which scores
    // the count on H2/H3 against a 40-iteration reference at a 0 dBFS peak deep in triode.
    static constexpr int kSatIters = 3;
    static constexpr int kTriodeIters = 8;
    int solveIters = kSolveIters;
    Solver solver = Solver::Shipped;

    // Operating point at the last solved sample, for tests and probes only.
    double lastVds = 0.0, lastW = 0.0;
};
} // namespace pedal::dsp
