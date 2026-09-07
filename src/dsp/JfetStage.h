#pragma once

#include <cmath>

#include "CircuitValues.h"

namespace pedal::dsp
{
/**
 * Stage 2 -- the 2N5457 common-source gain stage, the ONLY nonlinear element in the signal path.
 * Implements Path B of docs/nonlinear-component-modeling.md §2:
 *
 *   Vg --[ 1/k(s) ]--[ static shaper g() ]--*(-gm)--> drain Norton current
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
 *     1/k(s) = (1 + s*R5*C) / (K0 + s*R5*C),   K0 = 1 + gm*R5
 *
 * zero at the bypass corner 1/(2*pi*R5*C), pole K0x above it; DARK (no cap engaged) is the constant
 * 1/K0. The un-engaged branch's 1 MOhm pulldown is treated as fully off: including it would shift the
 * HF degeneration from R5 to R5||500k, i.e. 0.7% / 0.06 dB (circuit.md sanctions this).
 *
 * ================================ APPROXIMATION, DECLARED ================================
 * This is a Wiener-Hammerstein approximation. True degeneration is nonlinear feedback
 * (vgs = vg - i_d*Zs, an implicit solve); linearising the degeneration into 1/k(s) and putting all
 * the curvature on vgs is a deliberate modelling CHOICE, not an oversight. Path A (a full coupled
 * solve at an R-type root) is the escalation if an A/B shows audible missed dynamics.
 */
struct JfetParams
{
    // ================== EVERY FIELD BELOW IS A PLACEHOLDER AWAITING A FIT ==================
    // The 2N5457 is spread 5:1 on every amplitude parameter (IDSS 1-5 mA, Vgs(off) -0.5..-6 V,
    // Yfs 1000-5000 umhos), and the maker states Q1 is a "cherry picked" vintage part, so nominal
    // is even less trustworthy than usual. Only the R/C corners and the polarity are known ahead of
    // the reference renders. See docs/build-plan.md M2 (gm) and M5 (shaper).
    //
    // Derived from a datasheet-TYPICAL self-bias solve (IDSS = 3 mA, Vp = -3.0 V, R5 = 3.6 k):
    //     Id = IDSS*(1 - Vgs/Vp)^2 with Vgs = -Id*R5  =>  Id = 495 uA, Vgs = -1.78 V
    //     Vov = |Vgs - Vp| = 1.22 V,  gm = 2*Id/Vov = 813 uS,  V_drain = VA - Id*R6 = 11.1 V
    // The drain landing at mid-rail cross-checks circuit.md's independent "~11 V" bias estimate.
    //
    // !! KNOWN TENSION, do not mistake for a converged fit: the maker's published control points
    // imply the whole stage is only +7-8 dB, which needs gm ~ 180 uS -- about 4.5x BELOW this
    // nominal. circuit.md says to expect the fitted gm under nominal. M2 settles it from the mode
    // plateau ratio k = 1 + gm*R5, which needs no level calibration at all.

    double gm = 813.0e-6;   // S      -- small-signal transconductance. M2 measures this directly.
    double ro = 1.01e6;     // Ohm    -- drain output resistance, 1/(lambda*Id) at lambda = 2 mS/V.

    // Shaper. g(w) = T(w) + (a*s^2/2)*tanh^2(w/s), w = effective vgs in REAL GATE VOLTS, so g'(0) = 1
    // exactly and gm alone sets the gain -- the shaper only adds curvature.
    //
    // A JFET is a square-law device, so the curvature must be EVEN-dominant (H2 above H3). A tanh
    // structurally cannot produce that (finding (a)): tanh is odd, so its cubic forces H3 whenever it
    // makes H2. Hence the linear-core-plus-even-bump form, whose bump is exactly even and contributes
    // zero odd content at any drive.
    double aEven = 0.821;   // 1/V    -- quadratic coefficient = 1/Vov. THE square-law parameter.
    double bumpScale = 1.218; // V    -- even-bump scale, set to Vov. Then a*s = 1 and the bump
                              //         saturates at a*s^2/2 = Vov/2 = Id0/gm, i.e. exactly the
                              //         cutoff current -- the physical scale, not a tuned one.
    double beta = 0.0;      // 1/V^2  -- cubic coefficient of the odd core. 0 = pure square law.
                            //         !! CHECK THE SIGN FROM CAPTURES BEFORE CHOOSING A LIMITER
                            //         (finding (b)): a compressive core's H3 is ~180 deg out of
                            //         phase with an expansive one, and no amount of knee tuning
                            //         crosses that. beta > 0 expansive, < 0 compressive. M5 decides.

    // Per-side limits of the odd core, giving the stage its asymmetry. These are the DEVICE's own
    // bounds, in the current domain: cutoff (Id -> 0) on the negative swing, gate conduction /
    // IDSS on the positive. There is deliberately NO separate drain-voltage rail clamp -- §3 is
    // explicit that a JFET drain limits by its own physics and that bolting an op-amp-style clamp
    // on top double-limits it. Bounding the current here bounds the drain voltage via the load line
    // (V_D = VA - Id*R6) for free, which is what circuit.md's VA = 22 V headroom note is really about.
    // At guitar levels neither side is reached; this only shapes extreme input-trim settings.
    double limitPos = 1.65; // V -- 1.5*L + a*s^2/2 = 3.08 ~= (IDSS - Id0)/gm, the channel ceiling.
    double limitNeg = 1.20; // V -- see the monotonicity note below.

    // !! MONOTONICITY: limitNeg is NOT the physically exact value, and the difference is deliberate.
    // Asymptoting exactly at cutoff would need limitNeg = 0.812, but on the negative swing the even
    // bump's slope SUBTRACTS from the core's, and that triple folds back at w ~ -1.5 V -- a real
    // fold, found only by scanning the combined function, exactly as finding (c) warns (a bound
    // derived for one sub-term is not a bound on the sum). Fold-back inverts the waveform and breaks
    // ADAA, so monotonicity wins; the cost is an asymptote about 2x deeper than cutoff in a region
    // the signal only reaches under heavy input trim. JfetStageTest scans the shipped triple, so a
    // later fit cannot silently reintroduce the fold.
};

/** MODE positions, ordered by physical lever position top-to-bottom to match the APVTS choice list
 *  and the hardware toggle (circuit.md note #2). Deliberately NOT ordered by brightness.
 *  !! Which lug the lever's UP position closes is still inferred, not traced -- build-plan.md M1
 *  resolves it from the mode-differential corner frequencies, and may swap Bright with Mid. */
enum class Mode
{
    Bright = 0, // C2 10 nF engaged -> bypass corner ~4.42 kHz
    Dark,       // neither engaged  -> R5 fully degenerating, lowest gain, flat
    Mid         // C1 22 nF engaged -> bypass corner ~2.01 kHz
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

    /** Call at the rate this stage actually runs at -- the OVERSAMPLED rate. The shelf's pole sits at
     *  K0 x the bypass corner (~18 kHz in Bright at nominal gm), close enough to Nyquist at 48 kHz
     *  for the bilinear warp to badly misplace it, so this stage belongs inside the oversampled
     *  region and its caps must NOT also be prewarped (dsp.md). */
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        updateShelf();
        reset();
    }

    void reset()
    {
        x1 = 0.0;
        y1 = 0.0;
    }

    /** Gate volts -> drain Norton current, in amps, signed as the current INJECTED INTO node D.
     *  Negative small-signal gain: a rising gate pulls more drain current and drags the drain DOWN,
     *  so this stage INVERTS, as the real single-stage pedal does. Keep it -- polarity is audible in
     *  a null test against the reference renders even though it is inaudible solo. */
    inline double processSample(double vGate) noexcept
    {
        const double w = b0 * vGate + b1 * x1 - a1 * y1;
        x1 = vGate;
        y1 = w;
        return -params.gm * shape(w);
    }

    /** Drain Norton impedance, R6 || ro. Stamped into OutputNetwork, never applied here.
     *  ro*k(s) is frequency dependent in principle, but ro (~1 MOhm) is ~46x R6, so the parallel
     *  combination moves by under 1% (0.1 dB) across the whole k range -- modelled as constant. */
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

    /** DC degeneration factor K0 = 1 + gm*R5. This IS the mode plateau ratio M2 measures, and the one
     *  number in the model that a ratio of two captures hands over with no level calibration. */
    double degenerationDC() const { return 1.0 + params.gm * circuit::kR5; }

private:
    // Bypass capacitance engaged by the current MODE position; 0 means no branch grounded (DARK).
    double bypassCap() const
    {
        switch (mode)
        {
            case Mode::Bright: return circuit::kC2;
            case Mode::Mid:    return circuit::kC1;
            case Mode::Dark:   break;
        }
        return 0.0;
    }

    // Bilinear discretisation of 1/k(s) = (1 + s*tau) / (K0 + s*tau), tau = R5*C.
    void updateShelf()
    {
        const double k0 = degenerationDC();
        const double cap = bypassCap();

        if (cap <= 0.0)
        {
            // DARK: k is frequency-independent, so this degenerates to a plain gain. Taking the
            // tau -> 0 limit of the bilinear form instead would give a1 = 1, a marginally stable
            // filter rather than a constant.
            b0 = 1.0 / k0;
            b1 = 0.0;
            a1 = 0.0;
            return;
        }

        const double twoTauFs = 2.0 * circuit::kR5 * cap * fs;
        const double norm = 1.0 / (k0 + twoTauFs);
        b0 = (1.0 + twoTauFs) * norm;
        b1 = (1.0 - twoTauFs) * norm;
        a1 = (k0 - twoTauFs) * norm;
    }

    JfetParams params {};
    Mode mode = Mode::Dark;
    double fs = 48000.0;

    double b0 = 1.0, b1 = 0.0, a1 = 0.0;
    double x1 = 0.0, y1 = 0.0;
};
} // namespace pedal::dsp
