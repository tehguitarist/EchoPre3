# The capture dataset — what the model is calibrated against

> This file replaces the old capture-session checklist. That session happened (2026-09-10), the rig
> is gone, and 400 lines of instructions for setting up a DMM and choosing an interface input are no
> longer useful. What IS useful is a record of what is on disk, what it is calibrated to, what the
> rig itself measured, and the conventions the analysis scripts depend on.
>
> ⛔ **No more captures are coming.** Anything that would need one is recorded as such in
> `.claude/rules/circuit.md` rather than left as an open question.

## What is on disk

Under `analysis/captures/`. ⚠ `*.wav` is **gitignored** — the captures are not recoverable from
version control.

| set | what | count |
|---|---|---|
| **P4 main matrix** | the owner's own unit, both switch positions, VOLUME 7:30 → 17:00 | 28 |
| **P4 references** | `loop_V0000_none` (nothing in circuit) and `p4_V1030_bypass` (pedal in, bypassed) | 2 |
| **P4 probe** | a short self-contained probe signal at six settings, two into a 10 kΩ line input | 15 |
| **NAM renders** | seven model renders of three *other* units (P1, P2, P3), by three trainers | 7 |
| `captures/clipped/` | quarantined takes from before the pad was set — see the folder's README | 3 |

**P4 is an earlier two-position variant (BRIGHT/DARK, no MID).** The model is voiced to the newer
three-position units; P4 supplies everything absolute or structural. The rule and its price are
`circuit.md` note #28.

## Calibration — the two figures everything rests on

| | value | how |
|---|---|---|
| **play side** (`input_level_dbu`) | **+12.20 dBu** = `kInputRef` 4.4626 V/FS | owner's tracking calibration, confirmed by meter: 1.7745 V RMS at −5 dBFS / 220 Hz |
| **record side** (`output_level_dbu`) | **+14.30 dBu** = 4.02 V RMS at full scale | measured from the loop and bypass takes, 0.01 dB apart |

⚠⚠ **The two differ by 2.090 dB, and a capture's digital gain is therefore 2.090 dB away from the
pedal's actual voltage gain.** Getting that backwards is a 4.2 dB error in the control law, and it
happened once. ⭐ The free check that catches it: **a loop is a wire and this bypass is true bypass,
so both must read exactly 0.000 dB of voltage gain.** They read −0.026 and −0.015 dB.

📌 220 Hz rather than 1 kHz for the meter reading because the DMM used (Jaycar QM1529) is specified
only to 400 Hz; accuracy at that reading is ±0.135 dB.

## What the rig itself measured — and why it matters

- ⭐⭐ **No low-frequency pole.** 20–40 Hz reads −0.006..+0.065 dB (loop) and −0.053..+0.018 dB
  (bypass). That is what made the LF story decidable: all seven NAM captures carry a real ~20–35 Hz
  pole and this chain does not, so an LF pole in a P4 capture belongs to the pedal. It turned out
  there wasn't one — `C10` is as drawn and the apparent LF error was the VOLUME taper (note #21).
- ⭐ **Genuine true bypass**: nulls at −70.8 dB against the loop, 0.008 dB of insertion loss. No
  buffer, nothing in the bypass path to model.
- ⚠ **Real HF droop that must be deconvolved**: about −0.53..−0.75 dB over 8–20 kHz. That is the
  size of the entire ±1 dB target, so it is not optional.
- Noise floor −99.5 / −99.8 dBFS RMS.

### ⚠⚠ Deconvolve against the BYPASS capture, never the bare loop

They are not interchangeable: bypass-minus-loop is **+0.37 dB at 18 kHz**, best described as the
loop path carrying one extra pole at 48.4 kHz. A bypassed pedal cannot *add* treble, so the loop
path had a cable the bypass path did not — and **the bypass path shares its cabling with every pedal
capture.** Deconvolving against the loop imports 0.37 dB of HF error. The loop's only remaining job
is the unity check, which it passes.

### ⚠⚠ ~99 pF loads the pedal's output, and deconvolution structurally cannot remove it

In bypass the cable is driven by the interface's low output impedance; in an active capture it is
driven by the pedal's **59–102 kΩ**. So the pole exists in one path and not the other. Uncorrected it
reads as a knob-dependent *model* defect: `goal_check` reported HF misses growing with VOLUME to
+2.69 dB, and `phase_sweep` put P4 at 17–23° RMS against P1's 2.7–4.0°. Corrected, P4's DARK rows
read 0.13–1.88° RMS, ~10× better than P1.
⛔ **This is a CAPTURE-side correction only.** The plugin ships no load capacitance, because its
output goes to a DAW digitally. Both corrections live in `analysis/p4_corners.py` as one definition
each.

## Conventions the analysis scripts depend on

Filename: `<unit>_V<HHMM>_<mode>[_pad<N>][_load<R>].wav`

- **mode** is `bright` / `dark` / `mid`, plus `none` (nothing in circuit) and `bypass` (pedal in,
  footswitch bypassed). ⚠ Naming the loop "dark" was actively misleading and is not allowed.
- **`_pad<N>`** is digital attenuation on the *played* signal, in dB (`_pad4p5` = 4.5). **No suffix
  means pad 0**, so pre-existing captures keep their meaning. ⭐ `captures.render_args()` feeds the
  figure to `OfflineRender --input-scale`, so plugin-vs-capture comparisons are **matched-drive
  automatically** — which `circuit.md` note #8 makes binding for anything harmonic.
- ⚠⚠ **`find_captures()` EXCLUDES the reference captures by default.** Every comparison script
  renders the plugin per capture and diffs it, which is meaningless for a file with no pedal in it
  and would fail silently as a mysterious outlier. Pass `include_reference=True`, or use
  `find_reference_captures()`.
- ⚠ An unparseable filename is **skipped with a warning on stderr**, loudly, because a capture the
  harness cannot see is one that silently does not exist. (It used to raise and take every script
  down; a DAW take suffix was enough to trigger it.)
- 📌 Everything lives flat in `captures/`; only `clipped/` is separate. Any **other** subfolder
  hides its contents from `find_captures()` silently.

## Why the matrix is padded 12 dB, and where the load line survives

⚠⚠ **The pedal's own boost overruns the converter at every VOLUME position from 9:00 up** — worst
around the volume peak, at a predicted +4.66 dBFS (dark) and **+7.14 dBFS (bright)**. ⛔ It is not a
gain-knob problem: BRIGHT needs ~11.4 V peak and the interface's instrument input tops out at 6.16 V,
so *no* gain setting can accept it. The fix is a **digital pad on the playback**, which keeps the
analog calibration fixed and makes the offset exact and repeatable.

⚠ A pad eats drive and recorded level together, so **the padded matrix tops out 6–12 dB below real
playing level and cannot fit anything nonlinear.** The nonlinear dataset is exactly the pad-0 and
lightly-padded takes:

| capture | gate volts at its hottest cell |
|---|---|
| `p4_V0900_dark` | 3.58 V — past the 1.942 V cutoff onset, and the only cell that enters triode |
| `p4_V0730_bright` / `p4_V0730_dark` | full drive, recorded ~16 dB quieter |
| `p4_V0900_bright_pad4p5` | the mid-drive point the matrix otherwise lacks |

⛔ **Do not overwrite those.** ✅ The LINEAR fits are unaffected by drive, and that was checked
rather than assumed: the 9:00 DARK LF corner moves **1.9 %** across a 47 dB drive span ending past
triode.

## ⛔ Captures that must not be used, and why

- **7:30 and 8:00 (x ≤ 0.1) are excluded from every fit.** The control law moves ±6.75 dB (7:30) and
  ±2.98 dB (8:00) per ±10 min of knob error, against ±1.02 at 9:00. Two 7:30 takes of the same
  setting disagree by **6.4 dB (dark) and 10.4 dB (bright)**. ⭐ The tell was the fit residual —
  0.13–0.31 dB against 0.024–0.044 elsewhere — not the level. They are kept only because they are
  clean and reach the load line.
- **P2's rig is disqualified for anything absolute** on three independent axes: a 3.2 kHz cable pole
  (~542 pF), an absolute response 13.8 dB off at 18 kHz, and **all three of its captures are
  polarity-inverted**. ⚠ `null_depth()` gain-matches with a signed scalar and reports `|g|`, so that
  inversion is *invisible* in its null figure — run `polarity()` first, always.
- **P1 is the noisiest NAM model in the set** and was nonetheless the designated absolute anchor
  before P4 existed. Its harmonic data is floor throughout, which is how a confident `Vov` fit came
  to be wrong by 13–20 dB (note #22).
- **Nothing below ~100 Hz on a NAM capture.** A 132 ms receptive field is 2.6 periods at 20 Hz; the
  captures read up to 50 % THD at 20 Hz, with H3 above H2, which a square law forbids.

## ✅ Drift and repeatability are measured, not assumed

Two 9:00 DARK takes 52 minutes apart, with the knob moved away and returned in between, agree to
**+0.061 dB in level and 0.019 dB RMS in shape**. So rig drift, JFET thermal drift and knob
repeatability *together* are ≤ 0.061 dB at a normal knob position — which is why the control law's
0.160 dB residual is model or estimator error rather than setting error.

## If another capture session ever happens

In priority order, from what the current dataset cannot answer:

1. ⭐⭐ **A known RESISTIVE PAD on the pedal's OUTPUT.** This is the big one. The rig couples drive
   depth to recorded level — VOLUME sets both the load line and the output — so exactly one cell in
   28 captures enters the transistor's triode region, and that branch rests on it. Input padding
   cannot fix it (it lowers both together, which is what created the problem). Record the pad's
   value: it loads a 59–102 kΩ source, so it must be modelled.
2. **A MID-position unit**, so MID stops being an extrapolation.
3. **An inharmonic twin-tone segment** in the test signal. The present one is 220 + 660 Hz and
   660 = 3 × 220 exactly, so every intermodulation product lands on the harmonic grid and neither
   tone is a clean amplitude reference — the segment cannot measure IMD at all. The signal is
   append-only, so this needs a new segment and therefore a new session.
4. **Both NAM calibration figures written down at capture time.** None of the seven `.nam` files
   carries `input_level_dbu` or `output_level_dbu`; P1's was recovered only from the owner's
   recollection, and P2's and P3's are still unknown, which is why their nonlinear data is unusable.

⚠⚠ **And always write down WHERE a level is measured.** The interface's +12.20 dBu at 0 dBFS and the
pedal jack's −12 dBu (P1's rig) are the same chain with a 24.2 dB reamp box between them. Both are
correct; confusing them is a 24 dB error that would invert every harmonic conclusion on record.
