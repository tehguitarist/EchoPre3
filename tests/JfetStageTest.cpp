// Stage 2 validation (CLAUDE.md build sequence step 4: "Nonlinear stage: sine-clipping behaviour;
// confirm output polarity with a DC-step test"). Pure chowdsp_wdf console exe, no JUCE.
//
// This tests the STRUCTURE, not the fit. Every amplitude parameter in JfetParams is a placeholder
// awaiting the reference renders (build-plan.md M2/M5), so nothing here asserts a gain in dB. What
// it does assert are the properties that must survive any later fit:
//
//   * 1/k(s) is a shelf with its zero at the drawn bypass corner and a plateau ratio of exactly
//     1 + gm*R5 -- the quantity M2 extracts from a capture ratio with no level calibration.
//   * the stage inverts (single common-source stage, no second inversion downstream).
//   * the shaper is even-dominant, as a square-law device must be and a tanh structurally cannot be.
//   * the shaper is MONOTONE over the full reachable range -- the trap that the shipped parameter
//     triple was chosen to avoid, and which a later refit could silently reintroduce.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

#include "dsp/JfetStage.h"

#include "MeasureUtils.h"

namespace
{
// The stage runs inside the oversampled region, so it is validated at the shipped 4x default. Its
// shelf pole sits near 18 kHz at nominal gm and would be badly misplaced by the bilinear warp at
// base rate -- which is exactly why it lives there.
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

// 1/k(s) = (1 + s*R5*C) / (K0 + s*R5*C), the shelf the source-bypass branch produces.
std::complex<double> analyticShelf(double freq, double cap, double k0)
{
    const std::complex<double> sTau { 0.0, 2.0 * M_PI * freq * pedal::circuit::kR5 * cap };
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
    std::printf("Placeholder parameters (ALL awaiting a fit -- build-plan.md M2/M5):\n");
    std::printf("  gm = %.1f uS, ro = %.2f MOhm, Zout = R6||ro = %.2f k\n", params.gm * 1.0e6,
                params.ro / 1.0e6, stage.outputImpedance() / 1.0e3);
    std::printf("  K0 = 1 + gm*R5 = %.3f  (%.2f dB of mode lift -- THIS is what M2 measures)\n", k0, db(k0));

    // 1. The shelf, per mode. Normalise by gm so the numbers are the 1/k(s) response itself.
    //    DC must sit at 1/K0 and HF must reach unity: the degeneration is fully bypassed above the
    //    corner, so BOTH cap positions reach the SAME plateau. Bright vs Mid differ only in WHERE the
    //    lift starts -- modelling Bright as a louder Mid is a different and wrong shape.
    std::printf("\n1/k(s) shelf per MODE (normalised by gm):\n");
    struct ModeCase { const char* name; pedal::dsp::Mode mode; double cap; };
    const ModeCase cases[] = { { "Bright", dsp::Mode::Bright, circuit::kC2 },
                               { "Dark",   dsp::Mode::Dark,   0.0 },
                               { "Mid",    dsp::Mode::Mid,    circuit::kC1 } };

    for (const auto& c : cases)
    {
        stage.setMode(c.mode);
        const double atDc = shelfGainAt(stage, 1.0) / params.gm;

        std::printf("  %-7s DC %.4f (want %.4f)\n", c.name, atDc, 1.0 / k0);
        if (std::abs(atDc - 1.0 / k0) > 0.01 * (1.0 / k0))
            fail("DC gain is not 1/K0");

        if (c.cap > 0.0)
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
                const auto want = analyticShelf(fEff, c.cap, k0);
                const double errDb = std::abs(db(std::abs(got) / std::abs(want)));
                const double phErrDeg = pedal::test::phaseErrorDeg(std::arg(got), std::arg(want));
                const bool bad = errDb > 0.02 || std::abs(phErrDeg) > 0.5;
                std::printf("          %5.0f Hz: %.5f (want %.5f, %.4f dB)  phase %+7.3f (want %+7.3f)%s\n",
                            f, std::abs(got), std::abs(want), errDb, pedal::test::degrees(std::arg(got)),
                            pedal::test::degrees(std::arg(want)), bad ? "   <-- FAIL" : "");
                if (bad)
                    fail("shelf does not match the analytic 1/k(s) in magnitude and phase");
            }

            // The shelf's zero must sit at the drawn bypass corner 1/(2*pi*R5*C).
            const double wantHz = 1.0 / (2.0 * M_PI * circuit::kR5 * c.cap);
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
                fail("shelf is not positioned at the drawn bypass corner");
        }
    }

    // 1b. Both cap positions must converge on the SAME high-frequency plateau -- fully bypassed, the
    //     degeneration is gone either way. BRIGHT vs MID is only about WHERE the lift starts, so
    //     modelling Bright as a higher-gain Mid would be a different and wrong shape. Checked
    //     analytically, because the plateau is approached asymptotically and 40 kHz is not there yet
    //     (Bright is legitimately still at 0.92 of it).
    std::printf("\nBoth cap positions converge on one plateau (BRIGHT is not a louder MID):\n");
    for (const double f : { 40.0e3, 200.0e3, 1.0e6 })
        std::printf("  %7.0f kHz: Bright %.5f, Mid %.5f\n", f / 1000.0,
                    std::abs(analyticShelf(f, circuit::kC2, k0)), std::abs(analyticShelf(f, circuit::kC1, k0)));
    const double plateauRatio = std::abs(analyticShelf(1.0e6, circuit::kC2, k0))
                              / std::abs(analyticShelf(1.0e6, circuit::kC1, k0));
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
    std::printf("\nHarmonic structure of the shaper (square-law signature):\n");
    for (const double amp : { 0.1, 0.3, 0.6 })
    {
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
        std::printf("  A = %.2f V:  H2 %7.2f dBc,  H3 %7.2f dBc,  separation %5.2f dB\n", amp, h2db, h3db,
                    h2db - h3db);
        if (h2db <= h3db + 15.0)
            fail("shaper is not clearly even-dominant -- a JFET must be");
    }

    // 4. Monotonicity, scanned on the REAL combined function over the full reachable range. A bound
    //    derived for the even bump alone is only an upper bound on the admissible region, never the
    //    region: the core's own curvature subtracts from the bump's slope and folds the sum back
    //    earlier than the sub-term algebra predicts. The shipped limitNeg was chosen from this scan.
    std::printf("\nMonotonicity scan of the shipped parameter triple:\n");
    constexpr double kScanV = 6.0;
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

    // 6c. What ADAA COSTS in the linear regime, stated exactly rather than assumed away.
    //
    //     ADAA1 is a two-point average, so on a linear map it is EXACTLY the FIR (1 + z^-1)/2: a
    //     magnitude of cos(pi*f/fs) and a half-SAMPLE delay. It is tempting to write this test as
    //     "ADAA must not change the small-signal gain" -- that assertion is false, and asserting it
    //     would have condemned a correct implementation. The right check is that the deviation is
    //     that two-point average and NOTHING MORE, which pins the implementation without pretending
    //     the rolloff is not there.
    //
    //     This rolloff is why dsp.md insists the gate be measured at both ends. It is evaluated at
    //     the OVERSAMPLED rate, so it shrinks fast with the factor -- but at 1x it is severe, and it
    //     lands on a top octave that is already drooping. OSFidelity and FeatureProfile decide the
    //     gate; it is currently off at every factor.
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

            const double theta = M_PI * f / kFs;
            const double gotDb = db(std::abs(adaa) / std::abs(plain));
            const double wantDb = db(std::cos(theta));
            // Half a sample of delay, expressed in samples so it reads as the physical quantity.
            const double gotDelay = pedal::test::excessDelaySamples(
                pedal::test::phaseErrorDeg(std::arg(adaa), std::arg(plain)), f, kFs);
            std::printf("  %5.0f Hz: ADAA cost %+.5f dB (2-point average predicts %+.5f), "
                        "delay %+.4f smp (predicts +0.5000)\n", f, gotDb, wantDb, gotDelay);
            worstMagErr = std::max(worstMagErr, std::abs(gotDb - wantDb));
            worstDelayErr = std::max(worstDelayErr, std::abs(gotDelay - 0.5));
        }
        if (worstMagErr > 1.0e-4 || worstDelayErr > 1.0e-3)
            fail("ADAA's linear-regime response is not exactly the two-point average -- it is doing "
                 "something beyond antialiasing");
    }

    std::printf(ok ? "\nPASS: JFET stage structure\n" : "\nFAILED: JFET stage structure\n");
    return ok ? 0 : 1;
}
