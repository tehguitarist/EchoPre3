// Stage 1 validation (CLAUDE.md build sequence step 4, "Linear stages: frequency response vs
// expected transfer function"). Pure chowdsp_wdf console exe, no JUCE.
//
// Checks the WDF input network against the analytic transfer function of the same R/C values, and
// the analytic function against the corners circuit.md derives independently. Both directions
// matter: the first catches a mis-wired tree, the second catches a mis-transcribed value.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>

#include "dsp/InputNetwork.h"

#include "MeasureUtils.h"

namespace
{
// The stage runs inside the oversampled region (see InputNetwork.h), so it is validated at the
// shipped 4x default. The base-rate case is reported separately at the end as the 1x-OS limitation.
constexpr double kOsRate = 192000.0;
constexpr double kBaseRate = 48000.0;
using cplx = std::complex<double>;

// V_G / V_IN for  IN -[R3]- A -[C4]- G, with C3 shunting A and R4 shunting G. Lives in InputNetwork.h
// now rather than here, because OsDroopRestore derives the base-rate droop from it and two copies of
// a transfer function is exactly the kind of duplication that goes stale silently. The checks below
// are unchanged and still bidirectional: the WDF tree against this closed form, and this closed form
// against the corners circuit.md derives independently.
cplx analyticResponse(double freq) { return pedal::dsp::InputNetwork::analyticResponse(freq); }

std::complex<double> measureResponseAt(pedal::dsp::InputNetwork& net, double freq, double fs)
{
    net.reset();
    return pedal::test::measureResponse([&net](double x) { return net.processSample(x); }, freq, fs);
}

double measureGainAt(pedal::dsp::InputNetwork& net, double freq, double fs)
{
    return std::abs(measureResponseAt(net, freq, fs));
}

// Bisect `fn` for the frequency where it crosses `target`, over a bracket known to contain it.
template <typename Fn>
double findCrossing(Fn fn, double lo, double hi, double target, bool risingWithFreq)
{
    for (int i = 0; i < 60; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        const bool above = fn(mid) > target;
        ((above == risingWithFreq) ? hi : lo) = mid;
    }
    return 0.5 * (lo + hi);
}

bool ok = true;

void checkDb(const char* what, double got, double expected, double tolDb)
{
    const double errDb = std::abs(20.0 * std::log10(got / expected));
    std::printf("  %-30s %9.4f  (expected %9.4f, %.3f dB)%s\n", what, got, expected, errDb,
                errDb > tolDb ? "   <-- FAIL" : "");
    if (errDb > tolDb)
        ok = false;
}

void checkHz(const char* what, double got, double expected, double tolPct)
{
    const double errPct = std::abs(got - expected) / expected * 100.0;
    std::printf("  %-30s %8.1f Hz (expected %8.1f Hz, %.2f%%)%s\n", what, got, expected, errPct,
                errPct > tolPct ? "   <-- FAIL" : "");
    if (errPct > tolPct)
        ok = false;
}
} // namespace

int main()
{
    pedal::dsp::InputNetwork net;
    net.prepare(kOsRate);

    // 1. The WDF tree against the analytic transfer function of the same components, at the rate the
    //    stage actually runs at. Tight everywhere in the audio band: oversampling removes the warp.
    //    A little residual bilinear warp survives above the pinned corner even at 4x (it shrinks with
    //    the OS factor), so the top octave gets a wider band than the rest -- 0.15 dB at 12 kHz, an
    //    order of magnitude inside the unit-to-unit spread that will set the project's pass/fail band.
    //
    //    MAGNITUDE AND PHASE BOTH. Phase is not decoration here: step 9 validates against the
    //    reference renders with a sub-sample NULL, which collapses on a phase error even when the
    //    magnitude response is perfect. Phase also catches a class of bug magnitude hides -- see the
    //    excess-delay column and the guard below it.
    std::printf("WDF vs analytic transfer function (at %.0f kHz, the 4x running rate):\n", kOsRate / 1000.0);
    std::printf("  %-12s %-26s %-28s %s\n", "freq", "magnitude", "phase (deg)", "excess delay");
    double worstDelay = 0.0;
    for (const auto f : { 20.0, 100.0, 440.0, 1000.0, 3000.0, 7300.0, 12000.0, 18000.0 })
    {
        const auto got = measureResponseAt(net, f, kOsRate);
        const auto want = analyticResponse(f);
        const double magErrDb = std::abs(20.0 * std::log10(std::abs(got) / std::abs(want)));
        const double phErrDeg = pedal::test::phaseErrorDeg(std::arg(got), std::arg(want));
        const double delaySmp = pedal::test::excessDelaySamples(phErrDeg, f, kOsRate);
        worstDelay = std::max(worstDelay, std::abs(delaySmp));

        const double magTol = (f > 8000.0) ? 0.25 : 0.05;
        const double phTol = (f > 8000.0) ? 1.5 : 0.5;
        const bool bad = magErrDb > magTol || std::abs(phErrDeg) > phTol;
        std::printf("  %7.0f Hz   %7.4f (want %7.4f)   %+8.3f (want %+8.3f)   %+.4f smp%s\n", f,
                    std::abs(got), std::abs(want), pedal::test::degrees(std::arg(got)),
                    pedal::test::degrees(std::arg(want)), delaySmp, bad ? "   <-- FAIL" : "");
        if (bad)
            ok = false;
    }

    // The half-sample-delay guard. dsp.md warns that reconstructing a node voltage from a SOURCE
    // port mixes Vs[n] with Vs[n-1] -- a half-sample delay whose magnitude signature is a gently
    // drooping top end, indistinguishable by eye from bilinear warp. Its PHASE signature is not
    // subtle: a constant excess delay of ~0.5 samples across the band. This stage reads voltage
    // across R4, a passive port, so the excess delay must stay near zero at every frequency.
    std::printf("  worst excess delay across the band: %.4f samples (a source-port read would be ~0.5)\n",
                worstDelay);
    if (worstDelay > 0.05)
    {
        std::printf("  <-- FAIL: excess delay suggests a node voltage is being read from a source port\n");
        ok = false;
    }

    // 2. The analytic function against circuit.md's derived figures. This is the transcription
    //    check -- it fails if a component value here disagrees with the schematic trace.
    //
    //    Reference the passband at 200 Hz, not 1 kHz: C3's reactance is only ~723 k at 1 kHz, so it
    //    already loads R4 there and the "passband" reads ~0.08 dB low.
    std::printf("\nAnalytic response vs circuit.md's derived figures:\n");
    const double passband = std::abs(analyticResponse(200.0));
    using namespace pedal::circuit;
    checkDb("passband R4/(R3+R4)", passband, kR4 / (kR3 + kR4), 0.05);

    // The HP corner is set by C4 into R4 PLUS the source resistance R3 in the loop. circuit.md
    // quotes 7.2 Hz for the unloaded C4/R4 pair; loaded by R3 the real corner is ~6.5 Hz.
    const double hpPredicted = 1.0 / (2.0 * M_PI * (kR3 + kR4) * kC4);
    const double lpPredicted = 1.0 / (2.0 * M_PI * ((kR3 * kR4) / (kR3 + kR4)) * kC3);
    const double target = passband / std::sqrt(2.0);
    auto analyticMag = [](double f) { return std::abs(analyticResponse(f)); };
    checkHz("HP corner (C4 into R3+R4)", findCrossing(analyticMag, 1.0, 100.0, target, true), hpPredicted, 2.0);
    checkHz("LP corner (R3||R4 with C3)", findCrossing(analyticMag, 1000.0, 20000.0, target, false), lpPredicted, 2.0);
    std::printf("  (circuit.md quotes the UNLOADED C4/R4 corner as 7.2 Hz; including R3 gives %.2f Hz)\n",
                hpPredicted);

    // 3. Prewarp did its job: the WDF tree's own -3 dB point lands on the analog corner rather than
    //    ~6.6% low, where an un-prewarped bilinear transform would put it.
    std::printf("\nPrewarp check -- WDF -3 dB point vs the analog LP corner:\n");
    auto wdfMag = [&net](double f) { return measureGainAt(net, f, kOsRate); };
    checkHz("WDF LP corner", findCrossing(wdfMag, 1000.0, 20000.0, target, false), lpPredicted, 1.0);

    // 4. The base-rate (1x oversampling) top octave, reported not asserted. Prewarp pins the corner
    //    exactly but cannot invert the bilinear transform's zero at Nyquist, so a droop remains and
    //    grows toward it. This is the documented, bounded 1x limitation and the reason the stage was
    //    moved inside the oversampled region; dsp.md's low-OS shelf restore is the further remedy
    //    if it ever needs to sound right at 1x (build step 6, not now).
    pedal::dsp::InputNetwork baseNet;
    baseNet.prepare(kBaseRate);
    std::printf("\nBase-rate (1x OS) residual droop vs analog -- INFORMATIONAL:\n");
    for (const auto f : { 3000.0, 7300.0, 12000.0, 16000.0 })
        std::printf("  %5.0f Hz: %+6.2f dB\n", f,
                    20.0 * std::log10(measureGainAt(baseNet, f, kBaseRate) / std::abs(analyticResponse(f))));

    std::printf(ok ? "\nPASS: input network\n" : "\nFAILED: input network\n");
    return ok ? 0 : 1;
}
