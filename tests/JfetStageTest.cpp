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
#include <array>
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
std::complex<double> shelfResponseAt(pedal::dsp::JfetStage& stage, double freq, double fs = kFs)
{
    constexpr double kProbe = 1.0e-6;
    stage.reset();
    // = -gm * (1/k(f)). The stage inverts, so the measured response carries a 180 degree offset that
    // is removed here -- the inversion is asserted separately by the DC-step test, and folding it
    // into the shelf comparison would only hide it.
    return -pedal::test::measureResponse([&stage](double x) { return stage.processSample(x); }, freq, fs, kProbe);
}

double shelfGainAt(pedal::dsp::JfetStage& stage, double freq) { return std::abs(shelfResponseAt(stage, freq)); }

// How far the shelf may sit from the ANALOG 1/k(s) at the 4x rate section 1 spot-checks. Section 1c
// carries the per-rate budgets; this one only has to cover 192 kHz, where the worst measured error
// is 0.053 dB.
constexpr double kShelfMagTolDb = 0.08;

// What a PLAIN BILINEAR discretisation of 1/k(s) would produce -- the structure this stage shipped
// with, kept as the reference every accuracy number here is quoted against. Written out rather than
// measured because it is the thing being compared TO: an independently-coded closed form cannot
// inherit a bug from the implementation under test.
std::complex<double> bilinearShelf(double freq, double tau, double k0, double fs)
{
    const double twoTauFs = 2.0 * tau * fs;
    const double norm = 1.0 / (k0 + twoTauFs);
    const double b0 = (1.0 + twoTauFs) * norm, b1 = (1.0 - twoTauFs) * norm, a1 = (k0 - twoTauFs) * norm;
    const std::complex<double> z = std::exp(std::complex<double> { 0.0, -2.0 * M_PI * freq / fs });
    return (b0 + b1 * z) / (1.0 + a1 * z);
}

// Worst phase error left AFTER removing the best-fit pure delay. A pure delay is phase = -d*f with
// no intercept, and both designs are exact at DC, so the fit is deliberately constrained through the
// origin -- an unconstrained line would absorb genuine low-frequency error into its intercept and
// flatter whichever design is worse there.
double maxPhaseErrorAfterDelayFit(const double* freqs, const double* phaseDeg, int n)
{
    double num = 0.0, den = 0.0;
    for (int i = 0; i < n; ++i)
    {
        num += freqs[i] * phaseDeg[i];
        den += freqs[i] * freqs[i];
    }
    const double slope = num / den;
    double worst = 0.0;
    for (int i = 0; i < n; ++i)
        worst = std::max(worst, std::abs(phaseDeg[i] - slope * freqs[i]));
    return worst;
}

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
    std::printf("  Vov = %.4f V, so the square law's own scale is %.3f V of gate swing\n",
                params.vov, params.vov * (1.0 + params.gm * circuit::kR5));

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
            // ⚠ Against the analytic shelf at the REAL frequency -- NOT, as this test used to do,
            // at the bilinear-warped frequency. That older form asked "are these the right bilinear
            // coefficients", which is a tautology once bilinear is what the stage computes: it
            // passed at 0.0000 dB while the shipped filter was up to 3.5 dB from the analog shelf at
            // the base rate. Comparing against the circuit's own transfer function is the only
            // version of this check that can fail for the reason that matters.
            //
            // PHASE MATTERS HERE MORE THAN ANYWHERE. 1/k(s) is a shelf whose zero sits below its
            // pole, so it contributes real phase LEAD through the transition. That lead is the
            // audible signature of the MODE switch as much as the magnitude lift is, and it is the
            // part a "just apply an HF shelf" shortcut gets wrong while still matching magnitude.
            // The tolerance is on phase error with the best-fit PURE DELAY removed (section 1c):
            // the three-point design buys its magnitude accuracy with a near-constant fractional
            // sample of delay, which a sub-sample null aligns out and a raw phase check would
            // wrongly flag.
            for (const double f : { 500.0, 2000.0, 8000.0, 20000.0 })
            {
                const auto got = shelfResponseAt(stage, f) / params.gm;
                const auto want = analyticShelf(f, c.tau, k0);
                const double errDb = std::abs(db(std::abs(got) / std::abs(want)));
                const double bilinearErrDb =
                    std::abs(db(std::abs(bilinearShelf(f, c.tau, k0, kFs)) / std::abs(want)));
                const bool bad = errDb > kShelfMagTolDb;
                std::printf("          %5.0f Hz: %.5f (want %.5f, %.4f dB; bilinear would be %.4f)%s\n",
                            f, std::abs(got), std::abs(want), errDb, bilinearErrDb,
                            bad ? "   <-- FAIL" : "");
                if (bad)
                    fail("shelf does not match the analytic 1/k(s) in magnitude");
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

    // 1c. ⭐ THE DISCRETISATION ITSELF, ACROSS EVERY RATE THE STAGE ACTUALLY RUNS AT.
    //     Section 1 spot-checks four frequencies at the 4x default. That is where the shelf is
    //     easy: the errors this section exists to catch are ~6x larger at the base rate, and the
    //     base rate is the one setting a CPU-bound session ends up on. Sweeping the rate is what
    //     turned "the shelf is exact" into a number that depends on which rate you ask at.
    //
    //     ⚠ WHY THE BILINEAR COLUMN STAYS IN THE OUTPUT. Plain bilinear is not a strawman here --
    //     it is what this stage shipped with, and the test that was supposed to guard the shelf
    //     compared it against the analytic prototype AT THE WARPED FREQUENCY, so it reported
    //     0.0000 dB while the filter sat 3.5 dB from the analog shelf at 48 kHz. Printing what
    //     bilinear WOULD do, next to what this design does, keeps the comparison a measurement
    //     rather than a claim, and makes a silent revert visible in the log.
    //
    //     Phase is scored with the best-fit PURE DELAY removed. That is not leniency: the design
    //     buys its magnitude accuracy with a near-constant fractional sample of extra delay, a
    //     sub-sample null aligns exactly that out, and a raw phase number would therefore report a
    //     trade-off that does not exist. Both axes still have to beat bilinear outright.
    {
        std::printf("\n1c. Shelf vs the ANALOG 1/k(s), 20 Hz - 20 kHz, per rate:\n");
        std::printf("    %-10s %-7s %9s %9s %10s %10s\n", "rate", "mode", "mag dB", "(bilin)",
                    "phase deg", "(bilin)");

        struct RateCase { double fs; double magBudgetDb; double phaseBudgetDeg; const char* label; };
        const RateCase rates[] = { {  48000.0, 0.60, 6.0, "48k (1x)" },
                                   {  96000.0, 0.25, 1.5, "96k (2x)" },
                                   { 192000.0, 0.08, 0.4, "192k (4x)" },
                                   { 384000.0, 0.03, 0.1, "384k (8x)" } };

        // Log-spaced probes over the audible band. The delay fit below is constrained through the
        // origin because a pure delay has zero phase at DC and BOTH designs are exact at DC, so the
        // intercept is not a free parameter -- letting it float would absorb real error.
        constexpr int kNumProbe = 24;
        double probeHz[kNumProbe];
        for (int i = 0; i < kNumProbe; ++i)
            probeHz[i] = 20.0 * std::pow(1000.0, (double) i / (double) (kNumProbe - 1));

        for (const auto& r : rates)
        {
            // ⚠ The instrument's OWN floor, measured rather than assumed, because by 8x the errors
            //    under test are smaller than it. DARK is a pure constant gain with exactly zero
            //    phase, so whatever phase this reports is the correlation instrument's leakage at
            //    this rate -- a known-answer probe that costs one extra measurement. It matters
            //    because the bilinear column is computed in CLOSED FORM while this design's column
            //    is MEASURED: only one of the two carries that noise, so comparing them below the
            //    floor would systematically favour the closed form and fail a correct filter.
            double phaseFloorDeg = 0.0;
            {
                dsp::JfetStage d;
                d.setParams(params);
                d.prepare(r.fs);
                d.setMode(dsp::Mode::Dark);
                for (int i = 0; i < kNumProbe; ++i)
                    phaseFloorDeg = std::max(
                        phaseFloorDeg,
                        std::abs(pedal::test::degrees(std::arg(shelfResponseAt(d, probeHz[i], r.fs)))));
            }

            for (const auto& c : cases)
            {
                if (c.tau <= 0.0)
                    continue; // DARK is a constant gain -- exact at every rate, nothing to discretise

                dsp::JfetStage s;
                s.setParams(params);
                s.prepare(r.fs);
                s.setMode(c.mode);

                double magErr = 0.0, bilMagErr = 0.0;
                double phGot[kNumProbe], phBil[kNumProbe];
                for (int i = 0; i < kNumProbe; ++i)
                {
                    const auto want = analyticShelf(probeHz[i], c.tau, k0);
                    const auto got = shelfResponseAt(s, probeHz[i], r.fs) / params.gm;
                    const auto bil = bilinearShelf(probeHz[i], c.tau, k0, r.fs);

                    magErr = std::max(magErr, std::abs(db(std::abs(got) / std::abs(want))));
                    bilMagErr = std::max(bilMagErr, std::abs(db(std::abs(bil) / std::abs(want))));
                    phGot[i] = pedal::test::phaseErrorDeg(std::arg(got), std::arg(want));
                    phBil[i] = pedal::test::phaseErrorDeg(std::arg(bil), std::arg(want));
                }

                const double phErr = maxPhaseErrorAfterDelayFit(probeHz, phGot, kNumProbe);
                const double bilPhErr = maxPhaseErrorAfterDelayFit(probeHz, phBil, kNumProbe);

                // The improvement is only asserted where bilinear's own error clears the floor by a
                // comfortable margin. Above 4x it does not, and there is nothing left to beat.
                const bool phaseIsResolvable = bilPhErr > 4.0 * phaseFloorDeg;
                const bool noBetter = magErr >= bilMagErr || (phaseIsResolvable && phErr >= bilPhErr);
                const bool bad = magErr > r.magBudgetDb || phErr > r.phaseBudgetDeg || noBetter;
                std::printf("    %-10s %-7s %9.3f %9.3f %10.2f %10.2f   (floor %.2f)%s\n", r.label,
                            c.name, magErr, bilMagErr, phErr, bilPhErr, phaseFloorDeg,
                            bad ? "   <-- FAIL" : "");
                if (magErr > r.magBudgetDb || phErr > r.phaseBudgetDeg)
                    fail("shelf discretisation is outside its budget at this rate");
                if (noBetter)
                    fail("shelf discretisation is no better than plain bilinear -- has it reverted?");
            }
        }
    }

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

    // 3. Even dominance, measured through the stage rather than on a bare shaper. A JFET is square
    //    law, so H2 must sit well above H3, and the separation must FALL as drive rises (the cubic
    //    the feedback loop makes grows faster than the quadratic). Probed in units of Vov, since that
    //    is the device's own scale: at a 0 dBFS input the stage sees w ~ 0.24*Vov.
    {
        std::printf("\nHarmonic structure (square-law signature), probed in units of Vov:\n");
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);
        double lastSep = 1.0e9;
        for (const double frac : { 0.25, 0.50, 0.75 })
        {
            const double amp = frac * params.vov * k0; // gate volts giving w ~ frac*Vov
            constexpr int kN = 8192;
            double a[4] = {}, b[4] = {};
            stage.reset();
            for (int n = -2048; n < kN; ++n)
            {
                const double th = 2.0 * M_PI * 107.0 * (double) n / kN;
                const double y = stage.processSample(amp * std::sin(th));
                if (n < 0)
                    continue;
                for (int k = 1; k <= 3; ++k)
                {
                    a[k] += y * std::sin((double) k * th);
                    b[k] += y * std::cos((double) k * th);
                }
            }
            double h[4];
            for (int k = 1; k <= 3; ++k)
                h[k] = std::hypot(a[k], b[k]);
            const double h2 = db(h[2] / h[1]), h3 = db(h[3] / h[1]);
            std::printf("  A = %.2f*Vov at the gate (%.3f V):  H2 %7.2f dBc,  H3 %7.2f dBc,  sep %5.2f dB\n",
                        frac, amp, h2, h3, h2 - h3);
            if (h2 <= h3)
                fail("H3 has overtaken H2 -- a square-law device cannot do that");
            if (h2 - h3 > lastSep)
                fail("the H2/H3 separation is not closing with drive -- the loop's cubic is missing");
            lastSep = h2 - h3;
        }
    }

    // 4. ⭐⭐ THE OPERATING POINT AND THE LOAD LINE, which is what replaced the fitted shaper.
    //
    //    The old structure asymptoted the drain current at the CHANNEL ceiling, (IDSS - Id0), and
    //    could not express the load line at all: its even bump alone reached Vov/2 = 347 uA, already
    //    64 % of the load line's ~542 uA, leaving the core nowhere to go. That was recorded as a known
    //    impossibility. The device model has no such problem -- the parabola is unbounded and the
    //    ceiling comes from the triode region instead, where it comes from in the circuit.
    //
    //    ⚠ This is now a MAIN-PATH behaviour, not a corner case. At kInputRef = 4.4626 the drain
    //    enters triode at -6.7 dBFS, so the top 6.7 dB of an ordinarily-tracked take is inside it.
    {
        std::printf("\n4. Operating point and the load line:\n");
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);
        std::printf("    Id0 %.1f uA | Vds_q %.3f V | |Vp| %.4f V | IDSS %.2f mA | zLoad %.1f k\n",
                    params.id0() * 1.0e6, params.vdsQuiescent(), params.vpMagnitude(),
                    params.id0() * std::pow(1.0 + 0.5 * params.gm * circuit::kR5, 2.0) * 1.0e3,
                    params.zLoad / 1.0e3);

        const double onset = stage.triodeOnsetGateVolts();
        std::printf("    drain enters triode at a %.3f V gate swing\n", onset);
        if (onset < 1.0 || onset > 2.5)
            fail("the triode onset has moved a long way -- check Vov, the rail, or zLoad");

        // The channel ceiling must be far ABOVE the load line's, or the load line is not the binding
        // constraint and this whole section is measuring the wrong thing.
        const double iChannel = params.id0() * (std::pow(1.0 + 0.5 * params.gm * circuit::kR5, 2.0) - 1.0);
        const double iLoad = params.vdsQuiescent() / (params.zLoad + circuit::kR5);
        std::printf("    current ceiling: load line %.0f uA vs the channel's %.0f uA (%.1fx tighter)\n",
                    iLoad * 1.0e6, iChannel * 1.0e6, iChannel / iLoad);
        if (iChannel / iLoad < 3.0)
            fail("the load line is no longer the binding ceiling");

        // Walk up the gate and confirm the three regions appear in the right order at the right place.
        std::printf("    %10s %11s %9s %9s %s\n", "gate V", "i (uA)", "w (V)", "Vds (V)", "region");
        bool sawTriode = false, sawCutoff = false;
        for (const double vg : { 0.783, 1.5, onset * 1.02, 3.0, 4.016, -2.0, -4.0 })
        {
            stage.reset();
            double y = 0.0;
            for (int n = 0; n < 64; ++n)
                y = stage.processSample(vg);
            const double vovI = params.vov + stage.lastGateDrive();
            const char* region = (vovI <= 0.0) ? "CUTOFF"
                               : (stage.lastDrainSourceVolts() < vovI ? "TRIODE" : "saturation");
            std::printf("    %10.3f %11.2f %9.4f %9.3f %s\n", vg, -y * 1.0e6, stage.lastGateDrive(),
                        stage.lastDrainSourceVolts(), region);
            sawTriode |= (region[0] == 'T');
            sawCutoff |= (region[0] == 'C');
            if (-y > iChannel)
                fail("drain current has exceeded the channel ceiling -- the device law is wrong");
        }
        if (! sawTriode)
            fail("the drain never enters triode -- the load line is not implemented");
        if (! sawCutoff)
            fail("the device never cuts off");
        stage.reset();
    }

    // 5. gm alone must set the small-signal gain, so any linear oracle stays valid. dI/dw at the
    //    origin is 2*beta*Vov, and beta = gm/(2*Vov) by construction, so this is exactly gm -- but it
    //    is worth measuring rather than asserting, because it is the hinge the whole linear model
    //    hangs on and a slip in the derived quantities would move it silently.
    {
        constexpr double kProbe = 1.0e-7;
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);
        stage.reset();
        double y = 0.0;
        for (int n = 0; n < 8; ++n)
            y = stage.processSample(kProbe);
        const double gotGm = -y / (kProbe / k0); // undo the DC degeneration
        std::printf("\n5. small-signal transconductance: %.9e S (params.gm = %.9e)\n", gotGm, params.gm);
        if (std::abs(gotGm / params.gm - 1.0) > 1.0e-6)
            fail("gm is not setting the small-signal gain -- the linear model and every fit move");
    }

    // 6. ⭐⭐ THE WHOLE STAGE AGAINST AN INDEPENDENTLY-WRITTEN SOLVE OF THE SAME CIRCUIT.
    //
    //    The oracle below re-derives the operating point from the circuit constants by BISECTION on a
    //    residual written out longhand, rather than by the shipped Newton iteration. Different
    //    algorithm, different code, same equations -- so it cannot inherit a bug from the solver under
    //    test, which is the one thing section 7 of the previous structure eventually could not claim.
    //    DARK is used so the source impedance is the memoryless R5 and the oracle needs no state.
    {
        std::printf("\n6. Stage vs an independent bisection solve of the same circuit (DARK):\n");
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);

        const double vov = params.vov, id0 = params.id0(), beta = params.betaSq();
        const double vdsQ = params.vdsQuiescent(), zl = params.zLoad;
        auto oracle = [&](double vg) {
            auto residual = [&](double i) {
                const double vs = i * circuit::kR5;
                const double vovI = vov + vg - vs;
                double vds = vdsQ - i * zl - vs;
                if (vds < 0.0)
                    vds = 0.0;
                double I;
                if (vovI <= 0.0)
                    I = 0.0;
                else if (vds >= vovI)
                    I = beta * vovI * vovI;
                else
                    I = beta * (2.0 * vovI * vds - vds * vds);
                return i - (I - id0);
            };
            double lo = -id0, hi = vdsQ / (zl + circuit::kR5);
            for (int n = 0; n < 200; ++n)
            {
                const double m = 0.5 * (lo + hi);
                (residual(m) > 0.0 ? hi : lo) = m;
            }
            return 0.5 * (lo + hi);
        };

        std::printf("    %10s %13s %13s %10s\n", "gate V", "stage (uA)", "oracle (uA)", "err");
        double worst = 0.0;
        for (const double vg : { 0.05, 0.197, 0.783, 1.5, 2.0, 3.0, 4.016, -0.5, -2.0 })
        {
            stage.reset();
            double y = 0.0;
            for (int n = 0; n < 64; ++n)
                y = stage.processSample(vg);
            const double got = -y, want = oracle(vg);
            const double err = std::abs(got - want) / std::max(1.0e-9, std::abs(want));
            std::printf("    %10.3f %13.3f %13.3f %9.1e\n", vg, got * 1.0e6, want * 1.0e6, err);
            worst = std::max(worst, err);
        }
        std::printf("    worst relative disagreement: %.2e\n", worst);
        // ⚠ THIS TOLERANCE IS THE ITERATION COUNT, NOT THE MODEL. The oracle beside it is solved to
        // machine precision; the shipped stage runs JfetStage::kSolveIters = 8, which reaches ~2e-7
        // of relative current error at the drives probed here. Tightening this without raising the
        // count would be asserting a convergence the stage is not paid to reach -- and the count was
        // chosen on HARMONIC error (section 8c: 0.00 dB against a 40-iteration solve at every drive),
        // which is the axis that matters. So this checks the two implementations agree on the same
        // EQUATIONS, at the precision the shipped count delivers.
        if (worst > 1.0e-5)
            fail("the shipped solve disagrees with an independent solve of the same equations by "
                 "more than its own iteration count can explain");
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
        static constexpr int kN = 4096;
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


    // ================================ 8. PATH A -- THE IMPLICIT SOLVE ================================
    //
    // Section 7 cannot constrain Path A: it IS the oracle there, so its column is definitional. These
    // are the checks that do constrain it, and each one targets a different way the design could be
    // wrong while every existing test still passed.

    // 8a. ⭐⭐ THE LINEAR PATH MUST BE UNTOUCHED, and this is the check that makes the whole design
    //     safe. Path A's source one-port is DERIVED from the shelf coefficients so that the
    //     linearised loop 1/(1 + gm*Zs(z)) reproduces H(z) exactly. If that algebra is wrong, the
    //     stage's frequency response moves -- and with it the mode differential gm was fitted to, the
    //     three-point match that removed the 3.09 dB bilinear spread, the phase residuals, and
    //     OsDroopRestore's premise. Comparing Path A against the TRUNCATED path at a probe amplitude
    //     where both are linear isolates exactly that algebra, in magnitude AND phase, at every rate
    //     the stage runs at -- and phase is not optional here: this project has three defects on
    //     record that magnitude testing could not see.
    {
        std::printf("\n8a. The solve's linearisation vs the source one-port it was derived from:\n");
        double worstMag = 0.0, worstPhase = 0.0;
        for (const double fs : { 48000.0, 96000.0, 192000.0, 384000.0 })
        {
            for (const auto m : { dsp::Mode::Bright, dsp::Mode::Dark, dsp::Mode::Mid })
            {
                stage.setMode(m);
                stage.prepare(fs);
                double rd = 0.0, c1 = 0.0, c2 = 0.0;
                stage.sourcePortCoeffs(rd, c1, c2);
                for (const double f : { 20.0, 200.0, 2000.0, 12000.0, 0.45 * fs })
                {
                    // Zs(z) = (Rd + C1 z^-1)/(1 - C2 z^-1) straight off the difference equation, so
                    // the closed loop is 1/(1 + gm*Zs). Written out here rather than measured, so it
                    // cannot inherit a bug from the solve it is checking.
                    const std::complex<double> z1 = std::polar(1.0, -2.0 * M_PI * f / fs);
                    const std::complex<double> zs = (rd + c1 * z1) / (1.0 - c2 * z1);
                    const auto want = 1.0 / (1.0 + params.gm * zs);
                    const auto got = shelfResponseAt(stage, f, fs) / params.gm;
                    worstMag = std::max(worstMag, std::abs(db(std::abs(got) / std::abs(want))));
                    worstPhase = std::max(worstPhase,
                                          pedal::test::phaseErrorDeg(std::arg(got), std::arg(want)));
                }
            }
        }
        std::printf("    worst over 3 modes x 4 rates x 5 frequencies: %.3e dB, %.3e deg\n",
                    worstMag, worstPhase);
        // The two paths compute the response by completely different arithmetic (a direct-form
        // difference equation against a Newton solve), so they are not expected to be bit-identical;
        // they are expected to agree to the solve's own convergence, which is far below anything
        // audible or measurable.
        if (worstMag > 1.0e-6 || worstPhase > 1.0e-4)
            fail("the solve's small-signal response is NOT 1/(1 + gm*Zs) -- updateSourcePort()'s "
                 "algebra is wrong, and the mode differential, the three-point discretisation and "
                 "OsDroopRestore's premise all move with it");
        stage.prepare(kFs);
    }

    // 8b. The source one-port's DC impedance must be R5 EXACTLY, in every mode and at every rate.
    //     This is physics, not a tolerance: the bypass caps block DC, so the DC feedback path is the
    //     bare resistor whatever the switch is doing. It falls out of the derivation rather than
    //     being imposed, which is what makes it a real test of it. Realisability is checked here too
    //     -- Rd > 0 (a negative instantaneous resistance would be an unstable solve) and |C2| < 1
    //     (the port's own pole, which is the shelf's zero).
    {
        std::printf("\n8b. Derived source one-port Zs(z), per mode and rate:\n");
        std::printf("    rate      mode      Rd ohm    C1        C2 (pole)   Zs(DC) ohm   err\n");
        for (const double fs : { 48000.0, 96000.0, 192000.0, 384000.0 })
        {
            for (const auto m : { dsp::Mode::Bright, dsp::Mode::Dark, dsp::Mode::Mid })
            {
                stage.setMode(m);
                stage.prepare(fs);
                double rd = 0.0, c1 = 0.0, c2 = 0.0;
                stage.sourcePortCoeffs(rd, c1, c2);
                // vs = Rd*id + C1*id[n-1] + C2*vs[n-1]  ->  at DC, Zs = (Rd + C1)/(1 - C2)
                const double zsDc = (rd + c1) / (1.0 - c2);
                const double err = std::abs(zsDc / circuit::kR5 - 1.0);
                std::printf("    %-9.0f %-9s %8.1f %9.1f %10.5f %12.4f %10.1e\n",
                            fs, m == dsp::Mode::Bright ? "Bright" : (m == dsp::Mode::Dark ? "Dark" : "Mid"),
                            rd, c1, c2, zsDc, err);
                if (err > 1.0e-12)
                    fail("Zs(DC) is not R5 -- the DC feedback path must be the bare resistor, since "
                         "the bypass caps cannot pass DC");
                if (rd <= 0.0)
                    fail("negative instantaneous source resistance -- the Newton solve is not "
                         "guaranteed to converge");
                if (std::abs(c2) >= 1.0)
                    fail("the source one-port's own pole is outside the unit circle");
            }
        }
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);
        double rd = 0.0, c1 = 0.0, c2 = 0.0;
        stage.sourcePortCoeffs(rd, c1, c2);
        if (std::abs(rd - circuit::kR5) > 1.0e-9 || c1 != 0.0 || c2 != 0.0)
            fail("DARK must collapse the port to the bare resistor R5 with no state at all");
    }

    // 8c. ⭐⭐ THE CLOSED-FORM SOLVE AGAINST THE SAFEGUARDED NEWTON IT REPLACED.
    //
    //     Shichman-Hodges is piecewise quadratic and both the source one-port and the load line are
    //     linear in the drain current, so every branch has an exact algebraic root -- the stage does
    //     no iteration at all. The iterative solve is kept solely as the oracle checked here: it
    //     reaches the same answer by a completely different method, so neither can inherit the
    //     other's bug, and it is run to 40 iterations so it is the converged one.
    //
    //     ⚠⚠ THIS TEST EARNED ITS KEEP IMMEDIATELY. The first port of the closed form returned only
    //     the cancellation-stable SMALL root of each quadratic, on the reasoning that the physical
    //     root is the small one. That holds only while b < 0, and b flips sign at about -0.53 V of
    //     gate drive -- below which the stage fell through to the cutoff branch and returned -Id0 for
    //     every sample, a 0.9 mA error on a 0.35 mA quiescent current. Selection is by each branch's
    //     own physical validity condition now, never by magnitude.
    {
        std::printf("\n8c. Closed form vs a 40-iteration safeguarded Newton on the same equations:\n");
        std::printf("    %-8s %-8s %14s %14s\n", "rate", "mode", "worst rel", "worst abs A");
        double worstRel = 0.0, worstAbs = 0.0;
        for (const double fs : { 48000.0, 192000.0, 384000.0 })
        {
            for (const auto m : { dsp::Mode::Bright, dsp::Mode::Dark, dsp::Mode::Mid })
            {
                dsp::JfetStage closed, iter;
                closed.setParams(params);
                iter.setParams(params);
                closed.setMode(m);
                iter.setMode(m);
                closed.prepare(fs);
                iter.prepare(fs);
                closed.setUseClosedForm(true);
                iter.setUseClosedForm(false);
                iter.setSolveIters(40);

                double wr = 0.0, wa = 0.0;
                const int n = (int) (fs * 0.05);
                for (int k = 0; k < n; ++k)
                {
                    const double t = (double) k / fs;
                    // Two tones so the two branches interleave, plus periodic +12 dB-trim steps.
                    double x = 4.016 * std::sin(2.0 * M_PI * 220.0 * t) + 1.5 * std::sin(2.0 * M_PI * 3100.0 * t);
                    if (k % 997 == 0)
                        x = 12.0;
                    const double ya = closed.processSample(x);
                    const double yb = iter.processSample(x);
                    wa = std::max(wa, std::abs(ya - yb));
                    wr = std::max(wr, std::abs(ya - yb) / std::max(1.0e-9, std::abs(yb)));
                }
                std::printf("    %-8.0f %-8s %14.2e %14.2e\n", fs / 1000.0,
                            m == dsp::Mode::Bright ? "Bright" : (m == dsp::Mode::Dark ? "Dark" : "Mid"), wr, wa);
                worstRel = std::max(worstRel, wr);
                worstAbs = std::max(worstAbs, wa);
            }
        }
        // Both axes, because either alone can be fooled: the relative figure blows up where the true
        // current passes through zero, and the absolute figure hides an error that only appears where
        // the signal is small.
        if (worstRel > 1.0e-8 || worstAbs > 1.0e-12)
            fail("the closed-form solve disagrees with an independent solve of the same equations");
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);
    }

    // 8d. ⭐⭐ WHAT PATH A EXISTS FOR: compression and third-harmonic content, neither of which the
    //     truncated structure can produce AT ALL. Both are higher-order terms of the same expansion,
    //     so truncating after the second order discards them by construction -- the model read
    //     exactly 0.000 dB of compression in every band at every level, against 0.2-0.6 dB in the
    //     captures (analysis/compression_audit.py). beta is still 0: this cubic content is the
    //     LOOP's, not a fitted coefficient's.
    {
        std::printf("\n8d. Compression and H3 -- neither exists in a truncated expansion:\n");
        std::printf("      %10s %12s %10s %10s\n", "gate V", "comp dB", "H2 dBc", "H3 dBc");
        stage.setMode(dsp::Mode::Dark);
        stage.prepare(kFs);
        auto probe = [&stage](double amp, double& compDb, double& h2dbc, double& h3dbc) {
            constexpr int kN = 8192;
            auto run = [&](double a) {
                stage.reset();
                double s1 = 0, c1 = 0, s2 = 0, c2 = 0, s3 = 0, c3 = 0;
                for (int n = -2048; n < kN; ++n)
                {
                    const double th = 2.0 * M_PI * 107.0 * (double) n / kN;
                    const double y = stage.processSample(a * std::sin(th));
                    if (n < 0)
                        continue;
                    s1 += y * std::sin(th); c1 += y * std::cos(th);
                    s2 += y * std::sin(2 * th); c2 += y * std::cos(2 * th);
                    s3 += y * std::sin(3 * th); c3 += y * std::cos(3 * th);
                }
                return std::array<double, 3> {{ std::hypot(s1, c1), std::hypot(s2, c2), std::hypot(s3, c3) }};
            };
            constexpr double kRef = 1.0e-4;
            const auto lo = run(kRef);
            const auto hi = run(amp);
            compDb = db(hi[0] / (lo[0] * amp / kRef));
            h2dbc = db(hi[1] / hi[0]);
            h3dbc = db(hi[2] / hi[0] + 1.0e-30);
        };
        double lastComp = 0.0, lastH3 = -1000.0;
        for (const double amp : { 0.2752, 0.7830, 1.5 })
        {
            double c = 0.0, h2 = 0.0, h3 = 0.0;
            probe(amp, c, h2, h3);
            std::printf("      %10.4f %12.4f %10.1f %10.1f\n", amp, c, h2, h3);
            if (c >= 0.0)
                fail("compression is not compressive -- the fundamental's gain must FALL with level");
            if (c > lastComp)
                fail("compression must deepen with level");
            if (h3 < lastH3)
                fail("H3 must grow with level -- the loop's cubic is missing");
            lastComp = c;
            lastH3 = h3;
        }

        // ⭐ Cross-implementation check against the INDEPENDENT Python oracle that motivated the whole
        // change (analysis/compression_audit.py): an exact solve of id = gm*g(vg - id*R5) with g a
        // PURE square law plus its cutoff clamp reported -0.032 dB of compression at a 0 dBFS input,
        // A_gate = 0.783 V, at the shipped Vov. This stage now IS that square law, so the two should
        // agree -- and they are written months and one language apart, so neither can inherit the
        // other's bug. ⚠ The oracle carried no load line, which is why the check is made at 0.783 V:
        // that is still in saturation, where the two models are the same circuit.
        double comp = 0.0, h2x = 0.0, h3x = 0.0;
        probe(0.7830, comp, h2x, h3x);
        std::printf("      cross-check vs the Python oracle at 0 dBFS: %.4f dB (oracle -0.032)\n", comp);
        if (std::abs(comp + 0.032) > 0.006)
            fail("disagrees with the independent exact-solve oracle in the saturation region");
    }

    // 8e. ⭐ THIS PROJECT'S FREE KNOWN-ANSWER PROBE, applied to the MODEL rather than to a capture --
    //     and it turns out the probe has a validity condition nobody had written down.
    //
    //     The probe: below the mode shelf's zero every MODE position has Zs = R5, so all three must
    //     behave IDENTICALLY there. circuit.md notes #7/#10/#11 use it to measure the reference
    //     models' own floor with no reference capture (magnitude, harmonic, and compression flavours).
    //
    //     ⚠⚠ BUT COMPRESSION IS A THIRD-ORDER QUANTITY, so it reads the loop at 3f, not at f. The
    //     magnitude flavour of the probe is exact as soon as f is below the zero; this one is not
    //     exact until 3f is. That is a factor of three in probe frequency, and it is not academic:
    //     analysis/compression_audit.py's highest known-zero band is 800 Hz, where 3f = 2400 Hz is
    //     ABOVE the Bright shelf's 1864 Hz zero. The table below measures what that costs.
    //
    //     It does not overturn anything -- the systematic error is ~0.02 dB against a measured
    //     capture floor of 0.145 dB (P1) and 0.210 dB (P2), so it is 7-10x below the number it would
    //     have to corrupt. It does mean the floor those scripts report is very slightly pessimistic
    //     at their top band, and that any future tightening of that floor has to drop the 800 Hz row.
    {
        std::printf("\n8e. Known-answer probe: below the shelf zero the modes must behave identically.\n");
        std::printf("    ⚠ compression reads the loop at 3f, so the probe is only exact once 3f << fz:\n");
        std::printf("      probe f   3f       modelled spread\n");
        stage.prepare(kFs);
        auto compAt = [&stage](dsp::Mode m, double amp, double bin) {
            stage.setMode(m);
            constexpr int kN = 16384;
            auto run = [&](double a) {
                stage.reset();
                double s = 0, c = 0;
                for (int n = -4096; n < kN; ++n)
                {
                    const double th = 2.0 * M_PI * bin * (double) n / kN;
                    const double y = stage.processSample(a * std::sin(th));
                    if (n < 0)
                        continue;
                    s += y * std::sin(th); c += y * std::cos(th);
                }
                return std::hypot(s, c);
            };
            constexpr double kRef = 1.0e-4;
            return db(run(amp) / (run(kRef) * amp / kRef));
        };
        auto spreadAt = [&compAt](double bin) {
            const double b = compAt(dsp::Mode::Bright, 1.5, bin);
            const double d = compAt(dsp::Mode::Dark, 1.5, bin);
            const double m = compAt(dsp::Mode::Mid, 1.5, bin);
            return std::max({ b, d, m }) - std::min({ b, d, m });
        };
        for (const double bin : { 2.0, 8.0, 42.0, 68.0 }) // 23.4 .. 797 Hz at 192 kHz / 16384
        {
            const double f = bin * kFs / 16384.0;
            std::printf("      %7.1f Hz %7.1f Hz   %.5f dB%s\n", f, 3.0 * f, spreadAt(bin),
                        3.0 * f > 1864.0 ? "   <- 3f is past the Bright shelf zero" : "");
        }

        // 📌 A SECOND EFFECT USED TO SIT UNDER THE FIRST AND NO LONGER DOES. Under the previous
        // two-iteration Newton the spread stopped falling at ~0.0017 dB, because Rd*gm differs ~30x
        // between Dark and Bright so the same fixed count converged to different depths per mode --
        // a MODE-DEPENDENT bias that looked exactly like physics. The ten-iteration bracketed solve
        // converges fully in every mode, so it is gone: shipped and converged now agree to 1e-8 dB.
        // The check is kept because it is the thing that would come back if the count were ever cut.
        const double shipped = spreadAt(2.0);
        stage.setSolveIters(12);
        const double converged = spreadAt(2.0);
        stage.setSolveIters(10);
        std::printf("      at 23.4 Hz: %.6f dB at the shipped count, %.6f dB fully converged\n",
                    shipped, converged);
        if (converged > 1.0e-4)
            fail("the modes do not compress identically well below the shelf zero even with the "
                 "solve converged -- Zs is not R5 there, so the source one-port's DC behaviour is "
                 "wrong");
        if (shipped > 5.0e-3)
            fail("the fixed iteration count is leaving a mode-dependent compression bias large "
                 "enough to matter -- raise kSolveIters");
        stage.setMode(dsp::Mode::Dark);
    }

    std::printf(ok ? "\nPASS: JFET stage structure\n" : "\nFAILED: JFET stage structure\n");
    return ok ? 0 : 1;
}
