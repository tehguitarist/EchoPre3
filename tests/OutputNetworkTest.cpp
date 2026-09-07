// Stage 3 validation (CLAUDE.md build sequence step 4). Pure chowdsp_wdf console exe, no JUCE.
//
// The load-bearing assertion is that the network reproduces circuit.md's own published control-law
// table -- in particular that it is NON-MONOTONIC: silence at full CCW, a peak at Ra = 176 k, then a
// 3.9 dB fall-back to full CW. That shape is correct EP-3 wiring, not a schematic error, so a future
// session "fixing" it into a conventional divider will fail here.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

#include "dsp/OutputNetwork.h"

#include "MeasureUtils.h"

namespace
{
constexpr double kFs = 48000.0;
constexpr double kDrainZ = 20.0e3; // the drive impedance circuit.md's table is computed at
using cplx = std::complex<double>;

// V_OUT / V_SRC for a source behind rSrc feeding node D. Norton-equivalent to a current source of
// V_SRC/rSrc in parallel with rSrc, which is how OutputNetwork is actually driven.
cplx analyticResponse(double freq, double ra, double rSrc)
{
    using namespace pedal::circuit;
    const cplx s { 0.0, 2.0 * M_PI * freq };
    const double rb = kVolumePot - ra;
    const double rChain = kR9 + kR8 + rb; // E -> GND via R9, R8, Rb with OUT tapped inside

    const double yE = 1.0 / kR10 + 1.0 / ra + 1.0 / rChain;
    const cplx zE = 1.0 / yE;
    const cplx zC10 = 1.0 / (s * kC10);

    const cplx zBranch = zC10 + zE;
    const cplx vD = (cplx(rSrc) * zBranch) / (rSrc + zBranch) / cplx(rSrc); // per amp of Norton current
    const cplx vE = vD * (zE / zBranch);
    return vE * cplx((kR8 + rb) / rChain);
}

std::complex<double> measureResponseAt(pedal::dsp::OutputNetwork& net, double freq, double volumeX)
{
    net.reset();
    net.setVolume(volumeX);
    // Drive with the Norton current a 1 V source behind kDrainZ would deliver, so the result is
    // directly comparable with circuit.md's voltage-behind-20k control-law table.
    return pedal::test::measureResponse([&net](double v) { return net.processSample(v / kDrainZ); }, freq, kFs);
}

double measureGainAt(pedal::dsp::OutputNetwork& net, double freq, double volumeX)
{
    return std::abs(measureResponseAt(net, freq, volumeX));
}

// Inverse of the shipped taper: which knob position puts the pot's lower arm at `ra`.
double volumeXforRa(double ra)
{
    return std::pow(ra / pedal::circuit::kVolumePot, 1.0 / pedal::circuit::kVolumeTaperP);
}

bool ok = true;

void check(const char* what, double gotDb, double expectedDb, double tolDb)
{
    const double err = std::abs(gotDb - expectedDb);
    std::printf("  %-30s %8.2f dB  (expected %8.2f dB)%s\n", what, gotDb, expectedDb,
                err > tolDb ? "   <-- FAIL" : "");
    if (err > tolDb)
        ok = false;
}

double db(double x) { return 20.0 * std::log10(x); }
} // namespace

int main()
{
    pedal::dsp::OutputNetwork net;
    net.prepare(kFs);
    net.setDrainImpedance(kDrainZ);

    // 1. The WDF tree against the analytic transfer function of the same components, across the
    //    band and at several volume settings. This is what catches a mis-wired tree.
    //    MAGNITUDE AND PHASE BOTH. Phase carries the sign convention -- a network wired 180 degrees
    //    out has an identical magnitude response, and the step-9 null against the reference renders
    //    is where that would finally surface. C10's high-pass also puts real, volume-dependent phase
    //    lead in the bottom octaves, so this is a genuine check on the coupled solve, not a formality.
    std::printf("WDF vs analytic transfer function (magnitude and phase):\n");
    double worstDelay = 0.0;
    for (const double ra : { 50.0e3, 176.0e3, 350.0e3, 500.0e3 })
    {
        const double x = volumeXforRa(ra);
        for (const double f : { 50.0, 440.0, 2000.0, 10000.0 })
        {
            const auto got = measureResponseAt(net, f, x);
            const auto want = analyticResponse(f, ra, kDrainZ);
            const double magErrDb = std::abs(db(std::abs(got)) - db(std::abs(want)));
            const double phErrDeg = pedal::test::phaseErrorDeg(std::arg(got), std::arg(want));
            worstDelay = std::max(worstDelay, std::abs(pedal::test::excessDelaySamples(phErrDeg, f, kFs)));

            const bool bad = magErrDb > 0.1 || std::abs(phErrDeg) > 0.5;
            std::printf("  %3.0fk / %5.0f Hz   %8.2f dB (want %8.2f)   %+8.3f deg (want %+8.3f)%s\n",
                        ra / 1000.0, f, db(std::abs(got)), db(std::abs(want)),
                        pedal::test::degrees(std::arg(got)), pedal::test::degrees(std::arg(want)),
                        bad ? "   <-- FAIL" : "");
            if (bad)
                ok = false;
        }
    }

    // Sign convention, stated as a physical fact rather than left implicit: this network is passive
    // and driven by a current injected INTO node D, so a positive injected current must produce a
    // positive output voltage. The stage that inverts is the JFET, and it must be the ONLY one --
    // two accidental inversions cancel and read as correct on any magnitude-only test.
    const double midbandPhase = pedal::test::degrees(std::arg(measureResponseAt(net, 2000.0, volumeXforRa(176.0e3))));
    std::printf("  midband phase %+.3f deg -- passive network, must be near 0, NOT near 180\n", midbandPhase);
    if (std::abs(midbandPhase) > 2.0)
    {
        std::printf("  <-- FAIL: output network is inverting; it is passive and must not be\n");
        ok = false;
    }

    std::printf("  worst excess delay: %.4f samples (a source-port read would be ~0.5)\n", worstDelay);
    if (worstDelay > 0.05)
    {
        std::printf("  <-- FAIL: excess delay suggests V(OUT) is being read from a source port\n");
        ok = false;
    }

    // 2. circuit.md's published control-law table, midband (C10 is a short well below 1% here).
    //    These are the numbers the whole non-monotonic reading of this network rests on.
    std::printf("\ncircuit.md control-law table (midband, %.0f k drive):\n", kDrainZ / 1000.0);
    struct Point { double ra; double expectedDb; };
    const Point table[] = { { 10.0e3, -11.3 }, { 25.0e3, -7.1 }, { 50.0e3, -5.2 },  { 100.0e3, -4.1 },
                            { 176.0e3, -3.79 }, { 300.0e3, -4.2 }, { 400.0e3, -5.2 }, { 500.0e3, -7.7 } };
    for (const auto& p : table)
        check((std::to_string((int) (p.ra / 1000.0)) + "k").c_str(),
              db(std::abs(analyticResponse(5000.0, p.ra, kDrainZ))), p.expectedDb, 0.1);

    // 3. The shape itself: a genuine interior maximum near Ra = 176 k, and a fall-back to full CW.
    //    Asserted structurally rather than by trusting the table cells above.
    std::printf("\nNon-monotonicity (this network is SUPPOSED to peak and fall back):\n");
    double bestRa = 0.0, bestDb = -1.0e9;
    for (int i = 1; i <= 1000; ++i)
    {
        const double ra = pedal::circuit::kVolumePot * (double) i / 1000.0;
        const double d = db(std::abs(analyticResponse(5000.0, ra, kDrainZ)));
        if (d > bestDb) { bestDb = d; bestRa = ra; }
    }
    const double fullCwDb = db(std::abs(analyticResponse(5000.0, pedal::circuit::kVolumePot, kDrainZ)));
    std::printf("  peak %.2f dB at Ra = %.0f k (%.1f%% of pot); fall-back to full CW = %.2f dB\n",
                bestDb, bestRa / 1000.0, bestRa / pedal::circuit::kVolumePot * 100.0, bestDb - fullCwDb);
    if (std::abs(bestRa - 176.0e3) > 10.0e3) { std::printf("  <-- FAIL: peak not at Ra ~ 176 k\n"); ok = false; }
    if (std::abs((bestDb - fullCwDb) - 3.92) > 0.15) { std::printf("  <-- FAIL: fall-back not ~3.92 dB\n"); ok = false; }

    // 4. Full CCW must be silence -- Ra -> 0 genuinely shorts node E to ground. Measured through the
    //    WDF at one frequency against the WDF at the peak setting, so the two are directly comparable.
    const double ccwDb = db(measureGainAt(net, 1000.0, 0.0));
    const double wdfPeakDb = db(measureGainAt(net, 1000.0, volumeXforRa(bestRa)));
    std::printf("  full CCW: %.1f dB re peak%s\n", ccwDb - wdfPeakDb,
                (ccwDb - wdfPeakDb > -80.0) ? "   <-- FAIL" : "");
    if (ccwDb - wdfPeakDb > -80.0)
        ok = false;

    // 5. C10's high-pass corner moves WITH the volume setting -- lowest (~13 Hz) near the top of the
    //    range, climbing to ~54 Hz wound down. Reported, not asserted to a tight tolerance: the point
    //    is that it is not a fixed corner, which is why it is solved inside the network.
    std::printf("\nC10 high-pass corner vs volume setting (circuit.md stage 3 table):\n");
    for (const double ra : { 10.0e3, 100.0e3, 300.0e3, 500.0e3 })
    {
        const double ref = std::abs(analyticResponse(2000.0, ra, kDrainZ));
        double lo = 1.0, hi = 500.0;
        for (int i = 0; i < 50; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (std::abs(analyticResponse(mid, ra, kDrainZ)) < ref / std::sqrt(2.0) ? lo : hi) = mid;
        }
        std::printf("  Ra = %3.0f k -> %5.1f Hz\n", ra / 1000.0, 0.5 * (lo + hi));
    }

    // 6. The VOLUME taper, reported against the maker's four published points. NOT asserted: these
    //    are rounded marketing copy, and circuit.md is explicit that the ~2 dB fall-back discrepancy
    //    is to be settled by a real VOLUME sweep capture, not closed by tuning other constants.
    //    Printed so the open question stays visible every time the suite runs.
    std::printf("\nVOLUME taper (p = %.2f) vs the maker's published points -- INFORMATIONAL:\n",
                pedal::circuit::kVolumeTaperP);
    struct Knob { const char* pos; double rot; const char* claim; };
    const Knob knobs[] = { { "10-11 o'clock", 0.35, "unity" },
                           { "1-2 o'clock",   0.65, "+3 dB, maximum" },
                           { "3-5 o'clock",   0.90, "+1 to +2 dB" } };
    const double peakDb = bestDb;
    for (const auto& k : knobs)
    {
        const double ra = pedal::taper::powerLawTaper(k.rot, pedal::circuit::kVolumePot, pedal::circuit::kVolumeTaperP);
        const double d = db(std::abs(analyticResponse(5000.0, ra, kDrainZ)));
        std::printf("  %-14s (%.0f%% rot, Ra = %3.0f k): %6.2f dB, i.e. %+5.2f dB re peak  [maker: %s]\n",
                    k.pos, k.rot * 100.0, ra / 1000.0, d, d - peakDb, k.claim);
    }
    std::printf("  Peak of the taper-mapped curve lands at %.0f%% rotation.\n",
                volumeXforRa(bestRa) * 100.0);

    std::printf(ok ? "\nPASS: output / VOLUME network\n" : "\nFAILED: output / VOLUME network\n");
    return ok ? 0 : 1;
}
