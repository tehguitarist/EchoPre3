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
// It also A/Bs ADAA at every factor, which is what decided PedalAudioProcessor::kAdaaMaxOsIndex.
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
        s.adaa = Adaa::forceOff;
        for (int i = 0; i < kNumFr; ++i)
            ref8x[i] = relativeResponseDb(proc, s, frs[i], kRefHz, kSmall);
    }

    for (int osIdx = 0; osIdx < 4; ++osIdx)
        for (const bool useAdaa : {false, true})
        {
            Setup s;
            s.osIndex = osIdx;
            s.adaa = useAdaa ? Adaa::forceOn : Adaa::forceOff;
            std::printf("  %2dx %-10s", kOsFactors[osIdx], useAdaa ? "ADAA on" : "ADAA off");
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
        for (const bool useAdaa : {false, true})
        {
            Setup s;
            s.osIndex = osIdx;
            s.inputTrimDb = 12.0;
            s.adaa = useAdaa ? Adaa::forceOn : Adaa::forceOff;
            const auto sp = toneSpectrum(proc, s, 1.0, kToneBin, &finite);
            std::printf("  %2dx %-10s %10.2f %10.2f %10.2f\n", kOsFactors[osIdx], useAdaa ? "ADAA on" : "ADAA off",
                        sp.h2Dbc, sp.wantedThdDbc, sp.aliasDbc);
            if (osIdx == 3 && ! useAdaa)
                wantedRef = sp.wantedThdDbc;
            if (osIdx == 0 && ! useAdaa)
            {
                // Guard the one property that must hold for the ADAA verdict to keep meaning: the
                // 1x floor has to be the worst one. If a refit ever makes 1x the quietest, the whole
                // reasoning behind kAdaaMaxOsIndex needs re-running rather than inheriting.
                if (! std::isfinite(sp.aliasDbc))
                    finite = false;
            }
        }

    for (int osIdx = 0; osIdx < 4; ++osIdx)
    {
        Setup s;
        s.osIndex = osIdx;
        s.inputTrimDb = 12.0;
        s.adaa = Adaa::forceOff;
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
        for (const bool useAdaa : {false, true})
        {
            Setup s;
            s.osIndex = osIdx;
            s.adaa = useAdaa ? Adaa::forceOn : Adaa::forceOff;
            const auto sp = toneSpectrum(proc, s, 0.5, kToneBin, &finite);
            std::printf("  %2dx %-10s %10.2f %10.2f %10.2f\n", kOsFactors[osIdx], useAdaa ? "ADAA on" : "ADAA off",
                        sp.h2Dbc, sp.wantedThdDbc, sp.aliasDbc);
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
        ref.adaa = Adaa::forceOff;
        Setup one;
        one.osIndex = 0;
        one.modeIndex = m;
        one.adaa = Adaa::forceOff;
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

    if (! finite)
    {
        std::printf("\n  <-- FAIL: non-finite sample in a render\n");
        ok = false;
    }
    std::printf(ok ? "\nPASS: OSFidelity (finite; wanted distortion is factor-independent)\n"
                   : "\nFAILED: OSFidelity\n");
    return ok ? 0 : 1;
}
