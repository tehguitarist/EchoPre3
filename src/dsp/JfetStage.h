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
    //      the seven files carries input_level_dbu or output_level_dbu (checked). Nothing in the
    //      data can re-derive it, so the whole result rests on that one external fact.
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

    // Shaper. g(w) = T(w) + (a*s^2/2)*tanh^2(w/s), w = effective vgs in REAL GATE VOLTS, so g'(0) = 1
    // exactly and gm alone sets the gain -- the shaper only adds curvature.
    //
    // The exact square-law device transfer, normalised the same way, is simply
    //
    //     g(w) = w + w^2/(2*Vov)      for w >= -Vov, and g = -Vov/2 below it (cutoff)
    //
    // which is where aEven = 1/Vov comes from: it is not a fitted knob, it is the square law. A tanh
    // structurally cannot produce an even-dominant stage (nonlinear doc section 2 finding (a)), hence
    // the linear-core-plus-even-bump form, whose bump is exactly even and adds no odd content.
    // ⚠ Known deviation, measured: tanh^2 saturates where the true parabola does not, so at a 0 dBFS
    // input (w ~ 0.118 V) the bump is 4.5% (0.4 dB of H2) below the exact parabola, growing with
    // drive. An exact parabola with a cutoff clamp is the refinement; it is not the dominant error
    // (the Volterra truncation below is larger), so both are deferred together.
    double aEven = 2.2374867; // 1/V -- = 1/Vov. THE square-law parameter, now derived from measured gm.
    double bumpScale = 0.44693002; // V -- even-bump scale = Vov. Then a*s = 1 and the bump saturates
                                   //      at a*s^2/2 = Vov/2 = Id0/gm, i.e. exactly the cutoff
                                   //      current -- the physical scale, not a tuned one.
    double beta = 0.0;      // 1/V^2  -- cubic coefficient of the odd core. 0 = pure square law.
                            //         M5 could NOT settle this: H3 sits under both NAM models' error
                            //         floors (H4 comes back ABOVE H3, which no mild polynomial can
                            //         do), so the only cubic evidence is 0.09-0.35 dB of top-cell
                            //         compression, which fixes the SIGN (compressive, beta < 0) and
                            //         nothing else. Left at 0 rather than fitted to a floor.
                            // ⚠ UPDATED 2026-09-08: "H3 is all floor" is TOO STRONG, and it was M5's
                            //         blanket claim rather than a per-capture one. Re-measured per
                            //         capture, P1's H3 is indeed floor (rises 0.75-1.81 dB/dB where a
                            //         cubic needs 3.0, with H4 >= H3 in 1-3 of every 4 cells), but
                            //         **P2-bright and P3 are NOT**: P2-bright's H3 rises 2.65-2.74
                            //         dB/dB absolute (1.97 dB/dB in dBc, against the 2.0 a cubic
                            //         requires) over 25 dB of level with 0-1 inversions, reaching
                            //         -36.7 dBc; P3 reaches -41.2 dBc. That is real third-harmonic
                            //         content, and this model produces essentially NONE (-115 to
                            //         -150 dBc -- what a pure quadratic makes via the feedback loop).
                            //         ⛔ Still not fittable: both units' reamp levels are unknown, so
                            //         the same aEven-vs-level degeneracy applies to beta. Note also
                            //         P2-bright's top cell has H3 ABOVE H2, which a square-law device
                            //         cannot do -- so that cell is a harder nonlinearity than this
                            //         shaper has, not a bigger cubic. Needs a calibrated capture on a
                            //         unit whose H3 clears its floor; P1 is calibrated but its H3
                            //         does not clear, and P2/P3's H3 clears but they are not.

    // Per-side limits of the odd core, giving the stage its asymmetry. These are the DEVICE's own
    // bounds, in the current domain: cutoff (Id -> 0) on the negative swing, the channel ceiling
    // (Id -> IDSS) on the positive. There is deliberately NO separate drain-voltage rail clamp --
    // section 3 is explicit that a JFET drain limits by its own physics and that bolting an
    // op-amp-style clamp on top double-limits it.
    //
    // ⚠ But be honest about what that leaves out, because the fitted bias makes it quantifiable for
    // the first time: the LOAD LINE bites long before the channel ceiling does. With Vds = 13.1 V
    // and a ~20.6 k AC load the drain enters triode after 575 uA of extra current, i.e. at
    // g = +0.370 V -- while the channel ceiling sits at g = +3.00 V, 8x further out. The shaper's
    // structure cannot carry the tighter bound (the even bump alone asymptotes at Vov/2 = 0.223,
    // which would leave the core only 0.147 V and bend the map inside the normal operating range),
    // so the load line is NOT modelled. It is reachable: +0.370 V of g needs w = +0.281 V, which is
    // a 1.85 V gate swing, about +6.6 dB of input trim. Recorded as a known deferred limit, with the
    // numbers, rather than left as an unexamined "extreme settings only".
    double limitPos = 1.8482823; // V -- 1.5*L + a*s^2/2 = 3.00 = (IDSS - Id0)/gm, the channel ceiling.
    double limitNeg = 0.34264635; // V -- see the monotonicity note below.

    // !! MONOTONICITY: limitNeg is NOT the physically exact value, and the difference is deliberate.
    // Asymptoting exactly at cutoff would need limitNeg = (2/3)*Vov = 0.2980, but on the negative
    // swing the even bump's slope SUBTRACTS from the core's, and the sum folds back -- a real fold,
    // found only by scanning the combined function, exactly as finding (c) warns (a bound derived
    // for one sub-term is not a bound on the sum). The fold threshold measures 1.04x the exact
    // value; this ships 1.15x for margin, giving an asymptote 1.30x deeper than cutoff. Fold-back
    // inverts the waveform and breaks ADAA, so monotonicity wins. The reachable region matters here:
    // the fold sits near w = -0.55 V, which is +12 dB of input trim, not a theoretical corner.
    // JfetStageTest scans the shipped triple, so a later fit cannot silently reintroduce it.
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

    /** First-order antiderivative anti-aliasing on the shaper (build step 6, dsp.md / nonlinear doc
     *  section 5.1). A SETTABLE knob rather than a hardcoded `if (osFactor <= N)` inside this class,
     *  deliberately: dsp.md warns that a hardcoded gate makes the gate's own validation measure the
     *  gate instead of the mechanism. The processor owns the OS-factor policy; OSFidelity and
     *  FeatureProfile measure both states. */
    void setAdaa(bool shouldUseAdaa) noexcept { adaaEnabled = shouldUseAdaa; }
    bool adaaIsEnabled() const noexcept { return adaaEnabled; }

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
        excessShelf.reset();
        wPrev = 0.0;
        fPrev = 0.0; // shapeAntiderivative(0) == 0 by construction of both terms' constants
    }

    /** Gate volts -> drain Norton current, in amps, signed as the current INJECTED INTO node D.
     *  Negative small-signal gain: a rising gate pulls more drain current and drags the drain DOWN,
     *  so this stage INVERTS, as the real single-stage pedal does. Keep it -- polarity is audible in
     *  a null test against the reference renders even though it is inaudible solo. */
    inline double processSample(double vGate) noexcept
    {
        const double w = driveShelf.process(vGate);

        // The nonlinear EXCESS -- what the device adds beyond its own small-signal slope -- is the
        // only part the loop suppresses a second time, so it is the only part that goes through the
        // second shelf. Splitting it out this way also keeps the linear path bit-exact: the model's
        // frequency response is the shelf and nothing else, at any drive and with ADAA on or off.
        const double excess = adaaEnabled ? excessAdaa(w) : (shape(w) - w);
        return -params.gm * (w + excessShelf.process(excess));
    }

    /** First-order ADAA of shape(): the mean of the map over [wPrev, w], evaluated exactly from the
     *  closed-form antiderivative. The argument is the SHELF OUTPUT, not the stage input, which is
     *  exactly the case section 5.1 sanctions -- the map is memoryless in w, and the memory upstream
     *  of it is a linear filter, so w is smooth between samples.
     *
     *  !! ADAA1 is a two-point average, so on a LINEAR map it is exactly the FIR (1 + z^-1)/2: a
     *  cos(pi*f/fs) magnitude and half a sample of delay. That is intrinsic, not a defect, and
     *  JfetStageTest section 6c asserts it. It is also why this is switched off at every shipped
     *  factor -- see PedalAudioProcessor::kAdaaMaxOsIndex for the measurements.
     *
     *  The fallback below is a numerical necessity, not a nicety: as the step shrinks, the
     *  difference of two nearly-equal antiderivatives loses its significant digits, so the quotient
     *  goes to noise precisely where the signal is quiet. The midpoint value is the exact limit of
     *  that quotient, so the crossover is smooth. */
    /** ADAA of the EXCESS map g(w) - w, which is what processSample() actually needs.
     *
     *  ⭐ ADAA1 is linear in the map, so ADAA[g - id] = ADAA[g] - ADAA[id], and ADAA of the identity
     *  is exactly the two-point average. Subtracting it therefore costs nothing and removes ADAA's
     *  one real drawback outright: the (1 + z^-1)/2 rolloff no longer touches the linear path, so
     *  ADAA can no longer darken the top octave at low oversampling. That was the entire reason
     *  kAdaaMaxOsIndex is -1 -- re-run OSFidelity and FeatureProfile before assuming it still holds. */
    inline double excessAdaa(double w) noexcept
    {
        const double wPrevBefore = wPrev; // shapeAdaa() advances it, so capture it first
        return shapeAdaa(w) - 0.5 * (w + wPrevBefore);
    }

    inline double shapeAdaa(double w) noexcept
    {
        const double f = shapeAntiderivative(w);
        const double dw = w - wPrev;
        const double y = (std::abs(dw) > kAdaaEps) ? (f - fPrev) / dw : shape(0.5 * (w + wPrev));

        // State advances EVERY sample, including through the fallback, so switching ADAA on or off
        // mid-stream resumes from the real previous sample rather than a stale one.
        wPrev = w;
        fPrev = f;
        return y;
    }

    /** Drain Norton impedance, R6 || ro. Stamped into OutputNetwork, never applied here.
     *  ro*k(s) is frequency dependent in principle, but ro (1.44 MOhm at the fitted Id) is 65x R6, so
     *  the parallel combination moves by under 1% (0.06 dB) across the whole k range -- modelled as
     *  constant. That constancy is also why gm is taken straight from K0 - 1: see JfetParams. */
    double outputImpedance() const
    {
        return (circuit::kR6 * params.ro) / (circuit::kR6 + params.ro);
    }

    /** g(w): odd expansive-bounded core + exactly-even square-law bump. g(0) = 0, g'(0) = 1 exactly,
     *  g''(0) = aEven. Both terms have closed-form antiderivatives (see §2 findings a/b), which is
     *  what keeps first-order ADAA available for build step 6 -- do NOT replace this with a composed
     *  pre-warp-and-sigmoid, which has no elementary antiderivative. */
    inline double shape(double w) const noexcept
    {
        const double L = (w >= 0.0) ? params.limitPos : params.limitNeg;
        const double lSq = L * L;
        const double c = params.beta + 1.5 / lSq;
        const double r = 1.0 + (w * w) / lSq;
        const double core = w * (1.0 + c * w * w) / (r * std::sqrt(r));

        const double s = params.bumpScale;
        const double t = std::tanh(w / s);
        return core + 0.5 * params.aEven * s * s * t * t;
    }

    /** F(w) with F(0) = 0 and F'(w) = shape(w) exactly. Both of g()'s terms were chosen for having
     *  elementary primitives, and this is what that was for -- section 5.1 is explicit that
     *  substituting quadrature for a missing antiderivative is measured-wrong, not just inelegant.
     *
     *  Core, with a = 1/L^2, c = beta + 1.5/L^2, r = 1 + a*w^2 and k = c/a:
     *      int w(1 + c w^2) (1 + a w^2)^{-3/2} dw = (1/a)[ k*sqrt(r) + (k-1)/sqrt(r) ]
     *  Even bump, using int tanh^2(x) dx = x - tanh(x):
     *      int (a_even s^2/2) tanh^2(w/s) dw = (a_even s^2/2)(w - s*tanh(w/s))
     *
     *  L is sign-dependent, so F is piecewise -- but each piece is anchored to F(0) = 0, which makes
     *  F CONTINUOUS at the origin. That matters: a step spanning a zero crossing evaluates one branch
     *  at each end, and their difference is the true integral only because the two pieces meet. */
    inline double shapeAntiderivative(double w) const noexcept
    {
        const double L = (w >= 0.0) ? params.limitPos : params.limitNeg;
        const double lSq = L * L;
        const double c = params.beta + 1.5 / lSq;
        const double k = c * lSq; // c/a, with a = 1/L^2
        const double r = 1.0 + (w * w) / lSq;
        const double sqrtR = std::sqrt(r);
        const double coreF = lSq * (k * sqrtR + (k - 1.0) / sqrtR - (2.0 * k - 1.0));

        const double s = params.bumpScale;
        const double bumpF = 0.5 * params.aEven * s * s * (w - s * std::tanh(w / s));
        return coreF + bumpF;
    }

    /** DC degeneration factor K0 = 1 + gm*R5. This IS the mode plateau ratio M2 measures, and the one
     *  number in the model that a ratio of two captures hands over with no level calibration. */
    double degenerationDC() const { return 1.0 + params.gm * circuit::kR5; }

private:
    /** One instance of 1/k(s). There are two, and they must NOT share state: the drive path and the
     *  excess path carry different signals through identical coefficients. */
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

        // Same filter, separate state. Copying the coefficients rather than sharing one object is
        // what keeps the two paths independent.
        excessShelf.b0 = driveShelf.b0;
        excessShelf.b1 = driveShelf.b1;
        excessShelf.a1 = driveShelf.a1;
    }

    JfetParams params {};
    Mode mode = Mode::Dark;
    double fs = 48000.0;

    // Below this step the antiderivative difference is dominated by cancellation error; see
    // shapeAdaa(). w is in real gate volts (order 1), so this is a ~1e-6 relative threshold.
    static constexpr double kAdaaEps = 1.0e-6;

    Shelf driveShelf {};
    Shelf excessShelf {};
    bool adaaEnabled = false;
    double wPrev = 0.0, fPrev = 0.0;
};
} // namespace pedal::dsp
