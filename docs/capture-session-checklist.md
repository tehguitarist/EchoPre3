# Capture session checklist — Echo Pre 3 (owner's two-position unit)

Written 2026-09-09 against the owner's own plan, which is sound. This records the four gaps in it
and the reasoning behind each, so the session does not have to be repeated. Everything here assumes
the owner's rig: **interface output → pedal → interface input, with NO reamp box and no transformer
anywhere.** That is better than this project's earlier ask (`CLAUDE.md` #4, "reamp at unity") — it
removes the component that was the leading suspect for the LF discrepancy (circuit.md note #19),
which is now refuted but was only refuted by measurement.

---

## ⭐⭐ 0. THE ONE THING THAT CHANGES EVERYTHING: keep the RAW WAVs

**Record `analysis/test_signal_48k.wav` through the pedal and keep the returned WAV. Train NAM too
if you want, but the raw file is the reference from now on.**

Every "floor" this project has fought is NAM model error, not measurement noise
(circuit.md note #15 — the models are deterministic to −114 dB, so **none of it averages down**):

| quantity | floor with NAM models | floor with a raw capture |
|---|---|---|
| harmonic level (H2 in dBc) | 4–9 dB, up to 21 | the converter's, ~−100 dBFS |
| compression | 0.145–0.210 dB | negligible |
| per-band THD below 100 Hz | 1.9–20.2 dB (a 132 ms receptive field is 2.6 periods at 20 Hz) | none — no receptive field |
| LF magnitude | the whole open question | directly measured |

`Vov` is pinned only to a factor of 1.3–1.5 today (note #16) **because of that harmonic floor and
nothing else**. A raw capture removes the floor outright.

- Signal is **4 min 50 s at 48 kHz**, so 14 captures ≈ **70 minutes** of recording.
- **48 kHz, no sample-rate conversion anywhere.** Same rate in and out.
- **No plugins, no dither, no normalisation** on the return. 32-bit float if the DAW offers it.
- Do not trim the head or tail — the analysis aligns on the leading marker.

## ⚠ 1. Your DMM stops at 400 Hz — calibrate at 220 Hz, not 1 kHz

Jaycar **QM1529**, manual page 10, AC Voltage table: **"Frequency Range: 40~400Hz"**. 1 kHz is 2.5×
outside spec, and outside spec on a rectifier-based AC front end means it reads LOW by an unstated
amount. It is not on the datasheet at all, only the manual.

- ⇒ **Use a 220 Hz sine for the level set.** Inside spec with margin, and deliberately NOT a
  multiple of 50 Hz, so mains pickup cannot sit on the measurement. The interface is flat to within
  hundredths of a dB from 20 Hz up, so a level measured at 220 Hz is the 1 kHz figure — declaring
  `input_level_dbu` from it is exact, not a fudge.
- ✅ **The meter is almost certainly average-responding, and that does not matter here.** Such meters
  are calibrated to read correct RMS *for a sine*, which is exactly what we are feeding it. Just keep
  the tone a clean sine — no square waves, no noise, nothing clipped.
- 📌 The meter has no frequency counter, so it cannot confirm the tone. You are generating it
  digitally, so you already know it.

### ⭐ Measure HIGH in the 2 V range, not at −12 dBFS

AC accuracy is **±(1.0% of reading + 10 digits)**, and on the 2 V range a digit is 1 mV — so the
10-digit term is a **fixed ±10 mV** regardless of level. Measuring a small voltage wastes it:

| tone level | target V RMS | range | total error | in dB |
|---|---|---|---|---|
| −12 dBFS | 0.7926 | 2 V | 2.26 % | ±0.194 |
| −6 dBFS | 1.5815 | 2 V | 1.63 % | ±0.141 |
| **−5 dBFS** | **1.7745** | **2 V** | **1.56 %** | **±0.135** |
| −4 dBFS | 1.9910 | 2 V | 1.50 % | ±0.130 |

⛔ **Do not go past −4 dBFS.** The 2 V range tops out at 1.999 V; one step above and it autoranges to
20 V, where a digit is 10 mV and the fixed term becomes **±100 mV** — ten times worse.

➡ **Play a 220 Hz sine at −5 dBFS and set the interface output until the meter reads 1.775 V RMS.**
That fixes `kInputRef` = 4.4626 V/FS (0 dBFS = 3.1555 V RMS = 4.4626 V peak) and
`input_level_dbu` = **+12.20 dBu**.

✅ **±0.135 dB is comfortably good enough** — `Vov` is currently pinned only to a factor of 1.3–1.5,
i.e. ±2.8 dB, so the meter is nowhere near the limiting uncertainty. Don't buy a better one for this.
⛔ And the DC ranges, which are three times more accurate, are not an option: interface outputs are
AC-coupled, so there is no DC to measure.

## ⚠⚠ 2. The record side will clip at unity — and a DI input may clip anyway

The pedal **boosts up to about +3 dB** at its volume peak (1–2 o'clock), and the test signal's
hottest cell sits near **−1 dBFS**. At unity loop gain that lands at **+2 dBFS and clips**. Worse, a
DI/instrument input at minimum gain often cannot accept 3.1 V RMS at all.

- **Use a LINE input if the DI cannot take ~+13.5 dBu**, or drop the record path by a known amount.
- `output_level_dbu` does **not** have to be unity — it only has to be **measured**. Feed a known
  voltage into the input, note the dBFS it reads, and
  `output_level_dbu = 20·log10(V/0.7746) − (reading in dBFS)`.
- **Set the input gain once, verify no cell clips, then do not touch it for the rest of the session.**
- ⭐ `output_level_dbu` is the **only** thing that can ever anchor `kOutputMakeup`, which is still
  exactly 1.0 and has no other route. It is the single highest-value number of the session.

## ⭐ 3. Capture the clean loop AND the pedal bypassed — they are different tests

- **Loop only (no pedal):** the M0 unity check, which has never run. It also **directly settles the
  LF question**: notes #19a/#20 establish a real, linear, single ~20–35 Hz pole that this dataset
  cannot place. **If the loop is flat to 10 Hz and the pedal capture still rolls off near 30 Hz, the
  pole is the pedal's** and `C10` gets changed with evidence behind it.
- **Pedal in circuit, bypassed:** pins `kInputRef` directly and catches anything in the bypass path.
  If the bypass is true bypass these two will be identical, which is itself the confirmation. One
  extra 5-minute bounce.

## 📌 4. The volume matrix

7:00 · 9:00 · 10:30 · 12:00 · 13:30 · 15:00 · 17:00, both modes — a good spread, and every one of
them **within one rig**, which is what dissolves the confound that wrecked the existing dataset
(note #7's M4: volume is 1:1 confounded with unit AND trainer there, and back-solved taper exponents
came out 2.5 / 3.2 / 7.8).

- ⚠ **7:00 is full CCW and the circuit genuinely silences** (Ra → 0 shorts node E). **Verify it goes
  silent, but do not spend a capture on it** — there is no signal to train or analyse.
- ⇒ 6 positions × 2 modes + loop + bypassed = **14 captures ≈ 70 minutes**.
- ⭐ What this settles that nothing else can: the **taper exponent** (shipped `p` = 2.0; notes #20a's
  P1 and P3 both want 2.4–2.7), the **3.92 dB vs the maker's 1–2 dB fall-back**, the **C10 corner**,
  and the **volume peak position** (predicted at `Ra` = 176 k = 35 % of the pot, i.e. 1–2 o'clock).

## ✅ 5. Write it all into a text file beside the captures

Interface make/model and which input was used · the DMM reading and the tone frequency used ·
`input_level_dbu` · `output_level_dbu` · the input gain setting · sample rate · anything unusual.

⚠⚠ **And say WHERE each level was measured.** The existing dataset's +12.2 dBu (interface output)
versus −12 dBu (pedal jack, after a reamp box) is a 24.2 dB difference between two correct numbers,
and confusing them would invert every harmonic conclusion on record (note #10).

---

## What the session cannot fix, and does not need to

**MID is the maker's own invention, not an Echoplex position.** The owner's unit is the two-position
original: **BRIGHT = early-1970s EP-3, DARK = late-1970s EP-3**, matching the maker's published
"Early 1970s / Late 1970s / Hybrid Early & Late" naming. So the missing switch position is the
*synthesised* one, and the two real voicings are both captured. MID's τ = 38.263 µs stays inferred
from P1/P2 — scale it by the measured **cap ratio** (2.24) rather than transplanting an absolute
value, since the ratio is the robust quantity and the absolute is not.

⚠ Since BRIGHT is the same physical position on both variants, its shelf zero should land near the
**1.86 kHz** P1 and P2 measured. **Fit it rather than assuming it** — note #2's label reasoning was
confidently wrong once already, and a two-parameter shelf fit returns ≤ 0.25 dB residuals.
