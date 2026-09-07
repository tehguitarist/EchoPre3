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
