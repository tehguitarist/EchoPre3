#pragma once

// Component values transcribed from .claude/rules/circuit.md, which is the source of truth (traced
// from schematics/schematic.png). Nothing here is approximated or taken from a kit/forum trace.
//
// Only the R/C values and the polarity are trustworthy in advance. Every AMPLITUDE parameter of the
// JFET lives in JfetStage.h and must be fitted to the reference renders -- see the fit markers there.

namespace pedal::circuit
{
// --- Input network (circuit.md stage 1) ---
inline constexpr double kR3 = 110.0e3;  // series input resistor (NOT a pulldown -- verified inline)
inline constexpr double kC3 = 220.0e-12; // shunt to GND at node A, RF/brightness filter
inline constexpr double kC4 = 22.0e-9;  // input DC-blocking coupling cap into the gate
inline constexpr double kR4 = 1.0e6;    // gate bias pulldown to GND

// --- Gain stage (circuit.md stage 2) ---
inline constexpr double kR6 = 22.0e3;   // drain load to VA
inline constexpr double kR5 = 3.6e3;    // source degeneration ("3k6" -- 3.6 kOhm, not 3.6 Ohm)

// --- MODE source-bypass network (circuit.md stage 2b) ---
// The un-engaged branch keeps its 1 MOhm pulldown in series, ~280x larger than R5, so it is modelled
// as fully off. Including it would change the HF degeneration by 0.7% (0.06 dB) -- see JfetStage.h.
// The lug wiring (C1 -> lug 3, C2 -> lug 1) is traced; which LABEL each lug carries was measured,
// not inferred, and came out the reverse of the first guess -- see circuit.md note #2.
inline constexpr double kC1 = 22.0e-9;  // "BRIGHT" branch -> corner 1/(2*pi*R5*C1) ~ 2.01 kHz (measured 1.86 kHz)
inline constexpr double kC2 = 10.0e-9;  // "MID" branch    -> corner ~ 4.42 kHz (measured 4.17 kHz)

// --- Output / VOLUME network (circuit.md stage 3) ---
inline constexpr double kC10 = 100.0e-9;   // drain output coupling cap
inline constexpr double kR10 = 240.0e3;    // pulldown to GND at node E
inline constexpr double kR9 = 110.0e3;     // node E -> OUT
inline constexpr double kR8 = 110.0e3;     // OUT -> VOLUME lug 3
inline constexpr double kVolumePot = 500.0e3; // 500 kA; wiper (lug 2) grounded, BOTH ends drive nodes

// VOLUME power-law taper exponent (R = Rmax * x^p).
//
// ⭐⭐ MEASURED 2026-09-10 from P4's own VOLUME sweep -- the within-rig measurement this constant was
// blocked on since the project began. It was 2.0, derived from the maker's published control points
// (marketing copy), and every earlier attempt to check it failed for one reason: VOLUME was 1:1
// confounded with unit AND trainer, because the three NAM captures were three pedals recorded by
// three people. This is one pedal, one rig, one session, one calibration.
//
// TWO INDEPENDENT BANDS, six knob positions each, and they agree to 2 %:
//     midband control law, 1 kHz          p = 2.330   (worst 0.267 dB, RMS 0.160)
//     LF corner shape, 15-400 Hz, dark    p = 2.281   (worst 0.408 dB, RMS 0.0414)
//     LF corner shape, 15-400 Hz, bright  p = 2.276   (worst 0.404 dB, RMS 0.0403)
// The two LF rows are the known-answer probe, not a third sample: below the 1.9 kHz shelf zero every
// mode has Zs = R5, so the LF band MUST return the same taper whichever mode it is fitted from.
//
// 2.30 is shipped as the value between the two bands. Cost of the compromise, against each band's
// own optimum: LF +0.001 dB RMS, midband +0.026 dB RMS. The old 2.0 cost 2.076 dB worst / 0.956 RMS
// on the control law and 1.035 / 0.139 on the LF band, so this is a ~6x improvement on both.
//
// ✅ The peak still lands where the maker says it does: at p = 2.30 the network's gain peak is at
// x = 0.646 = 13:28, inside the published 1-2 o'clock, and the measured peak is at 13:30. That was
// the one published control point still load-bearing, and it survives.
// ✅ The peak-to-full-CW fall-back is 3.86 dB measured against 3.91 dB predicted -- 0.05 dB. The
// maker's published "1-2 dB" is refuted; the circuit as drawn was right all along (circuit.md #1).
//
// ⛔ 7:30 and 8:00 (x <= 0.1) are excluded from the fit: the control law moves +-6.75 dB (7:30) and
// +-2.98 dB (8:00) per +-10 min of knob error, against +-1.02 at 9:00. Both fail on their own
// residual, not merely on leverage -- see analysis/p4_component_fit.py's docstring.
inline constexpr double kVolumeTaperP = 2.30;

// --- Supply ---
// The whole power section (D1-D6, C5-C9, IC1) is supply-only and NOT modelled. VA is the only number
// the audio model needs from it: a hard zener clamp, confirmed by the maker ("26 VDC ramped, 22 VDC
// regulated"). Used solely to bound the drain's swing headroom, which guitar levels never reach.
inline constexpr double kVA = 22.0;
} // namespace pedal::circuit
