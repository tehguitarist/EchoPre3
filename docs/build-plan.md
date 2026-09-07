# Build Plan — Echo Pre 3, with NAM models as the reference

> Written 2026-09-07, at the end of step 1 (schematic analysis). This file adapts the generic
> build sequence in `CLAUDE.md` to the reference data we actually have: **seven NAM models, no
> raw pedal captures, no bypass anchor.** Read `docs/validation-and-capture.md` for the generic
> method; this file records what changes because of the data.

---

## 1. The reference data

Seven `.nam` models in `~/Downloads/Pedal {1,2,3} - {1430,1030,1000}/`. The folder number is the
VOLUME knob's clock position (confirmed by `SecretPreMid10Oclock.nam` naming itself), which is the
same `HHMM` convention `analysis/analyze.py:parse_filename` already parses.

| Unit | Trainer | VOLUME | Modes present | Stored loudness (LUFS) |
|---|---|---|---|---|
| **P1** | `danielnguyen` | 2:30 | Bright, Mid, Dark | −18.50 / −19.83 / −20.27 |
| **P2** | `thelamehorse` | 10:30 | Bright, Mid, Dark | −21.63 / −24.40 / −26.04 |
| **P3** | (unnamed) | 10:00 | Mid only | −15.94 |

All seven share one architecture: TONE3000 `SlimmableContainer` wrapping two WaveNet submodels
(`max_value` 0.5 and 1.0), 48 kHz, receptive field **6347 samples = 132 ms**.

### What this data is genuinely good for

- **Mode differentials.** Within one unit, the three modes differ in exactly one thing: which
  source-bypass branch is grounded. Everything else — rig gain, converter response, JFET unit,
  volume position — is identical and **cancels in the ratio**. This is the matched-pair pattern
  `dsp.md` prescribes, and we get it twice independently (P1 and P2).
- **Nonlinearity versus level.** A NAM model renders any signal we like, at any length, with no
  noise floor and perfect repeatability. The harmonic-versus-level characterisation that would
  need a careful reamp session is nearly free here.
- **Unit-to-unit spread.** Three physical pedals let us *measure* how much two Secret Preamps
  differ, and set the project's pass/fail band from that instead of inventing a threshold.

### Three hard limits — plan around them, do not fight them

**L1. VOLUME is confounded with unit and rig gain, so the taper cannot be fitted from these.**
P3 sits at a *lower* volume setting than P2 yet reports 8.5 dB more loudness. That gap is rig
gain, not circuit. Absolute level is only comparable *within* one trainer's set.

**L2. There is no bypass or unity anchor.** A NAM model cannot be bypassed, so nothing here
measures the pedal's absolute voltage gain or pins `kInputRef`. We can measure dBFS-in to
dBFS-out for one rig, but not volts.

**L3. The model's edges are soft.** The 132 ms receptive field means a 13 Hz high-pass corner is
seen for under two cycles — right at the edge of what this WaveNet can represent. At the top, the
model has baked in the trainer's converter response, so anything above roughly 15 kHz is theirs,
not the pedal's. Trust the middle; verify the edges before leaning on them.

---

## 2. Phase 0 — get the renders (start here; it is the human-latency item)

Do this before any DSP, because the JFET fit is the long pole and it is blocked on this data.

**0a. Redesign `analysis/gen_test_signal.py` for this pedal — DONE (2026-09-07).** No captures
existed yet, so this was the one moment the segment layout could change. The signal is now built
for a subtle clean preamp rather than a distortion. Full layout in `analysis/README.md`; the
reporting grids are:

| Quantity | Grid |
|---|---|
| Frequency response | 60 bands, 1/6 octave, 20 Hz–20 kHz, densified to 1/24 octave at the corners |
| Compression | 16 bands, 2/3 octave, 20 Hz–20 kHz, at 10 input levels |
| THD | 16 bands, 20 Hz–8 kHz, at 4 input levels, orders H2 through H8 |
| THD, continuous | Farina swept curve to about 10.4 kHz |

Removed: the SMPTE intermodulation pair, the plucked decay notes, and the single 1 kHz level
ladder. All three were distortion-pedal instruments; the per-band compression ladder measures the
same physics across sixteen bands instead of one.

Two limits are physics, not choices. **THD is unmeasurable above about 12 kHz at 48 kHz**, because
the second harmonic of a 12.5 kHz tone is already past Nyquist, so the tone grid stops at 8 kHz and
unmeasurable orders return as missing rather than as zero. And **every tone cell carries a 0.15 s
settle head that the analyzer discards**, which exceeds the 132 ms receptive field of the reference
NAM models so no cell inherits the previous one's tail.

The new instruments in `analysis/analyze.py` all pass a known-answer self-test
(`python analysis/analyze.py --selftest`): frequency response recovers a known bandpass to 0.037 dB
and harmonic levels recover a known cubic shaper to 0.003 dB. Run it after touching either file.

Signal length is 4.8 minutes. That matters for the reamp session below, not for the NAM renders.

**0b. Write `analysis/captures.py`** — `parse_capture` reusing the clock convention, plus
`render_args` for the OfflineRender CLI once that exists.

**0c. Render protocol.** Eight WAVs total: the seven models plus one pass-through. Every one of
these settings matters:

- NAM plugin input gain and output gain at **0.0 dB**, output **normalize OFF**. Normalize applies
  the stored loudness figure and destroys the only absolute level information we have.
- Noise gate off, tone stack off or bypassed, no IR.
- Whatever quality or CPU setting the plugin exposes at **maximum, and identical across all seven**
  — each file packs two submodels, and we need every render to use the same one.
- 48 kHz session, 32-bit float, offline bounce, nothing on the master bus.
- **Render the test signal once with no plugin at all.** That null render proves the loop is unity
  and sample-aligned, and it costs one bounce.

Name them so the existing parser reads them, e.g. `p1_V1430_bright.wav`, `p3_V1000_mid.wav`,
`null_V0000_mid.wav`, into `analysis/captures/`.

---

## 3. Phase 1 — characterisation, before a line of DSP is written

Analysis only. Each item below is a number we need in hand before the stage that consumes it.

**M0. Loop check.** Null render against the source: expect a null well under −100 dB. If it does
not null, stop — everything downstream inherits the error.

**M1. Resolve the mode labels.** Take Bright/Dark and Mid/Dark magnitude ratios at low level, for
P1 and P2. Each should be a shelf; the corners should land near **4.42 kHz** (10 nF) and
**2.01 kHz** (22 nF), reaching the same plateau. **Whichever label shows the lower corner is the
22 nF cap.** This settles the lever-to-lug mapping left open in `circuit.md` note #2.

> A hypothesis worth ten minutes, not a conclusion: the stored loudness figures put Bright above
> Mid above Dark in *both* units. The 22 nF cap lifts a wider band and should therefore win a
> broadband loudness comparison, so that ordering hints the labels map to the caps the opposite
> way from what `circuit.md` currently assumes. The ratio measurement decides it properly.

**M2. Get `gm` from the mode plateau — this is the most valuable number in the dataset.** With the
drain output resistance far above R6, the stage's loaded gain ratio between fully bypassed and
unbypassed is just `k = 1 + gm·R5`. R5 is known at 3.6 kΩ, so the plateau height of the M1 shelf
**hands us `gm` directly, as a ratio, with no level calibration needed at all.** That sidesteps
limits L1 and L2 entirely for the one parameter the datasheet spread makes least predictable. The
shelf's corner frequency independently cross-checks R5 against the drawn value.

**M3. Input low-pass corner** from the low-level clean sweep. Expect ~7.3 kHz, confounded with each
trainer's converter response — comparing P1 against P2 separates the pedal's corner from theirs.

**M4. Low-frequency high-pass corner** per model, as a level-independent probe of where each
unit's VOLUME pot actually sits. Predicted corners are roughly 28 Hz at 10:00, 24 Hz at 10:30 and
13 Hz at 2:30, so the two extremes differ by a factor of two and should be clearly separable.
**Validate the LF tones behave sanely first** (limit L3) before believing this.

**M5. Harmonic structure versus level.** Expect the even-dominant square-law signature, with H2
above H3. Record the sign of the cubic before choosing a limiter, per
`docs/nonlinear-component-modeling.md` §2.

**M6. Unit spread.** P1 against P2, Dark mode, level-normalised, in third octaves. **That spread
becomes the project's pass/fail band.** If two real pedals differ by 2 dB in the top octave,
chasing 0.5 dB against either one is measuring noise.

---

## 4. Which unit are we modelling?

Ship one pedal, not an average of three.

- **Mode differentials (M1, M2): fit to P1 and P2 together.** These measure R5, C1, C2 and the
  degeneration ratio — shared circuit physics, so two units is genuinely two samples of the same
  quantity.
- **Absolute response and level: anchor to P1.** It has all three modes, is the most recent, and
  its trainer described it honestly as a clean boost.
- **Hold P2 back as validation.** Fit nothing to it. If P1-fitted and P2-measured disagree by more
  than a couple of dB, surface that as unit variance rather than splitting the difference quietly.
- **P3 is a level sanity check only.** One mode, unknown rig, demonstrably offset gain.

---

## 5. The build sequence, re-ordered

Steps keep `CLAUDE.md`'s numbering; the change is that phase 0 and 1 run first and in parallel
with the cheap scaffolding.

| Step | Work | Gate |
|---|---|---|
| 2 | CMake scaffold, APVTS: VOLUME float, MODE 3-way choice, trims, OS, bypass, HQ | Loads in Logic |
| 3 | chowdsp_wdf smoke test, RC lowpass | −3 dB point within 1% |
| 4a | Input network + output/VOLUME network as **one coupled two-node solve** | Matches M3, M4 |
| 4b | JFET stage: Norton drain current with `Zout = ro·k(s) ∥ R6` stamped into 4a's solve | Matches M2, M5 |
| 5 | Three MODE topologies, precomputed scattering matrices | Matches M1 shelves |
| 6 | Oversampling + ADAA, AccurateOmega | Aliasing measured, not assumed |
| 7 | Integration and calibration | See §6 |
| 8 | UI: reuse peripherals, new centre face | Headless render at 0.5× and 2.5× |
| 9 | Reference validation against P2 holdout | Inside the M6 band |
| 10 | Full control sweep | No NaN, no clicks |

Step 4a and 4b are deliberately one coupled network, per the ⭐⭐⭐ trap in `circuit.md`: driving
the volume network from an ideal voltage source and applying the mode lift as a shelf double-counts
it, and on this pedal that error is worth about 20 dB.

Per `CLAUDE.md`'s tiering: plan and topology calls at the top tier, the `schematic-checker` and
`dsp-validator` agents at high effort on every stage gate, mechanical scaffolding and formatting
cheap.

---

## 6. Calibration, given no bypass anchor

`docs/calibration-and-gain-staging.md` assumes a unity capture exists. It does not here, so:

- **`kInputRef` cannot be measured.** Keep the template's 0.87 V/FS as a declared assumption, and
  record it as an assumption in the code comment, not as a measurement. Nothing downstream should
  read as if it were anchored.
- **Output makeup is level-matched to P1**, not to volts. State the reference explicitly: P1, mode
  as resolved by M1, VOLUME at 2:30.
- **The VOLUME taper is fitted to the maker's four published points**, not to the NAM data — with
  the peak placed at 1–2 o'clock, needing `p ≈ 2.0` at the physical drive impedance. The three NAM
  volume points serve only as an M4 shape check.
- **Leave the 3.9 dB versus 1–2 dB fall-back discrepancy open.** `circuit.md` note #1 is explicit
  that it is invariant to every assumption tested. Do not tune other constants to close it.

---

## 7. The old two-way pedal — now planned, not contingent

Confirmed 2026-09-07 that reamping and capturing is possible, which changes this from a fallback to
scheduled work. It is the only source for the two things the NAM models structurally cannot give:
**a VOLUME sweep and a bypass anchor.** The gain stage may well differ between revisions, but the
output and VOLUME network is the original EP-3 wiring, and that is the part in question.

The session is bounded. At 4.8 minutes per pass:

| Pass | Count | Purpose |
|---|---|---|
| Bypass | 1 | Anchors `kInputRef` and the output makeup for the first time |
| VOLUME sweep, one mode fixed | 10 | Taper shape, rotation direction, the fall-back discrepancy |
| Both switch positions, volume fixed | 2 | Cross-checks the mode differential against a second revision |

That is 13 passes, about 63 minutes of rendering plus setup. Capture the bypass pass first: if it
does not null cleanly against the source, the loop is wrong and every later pass inherits it.

Note the revision caveat in `circuit.md`: this unit has a two-way switch, so its source-bypass
network is not the three-position one modelled. Use it for the volume network and the level anchor.
Do not fit the MODE topology to it.

## 8. Open questions

1. Which cap the "Bright" label actually engages — M1 answers it, and the loudness metadata hints
   the current assumption is backwards.
2. Whether the LF corner probe survives the receptive-field limit — pre-checked in M4.
3. Where each trainer's reamp level sat, which bounds how hot a render we can believe.
4. Whether the old pedal's volume network matches the schematic, if we get that far.
