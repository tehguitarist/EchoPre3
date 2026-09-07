// Stage 2 validation (CLAUDE.md build sequence step 4: "Nonlinear stage: sine-clipping behaviour;
// confirm output polarity with a DC-step test"). Pure chowdsp_wdf console exe, no JUCE.
//
// This tests the STRUCTURE, plus the two things phase 1 measured. gm and the two shelf time
// constants are now MEASURED (build-plan.md M1/M2); the shaper's amplitude parameters are still
// derived rather than measured, so nothing here asserts a distortion figure in dBc against a
// capture. What it does assert are the properties that must survive any later fit:
//
//   * 1/k(s) is a shelf with its zero at the MEASURED bypass corner and a plateau ratio of exactly
//     1 + gm*R5 -- the quantity M2 extracts from a capture ratio with no level calibration.
//   * the stage inverts (single common-source stage, no second inversion downstream).
//   * the shaper is even-dominant, as a square-law device must be and a tanh structurally cannot be.
//   * the shaper is MONOTONE over the full reachable range -- the trap that the shipped parameter
//     triple was chosen to avoid, and which a later refit could silently reintroduce.
//   * ⭐ the degeneration suppresses the DISTORTION as k^2, not k, checked against an exact implicit
//     solve of the real feedback equation. Section 7. Every linear test in this file passes with
//     that factor of K0 (16.4 dB) missing, which is how it survived the first build.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

#include "dsp/JfetStage.h"

#include "MeasureUtils.h"

namespace
{
// The stage runs inside the oversampled region, so it is validated at the shipped 4x default. With
// gm measured, its shelf pole sits at K0 x the zero, i.e. 12.3 kHz (Bright) and 27.4 kHz (Mid) --
// the second is ABOVE Nyquist at 48 kHz, so the bilinear warp would badly misplace it at base rate.
// That is exactly why this stage lives inside the oversampled region.
constexpr double kFs = 192000.0;

bool ok = true;

void fail(const char* msg)
{
    std::printf("  <-- FAIL: %s\n", msg);
    ok = false;
}

// Small-signal response of the 1/k(s) shelf, recovered by dividing out gm. At this amplitude the
// shaper's quadratic term is ~1e-7 relative, so this measures the linear prefilter alone.
std::complex<double> shelfResponseAt(pedal::dsp::JfetStage& stage, double freq)
{
    constexpr double kProbe = 1.0e-6;
    stage.reset();
    // = -gm * (1/k(f)). The stage inverts, so the measured response carries a 180 degree offset that
    // is removed here -- the inversion is asserted separately by the DC-step test, and folding it
    // into the shelf comparison would only hide it.
    return -pedal::test::measureResponse([&stage](double x) { return stage.processSample(x); }, freq, kFs, kProbe);
}

double shelfGainAt(pedal::dsp::JfetStage& stage, double freq) { return std::abs(shelfResponseAt(stage, freq)); }

double db(double x) { return 20.0 * std::log10(x); }

// 1/k(s) = (1 + s*tau) / (K0 + s*tau), the shelf the source-bypass branch produces. tau is MEASURED
// (JfetParams), not R5*C: the fitted zeros sit ~7% below the drawn corner in both units and both
// branches, and this dataset cannot say whether R5 or the caps carry it.
std::complex<double> analyticShelf(double freq, double tau, double k0)
{
    const std::complex<double> sTau { 0.0, 2.0 * M_PI * freq * tau };
    return (1.0 + sTau) / (k0 + sTau);
}
} // namespace

int main()
{
    using namespace pedal;
    const dsp::JfetParams params {};
    dsp::JfetStage stage;
    stage.setParams(params);
    stage.prepare(kFs);

    const double k0 = stage.degenerationDC();
    std::printf("Parameters: gm and tau MEASURED (M1/M2); the shaper is derived, not measured:\n");
    std::printf("  gm = %.1f uS, ro = %.2f MOhm, Zout = R6||ro = %.2f k\n", params.gm * 1.0e6,
                params.ro / 1.0e6, stage.outputImpedance() / 1.0e3);
    std::printf("  K0 = 1 + gm*R5 = %.4f  (%.2f dB of mode lift -- THIS is what M2 measured: 6.5912)\n",
                k0, db(k0));
    std::printf("  tau: bright %.2f us (zero %.0f Hz), mid %.2f us (zero %.0f Hz)\n",
                params.tauBright * 1.0e6, 1.0 / (2.0 * M_PI * params.tauBright),
                params.tauMid * 1.0e6, 1.0 / (2.0 * M_PI * params.tauMid));
    std::printf("  Vov = %.4f V (= 1/aEven), so the square law's own scale is %.3f V of gate swing\n",
                params.bumpScale, params.bumpScale);

    // 1. The shelf, per mode. Normalise by gm so the numbers are the 1/k(s) response itself.
    //    DC must sit at 1/K0 and HF must reach unity: the degeneration is fully bypassed above the
    //    corner, so BOTH cap positions reach the SAME plateau. Bright vs Mid differ only in WHERE the
    //    lift starts -- modelling Bright as a louder Mid is a different and wrong shape.
    std::printf("\n1/k(s) shelf per MODE (normalised by gm):\n");
    struct ModeCase { const char* name; pedal::dsp::Mode mode; double tau; };
    const ModeCase cases[] = { { "Bright", dsp::Mode::Bright, params.tauBright },
                               { "Dark",   dsp::Mode::Dark,   0.0 },
                               { "Mid",    dsp::Mode::Mid,    params.tauMid } };

    for (const auto& c : cases)
    {
        stage.setMode(c.mode);
        const double atDc = shelfGainAt(stage, 1.0) / params.gm;

        std::printf("  %-7s DC %.4f (want %.4f)\n", c.name, atDc, 1.0 / k0);
        if (std::abs(atDc - 1.0 / k0) > 0.01 * (1.0 / k0))
            fail("DC gain is not 1/K0");

        if (c.tau > 0.0)
        {
            // Against the analytic shelf, evaluated at the BILINEAR-WARPED frequency. That is the
            // exact discrete-time correspondence, so agreement here isolates "are the coefficients
            // right" from "does bilinear warp exist" -- the latter is reported separately below.
            // Comparing to the unwarped analytic value instead would flag the warp as a coefficient
            // bug, which is what an earlier version of this test wrongly did at 40 kHz.
            //
            // PHASE MATTERS HERE MORE THAN ANYWHERE. 1/k(s) is a shelf whose zero sits below its
            // pole, so it contributes real phase LEAD through the transition. That lead is the
            // audible signature of the MODE switch as much as the magnitude lift is, and it is the
            // part a "just apply an HF shelf" shortcut gets wrong while still matching magnitude.
            for (const double f : { 500.0, 2000.0, 8000.0, 20000.0 })
            {
                const double fEff = pedal::test::warpedFrequency(f, kFs);
                const auto got = shelfResponseAt(stage, f) / params.gm;
                const auto want = analyticShelf(fEff, c.tau, k0);
                const double errDb = std::abs(db(std::abs(got) / std::abs(want)));
                const double phErrDeg = pedal::test::phaseErrorDeg(std::arg(got), std::arg(want));
                const bool bad = errDb > 0.02 || std::abs(phErrDeg) > 0.5;
                std::printf("          %5.0f Hz: %.5f (want %.5f, %.4f dB)  phase %+7.3f (want %+7.3f)%s\n",
                            f, std::abs(got), std::abs(want), errDb, pedal::test::degrees(std::arg(got)),
                            pedal::test::degrees(std::arg(want)), bad ? "   <-- FAIL" : "");
                if (bad)
                    fail("shelf does not match the analytic 1/k(s) in magnitude and phase");
            }

            // The shelf's zero must sit at the MEASURED bypass corner 1/(2*pi*tau).
            const double wantHz = 1.0 / (2.0 * M_PI * c.tau);
            const double target = std::sqrt((1.0 / k0) * 1.0); // geometric mid-shelf
            double lo = 10.0, hi = 40000.0;
            for (int i = 0; i < 50; ++i)
            {
                const double mid = 0.5 * (lo + hi);
                (shelfGainAt(stage, mid) / params.gm < target ? lo : hi) = mid;
            }
            const double midShelfHz = 0.5 * (lo + hi);
            // For (1+s*tau)/(K0+s*tau) the geometric mid-shelf sits at sqrt(K0) x the zero.
            const double expectMidHz = wantHz * std::sqrt(k0);
            const double errPct = std::abs(midShelfHz - expectMidHz) / expectMidHz * 100.0;
            std::printf("          bypass corner %.0f Hz -> mid-shelf %.0f Hz (expected %.0f Hz, %.2f%%)\n",
                        wantHz, midShelfHz, expectMidHz, errPct);
            if (errPct > 2.0)
                fail("shelf is not positioned at the measured bypass corner");
        }
    }

    // 1b. Both cap positions must converge on the SAME high-frequency plateau -- fully bypassed, the
    //     degeneration is gone either way. BRIGHT vs MID is only about WHERE the lift starts, so
    //     modelling Bright as a higher-gain Mid would be a different and wrong shape. Checked
    //     analytically, because the plateau is approached asymptotically and 40 kHz is not there yet
    //     (MID, the 10 nF branch, has the higher zero and so is the one still short of it there).
    std::printf("\nBoth cap positions converge on one plateau (BRIGHT is not a louder MID):\n");
    for (const double f : { 40.0e3, 200.0e3, 1.0e6 })
        std::printf("  %7.0f kHz: Bright %.5f, Mid %.5f\n", f / 1000.0,
                    std::abs(analyticShelf(f, params.tauBright, k0)),
                    std::abs(analyticShelf(f, params.tauMid, k0)));
    const double plateauRatio = std::abs(analyticShelf(1.0e6, params.tauBright, k0))
                              / std::abs(analyticShelf(1.0e6, params.tauMid, k0));
    if (std::abs(db(plateauRatio)) > 0.01)
        fail("Bright and Mid do not share a plateau");

    // 2. Polarity. A rising gate pulls more drain current and drags the drain DOWN, so the injected
    //    Norton current must be NEGATIVE for a positive gate step. The pedal genuinely inverts.
    stage.setMode(dsp::Mode::Dark);
    stage.reset();
    double stepOut = 0.0;
    for (int n = 0; n < 64; ++n)
        stepOut = stage.processSample(0.1);
    std::printf("\nDC-step polarity: +0.1 V gate -> %+.4e A injected at the drain\n", stepOut);
    if (stepOut >= 0.0)
        fail("stage does not invert -- a common-source stage must");

    // 3. Even dominance. A JFET is square-law, so H2 must sit well above H3. A tanh core
    //    structurally cannot do this (its cubic forces H3 whenever it makes H2), which is why the
    //    shaper is a linear core plus an exactly-even bump.
    //    The probe amplitudes are FRACTIONS OF Vov, not absolute volts: the shaper is self-similar
    //    under scaling by Vov, so fixed volts would mean something different after every refit. At a
    //    0 dBFS input the stage actually sees w ~ 0.26*Vov, so 0.75*Vov is already heavy trim.
    std::printf("\nHarmonic structure of the shaper (square-law signature), probed in units of Vov:\n");
    for (const double frac : { 0.25, 0.50, 0.75 })
    {
        const double amp = frac * params.bumpScale;
        constexpr int kN = 4096;
        double h[4] = { 0.0, 0.0, 0.0, 0.0 };
        for (int n = 0; n < kN; ++n)
        {
            const double th = 2.0 * M_PI * (double) n / kN;
            const double y = stage.shape(amp * std::sin(th));
            for (int k = 1; k <= 3; ++k)
                h[k] += y * std::sin((double) k * th);
        }
        for (int k = 1; k <= 3; ++k)
            h[k] *= 2.0 / kN;

        // H2 is a cosine component for a sine input (the even part is in quadrature), so recover it
        // with the matching cosine correlation instead.
        double h2 = 0.0;
        for (int n = 0; n < kN; ++n)
        {
            const double th = 2.0 * M_PI * (double) n / kN;
            h2 += stage.shape(amp * std::sin(th)) * std::cos(2.0 * th);
        }
        h2 = std::abs(h2 * 2.0 / kN);

        const double h2db = db(h2 / std::abs(h[1]));
        const double h3db = db(std::abs(h[3]) / std::abs(h[1]));
        std::printf("  A = %.2f*Vov = %.3f V:  H2 %7.2f dBc,  H3 %7.2f dBc,  separation %5.2f dB\n",
                    frac, amp, h2db, h3db, h2db - h3db);
        if (h2db <= h3db + 15.0)
            fail("shaper is not clearly even-dominant -- a JFET must be");
    }

    // 4. Monotonicity, scanned on the REAL combined function over the full reachable range. A bound
    //    derived for the even bump alone is only an upper bound on the admissible region, never the
    //    region: the core's own curvature subtracts from the bump's slope and folds the sum back
    //    earlier than the sub-term algebra predicts. The shipped limitNeg was chosen from this scan.
    std::printf("\nMonotonicity scan of the shipped parameter triple:\n");
    // +-13*Vov covers everything the stage can reach at +12 dB of input trim with room to spare.
    const double kScanV = 13.0 * params.bumpScale;
    constexpr int kSteps = 240000;
    double worstSlope = 1.0e9, worstAt = 0.0, prev = stage.shape(-kScanV);
    for (int i = 1; i <= kSteps; ++i)
    {
        const double w = -kScanV + 2.0 * kScanV * (double) i / kSteps;
        const double cur = stage.shape(w);
        const double slope = (cur - prev) / (2.0 * kScanV / kSteps);
        if (slope < worstSlope) { worstSlope = slope; worstAt = w; }
        prev = cur;
    }
    std::printf("  minimum slope over w in [%+.1f, %+.1f] V: %.5f at w = %+.3f V\n", -kScanV, kScanV,
                worstSlope, worstAt);
    if (worstSlope <= 0.0)
        fail("shaper folds back -- non-monotone, which inverts the waveform and breaks ADAA");

    // 5. gm alone must set the small-signal gain: g'(0) = 1 exactly, so any linear oracle or FR test
    //    is untouched by the shaper. If this drifts, every fitted gm silently changes meaning.
    constexpr double kEps = 1.0e-7;
    const double slope0 = (stage.shape(kEps) - stage.shape(-kEps)) / (2.0 * kEps);
    std::printf("  g'(0) = %.9f (must be 1 so gm alone sets the gain)\n", slope0);
    if (std::abs(slope0 - 1.0) > 1.0e-6)
        fail("g'(0) != 1 -- the shaper is contributing gain, so gm no longer means transconductance");

    // 6. ADAA (build step 6). The whole method rests on shapeAntiderivative() being the exact
    //    primitive of shape(); if it is not, ADAA silently becomes a wrong waveshaper rather than a
    //    less aliased one, and nothing else in the suite would notice. So it is tested against an
    //    independent numerical integral, not against itself.
    std::printf("\nADAA: antiderivative is the exact primitive of the shaper\n");
    if (std::abs(stage.shapeAntiderivative(0.0)) > 1.0e-15)
        fail("F(0) != 0 -- the two branches no longer meet at the origin");

    {
        double worstErr = 0.0, worstAt = 0.0;
        constexpr double kH = 1.0e-6;
        for (int i = -60; i <= 60; ++i)
        {
            const double w = 0.05 * (double) i; // spans +-3 V, straight through the sign branch
            const double numeric = (stage.shapeAntiderivative(w + kH) - stage.shapeAntiderivative(w - kH)) / (2.0 * kH);
            const double err = std::abs(numeric - stage.shape(w));
            if (err > worstErr) { worstErr = err; worstAt = w; }
        }
        std::printf("  worst |dF/dw - g(w)| over w in [-3, +3] V: %.3e at w = %+.2f V\n", worstErr, worstAt);
        if (worstErr > 1.0e-6)
            fail("shapeAntiderivative is not the primitive of shape -- ADAA would reshape, not antialias");
    }

    // 6b. The ADAA output must equal the true mean of the shaper over the sample's step. Simpson on
    //     a fine grid is the independent oracle. A step spanning the origin is included on purpose:
    //     that is the one place the piecewise definition could come apart.
    {
        struct StepCase { double from, to; };
        const StepCase steps[] = { { 0.10, 0.90 }, { -0.80, 0.70 }, { -1.50, -0.20 }, { 0.02, 0.021 } };
        double worstErr = 0.0;
        for (const auto& st : steps)
        {
            constexpr int kN = 20000; // even, for Simpson
            const double h = (st.to - st.from) / kN;
            double integral = stage.shape(st.from) + stage.shape(st.to);
            for (int i = 1; i < kN; ++i)
                integral += (i % 2 ? 4.0 : 2.0) * stage.shape(st.from + h * (double) i);
            integral *= h / 3.0;
            const double want = integral / (st.to - st.from);

            stage.reset();
            stage.shapeAdaa(st.from); // load the ADAA state with the previous sample
            const double got = stage.shapeAdaa(st.to);
            const double err = std::abs(got - want);
            worstErr = std::max(worstErr, err);
            std::printf("  step %+.3f -> %+.3f V: ADAA %.9f, true mean %.9f, err %.2e\n",
                        st.from, st.to, got, want, err);
        }
        if (worstErr > 1.0e-7)
            fail("ADAA output is not the true mean of the shaper over the step");
    }

    // 6c. What ADAA costs in the linear regime: NOTHING, and that is a change, not a given.
    //
    //     ⭐ ADAA1 is a two-point average, so applied to a whole map it is exactly the FIR
    //     (1 + z^-1)/2 on the linear part: cos(pi*f/fs) of magnitude and half a sample of delay.
    //     That rolloff is why kAdaaMaxOsIndex is -1 -- at 1x it tripled the top-octave droop to buy
    //     aliasing that was already 63 dB down.
    //
    //     Since the stage now feeds only the nonlinear EXCESS through ADAA (the loop suppresses only
    //     what it generates, so only the excess belongs in the second shelf), and ADAA1 is linear in
    //     the map, ADAA[g - id] = ADAA[g] - ADAA[id] removes that rolloff exactly. The linear path is
    //     untouched at every frequency, which is what this now asserts.
    //
    //     This test was previously written the other way round -- it asserted the deviation WAS the
    //     two-point average. Both versions are correct statements about their own structure, and
    //     that is the point: the assertion had to be re-derived from the new structure rather than
    //     kept and re-tuned. A kept-and-retuned threshold here would have hidden the improvement.
    {
        stage.setMode(dsp::Mode::Dark);
        double worstMagErr = 0.0, worstDelayErr = 0.0;
        for (const double f : { 1000.0, 6000.0, 20000.0 })
        {
            stage.setAdaa(false);
            const auto plain = shelfResponseAt(stage, f);
            stage.setAdaa(true);
            const auto adaa = shelfResponseAt(stage, f);
            stage.setAdaa(false);

            const double gotDb = db(std::abs(adaa) / std::abs(plain));
            const double gotDelay = pedal::test::excessDelaySamples(
                pedal::test::phaseErrorDeg(std::arg(adaa), std::arg(plain)), f, kFs);
            std::printf("  %5.0f Hz: ADAA cost %+.6f dB, delay %+.5f smp "
                        "(both must be 0; the old whole-map form gave %+.5f dB)\n",
                        f, gotDb, gotDelay, db(std::cos(M_PI * f / kFs)));
            worstMagErr = std::max(worstMagErr, std::abs(gotDb));
            worstDelayErr = std::max(worstDelayErr, std::abs(gotDelay));
        }
        if (worstMagErr > 1.0e-6 || worstDelayErr > 1.0e-4)
            fail("ADAA is altering the LINEAR path -- it is being applied to the whole map, not to "
                 "the nonlinear excess");
    }

    // 7. ⭐⭐ THE DISTORTION LAW: degeneration suppresses what it generates, so H2 falls as k^2.
    //
    //    This is the one property no linear test can see, and the first build shipped it wrong by
    //    exactly K0 (16.4 dB). Feeding the shelf output into the shaper models the DRIVE to the
    //    nonlinearity and stops there; the loop also attenuates the product the device makes inside
    //    it. Expanding id = gm*u + c*u^2 with u = vg - id*Zs gives id2 = c*(u1^2)/k(s) -- the same
    //    shelf a second time, on the squared term -- so H2/H1 = A/(4*Vov*k^2), not A/(4*Vov*k).
    //
    //    The oracle is an exact per-sample implicit solve of id = gm*g(vg - id*R5), Newton to
    //    machine precision, using THIS STAGE'S OWN shape() as the device law. That isolates the
    //    question being asked: any difference is the Volterra truncation of the structure, not a
    //    disagreement about what the device does. DARK is used because Zs = R5 is then memoryless,
    //    so the oracle needs no state and cannot itself be wrong about the dynamics.
    //
    //    The "shelf only" column is the shipped-in-error structure, reconstructed here (in DARK the
    //    shelf is the constant 1/K0, so it is just g(vg/K0)). Keeping it in the output means the bug
    //    stays visible as a number rather than as a paragraph.
    {
        std::printf("\n7. Distortion law vs an exact implicit solve of id = gm*g(vg - id*R5):\n");
        std::printf("     gate A      exact      this model   shelf-only (the bug)\n");
        stage.setMode(dsp::Mode::Dark);

        auto h2dbc = [](const auto& fn, double amp) {
            constexpr int kN = 8192;
            double c1 = 0.0, c2 = 0.0;
            for (int n = 0; n < kN; ++n)
            {
                const double th = 2.0 * M_PI * (double) n / kN;
                const double y = fn(amp * std::sin(th));
                c1 += y * std::sin(th);
                c2 += y * std::cos(2.0 * th);
            }
            return db(std::abs(c2) / std::abs(c1));
        };

        // Newton on F(id) = gm*g(vg - id*R5) - id. F' ~ -(1 + gm*R5*g') ~ -6.6, so it is well
        // conditioned everywhere; g' is taken numerically to keep the oracle independent of any
        // analytic derivative the stage might later grow.
        auto exact = [&stage, &params](double vg) {
            double id = 0.0;
            for (int i = 0; i < 80; ++i)
            {
                const double u = vg - id * circuit::kR5;
                constexpr double kH = 1.0e-7;
                const double gp = (stage.shape(u + kH) - stage.shape(u - kH)) / (2.0 * kH);
                const double step = (params.gm * stage.shape(u) - id) / (-params.gm * gp * circuit::kR5 - 1.0);
                id -= step;
                if (std::abs(step) < 1.0e-16)
                    break;
            }
            return id;
        };
        auto shelfOnly = [&stage, &params, k0](double vg) { return -params.gm * stage.shape(vg / k0); };
        auto model = [&stage](double vg) {
            stage.reset();
            // Memoryless in DARK, but run a few samples anyway so the shelf states are settled.
            double y = 0.0;
            for (int i = 0; i < 4; ++i)
                y = stage.processSample(vg);
            return y;
        };

        double worst = 0.0;
        for (const double amp : { 0.2, 0.5, 0.8 })
        {
            // The exact solve returns +id (into the drain resistor); the stage returns the injected
            // Norton current, which is -id. H2 re fundamental is unaffected by that sign.
            const double e = h2dbc(exact, amp);
            const double m = h2dbc(model, amp);
            const double b = h2dbc(shelfOnly, amp);
            std::printf("     %.2f V   %+7.2f dBc  %+7.2f (%+.2f)   %+7.2f (%+.2f)\n",
                        amp, e, m, m - e, b, b - e);
            worst = std::max(worst, std::abs(m - e));

            // The bug must stay caught: shelf-only over-produces H2 by ~20*log10(K0) = 16.4 dB.
            if (b - e < 10.0)
                fail("the shelf-only structure is NOT visibly wrong here -- this test has stopped "
                     "guarding the k^2 law and would pass with the bug reinstated");
        }
        std::printf("     worst deviation from the exact solve: %.2f dB "
                    "(Volterra truncation; Path A is the fix if this ever matters)\n", worst);
        if (worst > 1.0)
            fail("second-order feedback term is missing or mis-scaled -- H2 does not follow k^2");
    }

    // 7b. The suppression is FREQUENCY DEPENDENT, which is the half of it a constant 1/K0 would fake.
    //
    //     Scaling the excess by the DC factor 1/K0 would pass section 7 outright -- that test runs in
    //     DARK, where 1/k(s) IS the constant 1/K0. The distinguishing prediction is what happens when
    //     a bypass cap is engaged: the drive is filtered at the fundamental and the product is
    //     filtered at the HARMONIC, at two different points on the same shelf.
    //
    //         H2/H1 = (c*A/2gm) * |1/k(w)| * |1/k(2w)|
    //
    //     so the mode's H2 re DARK is K0^2 * |1/k(w)| * |1/k(2w)|, asymptoting to K0^2 = 32.7 dB once
    //     both the tone and its harmonic clear the shelf. Predicting the two factors separately is
    //     what makes this a test of the STRUCTURE rather than of one scalar.
    {
        std::printf("\n7b. MODE moves distortion as k(w)*k(2w), not as one scalar:\n");
        constexpr int kN = 4096;
        constexpr double kAmp = 0.01; // small on purpose: the prediction is the SECOND-ORDER
                                      // truncation, and Bright at 20 kHz barely attenuates the
                                      // drive, so a hot probe would measure higher-order
                                      // content against a second-order oracle.

        auto h2dbcAt = [&stage](double freq, double amp) {
            stage.reset();
            for (int n = 0; n < 2048; ++n) // settle the shelf states before measuring
                stage.processSample(amp * std::sin(2.0 * M_PI * freq * (double) n / kFs));
            // !! COMPLEX correlation, not a single sin/cos projection. The shelf gives the harmonic
            // its own phase, so a real-part-only read measures |H2|*cos(that phase) and comes back
            // several dB light exactly where the shelf is most active. Section 7 gets away with the
            // simpler form only because DARK is memoryless; here it would have been read as the
            // model failing this prediction by up to 7.6 dB.
            std::complex<double> c1 { 0.0, 0.0 }, c2 { 0.0, 0.0 };
            for (int n = 0; n < kN; ++n)
            {
                const double th = 2.0 * M_PI * freq * (double) (n + 2048) / kFs;
                const double y = stage.processSample(amp * std::sin(th));
                c1 += y * std::exp(std::complex<double> { 0.0, -th });
                c2 += y * std::exp(std::complex<double> { 0.0, -2.0 * th });
            }
            return db(std::abs(c2) / std::abs(c1));
        };

        for (const auto& c : cases)
        {
            if (c.tau <= 0.0)
                continue;
            for (const double m : { 107.0, 427.0 }) // integer bins of kFs/kN, so no leakage
            {
                const double f = m * kFs / kN;
                stage.setMode(dsp::Mode::Dark);
                const double dark = h2dbcAt(f, kAmp);
                stage.setMode(c.mode);
                const double lifted = h2dbcAt(f, kAmp);

                const double sh1 = std::abs(analyticShelf(pedal::test::warpedFrequency(f, kFs), c.tau, k0));
                const double sh2 = std::abs(analyticShelf(pedal::test::warpedFrequency(2.0 * f, kFs), c.tau, k0));
                const double want = db(k0 * k0 * sh1 * sh2);
                const double got = lifted - dark;
                std::printf("  %-6s %6.0f Hz: H2 re Dark %+6.2f dB (predicted %+6.2f, err %+.2f) "
                            "[1/k(w) %.3f, 1/k(2w) %.3f]\n", c.name, f, got, want, got - want, sh1, sh2);
                if (std::abs(got - want) > 0.5)
                    fail("mode-dependent distortion does not follow |1/k(w)|*|1/k(2w)| -- the excess "
                         "is being scaled rather than filtered");
            }
        }
    }

    std::printf(ok ? "\nPASS: JFET stage structure\n" : "\nFAILED: JFET stage structure\n");
    return ok ? 0 : 1;
}
