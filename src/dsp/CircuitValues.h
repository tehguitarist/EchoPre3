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

// VOLUME power-law taper exponent (R = Rmax * x^p). circuit.md's taper fit places the network's
// gain peak at 1-2 o'clock, which needs p ~ 2.0 at the physical ~20 kOhm drain drive impedance.
// Derived from the maker's four published control points -- marketing copy, superseded by a real
// VOLUME sweep capture. OutputNetworkTest prints all four points so the fit stays visible.
inline constexpr double kVolumeTaperP = 2.0;

// --- Supply ---
// The whole power section (D1-D6, C5-C9, IC1) is supply-only and NOT modelled. VA is the only number
// the audio model needs from it: a hard zener clamp, confirmed by the maker ("26 VDC ramped, 22 VDC
// regulated"). Used solely to bound the drain's swing headroom, which guitar levels never reach.
inline constexpr double kVA = 22.0;
} // namespace pedal::circuit
