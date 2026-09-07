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
> `docs/build-plan.md` (written 2026-09-07) is the working plan from here on. Phase 0a (test signal)
> was already done; phase 0b (`analysis/captures.py`, parsing `<unit>_V<HHMM>_<mode>.wav` and
> emitting `--volume`/`--mode` OfflineRender args) is now done too. **Steps 2 and 3, which the plan
> said could run in parallel with the capture work, are COMPLETE as of 2026-09-07:** the APVTS now
> declares the real control set (`volume` float + `mode` choice `Bright/Dark/Mid`, trims, OS, hq,
> bypass — the placeholder `gain`/`tone` knobs are gone), `PedalFace` shows the real one-knob-plus-
> switch panel (verified via `UISnapshot` at 0.5x/2.5x — this also caught and fixed a label-clipping
> bug: `ThreePositionSwitch` needs ~1.4x its height in width to fit "BRIGHT"/"DARK"/"MID", not the
> placeholder "I"/"II"/"III"), and `tests/WdfSmokeTest.cpp` (RC lowpass, chowdsp_wdf compile-time
> API) confirms the WDF toolchain wiring: measured −3 dB point 998.6 Hz vs. 1000 Hz target (0.14%
> error). Both are registered in CMakeLists.txt and pass via `ctest`.
> BLOCKED on phase 0c (the eight NAM renders) — that's on the user, in progress as of 2026-09-07.
>
> **STEPS 4a, 4b, 5 and 7 ARE NOW STRUCTURALLY COMPLETE (2026-09-07), placeholders and all.** The
> whole chain is wired and the plugin processes audio: `src/dsp/{CircuitValues,InputNetwork,
> JfetStage,OutputNetwork,EchoPreDsp}.h`, with the pass-through processor retired. Six tests pass via
> `ctest`. What is validated vs. what is still a placeholder:
>
> - ✅ **Input network** matches its analytic transfer function to 0.00 dB / 0.5° through the band.
> - ✅ **Output/VOLUME network** reproduces circuit.md's control-law table to 0.04 dB, peaks at
>   Ra = 176 k (35.3%), falls back 3.92 dB, and is −103.7 dB at full CCW. Phase exact to 0.02°.
> - ✅ **JFET stage** shelf matches analytic 1/k(s) to 0.0000 dB and 0.001°; even-dominant (H2 30–75 dB
>   above H3); monotone; g'(0) = 1 exactly.
> - ✅ **Chain**: inverts (−178.4°), and the mode plateau lands on K0 = 1+gm·R5 to 0.02 dB.
> - ⚠ **Every JFET amplitude parameter is a placeholder** from a datasheet-typical self-bias solve
>   (gm = 813 µS, Vov = 1.22 V). M2 replaces gm; M5 replaces the shaper. `kInputRef` stays a declared
>   assumption and `kOutputMakeup` is exactly 1.0 (uncalibrated) until the renders land.
>
> **Three findings worth carrying forward:**
> 1. ⭐ **Step 5 needs no scattering matrices.** Under Path B the source network folds analytically
>    into k(s), so MODE never touches a WDF topology — it is three shelf coefficient sets. The
>    build-plan's "precomputed scattering matrices" step is simply not needed.
> 2. ⭐ **The output network is a TREE, not an R-type network.** circuit.md's "bridging resistor"
>    warning is about the CONTROL LAW, not the graph: from node E there are exactly three paths to
>    ground. No R-type adaptor, no matrix. (The pot coupling is a *parameter* coupling — and
>    `ScopedDeferImpedancePropagation` is a HARD barrier whose destructor recalculates only what it
>    was given, in the order given, so listing the whole chain silently leaves stale impedances.
>    One barrier at the lowest common ancestor plus one manual propagation is the correct idiom.)
> 3. ⚠⚠ **PHASE TESTING CAUGHT A REAL BUG THAT MAGNITUDE TESTING CANNOT SEE.** The input network
>    shipped a 180° inversion (from the chowdsp voltage-source `makeInverter` idiom, which a passive
>    RC ladder must not have). Magnitude was perfect at every frequency. It would have cancelled the
>    JFET's own physical inversion, leaving the plugin the wrong way round — surfacing only as a
>    failed null at step 9. **Every stage test now checks magnitude AND phase**, plus an excess-delay
>    guard for dsp.md's source-port read trap. `analysis/analyze.py` gained `transfer_complex`,
>    `phase_deg`, `group_delay_ms` and `polarity`, all with known-answer self-tests — including one
>    that proves the magnitude instrument is *blind* to a flip. **Run `polarity()` before reading
>    anything into a poor null: an inverted render nulls at about +6 dB and looks like a
>    catastrophic modelling error rather than a one-character sign bug.**
>
> **Host-verified: `auval -v aufx Ep3p Lprc` PASSES** (render tests 11 kHz–192 kHz, mono + stereo,
> ramped parameter scheduling), so this is a real host load with the DSP running, not just a build.
> VERSION bumped to 0.2.0 so Logic rescans rather than serving the cached pass-through build.
>
> **Decisions made while closing out, each with a reason that should survive:**
> - **Oversampling uses the linear-phase FIR, not the polyphase IIR.** The IIR is non-linear-phase,
>   and step 9's validation is a sub-sample null — resampler-smeared phase is indistinguishable from
>   phase the circuit model got wrong. Revisit only if `PerfBenchmark` shows the FIR is a real cost.
> - **The `hq` parameter is REMOVED.** It gated the template's diode omega solve; this pedal has no
>   diodes and no omega solver, so it gated nothing. Removing it was free now and would break saved
>   sessions after release. The editor's toggle is param-guarded so it simply never appears — the
>   infrastructure is kept, dormant, because ADAA (step 6) is the one plausible lever this pedal may
>   actually acquire, and `dsp.md` says to let `FeatureProfile` decide rather than adding it blind.
> - **`trim_link` is now implemented** as the listener pair with a re-entrancy guard, tracking the
>   last value even while disengaged so engaging it mid-session measures from where the knob is.
>
> ### Residuals — known, deliberate, and NOT bugs to rediscover
> 1. **Every JFET amplitude parameter is a placeholder** (gm = 813 µS et al. from a datasheet-typical
>    self-bias solve). M2/M5 replace them. `kInputRef` = 0.87 is a declared ASSUMPTION (no bypass
>    anchor can exist), `kOutputMakeup` = 1.0 UNCALIBRATED.
> 2. **Base-rate (1× OS) top octave droops** −2.1 dB @ 12 kHz, −3.6 dB @ 16 kHz. Prewarp pins the
>    corner but cannot invert the bilinear zero at Nyquist; that is why the input network was moved
>    inside the oversampled region. `dsp.md`'s low-OS shelf restore is the remedy, at step 6.
> 3. **VOLUME updates per block, not per sample** (it re-solves WDF impedances). Fast automation may
>    zipper. Normal WDF practice; revisit only if it is audible.
> 4. **The ~2 dB VOLUME fall-back discrepancy is still open** (as-drawn 3.9 dB vs the maker's 1–2 dB).
>    `OutputNetworkTest` prints the maker's four points every run so it stays visible. Do NOT tune
>    other constants to close it — it needs a real VOLUME sweep capture.
> 5. **Not started:** step 6 (ADAA — the shaper's closed-form antiderivatives are noted in
>    JfetStage.h for exactly this), the `OfflineRender` console exe the A/B harness needs, the
>    `PerfBenchmark`/`FeatureProfile`/`OSFidelity` probes, and all calibration.
> 6. Pre-existing unrelated warning: `src/ui/PedalLookAndFeel.cpp:251` unused parameter.
>
> NEXT once renders land: phase 1 characterisation (M0–M6 in build-plan.md), starting with M0 (null
> check) and M1/M2 (mode-differential ratio → resolves the Bright/Dark label mapping and hands us
> `gm`). `tests/ChainTest.cpp` already measures the M1/M2 differential on the model, so M1/M2 becomes
> a direct comparison rather than new analysis.

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
