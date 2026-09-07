// chowdsp_wdf smoke test (CLAUDE.md build sequence step 3): a trivial RC lowpass, confirming the
// library/toolchain wiring is correct before any pedal-specific DSP is written.
//
// Pure chowdsp_wdf console exe, no JUCE (build.md "Testing pattern"):
//   c++ -std=c++17 -O2 -I libs/chowdsp_wdf/include tests/WdfSmokeTest.cpp -o build/WdfSmokeTest
// or via the registered CMake/ctest target: cmake --build build --target WdfSmokeTest && ctest -R WdfSmokeTest
//
// Circuit: ideal voltage source -> R -> C -> GND, output taken across C (Vout/Vin = 1/(1+sRC)).
// fc = 1 kHz at fs = 48 kHz is far enough from Nyquist that chowdsp's bilinear (trapezoidal)
// capacitor's frequency warp is negligible (dsp.md "Top-octave accuracy"), so this checks the
// library wiring, not warp compensation.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <chowdsp_wdf/chowdsp_wdf.h>

using namespace chowdsp::wdft;

namespace
{
constexpr double kFs = 48000.0;
constexpr double kFc = 1000.0;
constexpr double kCapValue = 1.0e-6;
constexpr double kResValue = 1.0 / (2.0 * M_PI * kFc * kCapValue);

// Steady-state linear gain (Vout/Vin) of the RC lowpass at `freq`: drives a fresh circuit instance
// for one second and takes the peak capacitor voltage after the transient has settled.
double measureGainAt (double freq)
{
    CapacitorT<double> c1 (kCapValue, kFs);
    ResistorT<double> r1 (kResValue);
    auto s1 = makeSeries<double> (r1, c1);
    auto p1 = makeInverter<double> (s1);
    IdealVoltageSourceT<double, decltype (p1)> vs { p1 };

    c1.reset();
    double magnitude = 0.0;
    const int nSamples = (int) kFs;
    for (int n = 0; n < nSamples; ++n)
    {
        vs.setVoltage (std::sin (2.0 * M_PI * freq * (double) n / kFs));
        vs.incident (p1.reflected());
        p1.incident (vs.reflected());

        const auto y = voltage<double> (c1);
        if (n > nSamples / 4) // discard the settling transient
            magnitude = std::max (magnitude, std::abs (y));
    }
    return magnitude;
}
} // namespace

int main()
{
    bool ok = true;

    // Bisect for the actual -3 dB corner and confirm it lands within 1% of the design fc
    // (CLAUDE.md step 3: "confirm the -3 dB point within 1%").
    const double target = 1.0 / std::sqrt (2.0); // -3 dB in linear gain
    double lo = 10.0, hi = 20000.0;
    for (int i = 0; i < 40; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        (measureGainAt (mid) > target ? lo : hi) = mid;
    }
    const double measuredFc = 0.5 * (lo + hi);
    const double fcErrPct = std::abs (measuredFc - kFc) / kFc * 100.0;

    std::printf ("RC lowpass: design fc = %.1f Hz, measured -3 dB point = %.2f Hz (%.3f%% error)\n",
                 kFc, measuredFc, fcErrPct);
    if (fcErrPct > 1.0)
    {
        std::printf ("FAIL: -3 dB point off by more than 1%%\n");
        ok = false;
    }

    // Sanity-check the surrounding shape (+/-1 octave), matching a first-order lowpass.
    auto checkOctave = [&] (double mult, double expectedDb, double marginDb)
    {
        const double gainDb = 20.0 * std::log10 (measureGainAt (kFc * mult));
        std::printf ("  %.1fx fc: %.2f dB (expected %.2f dB)\n", mult, gainDb, expectedDb);
        if (std::abs (gainDb - expectedDb) > marginDb)
        {
            std::printf ("FAIL: gain outside expected margin\n");
            ok = false;
        }
    };
    checkOctave (0.5, -1.0, 0.2);
    checkOctave (1.0, -3.0, 0.2);
    checkOctave (2.0, -7.0, 0.2);

    std::printf (ok ? "PASS: chowdsp_wdf RC lowpass smoke test\n" : "FAILED\n");
    return ok ? 0 : 1;
}
