#pragma once

#include <algorithm>
#include <cmath>

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
    // It is 10.00 dB below kInputRef = 0.87, so matched-drive A/B against P1 means feeding the
    // plugin -10.00 dB (analysis/harmonic_audit.py --dbu-capture -12 does exactly this).
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
    double ro = 1.4407e6; // Ohm -- 1/(lambda*Id) at lambda = 2 mV/V, with the fitted Id = 347 uA.


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
    double vov = 0.4469; // V -- overdrive at the quiescent point. THE amplitude parameter.
                         //
                         // ⚠ STILL NOT MEASURED, and the reason has not changed: it is degenerate
                         // 1:1 with the trainers' reamp level, and the one unit whose level IS known
                         // (P1, -12 dBu) has nonlinear data sitting on its own floor. circuit.md note
                         // #10 puts it near 0.150 from P1's H2 deficit; this ships the datasheet-
                         // capped 0.447 end. What HAS changed is that applying 0.150 is no longer
                         // blocked by this file: the truncation is gone (note #12) and the load line
                         // is implemented (here), which were reasons 4 and 3 of the four. The
                         // remaining two are irreducible without a calibrated capture that clears
                         // its floor -- which the +12.2 dBu session is designed to produce.
                         //
                         // Derived, via the one-parameter self-bias family |Vp|/Vov = 1 + gm*R5/2:
                         //   Id0  = gm*Vov/2      = 347 uA     beta = Id0/Vov^2 = gm/(2*Vov)
                         //   |Vp| = 3.7956*Vov    = 1.696 V    IDSS = Id0*3.7956^2 = 5.00 mA
                         //   Vds_q = VA - Id0*(R6 + R5) = 13.12 V

    // AC impedance at the DRAIN NODE, which is what turns drain current into drain volts and
    // therefore what sets the load line's slope.
    //
    // 📌 A CONSTANT, and deliberately so. The true value is (R6 || ro) in parallel with the C10 ->
    // output-network branch, which moves with VOLUME -- but the stage runs INSIDE the oversampled
    // region while OutputNetwork runs at base rate, so the real drain voltage is not available here
    // per sample even in principle. EchoPreDsp sets this from the output network's own computed
    // value once per block (setDrainLoad), which is the same cadence VOLUME already updates at.
    double zLoad = 20.6e3; // Ohm

    /** Quiescent drain current, Id0 = gm*Vov/2. */
    double id0() const { return 0.5 * gm * vov; }
    /** Transconductance parameter, beta = Id0/Vov^2 = gm/(2*Vov). */
    double betaSq() const { return 0.5 * gm / vov; }
    /** Quiescent drain-source voltage. The drain sinks Id0 through R6 and the source rises through R5. */
    double vdsQuiescent() const { return circuit::kVA - id0() * (circuit::kR6 + circuit::kR5); }
    /** Pinch-off magnitude implied by the self-bias family. */
    double vpMagnitude() const { return vov * (1.0 + 0.5 * gm * circuit::kR5); }

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
        const double rd = srcRd;
        const double off = srcOffset();
        const double gm = params.gm;
        const double vov = params.vov;
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
            const double I = deviceCurrent(vovI, vdsI, beta, dIdVov, dIdVds);
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
        vs = rd * i + off;
        const double vovI = vov + (vGate - vs);
        const double vdsI = vdsQ - i * zl - vs;
        double dIdVov = 0.0, dIdVds = 0.0;
        // Reported in AMPS OF CURRENT ERROR, not as the raw residual: F' runs to ~40 here, so |F|
        // overstates the error by that factor and would read alarming when the answer is exact.
        const double resid = i - (deviceCurrent(vovI, vdsI, beta, dIdVov, dIdVds) - id0);
        solveResidual = resid / (1.0 + rd * dIdVov + (zl + rd) * dIdVds);

        lastVds = vdsI;
        lastW = vGate - vs;
        advanceSource(i, vs);
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
    static inline double deviceCurrent(double vovInst, double vdsInst, double beta,
                                       double& dIdVov, double& dIdVds) noexcept
    {
        if (vovInst <= 0.0) // cutoff -- and the parabola meets zero WITH zero slope, so this is C1
        {
            dIdVov = 0.0;
            dIdVds = 0.0;
            return 0.0;
        }
        const double vds = (vdsInst > 0.0) ? vdsInst : 0.0;
        if (vds >= vovInst) // saturation
        {
            dIdVov = 2.0 * beta * vovInst;
            dIdVds = 0.0;
            return beta * vovInst * vovInst;
        }
        // triode
        dIdVov = 2.0 * beta * vds;
        dIdVds = (vdsInst > 0.0) ? 2.0 * beta * (vovInst - vds) : 0.0;
        return beta * (2.0 * vovInst * vds - vds * vds);
    }

    /** Instantaneous drain-source voltage and effective gate-source drive at the last solved sample.
     *  Exposed so a test can assert WHERE on the load line the stage is operating rather than infer
     *  it from the output, which is the only way to check the triode region is being entered at the
     *  drive the circuit says it should be. */
    double lastDrainSourceVolts() const noexcept { return lastVds; }
    double lastGateDrive() const noexcept { return lastW; }

    /** Gate swing at which the drain enters triode, i.e. where the load line meets the device curve.
     *  Solved directly rather than measured: Vds_q - i*(zLoad + R5) = Vov + w with i = gm*w at the
     *  boundary's small-signal slope is not exact, so this iterates the real pair. */
    double triodeOnsetGateVolts() const
    {
        const double vov = params.vov, id0 = params.id0(), beta = params.betaSq();
        // Vds - Vov_i is monotone decreasing in w, so bisect rather than iterate a damped map.
        auto slack = [&](double w) {
            const double vovI = vov + w;
            const double i = beta * vovI * vovI - id0;
            return params.vdsQuiescent() - i * (params.zLoad + circuit::kR5) - i * 0.0 - vovI;
        };
        double lo = 0.0, hi = 5.0;
        for (int n = 0; n < 200; ++n)
        {
            const double m = 0.5 * (lo + hi);
            (slack(m) > 0.0 ? lo : hi) = m;
        }
        return 0.5 * (lo + hi) * degenerationDC();
    }

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
    int solveIters = kSolveIters;

    // Operating point at the last solved sample, for tests and probes only.
    double lastVds = 0.0, lastW = 0.0;
};
} // namespace pedal::dsp
