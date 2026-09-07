# Echo Pre 3 — Project Memory  (from the pedal-plugin template)

> Echo Pre 3 is a circuit-level emulation of the Echoplex EP-3 preamp (per the Chase Tone Secret
> Preamp schematic) built as an AU/VST3 plugin using JUCE 8+ and chowdsp_wdf WDF modelling.
> Author/Company: Leigh Pierce

This project was scaffolded from a reusable template. The generic, hard-won engineering lives in
the rules + docs below — read them before writing DSP or UI. Replace every remaining `<...>`
placeholder as circuit.md gets filled in.

## Quick reference

```
Build:  cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
AU:     cmake --build build --target EchoPre3_AU     (auto-installs; bump VERSION to force Logic rescan)
Format: clang-format -i src/**/*.{cpp,h}
```

## Schematics

Put the schematic images in `schematics/` and load them whenever verifying a circuit detail.
`.claude/rules/circuit.md` is the source of truth for values/topology — fill it in first.

**Use the `schematic-checker` agent any time a circuit value or topology is in doubt; use
`dsp-validator` after any DSP stage change.** Both read `.claude/rules/circuit.md`/`dsp.md` —
keep those current and the agents stay useful with no extra setup.

@.claude/rules/circuit.md
@.claude/rules/dsp.md
@.claude/rules/architecture.md
@.claude/rules/ui.md
@.claude/rules/build.md

## Delegation & model tiering

Plan with a high-end model, delegate execution down to cheaper ones — reserve the expensive
reasoning for the step that's actually hard to get right. As of July 2026 that means:

- **Planning** (build-sequence ordering, schematic-topology judgement calls, deciding what a
  session should tackle next) — a top-tier model at high effort (e.g. **Fable 5**, high effort).
- **Important thinking work** (circuit/DSP correctness: the `schematic-checker` and
  `dsp-validator` agents, anything cross-checking values/topology/taper against `circuit.md` or
  `dsp.md`) — a strong reasoning model at high effort (e.g. **Opus 4.8**, high effort). Both
  agents' frontmatter (`.claude/agents/schematic-checker.md`, `.claude/agents/dsp-validator.md`)
  are pinned to this tier — don't downgrade them to save cost, they're exactly the "important"
  category this policy protects.
- **Routine work** (mechanical edits, boilerplate scaffolding, formatting, running builds/tests)
  — a fast mid-tier model at medium effort (e.g. **Sonnet 5**, medium effort).

Re-evaluate the concrete model names as new ones ship; the tiering principle (plan high, validate
high, execute routine work cheap) is what should persist.

## Essential reading (template learnings — do not skip)

- **`docs/nonlinear-component-modeling.md`** — the parts `chowdsp_wdf` has **no element for**: CMOS
  inverter clippers (CD4049UB/CD4069UB), JFET/MOSFET gain stages (J201 & friends), and op-amp output
  rails. Sources + datasheets (`docs/refs/`), recommended models, the structural traps that cost the
  most time (a degenerated common-source stage is a *current* source; finite CMOS open-loop gain is
  voicing not a refinement; a tanh cannot produce an even-dominant stage; check the sign of the cubic
  before choosing a limiter), and the cross-cutting solver / ADAA / fitting rules. **Triage your
  parts list against its §0 before writing any nonlinear stage.**
- **`docs/measurement-discipline.md`** — the analysis-side traps: building instruments and known
  answers, mutation-testing guards, thresholds, aggregates and membership, fits and degeneracy,
  screening candidate mechanisms, reading physical measurements, staleness, and process. Every entry
  is a failure mode that has inverted a conclusion already written down as fact. **Skim once at the
  start; re-read the relevant section before building an instrument, trusting an aggregate, or
  shipping a constant.**
- **`docs/calibration-and-gain-staging.md`** — input-load (`kInputRef`) calibration, output-makeup
  calibration (level-match to captures — NOT a ~0.9 headroom pad; see §2), the DRIVE taper-floor
  bug, output-load (negligible), internal-vs-output clipping, op-amp rails, VU idle gate. This is
  where the non-obvious time-sinks are documented.
- **`docs/validation-and-capture.md`** — how to measure how close the plugin is to the real pedal
  (1/3-oct FR, continuous Farina swept-THD, sub-sample null, knob-tracking pass/fail) and how to
  CAPTURE the pedal so the measurement is trustworthy (bypass anchor, one-knob-at-a-time, sweep
  Volume, no truncation). The capture MATRIX, not the signal, is the usual limitation.
- **`analysis/`** — the reusable harness: `gen_test_signal.py` (comprehensive A/B signal) +
  `analyze.py` (load/align, FR, THD, Farina swept-THD, sub-sample null, filename parser).
- **`docs/ui-peripheral-spec.md`** — full visual spec for the reusable UI elements.
- **`src/PluginEditor.{h,cpp}`** — working sample editor: three-column layout, side-panel trims +
  VU + 2-dp value readouts, oversampling/scale strip (LIVE/RENDER, HQ + Trim Link toggles,
  self-updating version stamp), full resizable-UI scaling with per-session + debounced
  cross-session persistence, TooltipWindow. Binds to the canonical APVTS IDs.
- **`src/PluginProcessor.{h,cpp}`** — a PLACEHOLDER pass-through processor: it declares the full
  canonical APVTS layout the UI binds to and exposes the input/output peak meters, but does NO
  circuit modelling. It exists so the UI compiles/loads/renders today (verified: builds clean, the
  headless `tests/UISnapshot.cpp` renders it at 0.5×/1×/2.5×). Replace its guts during the DSP build
  sequence, keeping the parameter IDs stable.
- **`src/ui/PedalFace.{h,cpp}`** — sample single-channel centre face (GAIN/TONE/VOLUME knobs with
  2-dp tooltips, a param-bound 3-position mode switch, LED, bypass footswitch, logo). This is the
  one per-pedal piece — rename labels/IDs and re-arrange `resized()`. Dual-stage pedals instantiate
  it per stage (architecture.md).
- **`src/ui/`** — drop-in `PedalLookAndFeel`, `VUMeter`, `ThreePositionSwitch`, `LEDIndicator`,
  each **image-first with a procedural (vector) fallback**. Art is embedded via `Assets.h` +
  `juce_add_binary_data(PedalAssets ...)`; reskin by replacing the source PNGs in `ui/` and running
  `tools/process_ui_assets.sh` (→ `assets/ui/`). Remove the images to fall back to the vector look.
- **`src/utils/TaperUtils.h`** — taper helpers (note `audioTaperR0` for large gain pots).

## Build sequence (validate each step before the next — do not skip ahead)

1. **Schematic analysis** → fill `circuit.md`. Heed the schematic-reading gotchas there. Use the
   `schematic-checker` agent to cross-check any value/topology question against what's already
   captured, rather than re-reading the schematic image from scratch each time.
   **Then triage the parts list against `docs/nonlinear-component-modeling.md` §0** and gather the
   external data for anything not WDF-native — before DSP, not during it.
2. **CMake scaffold** — APVTS + AU/VST3 targets loading in a DAW.
3. **chowdsp_wdf smoke test** — trivial RC lowpass, confirm −3 dB point within 1% (offline/unit
   test, not a visual guess).
4. **Stage-by-stage DSP**, validated at each step:
   - Linear stages: frequency response vs expected transfer function.
   - Nonlinear stage: sine-clipping behaviour; confirm output polarity with a DC-step test.
   - Run the `dsp-validator` agent against each stage before moving to the next — it cross-checks
     component values, taper curves, and WDF topology against `circuit.md`/`dsp.md` for you.
5. **Switch topologies** — verify each position independently (precomputed scattering matrices).
   `dsp-validator` covers this too (topology + `setSMatrixData()` usage).
6. **Oversampling + ADAA** on the nonlinear stage — verify aliasing reduction. Use AccurateOmega
   (not chowdsp's default omega4). Add a separate render-time OS factor.
7. **Full-chain integration + level calibration** — anchor `kInputRef` from a real measurement;
   **calibrate output makeup to the reference captures** (may exceed 1.0; don't pad for headroom —
   calibration doc §2). Build an `OfflineRender` console exe mirroring `processBlock` for A/B.
8. **UI** — reuse the peripheral elements; design the centre pedal face per this pedal.
9. **Reference validation** — generate the comprehensive signal (`analysis/gen_test_signal.py`),
   capture the pedal per `docs/validation-and-capture.md`, and A/B with the harness: FR (1/3-oct),
   continuous swept-THD, null depth, knob-tracking pass/fail. Decompose any level deficit (§4)
   before changing constants.
10. **Final sweep** — all controls full range: no instability, clicks, or NaN/Inf. (Output > 0 dBFS
    at extreme drive+volume is faithful, not a fault — the output trim manages it.)

## Current step

> Update this at the start/end of each session so progress doesn't rely on conversation history.
> **CURRENT: Step 1 (Schematic analysis) — COMPLETE, re-verified 2026-09-07, no open blockers.
> `schematics/schematic.png` is traced and `.claude/rules/circuit.md` is fully filled in (values,
> node graphs, triage, corners, validation targets). Both open questions resolved against the
> maker's published notes. 2N5457 datasheet fetched to `docs/refs/`. Project builds; AU installs
> with the placeholder pass-through DSP.
> NEXT: **`docs/build-plan.md` (written 2026-09-07) is the working plan from here on** — it adapts
> this build sequence to the reference data we actually have (seven NAM models, no raw captures, no
> bypass anchor). Immediate work is its phase 0: redesign `analysis/gen_test_signal.py` for this
> pedal, write `analysis/captures.py`, and get the eight renders made. Steps 2 and 3 (CMake scaffold
> → APVTS for VOLUME + 3-way EQ, then the chowdsp_wdf smoke test) run in parallel with that.**

## Project-specific carry-forwards

### Reference data: seven NAM models (see `docs/build-plan.md`)

- **No raw pedal captures exist and none are coming.** The reference is seven `.nam` models across
  three physical pedals, trained by three different people, at three VOLUME positions (2:30 / 10:30
  / 10:00). Only P1 and P2 have all three MODE positions; P3 has Mid only. They are driven by
  rendering a test signal through the NAM plugin — we never run inference on the files directly.
- ⭐ **The mode differential is the jackpot, and it hands us `gm` for free.** Within one unit the
  three modes differ in exactly one thing, so rig gain, converter response and unit variance all
  cancel in the ratio. The bypassed-to-unbypassed plateau ratio is `k = 1 + gm·R5` with R5 known,
  so `gm` comes out of a *ratio* — no level calibration needed — for the one parameter the 5:1
  datasheet spread makes least predictable. Measured twice, independently.
- ⚠ **The VOLUME taper CANNOT be fitted from this data.** Volume is confounded with both unit and
  rig gain: P3 sits at a lower volume setting than P2 yet reports 8.5 dB more loudness. Fit the
  taper to the maker's four published points (p ≈ 2.0) and use the NAM volume points only as a
  shape check via the LF high-pass corner, which is level-independent.
- ⚠ **There is no bypass anchor, so `kInputRef` cannot be measured.** Keep 0.87 V/FS as a declared
  assumption, commented as an assumption — not as a calibrated constant.
- ⚠ **All seven models have a 132 ms receptive field**, so a 13 Hz corner is seen for under two
  cycles. Verify the low-frequency probe behaves before leaning on it. The top octave carries each
  trainer's converters, not the pedal.
- 📌 **Hypothesis to test first (M1 in the plan): the BRIGHT/MID labels may be backwards.** The
  stored loudness figures order Bright above Mid above Dark in *both* units, but the 22 nF cap lifts
  a wider band and should win a broadband comparison. Settles `circuit.md` note #2 either way.
- **The old two-way pedal is the contingency**, held for the two things NAM data structurally
  cannot give: a VOLUME sweep and a bypass anchor. Trigger only if volume becomes the dominant
  error.

> Record decisions, measured constants (kInputRef, rail voltages, makeup), and open questions here
> as you go, so the next session resumes cleanly.

- Emulation target: Echoplex EP-3 tube preamp circuit, traced via the **Chase Tone Secret Preamp**
  schematic (not an original circuit design).
- **Rail: VA = 22 V**, charge-pumped from 9 V and clamped by the D6 1N4748A zener. The whole power
  section (D1–D6, C5–C9, IC1) is supply-only — excluded from the DSP model. The high rail is the
  point: this is a clean, high-headroom preamp, not a distortion.
- **Exactly one part needs an external (non-WDF) model: Q1, a 2N5457 JFET common-source stage.**
  No clipping diodes, no op-amps, no CMOS anywhere in the signal path. Follow
  `docs/nonlinear-component-modeling.md` §2 Path B, and heed the ⭐⭐⭐ "a degenerated CS stage is a
  CURRENT source" trap — on this pedal the MODE switch's entire audible job *is* that source-bypass
  lift, so getting it wrong is worth ~20 dB and will be loudly obvious rather than subtle.
- **MODE = 3-position ON-OFF-ON**, labelled by treble content: **up BRIGHT** (C2 10 nF, corner
  ≈4.4 kHz) / **middle DARK** (no bypass — flat, lowest gain) / **down MID** (C1 22 nF, corner
  ≈2.0 kHz). Both cap positions reach the same HF plateau; they differ in *where the lift starts*.
- ⭐ **The VOLUME control is deliberately NON-MONOTONIC — this is correct, do not "fix" it.** The
  wiper grounds and both end lugs feed signal nodes, reproducing the original EP-3 wiring: silence
  full CCW, **peak boost at 1–2 o'clock**, then falling back by full rotation. I initially flagged
  this as a probable schematic error; the maker's published control description reproduces the
  computed SHAPE and refutes that. See circuit.md "Validation notes" #1.
- ⚠ **Two VOLUME caveats found in the 2026-09-07 re-verification pass — both matter before the
  taper is fitted.** (a) The peak's POSITION depends on the drain drive impedance (Ra = 176 k at
  the physical ~20 kΩ, but 280 k at an ideal current source), so the taper fit and the Norton-source
  modelling of stage 2 are coupled and must be done together. (b) The as-drawn network falls back
  **3.9 dB** from peak to full CW, against the maker's stated **1–2 dB** — ~2 dB unexplained, and
  invariant to every assumption tested. Settle it with the VOLUME sweep capture; do NOT tune other
  constants to close it.
- **Free taper-calibration targets (from the maker's notes):** full CCW = no signal · 10–11 o'clock
  = unity · 1–2 o'clock = +3 dB max · 3–5 o'clock = +1–2 dB. Fit the 500 kA taper so the network's
  peak lands at 1–2 o'clock — that needs a power-law exponent **p ≈ 2.0** at the physical drive
  impedance (p ≈ 1.4 would put the peak at ~12 o'clock). Marketing copy — a real VOLUME sweep capture supersedes it.
- 📌 **Level anchor:** those figures imply the JFET stage's own gain is ≈ **+7–8 dB**, about 4 dB
  *below* a nominal-2N5457 estimate. Expect fitted `gm` under nominal — the maker specifies a
  "cherry picked" vintage JFET, so nominal SPICE is even less trustworthy than the usual 5:1 spread.
- ✅ **2N5457 datasheet fetched** (`docs/refs/onsemi_2N5457-2N5458_datasheet.pdf`, onsemi Rev. 6).
  Confirms IDSS 1–5 mA, Vgs(off) −0.5…−6 V, Yfs 1000–5000 µmhos — a 5× spread on every amplitude
  param, and the typical-characteristics graphs show sample units spanning nearly that whole range.
  Sanity range only — the maker's "cherry picked" claim means don't assume this unit is typical.
