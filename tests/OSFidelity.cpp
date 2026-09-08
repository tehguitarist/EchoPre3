// OSFidelity -- how close the low oversampling factors are to the high one (build.md "Performance &
// fidelity probes"). This is the common DAW case: most sessions never leave the default, and 1x is
// where a CPU-bound project ends up.
//
// It separates the three things that all read as "1x sounds different" but have different fixes:
//
//   1. TOP-OCTAVE DROOP -- the bilinear zero near Nyquist in the oversampled stages' caps. A pure
//      discretisation artefact, and the target of dsp.md's low-OS shelf restore.
//   2. ALIASING -- the shaper's harmonics folding back. Only a higher factor (or ADAA) moves it.
//   3. WANTED DISTORTION -- the harmonics that belong there. Must NOT change with the factor; if it
//      does, the oversampling selector has become a voicing control, which it must never be.
//
// ⚠ It used to A/B ADAA at every factor as well. ADAA was removed with the device-model rewrite --
// the map is now 2-D (overdrive and drain-source voltage), so ADAA1's derivation does not apply. See
// PluginProcessor.h. What remains is the OS factor's own effect, which is what this test is for.
//
// Registered with add_test() as a FINITE-ONLY probe: it asserts no NaN/Inf and that the wanted
// distortion is factor-independent. It deliberately does NOT gate on an absolute alias or droop
// figure -- those numbers are the measurement this probe exists to report, and freezing one as a
// threshold would turn a report into a tautology.

#include <cstdio>

#include "ProbeHarness.h"

using namespace pedal::probe;

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    PedalAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, kBlock);

    bool finite = true, ok = true;

    // A small drive for every response measurement: at -34 dBFS the shaper's quadratic term is
    // ~1e-4 relative, so these tables are the linear response and nothing else.
    constexpr double kSmall = 0.02;
    const double frs[] = {4000.0, 8000.0, 10000.0, 12000.0, 14000.0, 16000.0, 18000.0};
    constexpr int kNumFr = (int)(sizeof(frs) / sizeof(frs[0]));
    constexpr double kRefHz = 1000.0;

    // ---- 1. Top-octave droop, referenced to 8x with ADAA off -- the closest this model comes to
    //         the analog response. Each row is normalised at 1 kHz first, so what is tabulated is
    //         SHAPE: a broadband level difference between factors would be a different bug.
    std::printf("========== 1. TOP-OCTAVE DROOP vs 8x, ADAA off (Dark, dB re 1 kHz) ==========\n");
    std::printf("Negative = darker than 8x. This is what a low-OS shelf restore would have to undo.\n\n");
    std::printf("  %-14s", "factor");
    for (int i = 0; i < kNumFr; ++i)
        std::printf("%8.0f", frs[i]);
    std::printf("\n");

    double ref8x[kNumFr];
    {
        Setup s;
        s.osIndex = 3;
        for (int i = 0; i < kNumFr; ++i)
            ref8x[i] = relativeResponseDb(proc, s, frs[i], kRefHz, kSmall);
    }

    for (int osIdx = 0; osIdx < 4; ++osIdx)
        {
            Setup s;
            s.osIndex = osIdx;
            std::printf("  %2dx %-10s", kOsFactors[osIdx], "");
            for (int i = 0; i < kNumFr; ++i)
            {
                const double d = relativeResponseDb(proc, s, frs[i], kRefHz, kSmall) - ref8x[i];
                std::printf("%8.2f", d);
                if (! std::isfinite(d))
                    finite = false;
            }
            std::printf("\n");
        }

    // ---- 2. Aliasing against wanted distortion, hot. Full scale at +12 dB of input trim drives the
    //         shaper far past anything a guitar does, which is where a low factor is at its worst
    //         and where the ADAA question actually has to be settled.
    constexpr int kToneBin = 3413; // 4999.6 Hz -- exactly on a bin at this FFT size
    std::printf("\n========== 2. ALIASING vs WANTED DISTORTION (Dark, full scale +12 dB trim) ==========\n");
    std::printf("Wanted = harmonics under Nyquist; alias = every other bin. Wanted must not move.\n\n");
    std::printf("  %-14s %10s %10s %10s\n", "factor", "H2 dBc", "wanted dBc", "alias dBc");

    double wantedRef = 0.0;
    for (int osIdx = 0; osIdx < 4; ++osIdx)
        {
            Setup s;
            s.osIndex = osIdx;
            s.inputTrimDb = 12.0;
            const auto sp = toneSpectrum(proc, s, 1.0, kToneBin, &finite);
            std::printf("  %2dx %-10s %10.2f %10.2f %10.2f\n", kOsFactors[osIdx], "", sp.h2Dbc,
                        sp.wantedThdDbc, sp.aliasDbc);
            if (osIdx == 3)
                wantedRef = sp.wantedThdDbc;
            if (osIdx == 0 && ! std::isfinite(sp.aliasDbc))
                finite = false;
        }

    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup s;
        s.osIndex = osIdx;
        s.inputTrimDb = 12.0;
        const auto sp = toneSpectrum(proc, s, 1.0, kToneBin, &finite);
        if (std::abs(sp.wantedThdDbc - wantedRef) > 1.0)
        {
            std::printf("  <-- FAIL: %dx moves the WANTED harmonics %.2f dB vs 8x -- the factor is voicing\n",
                        kOsFactors[osIdx], sp.wantedThdDbc - wantedRef);
            ok = false;
        }
    }

    // ---- 3. The same at a level a guitar actually reaches. The gate has to be decided for how the
    //         pedal is used, not only for its worst case.
    std::printf("\n========== 3. THE SAME AT A REALISTIC LEVEL (-6 dBFS, 0 dB trim) ==========\n");
    std::printf("  %-14s %10s %10s %10s\n", "factor", "H2 dBc", "wanted dBc", "alias dBc");
    for (int osIdx = 0; osIdx < 4; ++osIdx)
        {
            Setup s;
            s.osIndex = osIdx;
            const auto sp = toneSpectrum(proc, s, 0.5, kToneBin, &finite);
            std::printf("  %2dx %-10s %10.2f %10.2f %10.2f\n", kOsFactors[osIdx], "", sp.h2Dbc,
                        sp.wantedThdDbc, sp.aliasDbc);
        }

    // ---- 4. Is the 1x droop mode-independent? dsp.md's low-OS shelf restore assumes it is -- it was
    //         written for a build whose HF caps all sat downstream of the nonlinearity. Here the
    //         JFET's own 1/k(s) shelf lives INSIDE the oversampled region and its pole moves with
    //         MODE, so this table is what decides whether one fixed shelf could ever work.
    std::printf("\n========== 4. IS THE 1x DROOP MODE-INDEPENDENT? (dB re 8x, normalised at 1 kHz) ==========\n");
    std::printf("  %-10s", "mode");
    for (int i = 0; i < kNumFr; ++i)
        std::printf("%8.0f", frs[i]);
    std::printf("\n");

    double spread[kNumFr] = {0.0};
    double lo[kNumFr], hi[kNumFr];
    for (int m = 0; m < 3; ++m)
    {
        Setup ref;
        ref.osIndex = 3;
        ref.modeIndex = m;
        Setup one;
        one.osIndex = 0;
        one.modeIndex = m;
        std::printf("  %-10s", kModeNames[m]);
        for (int i = 0; i < kNumFr; ++i)
        {
            const double d = relativeResponseDb(proc, one, frs[i], kRefHz, kSmall) -
                             relativeResponseDb(proc, ref, frs[i], kRefHz, kSmall);
            std::printf("%8.2f", d);
            if (m == 0)
            {
                lo[i] = hi[i] = d;
            }
            else
            {
                lo[i] = std::min(lo[i], d);
                hi[i] = std::max(hi[i], d);
            }
            spread[i] = hi[i] - lo[i];
        }
        std::printf("\n");
    }
    std::printf("  %-10s", "spread");
    double worstSpread = 0.0;
    for (int i = 0; i < kNumFr; ++i)
    {
        std::printf("%8.2f", spread[i]);
        worstSpread = std::max(worstSpread, spread[i]);
    }
    std::printf("\n\n  Worst mode-to-mode spread in the 1x droop: %.2f dB.\n", worstSpread);
    std::printf("  A single fixed-shape restore shelf can only ever be right to within this figure.\n");

    // --- 4. RESAMPLER DISPERSION -- the guard on useIntegerLatency ------------------------------
    // Oversampling can only ever make the top octave MORE faithful, so the phase error against the
    // highest factor must fall as the factor rises. It once did the opposite: JUCE's
    // useIntegerLatency=true appends a fractional-delay allpass, and at 4x -- the shipped default --
    // that cost +52 deg at 18 kHz against 8x, worse than 1x. Magnitude stayed within 0.03 dB
    // throughout, so no magnitude check could see it, and the non-monotonicity was the only tell.
    // This asserts the ORDERING rather than an absolute figure, so it stays a guard and not a
    // tautology; the excess DELAY is removed first, since that is what a sub-sample null aligns out.
    std::printf("\n  Resampler dispersion vs 8x (excess delay removed) -- must FALL as the factor rises\n");
    std::printf("  %-10s%10s%10s\n", "factor", "12 kHz", "18 kHz");

    // Phase must be UNWRAPPED before any slope is fitted: std::arg wraps to (-pi, pi], and at
    // 18 kHz a single sample of delay is already 135 deg, so a wrapped reading silently folds a
    // large error into a small one (it did -- the first version of this check reported 557 deg).
    const double kLadder[] = {1000.0, 2000.0, 4000.0, 6000.0, 8000.0, 10000.0,
                              12000.0, 14000.0, 16000.0, 18000.0};
    constexpr int kNumLadder = (int)(sizeof(kLadder) / sizeof(kLadder[0]));

    auto dispersion = [&](int osIndex, double* out12, double* out18) {
        Setup s;
        s.osIndex = osIndex;
        Setup ref8;
        ref8.osIndex = 3;

        // The BULK latency difference between the two factors has to come off before unwrapping:
        // 1x has no resampler at all, so it leads 8x by the oversampler's whole ~65 samples, which
        // is 8775 deg at 18 kHz. That is reported latency, not dispersion, and it swamps any ladder.
        configure(proc, s);
        const double lat = (double) proc.getLatencySamples();
        configure(proc, ref8);
        const double lat8 = (double) proc.getLatencySamples();
        const double dLat = lat - lat8;

        double ph[kNumLadder];
        double acc = 0.0, prevRaw = 0.0;
        for (int i = 0; i < kNumLadder; ++i)
        {
            const double raw =
                std::arg(responseAt(proc, s, kLadder[i], kSmall) / responseAt(proc, ref8, kLadder[i], kSmall))
                + 2.0 * M_PI * kLadder[i] * dLat / kFs;
            if (i > 0)
            {
                double d = raw - prevRaw;
                while (d > M_PI) d -= 2.0 * M_PI;
                while (d < -M_PI) d += 2.0 * M_PI;
                acc += d;
            }
            else
            {
                acc = raw;
            }
            prevRaw = raw;
            ph[i] = acc;
        }

        // Fit the pure-delay term over 1-8 kHz, where every factor is flat, then report what is
        // left at the top -- that residual is dispersion, which no sub-sample null can align out.
        double sxx = 0.0, sxy = 0.0;
        for (int i = 0; i < kNumLadder && kLadder[i] <= 8000.0; ++i)
        {
            sxx += kLadder[i] * kLadder[i];
            sxy += kLadder[i] * ph[i];
        }
        const double slope = sxy / sxx;
        const double toDeg = 180.0 / M_PI;
        *out12 = std::abs((ph[6] - slope * kLadder[6]) * toDeg);
        *out18 = std::abs((ph[9] - slope * kLadder[9]) * toDeg);
    };

    // 1x is excluded, and not for convenience: it has no resampler, so the quantity this check is
    // about does not exist there, and its bulk delay against 8x is the oversampler's whole latency
    // (~65 samples = 8775 deg at 18 kHz). getLatencySamples() cannot remove it here either, because
    // a factor change is applied at the START of the next block, so the value read straight after
    // configure() still describes the previous factor.
    // Only the 18 kHz column gates: it is where the allpass's dispersion is largest and where the
    // ordering broke. 12 kHz is printed alongside it so a partial regression is still visible.
    double prev18 = -1.0;
    for (int i = 1; i <= 2; ++i)
    {
        double d12 = 0.0, d18 = 0.0;
        dispersion(i, &d12, &d18);
        std::printf("  %2dx%18.2f%10.2f\n", kOsFactors[i], d12, d18);
        if (prev18 >= 0.0 && d18 > prev18 + 1.0)
        {
            std::printf("      <-- FAIL: %dx disperses MORE than the factor below it. Check that the\n"
                        "          Oversampling constructor still passes useIntegerLatency = false.\n",
                        kOsFactors[i]);
            ok = false;
        }
        prev18 = d18;
    }

    if (! finite)
    {
        std::printf("\n  <-- FAIL: non-finite sample in a render\n");
        ok = false;
    }
    std::printf(ok ? "\nPASS: OSFidelity (finite; wanted distortion factor-independent; dispersion falls with factor)\n"
                   : "\nFAILED: OSFidelity\n");
    return ok ? 0 : 1;
}
