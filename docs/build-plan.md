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
| **P1** | `thelamehorse` | 10:30 | Bright, Mid, Dark | −21.63 / −24.40 / −26.04 |
| **P2** | `danielnguyen` | 2:30 | Bright, Mid, Dark | −18.50 / −19.83 / −20.27 |
| **P3** | (unnamed) | 10:00 | Mid only | −15.94 |

⚠ **Corrected 2026-09-07: this table originally had P1 and P2 swapped.** The folders
(`~/Downloads/Pedal 1 - 1030`, `Pedal 2 - 1430`), the `.nam` `modeled_by` metadata and the capture
filenames in `analysis/captures/` all agree with the table as it now stands — only the table was
wrong. Anywhere below that says "P1" was written meaning danielnguyen's unit; §4 is corrected.

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

### 3b. Phase 1 — RESULTS (2026-09-07). M1–M6 are complete; two of the six changed the plan.

Full write-up in `.claude/rules/circuit.md` note #7; raw numbers in
`analysis/reports/phase1_characterization.json`; the script is `analysis/phase1_characterization.py`.
Headlines only here:

| | Result | Consequence |
|---|---|---|
| **M0** | Reinterpreted: no bypass render exists to null against, so it is an alignment/length pass plus a new known-answer LF probe. All seven full length, lag +3..+19 samples. | none |
| **M1** | ⭐ **Labels swapped.** BRIGHT = C1 22 nF (zero 1.86 kHz), MID = C2 10 nF (zero 4.17 kHz). Both units, shelf fits to ≤0.25 dB. | `JfetStage::bypassCap()` swapped; enum order untouched |
| **M2** | ⭐ **K0 = 1 + gm·R5 = 6.59**, so gm ≈ 1.5–1.7 mS — about **2× the nominal placeholder**, not below it. | unblocks step 4b and §9.3 |
| **M3** | Input LP confirmed at 6.7 kHz (P1) and 7.2 kHz (P3) vs 7.3 kHz drawn. **P2 carries a 3.2 kHz output-load pole** (542 pF on a 92 kΩ output), so it is disqualified for absolute HF but is still the best differential capture. | §4 anchor reversed; output load is NOT negligible on this pedal |
| **M4** | C10 corner measures 1.3–2.0× high in all three, but volume is 1:1 confounded with unit and rig. | **do not retune the taper** |
| **M5** | Even-dominance confirmed (H2 rises 0.75 dB/dB). H3 is under the models' error floor. | cubic sign only weakly settled |
| **M6** | Tolerance band = **0.33 dB RMS / 0.8 dB peak on the mode differential.** The 13.8 dB absolute figure is rig, not units. | `kHfBudgetDb` stays tight |

**The method lesson, which is the durable part.** The first pass extracted every corner as a naive
"−3 dB below the plateau" threshold and returned four plausible scalars, all wrong, one of them
inverting M1. Nothing on this pedal reaches a plateau inside the audio band — the mode shelf's pole
sits at K0 × its zero, i.e. 12.6–26.8 kHz — so an 8–10 kHz "plateau" window normalises each branch
to a different point on its own transition. **Fit a model and report its residual.** A wrong fit is
visible as a bad residual; a wrong threshold just returns a number.

**What phase 1 could NOT deliver, and why it is the dataset rather than the analysis.** Anything
requiring an absolute or cross-unit level: L1 and L2 stand untouched. Beyond those, M4 found a third
confound the plan did not anticipate — **the LF corner is rig-sensitive, so it is not the
level-independent volume probe §3 assumed.** Two rigs at nearly the same knob position (P1 at 10:30,
P3 at 10:00) disagree by 16% in the opposite direction to the 17% the circuit predicts between them.
The mode differential escapes every one of these because it is a within-unit ratio; nothing else in
this dataset does. That makes the two-way pedal's VOLUME sweep (§7) load-bearing rather than
optional — it is now the only route to the taper.

### 3c. What phase 2 (the JFET fit) can and cannot take from this

- ✅ **Take `K0 ≈ 6.6` and the two measured shelf time constants** (τ = 85.4 µs and 38.2 µs, i.e. the
  zeros at 1864 and 4166 Hz). Those three numbers fully specify the mode shelf and are all measured.
- ✅ **Take the H2-versus-level slope from P2 only** for the shaper's quadratic term. It is a shape,
  so it survives having no level anchor.
- ⛔ **Do not fit the cubic to H3** — it is below both models' error floors. The only cubic evidence
  is 0.09–0.35 dB of top-cell compression, which fixes the sign (compressive) and not much else.
- ⛔ **Do not fit anything to P1's harmonics.** Its H2 does not move with level, so it is floor.
- ⛔ **Do not touch `kVolumeTaperP`, `kInputRef` or `kOutputMakeup`.** Nothing here anchors them.
- 📌 **§9.3's per-mode low-OS shelf restore is now unblocked.** It was waiting on M2 because the
  droop's target depends on where the JFET's 1/k(s) pole sits, and that pole is at K0 × the bypass
  corner. K0 is measured, so the pole is now known: **12.6 kHz in Bright and 26.8 kHz in Mid.** Note
  Mid's sits *above* Nyquist at 48 kHz, so the base-rate warp there is worse than §9.3 assumed at the
  old placeholder K0 of 3.93. Re-run `OSFidelity` after the stage is refitted, before fitting a shelf.

## 4. Which unit are we modelling?

Ship one pedal, not an average of three.

- **Mode differentials (M1, M2): fit to P1 and P2 together.** These measure R5, C1, C2 and the
  degeneration ratio — shared circuit physics, so two units is genuinely two samples of the same
  quantity.
- **Absolute response: anchor to P1 (`thelamehorse`, 10:30) — REVERSED 2026-09-07, and this matters.**
  This section originally said to anchor to danielnguyen's unit as the most recent, with its trainer
  describing it honestly as a clean boost. Phase-1 M3 disqualifies it for that job: its capture rolls
  off at −8.6 to −10.7 dB/octave above 6 kHz, which no single RC can do, so its top three octaves are
  its rig, not the pedal (`circuit.md` note #7). P1 and P3 independently fit first-order low-passes
  at 6.7 kHz and 7.2 kHz against the drawn 7.3 kHz, so **P1 is the unit whose absolute response is
  usable**. ⛔ **"with P3 corroborating it" is STRUCK — REFUTED 2026-09-08, `circuit.md` note #9.**
  P3 has only a MID capture and M3's estimator has no mode-shelf term, so it cannot recover the pole
  from a bypassed mode at all (on the plugin, whose pole is 7300 Hz by construction: DARK recovers
  7295 Hz at 0.01 dB, MID returns 9e12 Hz at 1.14 dB). **P1 alone is the absolute anchor**; P3 joins
  P2 as differential-only.
- **Hold P2 (`danielnguyen`, 2:30) back as validation, and cross-check it on the MODE DIFFERENTIAL
  only.** Its differential is the cleanest in the set (0.03–0.08 dB shelf-fit residuals, versus
  0.24–0.25 dB for P1) because the ratio cancels the rig that ruins its absolute response. Do not
  read an absolute-FR disagreement with P2 above ~2 kHz as unit variance — it is known rig response,
  and M6 measures the real unit variance at 0.33 dB RMS on the differential.
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

---

## 9. Step 6 and the probes — closed out 2026-09-07 (no captures needed)

Everything in this section was done while phase 0c was still blocked, because none of it depends on
the renders. What it needed instead was the ability to render the plugin at all, which is why the
`OfflineRender` console app came first.

### 9.1 `OfflineRender` exists, and it calls `processBlock` rather than mirroring it

`analysis/captures.py` had been pointing at `build/OfflineRender_artefacts/Release/OfflineRender`
and emitting its flags since phase 0b, and five scripts call it. The binary now exists, with the CLI
those callers already assume: `OfflineRender <in.wav> <out.wav> --os <factor>` plus `--volume`,
`--mode`, `--input-trim`, `--output-trim`, `--bypass`, `--block`.

⭐ **It instantiates a real `PedalAudioProcessor` and calls `processBlock`.** `build.md` describes
this exe as one that "mirrors processBlock", and a mirror is a second implementation of the gain
staging that has to be kept in step with the first. When it drifts, the harness reports a mismatch
against the reference renders that exists only in the harness — and every calibration constant this
project has left to fit is read off exactly those measurements. The instrument has to be the thing
under test. The cost is that it links the editor and the UI assets; that is build time only.

Verified against the test signal: full length, integer alignment lag 0, `polarity()` returns −1 at
−178.6° (the plugin inverts, as it must), output finite, peak 2.41 at volume 0.5 in Dark.

⚠ **One real bug fell out of that alignment check.** `setLatencySamples` was truncating the
oversampler's fractional latency — about 65.9 samples at 8× — to 65, throwing away almost a whole
sample that the host's delay compensation then never puts back. It now rounds. Worst-case residual
is halved to 0.5 samples, and the rest is sub-sample, which is what `frac_align` is for.

### 9.2 ADAA is implemented, exact, measured — and switched off

The shaper's closed-form antiderivative is derived and implemented (`JfetStage::shapeAntiderivative`),
and `JfetStageTest` section 6 checks it three ways: F(0) = 0, dF/dw matches g(w) to 1e-9 across
±3 V through the sign branch, and the ADAA output equals a Simpson integral of the shaper over four
steps (including one spanning the origin) to 1e-15.

⚠ **Section 6c was nearly written as the wrong assertion.** "ADAA must not change the small-signal
gain" is false: ADAA1 on a linear map is exactly the FIR (1 + z⁻¹)/2, so it has a cos(πf/fs)
magnitude and half a sample of delay. Asserting the intuitive property would have condemned a
correct implementation. The test now asserts the two-point-average response itself, which pins the
implementation without pretending the rolloff is not there.

**Measured, hot tone (full scale at +12 dB trim, Dark), then rejected:**

| factor | alias floor removed | 12 kHz cost | CPU cost |
|---|---|---|---|
| 1× | 5.63 dB | 2.99 dB | ~0 pp |
| 2× | 1.18 dB | 0.68 dB | ~0 pp |
| 4× | 0.29 dB | 0.17 dB | ~0 pp |
| 8× | 0.07 dB | 0.04 dB | 0.15 pp |

CPU was never the question. At 1×, the only factor where either effect exceeds a dB, ADAA buys
5.6 dB of a floor already at −63 dBc under that drive (−128 dBc at a realistic level) by tripling
the top-octave droop from −1.07 to −4.06 dB at 12 kHz. And needing no judgement at all: **1× + ADAA
is beaten outright by plain 2× on both axes** — 10.6 dB quieter and 3.9 dB less dark — for 0.86
percentage points of CPU. `kAdaaMaxOsIndex = -1`; the mechanism stays, dormant and tested.

⚠ **The 2× row is marginal and should not be misremembered as clear-cut.** It clears a 1 dB
alias-gain threshold and a 1 dB response budget by 0.18 and 0.32 dB respectively, so its verdict
flips on where those thresholds sit, and both are stated assumptions rather than measurements. It is
off there too, on the grounds that a sub-dB effect whose sign depends on a placeholder threshold is
not a basis for shipping a behaviour. **`FeatureProfile`'s `kHfBudgetDb` is provisional until M6
measures the real unit-to-unit spread** — if two physical Secret Preamps turn out to differ by
several dB in the top octave, widen it and re-read the table.

📌 **The alias floor stops improving past 2×: −79.3 dBc at 2×, 4× and 8× alike.** That is no longer
fold-back but the decimation FIR's own stopband. It also means the "alias gain" at those factors is
largely ADAA's own high-frequency attenuation being measured a second time — gain and cost track at
roughly 1.8:1 at *every* factor, which is what a broadband rolloff looks like rather than what
selective antialiasing looks like.

### 9.3 ⚠⚠ The low-OS shelf restore CANNOT be a single fixed shelf here — and must not be fitted yet

`dsp.md` prescribes a fixed-shape high-shelf to recover the low-OS top octave, on the stated grounds
that the droop is "essentially POT-INDEPENDENT". **That does not hold on this pedal**, and
`OSFidelity` section 4 now measures the spread every run. 1× droop, dB re 8×, normalised at 1 kHz:

| mode | 8 kHz | 12 kHz | 16 kHz | 18 kHz |
|---|---|---|---|---|
| Bright | +0.40 | +0.02 | −1.60 | −3.27 |
| Dark | −0.10 | −1.07 | −3.20 | −5.03 |
| Mid | +0.25 | −0.59 | −2.68 | −4.52 |

Up to **1.8 dB of mode-to-mode spread**, and Bright is *brighter* than 8× at 8–12 kHz, so a single
shelf would push it the wrong way in that band. The reason is structural: `dsp.md`'s rule was
written for a build whose HF caps all sat downstream of the nonlinearity, whereas here the JFET's own
1/k(s) shelf lives *inside* the oversampled region and its pole moves with MODE. A per-mode shelf is
the correct structure if one is ever built — the mode already selects coefficient sets, so it is
cheap.

⛔ **Do not fit it now.** The shelf's target depends on where that pole sits, the pole sits at
K0 × the bypass corner, and K0 = 1 + gm·R5 with `gm` still a placeholder that M2 may move by up to
4.5×. Fitting the restore today would be fitting to a number we already know is wrong. The
measurement is standing; the fit waits for M2.

At the shipped 4× default the droop is ≤ 0.14 dB to 18 kHz, so nothing is being deferred that a
normal session hears.

### 9.4 Performance (this machine; ratios are the durable part)

| factor | CPU, % of realtime | latency, samples |
|---|---|---|
| 1× | 0.41 | 0 |
| 2× | 1.29 | 49 |
| 4× (default) | 2.07 | 61 |
| 8× | 3.51 | 65 |

Mode makes no measurable difference. **`dsp.md`'s "IIR instead of FIR" optimisation is not worth
scoping**: the whole plugin is 3.5% of a core at the highest factor, so the linear-phase FIR the
sub-sample null depends on is not costing anything worth recovering.

⭐ **The probe found that the specified bypass optimisation was missing, and it has since been
implemented.** `architecture.md` specifies "DSP skipped when bypassed"; `processBlock` was running
the whole chain and crossfading it against the dry copy regardless of the mix, so a bypassed
instance cost what an active one did — 3.47% against 3.51% at 8×. With the skip in, bypass costs a
flat **0.10–0.11% at every factor**: 33× cheaper at 8×, and no longer a function of the factor.
`PerfBenchmark` keeps printing both rows so a regression shows up as the two converging again, and
`BypassClickTest` guards the transition.

⚠⚠ **Resetting the OVERSAMPLER when the skip is entered is not optional, and this is the finding to
carry forward.** It stops being fed, so its FIR delay line still holds pre-bypass audio; re-engaging
without clearing it splices that tail onto the live signal at about one reported latency after the
toggle. Measured with it left unreset, the worst sample-to-sample step reaches **1.09× / 1.54× /
1.63×** the crossfade's own bound at 2× / 4× / 8× — an audible cut inside a fade that is otherwise
doing its job. (1× passes only because a 1× oversampler has no filter state to go stale.)

📌 **The chain's own state is RESET too, but for a different reason than the obvious one, and the
obvious one is wrong.** The intuition is that resuming from frozen capacitors must thump: the caps
hold a signal from seconds ago, so re-engaging steps their inputs by the difference between two
unrelated samples and drives the 7.2 Hz input high-pass, whose ~22 ms tail outlives the 5 ms fade.
**Measured, it is a wash.** Peak re-engage excursion above the steady state, four factors × eight
toggle phases:

| factor | reset | resume |
|---|---|---|
| 1× | 0.0148 | 0.0187 |
| 2× | 0.0167 | 0.0154 |
| 4× | 0.0174 | 0.0129 |
| 8× | 0.0176 | 0.0143 |

Neither leads consistently; the spread is scatter between phase draws. The cause is the paragraph
above — once the oversampler is cleared, its FIR ramps the chain's input up from zero over its own
impulse response rather than stepping it, so the high-passes are barely kicked either way and the
frozen charge has little left to do. **Reset is chosen on DETERMINISM instead:** with a cleared chain
the output after re-engaging depends only on the input and the parameters, not on how long ago the
pedal was switched off or what was playing then. `OfflineRender` drives this same processor and step
9 validates the model with a sub-sample null, so a chain carrying unrecorded history into a render
is a nondeterminism in the instrument the remaining calibration constants are read off.

⚠ **A measurement trap that inverted this conclusion once already:** a fixed 0.2 s bypass hold is
*exactly* 200 periods of a 1 kHz probe tone, so a resumed state comes back perfectly in phase and
looks free. `BypassClickTest` sweeps the hold length as well as the toggle instants. Any future
toggle-timing measurement on this project needs the same care.

### 9.5 What these probes will and will not tell you

All three are registered with `add_test()` as **finite-only**: they assert no NaN/Inf, and
`OSFidelity` additionally asserts the wanted distortion does not move with the factor (if it did,
the oversampling selector would have become a voicing control). None of them gates on an absolute
CPU or alias figure. CI machine speed varies, and the accuracy numbers are the measurement being
reported — freezing one as a threshold would make the report circular.

They share `tests/ProbeHarness.h` so that a delta paired across two probes is a delta in the feature
rather than in the instrument.

---

## 10. Step 4b — the JFET refit (2026-09-08). One measured fit, one structural bug.

Everything phase 1 measured is now in `JfetStage.h`, and getting it in surfaced an error in the
stage's structure that no linear test could see. Ten tests pass; CPU is unchanged (2.02–2.08 % at
the 4× default).

### 10.1 What was fitted

| Parameter | Was | Now | Source |
|---|---|---|---|
| `gm` | 813 µS (datasheet-typical self-bias solve) | **1553 µS** | M2, `gm = (K0−1)/R5` with K0 = 6.5912 |
| shelf τ, Bright | 79.2 µs (drawn R5·C1) | **85.369 µs** | M1, mean of two units' shelf fits |
| shelf τ, Mid | 36.0 µs (drawn R5·C2) | **38.263 µs** | M1 |
| `Vov` (⇒ `aEven`, `bumpScale`, limits) | 1.218 V | **0.4469 V** | derived from measured `gm`, see below |
| `ro` | 1.01 MΩ | **1.44 MΩ** | 1/(λ·Id) at the refitted Id = 347 µA |

⚠ **Use `K0 − 1` for `gm`, not `circuit.md`'s ro-corrected 1.58–1.71 mS.** That correction belongs to
a model whose drain resistance rises as `ro·k(s)`; this one folds Rout into the constant `R6 ∥ ro`
(they differ by 0.06 dB), and under *that* structure the model's own mode differential is exactly
`1 + gm·R5`. Taking the corrected value would make the model miss the measurement it was fitted to.

⭐ **The bias solve collapses to a one-parameter family once `gm` is measured**, which is worth more
than the number it produced. From `Id = IDSS(Vov/|Vp|)²`, `gm = 2Id/Vov` and `Vgs = −Id·R5`:

```
|Vp|/Vov = 1 + gm·R5/2 = 3.7956        IDSS/Id = (|Vp|/Vov)² = 14.407
```

so choosing any one of (IDSS, |Vp|, Id, Vov) fixes the rest. The datasheet's IDSS ≤ 5 mA caps the
family at Vov = 0.447 V; Vgs(off) ≥ 0.5 V floors it at 0.131 V; the load line would allow up to
1.053 V, so the datasheet binds first. **Shipped at the IDSS = 5 mA end** — Id = 347 µA, |Vp| =
1.696 V, Vs = 1.249 V, **Vd = 14.36 V** — because that is where "cherry picked to cream-of-the-crop
specs" points and because it is the *minimum-curvature* admissible point, the conservative choice for
a headroom pedal. Note the drain lands at 14.4 V, not the ~11 V mid-rail `circuit.md` estimated from
a nominal part: high `gm` at this `Id` needs a small `Vov`, which needs a small `Id`.

⚠ **The shaper's curvature is degenerate with the trainers' reamp level, 1:1, and no ratio in this
dataset breaks it.** H2 amplitude goes as `aEven × drive`, and L1/L2 leave the drive unknown. What
the datasheet cap does buy is a **one-sided bound: the reference renders were driven at ≤ 0.41 V/FS,
i.e. ≥ 6.6 dB below `kInputRef` = 0.87.** At the shipped Vov the P2 ladder's top three cells imply
0.41–0.64 V/FS, ordinary reamp territory; at the family's other end they imply 0.12–0.19 V/FS, also
plausible. **Binding on step 9: an A/B at matched DIGITAL level compares the plugin's distortion at a
drive the reference never saw.** Match the drive first, then null.

### 10.2 ⚠⚠ The structural bug: degeneration suppresses distortion TWICE, not once

Worth **16.4 dB = 20·log10(K0)**, invisible to every linear test in the suite, and shipped in the
first build. Feeding the shelf output into the shaper models the *drive* to the nonlinearity and then
stops. Local feedback also suppresses whatever the device generates inside the loop. Expanding
`id = gm·u + c·u²` with `u = vg − id·Zs` to second order:

```
order 1:   u1  = vg / k(s)              (the shelf, as before)
order 2:   id2 = c·(u1²) / k(s)         <-- the SAME shelf again, on the squared term
⇒          H2/H1 = A / (4·Vov·k²),  not  A / (4·Vov·k)
```

The stage now carries **two instances of 1/k(s)**: one on the drive path, one on the shaper's
nonlinear *excess* (`g(w) − w`). Exact to second order and structurally right above it, since every
term generated inside the loop is suppressed by it. Verified against an exact per-sample implicit
solve of `id = gm·g(vg − id·R5)` using the stage's own `shape()` (`JfetStageTest` §7):

| gate amplitude | 0.2 V | 0.5 V | 0.8 V |
|---|---|---|---|
| shelf only (the bug) | +16.34 dB | +16.13 dB | +15.67 dB |
| this model | −0.04 dB | −0.25 dB | −0.72 dB |

The residual is the Volterra truncation; **Path A (the implicit solve, with the source network's
state carried through it) is the escalation, and this table is its criterion in hand** — under 0.7 dB
of H2 at full scale, 3.1 dB at +5 dB of trim.

⚠ **An instrument trap found while proving the frequency dependence** (`JfetStageTest` §7b, which
checks that the excess is *filtered* rather than *scaled* — the prediction is
`K0²·|1/k(ω)|·|1/k(2ω)|`, since the drive is filtered at the tone and the product at the harmonic).
Reading H2 with a single `cos(2θ)` projection measures its **real part**, not its magnitude, and the
shelf gives the harmonic its own phase — so a correct model reported errors of up to **7.6 dB, rising
with frequency**, which looks exactly like "the high end is wrong". A complex correlation makes all
four points exact to 0.00 dB. Recorded in `docs/measurement-discipline.md` §1.

📌 **Two consequences.** MODE now moves distortion as `k²`: fully bypassed, H2 rises 32.7 dB relative
to the fundamental versus DARK, because the drive rises by K0 *and* the suppression is gone. This is
the qualitative shape M5 saw as "more compression in Bright at the same input", and it is a strong
prediction for the two-way pedal's session to test. And the H2-to-drive inference above only lands on
a physically admissible device *because* of this factor: under the old structure, `kInputRef` = 0.87
demanded Vov = 6.3 V, which the 22 V rail and R6 forbid outright.

⭐ **The method note.** `ChainTest`'s mode differential — the test that exists to catch exactly this
class of error — passed throughout, because the differential is a *linear* measurement and the linear
path was right. A correct frequency response is not evidence that the nonlinear path is right; the
only instrument that could settle it was an independent solve of the equation the model approximates.

### 10.3 ADAA: the same restructure made it free, and it is still off

ADAA1 is linear in the map, so `ADAA[g − id] = ADAA[g] − ADAA[id]`. Applying it to the excess alone
therefore cancels the two-point average on the linear path exactly — **ADAA's entire cost in §9.2's
table is gone** (12 kHz cost 2.99 dB → 0.00 dB at 1×), and `FeatureProfile`'s 1× verdict flipped from
a rejection to "FREE WIN — keep always on".

**It stays off, on a different column.** The cost moved rather than vanishing: averaging the map also
averages away the harmonic the device is supposed to make, and at 1× that is **1.18 dB of wanted H2**
(1.29 dB at a realistic −6 dBFS). `OSFidelity`'s own premise is that the wanted distortion must not
move with the factor, or the OS selector becomes a voicing control. And the threshold-free argument
is unchanged and now larger: **1× + ADAA is beaten outright by plain 2× by 18.0 dB of alias floor**,
for 0.83 percentage points of CPU.

📌 §9.2's "genuinely marginal 2× row" is **dissolved, not re-argued**: ADAA at 2× now makes the floor
slightly *worse* (−0.52 dB), because at 2× and above the floor is the decimation FIR's stopband
(−105.8 / −105.5 / −105.4 dBc, flat), not fold-back.

### 10.4 What §9.3's shelf restore looks like now — still per-mode, and worse

`OSFidelity` re-run after the refit. The 1× droop's **mode-to-mode spread is now 3.09 dB** (it was
1.8 dB at the placeholder `gm`), and Mid is *brighter* than 8× out to 14 kHz:

| mode | 8 kHz | 10 kHz | 12 kHz | 14 kHz | 16 kHz | 18 kHz |
|---|---|---|---|---|---|---|
| Bright | +0.43 | +0.26 | −0.17 | −0.92 | −2.09 | −3.89 |
| Dark | −0.10 | −0.47 | −1.07 | −1.94 | −3.20 | −5.03 |
| Mid | +0.50 | +0.53 | +0.42 | +0.07 | −0.62 | −1.95 |

So `dsp.md`'s "the droop is pot-independent, one fixed shelf will do" premise fails here by a wider
margin than before, exactly as predicted: the JFET's own 1/k(s) pole is inside the oversampled region
and moves with MODE, and it now sits at 12.3 kHz (Bright) and 27.4 kHz (Mid, above Nyquist at 48 kHz).
**A per-mode shelf is the right structure, and it is now unblocked** — `gm` is measured, so the pole
is no longer a moving target. At the 4× default the droop is ≤ 0.14 dB, so nothing a normal session
hears is waiting on it.

### 10.5 Known, deliberate, and NOT bugs to rediscover

1. **The load line is not modelled.** The shaper's positive limit is the channel ceiling
   (g = +3.00 V); the drain actually enters triode at **g = +0.370 V**, 8× nearer — reachable at
   w = +0.281 V, a 1.85 V gate swing, about **+6.6 dB of input trim**. The current shaper structure
   cannot carry the tighter bound (the even bump alone asymptotes at Vov/2 = 0.223, leaving the core
   0.147 V, which would bend the map inside the normal operating range). Recorded with numbers rather
   than left as "extreme settings only".
2. **`tanh²` is not the true parabola.** The exact device law is `g(w) = w + w²/(2Vov)` down to
   cutoff; the bump saturates where the parabola does not, so at a 0 dBFS input it is **4.5 % (0.4 dB
   of H2) low**, growing with drive. Smaller than the Volterra truncation above, so both are deferred
   together — an exact parabola with a cutoff clamp is the joint refinement.
3. **`beta` (the cubic) stays 0.** M5 could not settle it: H3 is under both models' error floors, and
   the only evidence is 0.09–0.35 dB of top-cell compression, which fixes the sign (compressive) and
   nothing else. Fitting it to a floor would be worse than leaving it out.
4. **`kInputRef` and `kOutputMakeup` are untouched.** Nothing here anchors them, and §10.1's bound
   constrains the *reference's* drive, not the plugin's.

---

## 11. The mode shelf's discretisation (2026-09-08). §9.3's per-mode restore dissolves.

§9.3 and §10.4 both concluded that the low-OS top-octave restore must be **per-mode**, because the
1× droop spread 3.09 dB across MODE and `dsp.md`'s prescription assumes it is pot-independent. That
conclusion was correct about the measurement and wrong about the cause. **The entire mode-dependent
part of the droop was the mode shelf's own bilinear discretisation, not physics**, and fixing the
discretisation removes it.

### 11.1 What the spread actually was

`1/k(s) = (1 + s·τ)/(K0 + s·τ)` was discretised by a plain bilinear transform. Bilinear warps every
corner down by `tan(θ/2)/(θ/2)`, and this shelf is unusually exposed to that because its pole sits
`K0` = 6.6× above its zero: 12.3 kHz in Bright and **27.4 kHz in Mid, above Nyquist at 48 kHz**.
Warping a pole that is already past Nyquist back down into the band makes the shelf reach its plateau
early, which reads as a top-octave **lift**.

Computed from the coefficients alone, that error at 48 kHz is +1.17 dB (Bright) and +3.53 dB (Mid)
re 1 kHz. `OSFidelity`'s measured Bright-minus-Dark and Mid-minus-Dark rows were +1.14 and +3.08.
**The two agree to 0.05 dB across every frequency in the table** — so the mode spread was, to within
the measurement, exactly this one line of code.

| | 8 kHz | 12 kHz | 16 kHz | 18 kHz | worst spread |
|---|---|---|---|---|---|
| 1× droop spread, before | 0.60 | 1.49 | 2.58 | 3.08 | **3.09 dB** |
| 1× droop spread, after | 0.31 | 0.27 | 0.44 | 0.50 | **0.50 dB** |

0.50 dB sits inside this project's own tolerance band (M6: 0.33 dB RMS / 0.8 dB peak on the mode
differential). ➡ **A single fixed-shape restore is admissible again; `dsp.md`'s premise holds after
all, and the per-mode structure §9.3/§10.4 called for is not needed.**

### 11.2 What replaced it, and the two candidates that lost

The shelf is now matched to the analog magnitude at **three frequencies: DC, Nyquist, and the shelf's
own log-midpoint `fz·√K0`**. A first-order section has exactly three degrees of freedom, so three
constraints determine it with nothing left to fit — and all three frequencies come from the circuit.
Closed form, no iteration, so it stays a `prepare()`-time cost. The algebra is in `JfetStage.h`.

⛔ **Prewarping both corners was tried first and is WORSE than plain bilinear** — 3.06 dB at 18 kHz
against 1.17. A first-order section has room for a separate prewarp constant in numerator and
denominator, so pinning both the zero and the pole is possible; it just pins the two ends of a
transition and lets the curve between them bow out. Measured before it was believed.

⛔ **Matching the exact complex response at the log-midpoint** (DC gain plus one full complex
constraint) also loses, at 3.19 dB for Mid at 48 kHz. Magnitude matching at three points beats phase
matching at one.

Worst |error| against the analog shelf over 20 Hz – 20 kHz:

| | 48 kHz (1×) | 96 kHz (2×) | 192 kHz (4×) | 384 kHz (8×) |
|---|---|---|---|---|
| bilinear (Mid) | 3.529 dB | 0.801 dB | 0.192 dB | 0.048 dB |
| three-point (Mid) | 0.459 dB | 0.203 dB | 0.053 dB | 0.013 dB |

Note it improves the **4× default and 8× too**, not only the low factors.

### 11.3 ⚠ The phase check that looked like a trade-off and was not

A magnitude-only design is exactly where phase gets quietly traded away, and this project has been
bitten by a phase bug that magnitude testing could not see. Checked, and the raw numbers *do* look
like a trade: Mid at 18 kHz reads −22.7° against bilinear's −13.5°.

**It is a near-constant fractional sample of extra delay, and it disappears once that delay is
removed** — which is what a sub-sample null does anyway. Scored with the best-fit pure delay taken
out (constrained through the origin, since a pure delay has zero phase at DC and both designs are
exact at DC), the new design is also **roughly twice as accurate in phase at every rate**: 3.95°
against 9.59° for Mid at 48 kHz. Better on both axes, not a trade. Had the raw number been taken at
face value the change would have been rejected.

### 11.4 ⚠⚠ Two tests were passing for the wrong reason, and a correct model failed them

Both had to be fixed before the improvement could land, and both are the same failure: **a test
written so that it can only confirm what the implementation already does.**

1. **`JfetStageTest` compared the shelf against the analytic prototype AT THE BILINEAR-WARPED
   FREQUENCY.** That is a defensible isolation — it asks "are these the right bilinear coefficients"
   — but once bilinear is what the stage computes, it is a tautology. It reported **0.0000 dB while
   the shipped filter sat 3.5 dB from the analog shelf at the base rate.** It now compares against
   the circuit's own transfer function at the real frequency, and prints what bilinear *would* have
   given alongside, so a silent revert shows up in the log.
2. **`ChainTest` asserted the mode plateau lands within 0.15 dB of `K0`** at an 80 kHz probe. `K0` is
   the shelf's **asymptote**; the analog shelf is still 0.10 dB (Bright) and 0.47 dB (Mid) short of it
   at 80 kHz, because `1/k(s)` approaches unity only as 1/f. So the assertion demanded an error — and
   the old bilinear discretisation supplied one of almost exactly the right size, cancelling to
   0.06 dB. **Fixing the discretisation broke it: the correct model failed a test the warped one
   passed.** It now compares against the analytic shelf at 80 kHz and prints the `K0` asymptote
   beside it. The failure mode it exists for is untouched — double-counting the drain lift lands the
   ratio near `K0²`, about 33 dB.

📌 `JfetStageTest` section 1c is new and is the coverage that was missing: the shelf was only ever
validated at 192 kHz (the 4× default), where the error it now catches is 6× smaller. **Sweep the
rate whenever a stage's accuracy could depend on it** — "the shelf is exact" turned out to be a
number that depends on which rate you ask at.

⚠ Section 1c also needed the **instrument's own floor measured rather than assumed**. By 8× the
errors under test are below the correlation instrument's leakage, and the comparison is asymmetric —
the bilinear column is computed in closed form while the shipped column is measured, so only one
carries that noise. DARK is a pure constant gain with exactly zero phase, so probing it gives the
floor (0.06°) for free, and the "must beat bilinear" assertion is only applied where bilinear's own
error clears it by 4×. Without that, a correct filter failed at 384 kHz.

---

## 12. The low-OS droop restore (2026-09-08). Derived, bounded, and it goes BEFORE the JFET.

§11 removed the mode-dependent part of the 1× droop. What was left is mode-independent — which is
the premise `dsp.md`'s restore was written on — so the restore became buildable. It is now in:
`src/dsp/OsDroopRestore.h`, guarded by `tests/DroopRestoreTest.cpp`.

### 12.1 It is a derivation, not a fit

The residual droop comes from one place: the input network's trapezoidal caps. A trapezoidal-cap WDF
**is** the bilinear transform of its own prototype, so the error is the network's analytic transfer
function evaluated at the warped frequency, divided by the same function at the real one. No filter
to run, nothing to measure. Checked against `OSFidelity`'s measured droop at 1×, 2× and 4×: it
predicts **every cell to 0.01 dB**.

That is the whole reason this is worth shipping. `dsp.md` describes "a single fixed-shape high-shelf,
gain set per OS factor", which leaves open whether the shape is fitted to a residual. It is not — the
coefficients fall out of `InputNetwork::analyticResponse()` at whatever (base rate, oversampled rate)
the host supplies, so the restore self-scales to every sample rate and shrinks to nothing as the
factor rises, and it bypasses itself outright when the whole droop is under 0.005 dB.

📌 The analytic transfer function moved out of `InputNetworkTest` and into `InputNetwork.h` so there
is one definition rather than two. `DroopRestoreTest` section 1 asserts it still describes the actual
WDF tree — measured, 0.0000 dB. **That check is the one that could rot silently**: without it the
restore would go on correcting a network that had changed, and every other assertion would still pass.

### 12.2 ⚠ It goes BEFORE the JFET, and that was decided by measurement

"One biquad at base rate" reads as *after the chain*, and that is what was built first. It corrects
the linear path perfectly well — and also boosts the **harmonics**, which never carried the droop:
they are generated downstream of the input network, so nothing attenuated them. Measured at 1×,
48 kHz base:

| | no restore | after the chain | before the JFET |
|---|---|---|---|
| droop @ 18 kHz | −5.03 dB | −1.69 dB | −1.68 dB |
| wanted H2 vs 8× | +0.07 dB | **+0.63 dB** | +0.16 dB |
| alias floor | −79.39 dBc | **−77.86 dBc** | −79.31 dBc |

The post-chain placement buys the same response by moving the distortion 0.6 dB and giving up 1.5 dB
of alias floor — i.e. by making the oversampling selector a voicing control, which is the one thing
`OSFidelity` exists to prevent. Pre-compensation gets the same response for 0.09 dB and 0.08 dB.
Its only cost is a little linear accuracy at 2× — measured droop at 18 kHz is −0.37 dB against
−0.19 dB post-chain — because the shelf's plateau is pinned at its own Nyquist, and that constraint
weakens as the running rate rises above the base rate.

### 12.3 ⚠⚠ Why it is bounded, and why the "better" design is the wrong one

The droop runs to −∞ at Nyquist — the bilinear zero sits exactly there — so a restore that chases the
top octave must invert a near-Nyquist zero by putting a pole beside it. That design was built and
measured first, and on a response plot it is much the better one: matching the target at 0.325·fs and
0.470·fs tracks the droop to **0.44 dB all the way to 20 kHz**. It does it with **+29 to +37 dB of
gain at Nyquist**, precisely where 1× — which has no decimation filter at all — puts its alias
products.

➡ **Rejected.** The shipped design pins its plateau to the droop's value at 19.2 kHz instead: it
corrects toward the top of the band and stops. Peak boost ≤ 6.6 dB at every rate and factor.

⛔ And the plateau cap is not a compromise forced by laziness — requiring an exact match at two
frequencies instead is **infeasible for a first-order section in 20 of the 24 (base rate × factor)
cells tested**, and in the four where it is feasible it needs 15–45 dB of boost. The unbounded design
does not exist to be chosen.

### 12.4 What it achieves

Worst |error| against the analog input network over 20 Hz – 20 kHz:

| base | 1× | 2× | 4× | 8× |
|---|---|---|---|---|
| 48 kHz, uncorrected | 7.941 | 1.088 | 0.245 | 0.060 |
| 48 kHz, restored | **3.268** | **0.604** | **0.141** | **0.035** |

and inside the band it actually corrects (48 kHz base, 1×, dB vs analog):

| freq | 8k | 12k | 14k | 16k | 18k | 20k |
|---|---|---|---|---|---|---|
| uncorrected | −0.10 | −1.07 | −1.94 | −3.20 | −5.03 | −7.95 |
| restored | +0.33 | +0.03 | −0.30 | −0.80 | −1.62 | −3.21 |

⚠ **The two design frequencies (0.25·fs and 0.40·fs) are CAPPED at their 48 kHz values.** Without the
cap a 192 kHz session designs the shelf around 48 and 76.8 kHz, entirely above the audible band, and
then slightly overshoots inside it. `DroopRestoreTest`'s "never worse than doing nothing" check
caught that at 8×. Hearing does not scale with the sample rate, so above a 48 kHz base the design
stops following it.

📌 Like `JfetStageTest` section 1c, `DroopRestoreTest` has to **measure the instrument's own floor**:
the uncorrected column is closed-form while the restored column is measured through the filter, so
only one carries the correlation instrument's leakage — and on the rows where the restore correctly
bypasses itself the entire droop is 0.004 dB, an order below that noise. A correctly-bypassed restore
read as a 0.001 dB regression until the floor was measured. **That is now twice in one session that
an asymmetric comparison — closed form against measurement — failed a correct implementation.**

---

## 13. Per-band THD/compression, and Path A — the implicit solve (2026-09-08)

Two questions, one session: *are we light on THD at the bottom, and sometimes in the treble?*, and
*build the compression mechanism, circuit-accurate if possible*. The first turned out to be mostly a
question about the reference data; the second was a real structural change.

New instrument: `analysis/band_audit.py` (per-band THD and per-band compression, plugin vs capture,
at matched drive, with a **measured floor beside every delta**). Raw output
`analysis/reports/band_audit.json`.

### 13.1 The low-frequency THD deficit is the REFERENCE's, not the model's — below ~100 Hz

At −6 dBFS, matched drive, P1's captures read up to **50 % THD at 20 Hz** and 12–37 dB more H2 than
the plugin through 20–50 Hz. None of that survives contact with a floor:

| band | P1 floor (mode spread, dB) | H2 delta, dark | clears floor by | order inversion |
|---|---|---|---|---|
| 20 | 20.2 | −37.2 | 1.8× | **H3 ≥ H2 in all three modes** |
| 31.5 | 11.7 | −15.9 | 1.4× | **H3 ≥ H2 in all three modes** |
| 50 | 8.3 | −16.1 | 2.0× | — |
| 125 | 2.8 | −11.6 | 4.1× | — |
| 315 | 1.9 | −11.2 | 6.0× | — |
| 500 | 2.0 | −9.2 | 4.7× | — |

Three independent reasons the bass rows are the models' own error:

1. ⭐⭐ **H3 comes back ABOVE H2 at 20 and 31.5 Hz in every P1 mode, and in P2-dark.** A square-law
   device cannot do that. It is the same order-inversion test that disqualified everything above H2
   in phase 1, now localised to the bass.
2. **The known-answer probe reads 20.2 dB at 20 Hz where the circuit forces 0.00.** Below the shelf
   zero all three modes have `Zs = R5`, so their H2 in dBc must be identical; they differ by 20 dB.
3. **50 % THD is physically impossible here.** At P1's calibrated −12 dBu the gate sees ~0.14 V into
   a 22 V-rail stage biased at `Vov` ≥ 0.131 V.

The mechanism is structural, not sloppiness: **all seven models have a 132 ms receptive field, which
is 2.6 periods at 20 Hz**, and the output high-pass they are trying to reproduce sits at 24–44 Hz.
⛔ **Do not fit anything to a capture below ~100 Hz.** This is the same conclusion §1's limit L3
reached from the receptive field alone, now measured on the harmonics.

⚠ **And note what the floor probe CANNOT see.** It measures mode *spread*, so an error common to all
three modes reads zero. An LF artefact from a receptive-field limit is exactly that kind of error —
the high-pass is identical in every mode — so at low frequency the probe **systematically
under-reports**, and the 8.3 dB it gives at 50 Hz is a lower bound on the real error there.

### 13.2 From 125 Hz up the deficit is real, FLAT, and already explained

Above the floor-corrupted region the deficit stops having a shape: P1 dark reads −11.6 / −8.9 /
−11.2 / −9.2 / −9.8 / −7.9 / −7.0 dB at 125 / 200 / 315 / 500 / 800 / 1250 / 1600 Hz. Mean ≈ −9.6 dB,
no trend. That is **circuit.md note #10's `Vov` result, unchanged** — a single scalar, not a
per-band shape. ➡ There is no frequency-dependent THD mechanism missing from the linear structure.

**The treble claim does not survive either, and the same test kills it.** P1-dark's 8 kHz delta
(−18.0 dB) is the biggest in the set, but the circuit forbids any frequency dependence of H2 in DARK
(`k` is constant there), and P1-dark's 5 kHz cell **fails the order-inversion test**. The captures'
top octave is each trainer's converters, as §1's limit L3 already says. P1-bright's treble deltas
(−10.7 to −13.5 dB) are simply its midband deficit continuing.

📌 **P3 is floor throughout**: its THD is flat at ≈ −30 dBc from 20 Hz to 8 kHz, which the circuit
forbids outright, since distortion moves as k². ⚠ Its H2 nevertheless rises at a clean 1.9–2.1 dB/dB,
so "moves with level like a square law" is **not** sufficient evidence that a harmonic is real —
add the frequency-shape check. ⚠ Also note `band_audit.py` applies P1's −12 dBu offset to every
capture, so **only P1's column is a valid matched-drive comparison**; P2/P3 carry an unknown ~8 dB.

### 13.3 ⭐⭐ Path A: solve the loop instead of expanding it

The compression question had a definite answer already recorded (circuit.md note #11): the model
produced **exactly 0.000 dB of compression at every band and every level**, and essentially no H3
(−115 to −150 dBc), because both are higher-order terms of an expansion truncated after the second
order. `src/dsp/JfetStage.h` now solves

```
id = gm·g(w),   w = vGate − vs,   vs = Zs(z)·id
```

per sample by Newton, replacing the shelf-on-the-excess structure. `beta` is still 0 — the cubic
content is the **loop's**, not a fitted coefficient's.

**⭐⭐ The design decision that makes this safe: the discrete source one-port is DERIVED FROM THE
SHIPPED SHELF, not from a fresh discretisation of R5 ∥ C.** Linearised, the solve gives
`W/Vg = 1/(1 + gm·Zs(z))`; demanding that equal the shelf `H(z)` §11 designed inverts to

```
gm·Zs(z) = 1/H(z) − 1 = [(1−b0) + (a1−b1)z⁻¹] / [b0 + b1 z⁻¹]
vs[n] = Rd·id[n] + C1·id[n−1] + C2·vs[n−1]
Rd = (1−b0)/(gm·b0)    C1 = (a1−b1)/(gm·b0)    C2 = −b1/b0
```

so the **small-signal response is unchanged to 1.1e-11 dB and 4.8e-15°** across three modes × four
rates × five frequencies. Everything validated against it stands: the mode differential `gm` was
fitted to, §11's three-point match, the phase residuals, `OsDroopRestore`'s premise. ⛔ Discretising
R5 ∥ C directly — the obvious thing to write — would have silently reintroduced plain bilinear on a
shelf whose pole sits above Nyquist at base rate, i.e. **exactly the 3.5 dB error §11 removed**.

✅ Two facts fall out of the algebra rather than being imposed, which is what says it is right:
**Zs(z=1) = R5 to 1e-16 in every mode at every rate** (physically it must be — the caps block DC),
and **DARK collapses to Rd = R5 with no state at all**.

### 13.4 What it produces, and the two intuitions that were wrong

DARK, 192 kHz, third-harmonic and compression the truncation could not make:

| gate V | Path A comp | Path A H3 | truncated comp | truncated H3 |
|---|---|---|---|---|
| 0.2752 (P1's calibrated drive) | −0.004 dB | −77.0 dBc | −0.000 dB | −114.4 dBc |
| 0.7830 (0 dBFS at `kInputRef`) | **−0.032 dB** | **−57.6 dBc** | −0.002 dB | −79.5 dBc |
| 1.5000 | **−0.178 dB** | **−41.2 dBc** | −0.018 dB | −60.9 dBc |

⭐ **Cross-implementation check: `analysis/compression_audit.py`'s independent Python oracle, written
weeks earlier, reported −0.032 dB at 0.783 V. The C++ solve reads −0.0324.** Neither can inherit the
other's bug. `JfetStageTest` section 8d asserts it.

⚠ **INTUITION 1 THAT WAS WRONG: a warm start is worse than a cold one, decisively.** Per-sample
implicit solvers normally warm-start from the previous sample, and at 8× the signal barely moves —
so it looks obviously right. Worst Newton residual after two iterations (volts of gate drive):

| start | 48k Dark | 48k Mid | 384k Dark | 384k Bright |
|---|---|---|---|---|
| cold (closed-form linear root) | 1.3e-02 | 1.8e-04 | 1.3e-02 | 1.2e-11 |
| warm (previous solved w) | 7.4e-01 | 3.8e-01 | 8.2e+00 | 7.8e-04 |

The closed-form start **already inverts the dominant linear term exactly**, which is 85–97 % of the
answer; the previous sample's `w` carries the whole sample-to-sample change as error.

⚠ **INTUITION 2 THAT WAS WRONG: the residual is the wrong criterion for the iteration count.** Two
iterations leave 13 mV of residual at the loudest reachable state (3.0 V of gate drive = 0 dBFS at
`kInputRef` with input trim at +12 dB), which looks alarming. Measured on what is audible instead:

| iterations | worst H1 err | worst H2 err | worst H3 err | CPU at 4× | CPU at 8× |
|---|---|---|---|---|---|
| 1 | 0.114 dB | 0.59 dB | 1.64 dB | — | — |
| **2 (shipped)** | **0.002 dB** | **0.02 dB** | **0.02 dB** | **4.8 %** | **9.0 %** |
| 3 | 0.000 dB | 0.00 dB | 0.00 dB | 6.2 % | 11.9 % |

The 13 mV is a **one-sample transient at a discontinuity**, which is why it never reaches the
harmonics. 0.02 dB sits ~20× below this stage's own shelf error and ~300× below the captures'
harmonic floor, so a third iteration would buy precision nothing else in the model can use.

📌 **CPU is no longer a non-issue, and §9.4's verdict needs amending.** Path A costs **2.4× the
truncated path**: 2.07 → 4.78 % at the 4× default, 3.51 → 8.95 % at 8×. Still comfortable for one
stereo instance, but "polyphase IIR not worth scoping" was decided when the whole chain was 2 %.
📌 **Two attempts to recover it, one worthless and one real.** A fused shape-and-slope function
measured **exactly free** (5.01 vs 5.05 %) — both are inline on the same argument, so the compiler
had already shared the subexpressions — and was removed rather than kept. What did help: the ADAA
state advance was running an antiderivative **every sample even with ADAA off**, which is the shipped
state at every factor. Gating it saved 0.24 pp at 4× and 0.52 pp at 8×, at the cost of one sample of
stale state if ADAA is ever enabled mid-stream (the processor sets it in `configure()`, beside a
`reset()`, so it never is). The rest of the cost is the iteration count and nothing else.

✅ **The cubic it produces is a real cubic**, checked end-to-end rather than only at the stage: through
the whole chain the plugin's H3 now rises **2 dB per dB in dBc** — 3 dB/dB absolute, exactly what a
third-order term must do — and sits at −60 to −110 dBc where the truncation gave −115 to −150.
⚠ It is still 25–40 dB under P1's captured H3, and that is NOT actionable: note #10 established that
**P1's H3 is its own floor** (it rises 0.75–1.81 dB/dB where a cubic needs 3.0). The units whose H3
does clear its floor, P2-bright and P3, have no known reamp level.

### 13.5 ⚠⚠ The compression known-answer probe has a validity condition nobody had written down

The probe (below the shelf zero all modes have `Zs = R5`, so they must agree) is used in three
flavours — magnitude (note #7), harmonic (#10), compression (#11). **The compression flavour needs
3f below the zero, not f**, because compression is a third-order quantity and reads the loop at 3f.
Measured on the plugin, where the answer is known by construction:

| probe f | 23 Hz | 94 Hz | 492 Hz | 797 Hz |
|---|---|---|---|---|
| model's own spread | 0.0017 dB | 0.0015 | 0.0201 | **0.0585** (3f = 2391 Hz, past the zero) |

`compression_audit.py`'s top known-zero band was **800 Hz**, where the probe carries ~0.06 dB of its
own systematic error. ✅ **Re-measured with that row dropped, the reported floors do not move at all**
— 0.145 dB (P1) and 0.210 (P2) either way — so some lower band is the binding one and the probe's
error was never reaching the answer. Both figures are now reported so that stays checkable rather
than assumed. ⚠ The correction still matters for the *plugin* side, where 0.06 dB would otherwise be
30× the model's real 0.0017 dB spread, and for any future dataset whose binding band is nearer the
top of the list.

⭐ **A second effect hides under the first, and only this probe sees it.** At the lowest probe
frequency the spread stops falling, at 0.0017 dB — not the 3f effect (it does not scale with f) and
not a modelling error. It is the **fixed iteration count leaving a mode-dependent bias**: `Rd·gm`
differs ~30× between Dark (5.59) and Bright (0.17 at 192 kHz), so the same two iterations converge
to different depths per mode. Converging the solve collapses it to 0.00005 dB. At 85× below the
capture floor it changes nothing, but it is exactly the residue that would later be mistaken for a
real mode asymmetry, so `JfetStageTest` 8e bounds both numbers separately.

### 13.5b 📌 ADAA's profile moved a long way, and the conclusion did not

`FeatureProfile` re-run under Path A. ADAA is implemented inside the residual (the form `dsp.md`
sanctions for a map in an implicit solve) and on the EXCESS only, so its linear cost is still exactly
0.00 dB at 12 kHz at every factor. What changed is its value:

| factor | alias improvement | 12 kHz cost | CPU pp | verdict |
|---|---|---|---|---|
| 1× | **+16.71 dB** | +0.00 | +0.24 | free win in isolation |
| 2× | −5.30 dB | +0.00 | +0.50 | now makes aliasing WORSE |
| 4× | −15.86 dB | +0.00 | +0.97 | worse |
| 8× | −14.59 dB | +0.00 | +1.90 | worse |

At 1× ADAA is now worth **+16.7 dB** against §9.2's +5.6 dB, because Path A's nonlinearity generates
real higher-order content for it to work on. ⛔ **It stays off anyway**, for the same reason as
before and by a smaller margin: 1× + ADAA is beaten outright by plain 2× by 10.3 dB of alias floor
for 1.3 pp of CPU. ✅ §9.2's "genuinely marginal 2× row" is now unambiguous — past 2× the floor is
the decimation FIR's stopband, and ADAA only adds to it.

### 13.6 Where the compression gap now stands — and it is `Vov`, coupled as note #11 warned

End-to-end (`analysis/compression_audit.py`, matched drive, 8×), the plugin now moves in the right
direction on every axis it should: **Bright compresses more than Dark, and more at high frequency**,
which is the physically correct sign the captures show. It is still 10–20× short in magnitude
(plugin −0.002 to −0.038 dB against captures' −0.03 to −1.74).

That gap is `Vov`, and note #11's coupling warning is now discharged in one direction: the truncation
is gone, so applying `Vov` no longer ships a stage that is quantitatively wrong in its own operating
range. Path A across the admissible bracket, DARK, all derived parameters moved consistently:

| `Vov` V | comp at 0.2752 V | H2 | H3 | comp at 0.7830 V | H2 | H3 |
|---|---|---|---|---|---|---|
| 0.4469 (shipped) | −0.004 dB | −49.1 | −76.2 | −0.032 dB | −39.5 | −57.6 |
| 0.2500 | −0.012 | −43.9 | −66.4 | −0.143 | −32.4 | −43.4 |
| 0.1502 (note #10) | −0.036 | −39.1 | −56.7 | **−0.808** | −21.0 | −27.5 |
| 0.1310 (datasheet floor) | −0.049 | −37.6 | −53.7 | −1.100 | −18.8 | −25.7 |

⛔ **`Vov` is still NOT applied**, and the three surviving reasons from note #10 are unchanged: the
−12 dBu is a recollection nothing in the data can re-derive; the capture floor pins `Vov` only to
about a factor of 1.4; and it moves the drain 14.4 → 19.4 V, which **invalidates the load-line
arithmetic in `JfetStage.h`**. The fourth reason (the truncation) is now discharged. ➡ Apply it
together with a re-derived load line, or wait for a calibrated capture.

---

## 14. The three unread measurements (2026-09-08): floors, and the twin-tone segment

`analysis/imd_and_floors.py`, raw `analysis/reports/imd_and_floors.json`. No new captures. Run
because two helpers had been sitting in `analyze.py` uncalled since the harness was written and no
script had ever touched an `imd_guitar_*` segment.

### 14.1 ⭐⭐ The reference captures have no noise and perfect repeatability — so "floor" has meant the wrong thing

| capture | noise floor | repeatability |
|---|---|---|
| P1 bright / dark / mid | −117.7 / −112.1 / −131.7 dBFS | −122.0 / −123.7 / −122.6 dB |
| P2 bright / dark / mid | −107.6 / −124.0 / −112.7 dBFS | −135.1 / −114.9 / −114.3 dB |
| P3 mid | −99.2 dBFS | −115.5 dB |

The repeatability figure is the residual between a cell and a byte-identical duplicate of it placed
elsewhere in the signal. At −114 to −135 dB these models are, for practical purposes, **deterministic
and memoryless-repeatable**. ➡ **The 4–9 dB harmonic "floor" this project quotes is therefore NOT
NOISE. It is the models' systematic error.** Three consequences that change how every floor number
here should be read:

1. ⛔ **It will not average down.** More cells, longer dwells and repeat measurements buy nothing.
   Any plan that assumed a noise floor could be beaten by more integration is void.
2. **It is a fixed function of the signal, not a random variable.** Quoting it with a ± implies a
   spread it does not have.
3. ⚠ **The mode-spread probe measures a DIFFERENCE of two systematic errors**, which can partially
   cancel. That is a second reason it under-reports, on top of the common-mode blindness §13.1
   already recorded.

### 14.2 ⚠⚠ The twin-tone segment cannot measure intermodulation, by arithmetic

It is 220 Hz + 660 Hz, and **660 = 3 × 220 exactly**. Every product of a memoryless nonlinearity is
`m·f1 + n·f2 = (m + 3n)·220`, so all of them land on the 220 Hz harmonic grid:

| product | lands on | collides with |
|---|---|---|
| f2 − f1 = 440 | 2·f1 | H2 of f1 |
| f2 + f1 = 880 | 4·f1 | H4 of f1 |
| 3·f1 = 660 | f2 | **the second input tone** |
| 2·f1 − f2 = −220 | f1 | **the first input tone** |

So no product is separable from harmonic distortion, and **neither input tone is a clean amplitude
reference**, each being contaminated by a third-order product of the other. The contaminations are
third order and this stage is even-dominant, so the reference error is small but not zero, and no
"use the other tone" workaround escapes it. ➡ The test signal is append-only, so fixing this means a
NEW segment with an **inharmonic** pair, 220 Hz with 1234 Hz say, where the products separate
completely. Worth adding whenever the signal is next revised; not worth a re-capture on its own.

### 14.3 ⭐ What it can still do: an off-grid known-answer probe, and the answer is clean

A memoryless polynomial driven by these two tones can place energy ONLY on the 220 Hz grid. Every
capture reads **−136.5 to −136.8 dBc at half-integer multiples**, identically at all three levels,
which is the files' own quantisation floor rather than a measurement of anything. ➡ **The reference
models put essentially nothing off the harmonic grid.** Their error is wrong AMPLITUDES on the
correct bins, not spurious junk, and there is no evidence in this segment of memory effects or
aliasing artefacts in the references.

### 14.4 The instrument validates on the plugin, and then splits the captures the same way §13 did

Level slope of each product, in dBc. A second-order product must rise 1.0 dB/dB, a third-order 2.0.

| | 2f1 = f2−f1 | f1+f2 | 2f2 | 2f2−f1 | f1+2f2 |
|---|---|---|---|---|---|
| **required** | 1.0 | 1.0 | 1.0 | 2.0 | 2.0 |
| **plugin** | **1.00** | **1.00** | **1.00** | **2.00** | **2.00** |
| P1 dark | 0.94 | 0.10 | −0.46 | 0.20 | 0.13 |
| P1 mid | −0.05 | 0.77 | 0.77 | 0.21 | −0.27 |
| P2 bright | 0.72 | 1.10 | 1.05 | 1.64 | 0.95 |
| P3 mid | 1.00 | 1.03 | 1.02 | 0.63 | 2.08 |

⭐ The plugin returns the textbook slopes exactly, which is the known-answer check that says the
instrument works. ⭐⭐ **The captures then reproduce circuit.md note #10's split from a completely
different signal and different bins: P1's twin-tone products are level-independent and therefore
floor, while P2-bright and P3 clear theirs.** P1's third-order bin reads −60.6, −58.5, −59.0 dBc
across 16 dB of input, i.e. flat. P2-bright's climbs −87.3 → −77.6 → −61.0, a real 1.64 dB/dB.

**What the comparison says where it is valid.** P1's second-order products are short by 6 to 15 dB,
which is the same deficit the single-tone H2 grid reports, on a two-tone signal at a higher crest
factor and in different bins. That is independent corroboration of the `Vov` story rather than a new
lever. The third-order products are 40 dB short, but P1's are floor and P2/P3 have no known drive.

➡ **Net: no new fitting leverage, and the same conclusion by a third route.** The cruel split is
confirmed, not broken: the calibrated unit's nonlinear data is floor, and the units whose nonlinear
data is real are uncalibrated. **Asking the P2 and P3 trainers for their `input_level_dbu` remains
the single highest-value action available, and it is one message.**

---

## 15. Test-signal audit, and the capture level that removes the whole problem (2026-09-08)

Prompted by the owner asking whether the signal needs revising, and noting that −12 dBFS is where
strummed rhythm guitar averages and is the calibration level other amp sims use.

### 15.1 −12 dBFS is already covered, and the signal is not the limitation

The compression ladder runs −46 to −1 dBFS in 5 dB steps, so it samples **−11 dBFS, one dB from the
stated operating point**, and its cells hold 48 cycles so they double as a harmonic read. The tone
grid's nearest rows are −16 and −6, but H2 rises a clean 1 dB per dB between them, so interpolation
there costs nothing. ➡ **No level needs adding.**

### 15.2 ⭐⭐ The real hole is COVERAGE, and it is the capture rig's level, not the signal's

At P1's −12 dBu the rig delivers 0.2752 V per full scale, which is 10.00 dB below `kInputRef`. So
every capture cell lands far lower on the gate than its digital level suggests:

| capture dBFS | gate volts | the user dBFS that equals it |
|---|---|---|
| −26 | 0.0124 | −36.0 |
| −16 | 0.0392 | −26.0 |
| −6 | 0.1241 | −16.0 |
| **−1 (hottest)** | **0.2207** | **−11.0** |

➡ **The reference constrains the model only up to a user playing at −11 dBFS. Above that it is
extrapolating.** Rhythm guitar at −12 dBFS average is just inside that, but with a ~12 dB crest
factor its peaks reach 0 dBFS, i.e. **0.783 V on the gate**, which P1's rig would need +10 dBFS to
produce. The model's behaviour through the loudest 11 dB of ordinary playing is unconstrained by any
capture in the set.

### 15.3 ⭐⭐⭐ −2 dBu is the number, and it is exact

| `input_level_dbu` | V/FS | vs `kInputRef` |
|---|---|---|
| −12 (P1) | 0.2752 | −10.00 dB |
| −4 | 0.6912 | −2.00 dB |
| −3 (≈ P3) | 0.7755 | −1.00 dB |
| **−2** | **0.8701** | **+0.00 dB** |
| 0 | 1.0954 | +2.00 dB |

`0.7746 × 10^(−2/20) × √2 = 0.8701`, and `kInputRef` is 0.87. **Capture at −2 dBu and the reference's
digital levels map 1:1 onto the plugin's**: matched-drive A/B becomes matched-level A/B, the drive
offset vanishes, the existing signal covers the whole operating range with nothing left over, and the
aEven-versus-reamp-level degeneracy cannot recur for that unit. This supersedes §5's vaguer "aim near
P3's level". ⚠ `kInputRef` is itself still an assumption, so this pins the capture TO the plugin's
declared calibration rather than pinning the calibration; the bypassed capture is still what anchors
`kInputRef` itself.

### 15.4 Two real signal defects, neither needing a re-capture

1. ⚠⚠ **The twin-tone pair is harmonically related** — 220 and 660 Hz, 660 = 3 × 220 — so it cannot
   measure intermodulation at all (§14.2). Needs a NEW segment with an inharmonic pair.
2. ⚠ **The IMD levels break the signal's own design rule.** `gen_test_signal.py` states that sweep,
   compression and tone levels are drawn from ONE grid so the three instruments can be cross-checked
   at the same input level. The IMD levels are −19, −11, −3, and **−19 and −3 are not on that grid**.
   So the one segment measuring a different physical quantity is also the one that cannot be compared
   to the others at matched level. Put a replacement pair on the shared grid.

📌 **Not a defect, though it reads like one:** the 20 Hz tone cell is 29.5 cycles long and perfectly
well formed. The reference models' 132 ms receptive field sees only **2.6 periods** of it, which is
why that band is floor (§13.1). No change to the signal can fix that.

### 15.5 ⚠⚠ dBu versus dBFS, and where in the chain a level is measured (worth 24 dB)

Raised because the same rig is described both as **−12 dBu** and as **+12.2 dBu = 3.156 V RMS =
4.46 V peak at 0 dBFS**. **Both are correct**, and they describe different points:

| point in the chain | level | V per full scale |
|---|---|---|
| interface output at 0 dBFS | +12.2 dBu | 4.4626 V peak |
| reamp box attenuation | −24.2 dB | — |
| **pedal input jack — this is what NAM's `input_level_dbu` means** | **−12 dBu** | **0.2752 V peak** |

`4.4626 / 10^(24.2/20) = 0.2752`, so the two are the same rig with an ordinary reamp box between
them. ➡ **Always state where a level is measured.** Taken at the wrong point this is a 24.2 dB error,
which would have inverted every harmonic conclusion in §13 and §14.

**The circuit settles it independently.** Solving `H2/H1 = A_gate/(4·Vov·k²)` for the rig's V/FS from
P1's measured H2, over the datasheet-admissible `Vov` range, gives **V/FS ∈ [0.103, 0.999] V**, i.e.
**−20.6 to −0.8 dBu at the jack**. That excludes the interface-side figure by 13 dB and is consistent
with −12 dBu. It is a wide bracket and does not pin −12 against −5, but it moves note #10's blocker
(1) from "rests entirely on a recollection" to "corroborated in magnitude by measurement".

### 15.6 `kInputRef`: the owner's level is correct, and it makes the LOAD LINE the main event

⚠⚠ **A CLAIM IN THE FIRST DRAFT OF THIS SECTION WAS WRONG AND IS CORRECTED HERE.** It said 4.46 V/FS
was "forbidden by the device" because a 0 dBFS peak puts 4.0 V on the gate, "2.4× past pinch-off".
**That comparison ignores the degeneration, which is the entire job of R5.** The source follows, so
the effective gate-source drive is the gate swing divided by `k`, and at low frequency `k = K0 = 6.59`:

| gate volts | effective vgs `w` | w / Vov | source vs |
|---|---|---|---|
| 0.783 | 0.108 V | 0.24 | 0.675 V |
| 1.009 | 0.137 V | 0.31 | 0.873 V |
| 2.000 | 0.254 V | 0.57 | 1.747 V |
| 4.016 | 0.489 V | 1.09 | 3.527 V |

4 V at the gate is **0.49 V across the junction**, against a pinch-off of 1.696 V. Not close. The
model handles it and returns about 2 dB of compression. ➡ **Comparing an input swing to |Vp| on a
degenerated stage is a category error.** It is the same trap `circuit.md` stage 2 warns about from the
other direction, and it slipped through here.

**The owner's level is right.** A well-recorded guitar metering −12 dBFS RMS does measure about
0.78 V, which fixes `kInputRef ≈ 4.46 V/FS`. It is a statement about guitars and recording practice,
not about this circuit, and nothing in the circuit contradicts it.

⭐⭐ **What it does change is which unmodelled mechanism matters.** The real limit is the LOAD LINE,
not pinch-off: `Vds = VA − Id·(R6 + R5)` falls to the instantaneous overdrive at `w = 0.245 V`, a
**1.62 V gate swing** (`JfetStage.h` records 1.85 V on a slightly stricter criterion). Where that
lands depends entirely on `kInputRef`:

| `kInputRef` | gate at 0 dBFS | load line reached at | verdict |
|---|---|---|---|
| **0.8700, shipped** | 0.783 V | **+7.5 dBFS** | unreachable without input trim — a corner case |
| 1.5000 | 1.350 V | +2.7 dBFS | reachable on peaks |
| **4.4626, the owner's calibration** | 4.016 V | **−6.7 dBFS** | **the top 6.7 dB of every normally-tracked take** |

➡ **At 0.87 the load line is a documented corner case reachable only at +6.6 dB of trim. At 4.46 it
is the pedal's character on every pick attack.** `JfetStage.h` currently does not model it at all, so
at the owner's calibration the stage would return smooth 2 dB compression exactly where the real
circuit slams its drain into triode.

⚠ **So this is the THIRD instance of the same coupling pattern on this project** (truncation with
`Vov`, `Vov` with the load line, now `kInputRef` with the load line): **applying `kInputRef` alone
would ship a stage that is quantitatively wrong precisely where users play.** Do the two together.
📌 Note `kInputRef` cancels out of the linear gain staging — `processBlock` multiplies by it going in
and divides by it coming out — so changing it is a pure "how hard is the circuit driven" control with
no level side effect, and it does not invalidate any existing fit, since every capture comparison is
made at matched DRIVE computed from the ratio.

⭐ **The measurement that would confirm it costs one extra pass during the capture session:** record a
DI of the guitar through the same interface input at a noted gain setting. The DI's peak dBFS, with
the interface calibration known, gives the guitar's actual peak volts, which IS `kInputRef` directly.

---

## 16. The device model (2026-09-09): kInputRef, the load line, and the shaper that had to go

One change with three parts, done together because they are not separable. Code and reasoning in
`src/dsp/JfetStage.h`; guarded by `tests/JfetStageTest.cpp` section 8. All 11 tests pass; `auval`
passes.

### 16.1 The causal chain — the calibration did not cost the CPU, it changed what had to be modelled

1. **`kInputRef` 0.87 → 4.4626 V/FS.** A well-recorded guitar metering −12 dBFS RMS measures ~0.78 V
   RMS, and a full-scale sine at that calibration is 3.156 V RMS = 4.4626 V peak. This is the owner's
   own tracking calibration and the one the capture session will use.
2. **That moves where a performance sits on the device curve.** The drain enters triode at a **1.691 V
   gate swing** — a property of the rail, R6 and the bias point, unchanged by any of this. At the old
   calibration that was **+7.5 dBFS**, i.e. unreachable without input trim. At the new one it is
   **−7.5 dBFS**, i.e. the top 7.5 dB of every normal take.
3. **So the load line had to be built.** It had been deliberately deferred as an extreme-settings
   corner case, correctly, under the old calibration.
4. **Building it forced the shaper out.** The fitted shaper's even bump ALONE asymptotes at
   `Vov/2` = 347 µA, and the load-line ceiling is ~542 µA — 64 % of the budget gone before the core
   gets any. `JfetStage.h` had recorded that as a known impossibility and it was right.
5. **The replacement is the device's own equations**, Shichman-Hodges with cutoff and triode, solved
   implicitly against the source one-port AND the drain load. `aEven`, `bumpScale`, `beta`,
   `limitPos`, `limitNeg` are all gone; `Vov` is the single amplitude parameter.

### 16.2 What the stage now does, and it is finally shaped like a preamp

DARK, 192 kHz, 107 Hz probe, at the shipped `Vov`:

| input | gate V | in triode? | compression | H2 | H3 |
|---|---|---|---|---|---|
| −24 dBFS | 0.253 | no | −0.003 dB | −49.7 dBc | −78.4 |
| −12 dBFS | 1.009 | no | −0.057 dB | −36.6 dBc | −52.6 |
| −6 dBFS | 2.013 | no | −0.649 dB | −22.0 dBc | −27.7 |
| −3 dBFS | 2.843 | **YES** | −2.369 dB | −19.5 dBc | −17.1 |
| 0 dBFS | 4.016 | **YES** | −4.903 dB | −21.3 dBc | −12.9 |

Clean through normal playing, gritting up on peaks, and going odd-dominant once the drain bottoms —
which is what a stage clipping against cutoff and triode must do.

### 16.3 ⚠⚠ The solve got much harder, and plain Newton does not work on it

The square law is not bounded the way the fitted shaper was, and `F'` runs from 1 in cutoff to ~40 in
strong saturation. Plain Newton **cycles** — measured doing exactly that, a period-3 orbit with the
residual stuck near 1e-2 A regardless of iteration count. A warm start does not fix it and makes it
worse (stuck at 2.7e-2 A at every count). The fix is a **safeguarded Newton**, and the bracket is
free and tight: `lo = −Id0` (cutoff) and `hi = (Vds_q − vOff)/(zLoad + Rd)` (drain bottomed), about
980 µA wide.

⚠ **The bracket comparison must be NON-STRICT.** The root sits exactly ON an end whenever the device
is in cutoff or the drain is bottomed, which are not corner cases here — they are what the loud
half-cycles do. With strict inequalities a converged iterate is rejected as "outside", the solve
bisects away from the answer, and **more iterations make it worse**. Measured doing that.

⚠⚠ **And do NOT "improve" the final iterate with `i = I(...) − Id0`.** That is a fixed-point step and
this map is not a contraction: `|dI/di|` reaches ~8.8 in the normal range, so it MULTIPLIES the error.
The old structure did exactly that and it was mild enough to look harmless; against the square law it
sent a 3 V input to 9.5 mA of drain current, past IDSS.

### 16.4 The iteration count is 8, chosen on harmonics — and the CPU story was mostly a bug

Harmonic error against a 40-iteration solve, worst cell (Mid, 48 kHz):

| iterations | 3 | 4 | 5 | 6 | 8 |
|---|---|---|---|---|---|
| H2 error at 0 dBFS | −14.52 dB | −6.55 | −2.52 | −1.04 | **0.00** |
| H2 error at −12 dBFS | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |

⭐ **Ordinary playing needs THREE; it is the load-line region that needs eight.** ⛔ And a hard input
discontinuity is NOT what drives it — first guess, refuted: a bandlimited tone and the same tone with
steps converge at the same rate to within a factor of two.

📌 **CPU: 4.78 % → 5.74 % at the 4× default, 8.95 % → 10.97 % at 8×.** A 20 % rise, not the tripling
an intermediate measurement showed. ⚠⚠ **That intermediate figure was a STALE-PARAMETER BUG in my own
probe**, and it is the exact trap `ProbeHarness`'s `Setup` comment warns about: `setSolveIters` was
applied conditionally, so a "shipped" configuration inherited the previous run's count. PerfBenchmark's
shipped column read 11.71 % against the 20-iteration column's 11.76 % — it was measuring 20 twice.
`setSolveIters(0)` now RESTORES the shipped count rather than leaving it.

### 16.5 ⚠ FeatureProfile under-discriminates on this, and says so

Its three columns are chain-level and largely blind to solve convergence: the alias and 12 kHz
figures are taken at +12 dB of trim where clipping swamps it, and even H2 moves under 0.2 dB from 2
iterations to 20. An earlier cut read **2 iterations as "converged — buys nothing"** when a
stage-level probe put its H2 14.5 dB out. ➡ `JfetStageTest` 8c is the authority: it drives the STAGE
at a known gate voltage. FeatureProfile's CPU column is exact and is what it is now kept for.

### 16.6 What was preserved, and what was given up

✅ **Preserved.** Small-signal response identical to the derived source one-port at **1.6e-10 dB and
4.7e-10°** over 3 modes × 4 rates × 5 frequencies. `Zs(z=1) = R5` exactly everywhere. Compression at
0 dBFS-equivalent drive still matches `compression_audit.py`'s independent Python oracle
(**−0.0321** against −0.032). ⭐ And the mode-dependent bias §14 found in the old fixed-count solve is
**gone**: 0.000044 dB at the shipped count and fully converged alike.

⛔ **Given up: ADAA.** ADAA1 needs a memoryless 1-D map whose argument is near-linear between samples.
The map is now `I(Vov_i, Vds_i)` — two arguments, the second a function of the output current — so the
derivation does not apply, and no closed-form antiderivative of the composite exists to substitute.
It was already off at every shipped factor, and FeatureProfile's last run under the old structure put
1× + ADAA as beaten outright by plain 2×. If it is ever wanted back, the saturation branch alone is
1-D in `w` with the trivial antiderivative `beta·(Vov + w)³/3`; triode would fall back to plain
evaluation, which makes the behaviour signal-dependent, so let the profile decide first.


---

## 17. The optimisation pass (2026-09-09): the solve had an algebraic answer all along

Prompted by "5 % seems high for such a simple circuit", with a suggestion of a fast `tanh`.
📌 **There is no `tanh` left to speed up** — the device-model rewrite (§16) removed the fitted
shaper, and what replaced it is pure polynomial with no transcendentals at all. The 5 % was the eight
Newton iterations, and the instinct that it was too much was right.

### 17.1 ⭐⭐ Shichman-Hodges is piecewise QUADRATIC, so every branch has an exact root

Both networks around the device are LINEAR in the drain current: the source one-port is
`vs = Rd·i + vOff` and the load line is `Vds_i = Vds_q − i·zLoad − vs`. Substituting them into a
quadratic device law leaves a quadratic in `i`. With `A = Vov + vGate − vOff` and
`B = Vds_q − vOff`, `R = zLoad + Rd`:

| branch | equation in `i` |
|---|---|
| saturation | `β·Rd²·i² − (2βA·Rd + 1)·i + (βA² − Id0) = 0` |
| triode | `β·R·D·i² + (1 − β(B·D − R·C))·i + (Id0 − β·B·C) = 0`, `C = 2A − B`, `D = zLoad − Rd` |
| cutoff / drain bottomed | `i = −Id0`, no solve at all |

Branches are tried in the order they occur in normal use, so the common case costs **one square
root**. Because the composite map is monotone and C1, exactly one branch's root satisfies its own
validity condition — that is the selection criterion, with no tolerance to tune.

### 17.2 ⚠⚠ The port dropped a root the prototype had, and it was worth 0.9 mA

The prototype tested **both** roots of each quadratic against the branch conditions. The first C++
port kept only the cancellation-stable *small* root, `c/q`, on the reasoning that the physical root is
the small one. That is true only while `b < 0`, and `b = −(2βA·Rd + 1)` flips sign at
`A = −1/(2β·Rd)`, i.e. at about **−0.53 V of gate drive**. Below that the stage fell through to the
cutoff branch and returned `−Id0` for every sample — **a 0.9 mA error on a 0.35 mA quiescent
current**, on roughly half of every waveform.

➡ **Root selection is by each branch's own physical validity condition, never by magnitude.** Both
roots are computed, in the stable pair `q/a` and `c/q`, and each is offered to the branch test.
⚠ The stable pair still matters: in saturation `a = β·Rd²` falls to 2.7e-6 at 384 kHz in Bright while
`b ≈ −1`, so the schoolbook small root is a difference of nearly equal numbers exactly where the
stage is quietest.

### 17.3 What it costs, and what it does not change

CPU as a percentage of realtime, stereo, per solve strategy:

| factor | closed form | iterative ×8 | iterative ×20 |
|---|---|---|---|
| 1× | **0.48 %** | 1.34 % | 2.80 % |
| 2× | **1.40 %** | 3.13 % | 6.07 % |
| **4× (default)** | **2.24 %** | 5.74 % | 11.64 % |
| 8× | **3.91 %** | 11.05 % | 22.84 % |

⭐ **The full device model with its load line now costs LESS than the fitted-shaper approximation it
replaced** (4.78 % at the 4× default) and is level with the original truncated model (2.07 %). The
accuracy columns are identical to 0.000 dB on both axes, because the closed form is not an
approximation of the iterative solve — it is the same equations solved algebraically.

The iterative solve is **retained**, not deleted: it is the independent oracle `JfetStageTest` 8c
checks the closed form against on every run, reaching the same answer by a completely different
method so neither can inherit the other's bug. Agreement is **1e-17 A absolute** across three modes ×
three rates on a two-tone signal with periodic +12 dB-trim steps. `FeatureProfile` A/Bs the two and
fails if their outputs ever differ.

### 17.4 ⚠ And a calibration bug the same pass had to fix first

Three analysis scripts each carried their own `PLUGIN_VFS = 0.87`. When `kInputRef` moved to 4.4626
all three kept computing matched-drive offsets that were **silently 14.2 dB wrong** — every harmonic
and compression comparison made with them since. `captures.plugin_vfs()` now PARSES the constant out
of `src/PluginProcessor.h`, so the two cannot drift and a rename raises instead of returning a stale
number. ➡ **A duplicated calibration constant is not a style problem. It is a measurement that
reports the wrong answer without failing.**

## 18. Three measurements, no new data (2026-09-09): Vov fitted, the HF dip explained, phase reconciled

The three items `docs/HANDOVER.md` listed are closed. Instruments `analysis/vov_fit.py`,
`analysis/hf_shape_fit.py`, `analysis/phase_reconcile.py`; raw JSON alongside them in
`analysis/reports/`. Circuit consequences are `.claude/rules/circuit.md` notes **#16–#18**, and the
`Vov` reasoning lives in `src/dsp/JfetStage.h` where the parameter is. **No DSP constant changed.
All 11 tests pass, warning-free.**

One production-adjacent change: **`OfflineRender --vov V`**, a measurement flag beside
`--input-scale`. It is not a plugin control — every other quantity in the device model (`Id0`,
`beta`, `|Vp|`, `IDSS`, `Vds_q`) is derived from `Vov`, so the parameter cannot be swept from
outside the stage, and fitting it means sweeping it. It reaches the DSP through a `setVov` test hook
alongside the existing `setSolveIters` / `setUseClosedForm` ones.

### 18.1 `Vov` — two routes, one answer, still not shipped

| Route | Observable | Order | Fitted `Vov` |
|---|---|---|---|
| A | H2 in dBc, 125–800 Hz, 3 modes × 5 bands × 4 levels | 2nd | **0.126** (dark 0.136, mid 0.133) |
| B | compression GROWTH over the top 10 dB, 5–8 kHz | 3rd | **0.165** (per cell 0.127–0.182) |

A factor of **1.31** apart, inside the ~1.5 the reference's floor allows, and bracketing note #10's
independently-derived 0.150. The shipped **0.4469 sits outside both by about 3×**. Every route is
validated on a plugin render at an off-grid `Vov` = 0.2200 first (`--self-test`), and both recover it.

⚠⚠ **Two method traps, either of which silently kills route B:**
- **`comp_db` is the wrong observable; its GROWTH with level is the right one.** The reference models
  carry a level-independent per-band gain error that `comp_db` reports as compression — P1-dark reads
  −0.089 / −0.087 / −0.088 dB across the top 10 dB at 5 kHz, which is an offset, not compression.
  Differencing across level cancels it exactly and takes the route's floor from **0.145 dB to
  ~0.013 dB** — the worst DARK increment, where the circuit permits almost nothing. ➡ Difference out a contaminating constant rather than widening the tolerance.
- **Do not pool by median across modes.** DARK has `Zs = R5` at every frequency, so it barely
  compresses and its cells do not move with `Vov`. A median over three modes sits on that one:
  pooled that way the statistic moved **0.01 dB over the whole admissible range** and looked like an
  observable with no leverage. Bright's 8 kHz cell spans 0.85 dB over the same range.
  ➡ **A flat statistic is not a flat observable.**

⛔ **Not applied, and note #10's reason 3 is discharged and replaced by a better one.** The load line
does not move — `triodeOnsetGateVolts()` reads 1.691 V at the shipped value and 1.737 V at 0.150,
because lowering `Vov` raises `Vds_q` by almost as much as it lowers the current. **CUTOFF moves, and
overtakes triode:** first clipping goes from triode at −7.5 dBFS to cutoff at −12.2 dBFS. Applying the
fit would therefore change the stage's clipping MECHANISM, on a parameter pinned to a factor of
1.3–1.5, from the one capture whose harmonic data clears its own floor by 4.7 dB. ➡ Decide it on a
capture that reaches the load-line region, which is exactly what the +12.2 dBu session produces.

### 18.2 The 4–8 kHz dip: P1's rig, and P2 confirming the input pole

`goal_check.py`'s four unexplained core-band cells are the bottom of one continuous curve
(+3.4 dB at 25 Hz → 0 near 1 kHz → −0.6 dB at 5 kHz → **+0.95 dB at 16 kHz**), so any mechanism has
to lift and then turn over — which a single-pole difference structurally cannot do.

Modelling each capture as the **plugin** × a high-pass × two low-passes:

| capture | LF high-pass | lower pole | upper pole | residual |
|---|---|---|---|---|
| P2, all three modes | 20–21 Hz | 3.18–3.29 kHz | **7.34–7.46 kHz** | **0.021–0.034 dB** |
| P3 (mid) | 18 Hz | 4.53 kHz | 7.95 kHz | 0.222 dB |
| P1, all three modes | 28–31 Hz | 12.4–12.9 kHz | 12.4–12.9 kHz | 0.176–0.263 dB |

⭐⭐ P2 recovers the pedal's own `R3 ∥ R4` into `C3` pole to within 2 %, with no prior, in every mode,
alongside note #7's 3.2 kHz cable pole. **P1 fits no cascade containing a 7.3 kHz pole**, and an extra
pole can only darken — so P1 is *brighter* at 4–8 kHz than the drawn circuit can be. The plugin is
not too dark there.

⭐ **The estimator escapes note #9's shelf-blindness by fitting against the PLUGIN rather than the
raw sweep**, so the mode shelf appears on both sides and cancels. Evidence rather than assertion:
P2's fitted poles are mode-independent to 3 %.

⚠⚠ **A bound the fit rests on is not a fit.** Flooring the extra pole at 8 kHz — plausible, since the
"extra" pole ought to sit above the pedal's own — put P2 exactly on that bound in all three modes at
a 2.1 dB residual and hid the entire result. The two pole terms enter the model identically, so
neither is "the input pole" until something outside the algebra says which.

### 18.3 Phase: the instruments agree; one number was mislabelled

One residual curve per capture, re-read under each differing configuration choice in turn (worst
|residual| over 200 Hz–12 kHz against P1, bright/dark/mid):

```
phase_sweep as recorded          2.15  2.41  6.21
+ include the 200 Hz point       6.24  5.85  6.47   <- the dominant rung
+ every bin, not 10 points       6.24  7.94  6.78
+ raw CSD, not Farina            6.20  7.13  6.72
+ unsettled segment              6.20  7.13  6.72
+ fit over 200 Hz - 12 kHz       7.30  6.89  8.05
= goal_check (also 8x OS)        7.14  6.79  7.97
```

The recorded figure is a max over **six hand-listed report frequencies from 500 Hz up**; the same
instrument reads −5.85 to −6.47° at its own 200 Hz point, which the claim's band label includes.
📌 The OS factor was never the explanation — 4× → 8× is worth 0.2°.

✅ **The 5° target is met above 500 Hz**: 500 Hz–2 kHz 1.6–2.6°, 2–8 kHz 1.4–2.3°, 8–12 kHz 1.8–4.3°.
The whole miss is 200–500 Hz, the tail of the missing LF pole, already confounded across units and
already blocked on the VOLUME sweep. §18.2 also removes the minimum-phase companion that was
suspected: there is no model-side magnitude dip at 4–8 kHz for phase to be carrying.
