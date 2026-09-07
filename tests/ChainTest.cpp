// Full-chain integration test (CLAUDE.md build sequence step 7 / step 10). Pure chowdsp_wdf, no
// JUCE: the chain is prepared with both halves at the same rate, so no oversampler is needed and
// this measures the CIRCUIT coupling rather than the resampling.
//
// The two things only a full-chain test can establish:
//
//  1. OVERALL POLARITY. Each stage can be individually correct while the assembly is wrong -- two
//     accidental inversions cancel and every magnitude test still passes. That is not hypothetical:
//     the input network shipped an inversion (from the chowdsp voltage-source idiom) that would have
//     cancelled the JFET's own physical inversion and left the plugin the wrong way round.
//
//  2. THE MODE DIFFERENTIAL, END TO END. This is build-plan.md M1/M2 -- the single most valuable
//     measurement available from the reference renders, and the one that hands over `gm` as a pure
//     RATIO needing no level calibration. Having the instrument here now means M1/M2 becomes a
//     direct comparison the moment the renders land.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

#include "dsp/EchoPreDsp.h"

#include "MeasureUtils.h"

namespace
{
constexpr double kFs = 192000.0;

// Where the mode lift is read as a "plateau". High enough that both branches are well past their
// zeros, low enough to stay clear of Nyquist at kFs.
constexpr double kPlateauHz = 80000.0;

// The analytic source-degeneration shelf 1/k(s) = (1 + s*tau)/(K0 + s*tau) -- the same closed form
// JfetStageTest validates the stage against, repeated here so the chain's mode differential is
// checked against the circuit rather than against the stage's own output.
std::complex<double> analyticShelf(double freq, double tau, double k0)
{
    const std::complex<double> sTau { 0.0, 2.0 * M_PI * freq * tau };
    return (1.0 + sTau) / (k0 + sTau);
}

bool ok = true;

void fail(const char* msg)
{
    std::printf("  <-- FAIL: %s\n", msg);
    ok = false;
}

std::complex<double> chainResponse(pedal::dsp::EchoPreDsp& d, double freq, double amp)
{
    d.reset();
    return pedal::test::measureResponse(
        [&d](double x) { return d.processBase(d.processOversampled(x)); }, freq, kFs, amp);
}

double db(double x) { return 20.0 * std::log10(x); }
} // namespace

int main()
{
    using namespace pedal;
    const dsp::JfetParams params {};
    dsp::EchoPreDsp chain;
    chain.prepare(kFs, kFs);
    chain.setParams(params);
    chain.setVolume(0.65); // ~1-2 o'clock, near the network's gain peak

    // A small probe keeps the JFET in its small-signal region so these are linear measurements.
    constexpr double kProbe = 1.0e-4;

    // 1. OVERALL POLARITY. One common-source stage, no second inversion anywhere, so the pedal
    //    inverts. Measured in the midband where each stage's own phase shift is small.
    chain.setMode(dsp::Mode::Dark);
    const double midPhase = pedal::test::degrees(std::arg(chainResponse(chain, 300.0, kProbe)));
    std::printf("Overall polarity: midband phase %+.2f deg at 300 Hz\n", midPhase);
    std::printf("  (one common-source stage => the pedal INVERTS; near 0 would mean a stage pair cancelled)\n");
    if (std::abs(std::abs(midPhase) - 180.0) > 25.0)
        fail("chain is not inverting -- an even number of inversions somewhere");

    // 2. THE MODE DIFFERENTIAL, measured exactly as M1 will measure it on the renders: the ratio of
    //    two modes at the same settings. Rig gain, unit variance and level calibration all cancel.
    //
    //    The plateau must equal K0 = 1 + gm*R5 EXACTLY. That is the load-bearing check on the whole
    //    Norton architecture: the output network's transfer is mode-independent, so it divides out
    //    of the ratio and leaves only k(s). If the MODE lift had been applied as a voltage shelf AND
    //    the output network driven from an ideal source, it would be double-counted and this plateau
    //    would come out near K0^2 -- about 33 dB instead of 16.4.
    //
    //    ⚠ This ratio is a LINEAR measurement, and it passed unchanged while the stage was
    //    suppressing distortion by k instead of k^2 (JfetStageTest section 7). A correct mode
    //    differential is not evidence that the nonlinear path is right.
    const double k0 = 1.0 + params.gm * circuit::kR5;
    std::printf("\nMode differential re DARK (this is build-plan.md M1/M2):\n");
    std::printf("  K0 = 1 + gm*R5 = %.4f (%.2f dB) -- the ASYMPTOTE both ratios climb toward\n", k0, db(k0));

    struct Case { const char* name; dsp::Mode mode; double tau; double cornerHz; };
    // Corners from the MEASURED time constants (M1), not from R5*C: the fitted zeros sit ~7% below
    // the drawn ones in both units and both branches. See JfetParams.
    const Case cases[] = {
        { "Bright", dsp::Mode::Bright, params.tauBright, 1.0 / (2.0 * M_PI * params.tauBright) },
        { "Mid",    dsp::Mode::Mid,    params.tauMid,    1.0 / (2.0 * M_PI * params.tauMid) }
    };

    for (const auto& c : cases)
    {
        std::printf("  %s / Dark  (bypass corner %.0f Hz):\n", c.name, c.cornerHz);
        for (const double f : { 100.0, 1000.0, 5000.0, 20000.0, 60000.0 })
        {
            chain.setMode(dsp::Mode::Dark);
            const auto dark = chainResponse(chain, f, kProbe);
            chain.setMode(c.mode);
            const auto lifted = chainResponse(chain, f, kProbe);
            const auto ratio = lifted / dark;
            std::printf("    %6.0f Hz: %+6.2f dB, %+7.2f deg\n", f, db(std::abs(ratio)),
                        pedal::test::degrees(std::arg(ratio)));
        }

        // Well above the corner the branch bypasses R5 and the ratio climbs toward K0.
        //
        // ⚠ IT DOES NOT REACH K0 AT 80 kHz, AND THIS CHECK USED TO PRETEND IT DID. K0 is the shelf's
        // ASYMPTOTE; the analog shelf is still 0.10 dB (Bright) and 0.47 dB (Mid) short of it at
        // 80 kHz, because 1/k(s) approaches unity only as 1/f. Asserting "within 0.15 dB of K0"
        // therefore demanded an error, and it was satisfied by one: the shelf's old plain-bilinear
        // discretisation over-produced the top octave by almost exactly the amount the analog shelf
        // was short, so the two cancelled at 80 kHz to 0.06 dB. Fixing the discretisation broke this
        // assertion -- a correct model failing a test that a warped one passed.
        //
        // So compare against the analytic shelf AT 80 kHz, and keep printing the K0 asymptote
        // alongside. The failure mode this check exists for is unharmed: double-counting the drain
        // lift lands the ratio near K0^2, about 33 dB, which no tolerance here could absorb.
        chain.setMode(dsp::Mode::Dark);
        const double darkHf = std::abs(chainResponse(chain, kPlateauHz, kProbe));
        chain.setMode(c.mode);
        const double liftedHf = std::abs(chainResponse(chain, kPlateauHz, kProbe));
        const double wantDb = db(std::abs(analyticShelf(kPlateauHz, c.tau, k0)) * k0);
        const double plateauErrDb = std::abs(db(liftedHf / darkHf) - wantDb);
        std::printf("    at %.0f kHz: %+.3f dB vs analytic %+.3f dB (err %.3f dB; K0 asymptote %+.3f dB)\n",
                    kPlateauHz / 1000.0, db(liftedHf / darkHf), wantDb, plateauErrDb, db(k0));
        if (plateauErrDb > 0.10)
            fail("mode lift does not match the analytic 1/k(s) -- it is being double-counted or lost");
    }

    // 3. Full control sweep: every mode x the whole volume range, at a hot level, must stay finite.
    std::printf("\nControl sweep (all modes x volume, hot input) -- finiteness:\n");
    int checked = 0;
    for (const auto m : { dsp::Mode::Bright, dsp::Mode::Dark, dsp::Mode::Mid })
    {
        for (int i = 0; i <= 20; ++i)
        {
            chain.setMode(m);
            chain.setVolume((double) i / 20.0);
            chain.reset();
            for (int n = 0; n < 4096; ++n)
            {
                // 2 V peak is far hotter than any guitar, exercising the shaper's limits.
                const double y = chain.processBase(
                    chain.processOversampled(2.0 * std::sin(2.0 * M_PI * 220.0 * (double) n / kFs)));
                if (! std::isfinite(y))
                {
                    fail("non-finite output in the control sweep");
                    i = 21;
                    break;
                }
                ++checked;
            }
        }
    }
    std::printf("  %d samples across 63 control settings, all finite\n", checked);

    std::printf(ok ? "\nPASS: full chain\n" : "\nFAILED: full chain\n");
    return ok ? 0 : 1;
}
