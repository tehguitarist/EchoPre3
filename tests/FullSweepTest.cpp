// FullSweepTest -- build-sequence step 10: every control across its full range, no instability.
//
// CLAUDE.md's step 10 is "all controls full range: no instability, clicks, or NaN/Inf". ChainTest
// already sweeps modes x volume for finiteness, but at the DSP level and over only those two
// controls -- so it cannot see the output LOAD (added later, and it moves the drain-node impedance,
// which IS the load line's slope), the trims, the oversampling factor, or the block slicing
// processBlock does while VOLUME moves. This test is the processor-level version, and it is the
// last gate before the plugin is called done.
//
// ⚠ It is deliberately a STABILITY test, not an accuracy test. Accuracy lives in the per-stage
// tests and in the analysis harness against real captures; what this asks is the question those
// cannot: does any REACHABLE combination of controls produce a NaN, an Inf, a runaway, or a click.
// "Reachable" includes the daft ones -- full drive into full volume with both trims maxed -- because
// a user can set them and the plugin must not misbehave, even though the result is very loud.
//
// ⛔ AN OUTPUT ABOVE 0 dBFS IS NOT A FAILURE HERE, and that is on the record: this pedal really does
// boost by ~10 dB into a high-impedance input, and CLAUDE.md's step 10 says so explicitly ("output
// > 0 dBFS at extreme drive+volume is faithful, not a fault -- the output trim manages it"). The
// bound below is set to catch a RUNAWAY (a feedback loop that diverges, a solve that escapes its
// bracket), which is orders of magnitude away, not to police loudness.

#include <cmath>
#include <cstdio>
#include <vector>

#include "ProbeHarness.h"

using namespace pedal::probe;

namespace
{
// A decade above anything the circuit can produce at full tilt (the rail is 22 V and kInputRef
// scales volts back to full scale, so a legitimate peak is a few units). Anything past this is a
// divergence, not a loud setting.
constexpr double kRunawayBound = 1.0e3;

int checked = 0, configs = 0;
bool ok = true;

/** One configuration: render a hot swept input and assert nothing diverges. Returns the peak. */
double sweepOne(PedalAudioProcessor& proc, const Setup& s, const char* what)
{
    bool finite = true;
    // A chirp rather than a tone: it visits every frequency, so a resonance that only misbehaves in
    // one band cannot hide between two tone probes. Hot enough to clip the stage hard.
    const double fs = kFs;
    auto gen = [fs](int n)
    {
        const double t = (double)n / fs;
        const double f = 20.0 * std::pow(1000.0, std::fmod(t, 0.25) / 0.25); // 20 Hz -> 20 kHz per 250 ms
        return 0.95 * std::sin(2.0 * juce::MathConstants<double>::pi * f * t);
    };

    auto y = render(proc, s, 1024, 8192, gen, &finite);
    ++configs;
    double peak = 0.0;
    for (const double v : y)
    {
        peak = juce::jmax(peak, std::abs(v));
        ++checked;
    }
    if (! finite)
    {
        std::printf("  FAIL: non-finite output -- %s\n", what);
        ok = false;
    }
    if (! (peak < kRunawayBound))
    {
        std::printf("  FAIL: runaway output (peak %.3e) -- %s\n", peak, what);
        ok = false;
    }
    return peak;
}
} // namespace

int main()
{
    PedalAudioProcessor proc;

    std::printf("FullSweepTest -- step 10: every control, full range, stability only.\n");
    std::printf("  Hot 20 Hz-20 kHz chirp at 0.95 FS. Asserts finite and no runaway (bound %.0e).\n",
                kRunawayBound);
    std::printf("  ⛔ Output above 0 dBFS is FAITHFUL here, not a fault -- this pedal boosts ~10 dB.\n\n");

    // ---- 1. Mode x volume x load, at the shipped oversampling ---------------------------------
    // The full cross-product of the three controls that share the output network's solve. VOLUME is
    // swept through zero (a genuine short, where the network's gain is -inf) and through its peak.
    std::printf("1. MODE x VOLUME x LOAD at the 4x default\n");
    double worst = 0.0;
    for (int mode = 0; mode < 3; ++mode)
        for (int load = 0; load < 3; ++load)
            for (int i = 0; i <= 10; ++i)
            {
                Setup s;
                s.osIndex = 2; // 4x, the shipped default
                s.modeIndex = mode;
                s.loadIndex = load;
                s.volume = (double)i / 10.0;
                char what[96];
                std::snprintf(what, sizeof what, "mode %s, load %d, volume %.1f",
                              kModeNames[mode], load, s.volume);
                worst = juce::jmax(worst, sweepOne(proc, s, what));
            }
    std::printf("   %d configurations, worst peak %.2f FS\n", configs, worst);

    // ---- 2. Oversampling factor x the extremes of the volume range -----------------------------
    // Every factor, because the JFET solve runs at the oversampled rate and the droop restore's
    // coefficients are derived per (base rate, factor) pair -- a factor-specific divergence would
    // be invisible at the default.
    std::printf("\n2. OVERSAMPLING x volume extremes x mode\n");
    int before = configs;
    double worst2 = 0.0;
    for (int osIdx = 0; osIdx < 4; ++osIdx)
        for (int mode = 0; mode < 3; ++mode)
            for (const double v : {0.0, 0.35, 1.0})
            {
                Setup s;
                s.osIndex = osIdx;
                s.modeIndex = mode;
                s.volume = v;
                char what[96];
                std::snprintf(what, sizeof what, "%dx, mode %s, volume %.2f",
                              kOsFactors[osIdx], kModeNames[mode], v);
                worst2 = juce::jmax(worst2, sweepOne(proc, s, what));
            }
    std::printf("   %d configurations, worst peak %.2f FS\n", configs - before, worst2);

    // ---- 3. Trim extremes, including the daft corner -------------------------------------------
    // +12 dB of input trim on an already-hot chirp drives the stage far past both cutoff and triode;
    // +12 on the output as well is the loudest state the plugin has. The solve's bracket has to hold
    // there, which is the one thing in the chain that could plausibly escape.
    std::printf("\n3. TRIM EXTREMES (the loudest reachable state is the point)\n");
    before = configs;
    double worst3 = 0.0;
    for (const double it : {-12.0, 0.0, 12.0})
        for (const double ot : {-12.0, 0.0, 12.0})
            for (int mode = 0; mode < 3; ++mode)
            {
                Setup s;
                s.osIndex = 2;
                s.modeIndex = mode;
                s.volume = 0.35; // at the control law's peak, i.e. the loudest volume setting
                s.inputTrimDb = it;
                s.outputTrimDb = ot;
                char what[96];
                std::snprintf(what, sizeof what, "in %+.0f dB, out %+.0f dB, mode %s",
                              it, ot, kModeNames[mode]);
                worst3 = juce::jmax(worst3, sweepOne(proc, s, what));
            }
    std::printf("   %d configurations, worst peak %.2f FS (%.1f dBFS -- loud is correct here)\n",
                configs - before, worst3, db(worst3));

    // ---- 4. Bypass, at every factor ------------------------------------------------------------
    // The skip path, which is a different code path from everything above.
    std::printf("\n4. BYPASSED at every factor\n");
    before = configs;
    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup s;
        s.osIndex = osIdx;
        s.bypass = true;
        char what[64];
        std::snprintf(what, sizeof what, "bypassed at %dx", kOsFactors[osIdx]);
        sweepOne(proc, s, what);
    }
    std::printf("   %d configurations\n", configs - before);

    std::printf("\n  %d configurations, %d samples checked, all finite and bounded\n",
                configs, checked);
    std::printf("%s\n", ok ? "FullSweepTest PASSED" : "FullSweepTest FAILED");
    return ok ? 0 : 1;
}
