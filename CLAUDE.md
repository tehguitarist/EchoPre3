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
Format: clang-format -i <NEW files only>   # see the warning below
```

⚠ **This tree is HAND-formatted; `clang-format` cannot reproduce it under any config.** Running the
old blanket `clang-format -i src/**/*.{cpp,h}` reformats the whole codebase (it did: ~800 lines of
pure churn across six files in one pass, burying the real diff). The shipped `.clang-format` was the
template's LLVM default with `BreakBeforeBraces: Attach`, while every file here is Allman with JUCE
spacing — so it disagreed with the code on braces, pointer alignment and `! x`. It has been corrected
to describe the code that actually exists, which roughly halves the churn, but aligned trailing
comments and aligned constant tables are deliberate and clang-format will always reflow them.
**Run it on NEW files only, then check the diff.**

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
> 5. **Not started:** all calibration (it needs the renders). Step 6, `OfflineRender` and the three
>    probes are DONE — see the step 6 block below.
> 6. ~~Pre-existing unrelated warning: `src/ui/PedalLookAndFeel.cpp:251` unused parameter.~~ Fixed
>    (the mouse-over flag is unnamed now — no button on this pedal draws a hover state).
>
> **STEP 6 AND THE THREE PROBES ARE DONE (2026-09-07), plus `OfflineRender` — all without captures.**
> Full write-up in `docs/build-plan.md` §9. Nine tests pass via `ctest`; the build is warning-free.
>
> - ✅ **`OfflineRender` exists** with the CLI the five analysis scripts already assumed. ⭐ It
>   **calls `processBlock` on a real `PedalAudioProcessor` rather than mirroring it** — a mirrored
>   copy of the gain staging drifts, and the harness would then report a mismatch that exists only in
>   the harness, which is fatal when every remaining calibration constant is read off exactly those
>   measurements. Verified: align lag 0, `polarity()` −1 at −178.6°, finite, full length.
> - ⚠ **A real bug fell out of that:** `setLatencySamples` was TRUNCATING the oversampler's
>   fractional latency (≈65.9 → 65 at 8×), losing nearly a sample the host's delay compensation never
>   restores. It rounds now.
> - ✅ **ADAA is implemented and proven exact** (antiderivative vs an independent Simpson integral to
>   1e-15, across the sign branch) — **and switched off at every factor**, `kAdaaMaxOsIndex = -1`.
>   It is free on CPU; it is off because at 1× it triples the top-octave droop (−1.07 → −4.06 dB at
>   12 kHz) to buy 5.6 dB of a floor already at −63 dBc, and because **1× + ADAA is beaten outright
>   by plain 2× on both axes for 0.86 pp of CPU**. The 2× row is marginal on placeholder thresholds
>   and is documented as such — don't misremember it as clear-cut.
> - ⚠ **A test was nearly written as the wrong assertion:** "ADAA must not change the small-signal
>   gain" is FALSE — ADAA1 on a linear map is exactly the FIR (1+z⁻¹)/2, so cos(πf/fs) and half a
>   sample of delay. The test now asserts that two-point-average response itself.
> - ⚠⚠ **`dsp.md`'s low-OS shelf restore CANNOT be a single fixed shelf on this pedal.** Its
>   stated premise (the droop is pot-independent) fails here — the 1× droop spreads **1.8 dB across
>   MODE**, and Bright is *brighter* than 8× at 8–12 kHz. Cause is structural: the JFET's own 1/k(s)
>   shelf lives inside the oversampled region and its pole moves with MODE. A per-mode shelf is the
>   right structure. ⛔ **Do NOT fit it yet** — the pole sits at K0 × the bypass corner and
>   K0 = 1 + gm·R5 with `gm` a placeholder M2 may move 4.5×. At the 4× default the droop is
>   ≤ 0.14 dB, so nothing a normal session hears is being deferred.
> - 📌 **CPU is a non-issue: 2.07 % of realtime at the 4× default, 3.51 % at 8×.** So `dsp.md`'s
>   "polyphase IIR instead of the FIR" optimisation is **not worth scoping** — the linear-phase FIR
>   the sub-sample null depends on costs nothing worth recovering.
> - ⭐ **`PerfBenchmark` found the specified bypass optimisation was missing, and it is now in.**
>   `architecture.md` says the DSP is skipped when bypassed; `processBlock` was running the whole
>   chain and crossfading regardless, so bypass cost what active cost (3.47 % vs 3.51 % at 8×). It is
>   now a flat **0.10–0.11 % at every factor** — 33× cheaper at 8×, and no longer scaling with the
>   factor at all — guarded by `BypassClickTest`.
> - ⚠⚠ **The oversampler MUST be reset when the skip is entered; the chain's own state is a
>   judgement call the measurement did not decide.** Leaving the oversampler unreset splices its
>   stale pre-bypass FIR tail onto the live signal at about one reported latency after the toggle,
>   and that breaches the crossfade's own step bound by **1.09× / 1.54× / 1.63× at 2× / 4× / 8×** —
>   a real click, inside a fade that is otherwise working. But **reset-vs-resume of the WDF/shelf
>   state is a wash**: peak re-engage excursion 0.0148/0.0167/0.0174/0.0176 FS (reset) against
>   0.0187/0.0154/0.0129/0.0143 (resume) at 1/2/4/8×, neither leading consistently. Once the
>   oversampler is cleared its FIR ramps the chain's input up from zero instead of stepping it, so
>   the high-passes are barely kicked either way. **RESET is chosen on determinism, not on sound**:
>   post-bypass output must not depend on how long ago the pedal was switched off, because
>   `OfflineRender` drives this same processor and step 9 is a sub-sample null.
> - ⚠ **A fixed bypass hold of 0.2 s is exactly 200 periods of a 1 kHz probe tone**, which hands a
>   resumed state a perfect phase match and made resume look free. `BypassClickTest` sweeps the hold
>   length as well as the toggle instants. Watch for this in any future toggle-timing measurement.
>
> ### PHASE 1 CHARACTERISATION IS COMPLETE (2026-09-07). The seven renders landed; M0–M6 are done.
>
> Script `analysis/phase1_characterization.py`, raw numbers `analysis/reports/phase1_characterization.json`,
> full write-up `.claude/rules/circuit.md` note #7, plan consequences `docs/build-plan.md` §3b/§3c.
> Ten tests still pass via `ctest`.
>
> ⭐⭐ **`gm` is measured, and it is roughly TWICE the placeholder, not a fifth of it.** The
> mode-versus-DARK differential's plateau is `K0 = 1 + gm·R5`, so it comes out of a ratio with no
> level calibration — the whole point of the NAM dataset. **K0 = 6.59** (P1 6.46, P2 6.72; two
> branches whose poles sit an octave apart agree to 0.37 dB), giving **gm ≈ 1.5–1.7 mS** against the
> 813 µS placeholder. The DARK stage gain this implies is **+10.9 dB at 10:30 and +12.5 dB at 2:30**
> (the load moves with VOLUME), not the +7–8 dB the maker's copy implied — derived via
> `Av_dark = ZL·(K0−1)/(R5·K0)`, which is independent of `ro`, though still derived rather than
> measured since L2 leaves no absolute anchor. `JfetStage.h`'s "KNOWN TENSION" note (gm ~180 µS) is
> refuted by ~9×.
>
> ⭐⭐ **The MODE labels were BACKWARDS and are now swapped in the code.** BRIGHT engages C1 = 22 nF
> (measured shelf zero 1.86 kHz), MID engages C2 = 10 nF (4.17 kHz) — both units, shelf fits to
> ≤ 0.25 dB RMS. `JfetStage::bypassCap()` and two tests updated; the `Mode` enum ORDER was left alone
> because it is the APVTS choice index. circuit.md's aesthetic argument ("22 nF lifts the upper mids,
> so it reads as *mid*") lost to a two-parameter fit — the 22 nF branch is above the 10 nF branch at
> *every* frequency, so it is simply the brighter position.
>
> ⚠⚠ **The method finding matters more than any single number.** The first pass read every corner as
> "−3 dB below the plateau" and produced four plausible scalars, **all wrong, one of them inverting
> the MODE verdict.** Nothing on this pedal reaches a plateau inside the audio band: the shelf pole
> sits at K0 × its zero, i.e. 12.6 kHz (Bright) and 26.8 kHz (Mid, above Nyquist at 48 kHz). An
> 8–10 kHz "plateau" window therefore normalises each branch to a different point on its own
> transition. **Fit a model to the curve and report the residual** — a wrong fit is visible as a bad
> residual; a wrong threshold just returns a number. This sits alongside the phase-testing finding
> above as the two measurement traps this project has actually been bitten by.
>
> ### The other four, and what NOT to act on
> - ✅ **M3 input LP confirmed at ~7 kHz — by two units, and it disqualified the third.** P1 fits
>   6.7 kHz and P3 7.2 kHz against the drawn 7.3 kHz. **P2 fits 1.4 kHz and rolls off at −8.6 to
>   −10.7 dB/octave, which no single RC can do**, so its top three octaves are its trainer's rig
>   (limit L3), not the pedal. build-plan §4's "anchor absolute response to P2/danielnguyen" is
>   reversed: **anchor to P1, corroborate with P3, use P2 for the differential only.**
> - ⛔ **M4: do NOT retune the VOLUME taper, drive impedance or C10.** The C10 corner fits cleanly
>   (0.03–0.15 dB residuals, mode-independent within a unit as it must be) but measures 1.3–2.0×
>   above prediction in all three. Volume is 1:1 confounded with unit AND trainer here, and the
>   confound is provably the same size as the signal: P1 (10:30) and P3 (10:00) sit 17% apart by
>   circuit but measure 16% apart *the other way*. Back-solved taper exponents are 2.5 / 3.2 / 7.8 —
>   three disagreeing values is what a rig-dominated residual looks like, not a wrong exponent.
>   Keep `p ≈ 2.0`. **The two-way pedal's VOLUME sweep (build-plan §7) is now load-bearing, not a
>   contingency — it is the only within-rig sweep that can settle this.**
> - ⚠ **M5: even-dominance confirmed, cubic only weakly settled.** In every capture at every probe
>   frequency **H4 comes back above H3**, which is impossible for a mild polynomial — so everything
>   above H2 is the models' error floor (−57 dBc P1, −77 dBc P2). What survives: on P2, H2 rises
>   0.75 dB/dB across the top four cells (a square law gives 1.0) and reaches −47.6 dBc. **P1's H2
>   does not move with level at all, so P1's harmonic data is floor throughout — do not fit it.**
>   Compression is <0.01 dB until the top cell, then −0.09 dB Dark / −0.18..−0.35 dB Bright; more
>   compression in Bright is the right sign. Fit the shaper's quadratic to P2's H2-vs-level slope;
>   the cubic's magnitude is not available from this dataset.
> - ⭐ **M6: the tolerance band is 0.33 dB RMS / 0.8 dB peak, measured on the MODE DIFFERENTIAL.**
>   The absolute P1-vs-P2 comparison spans 13.8 dB at 18 kHz, but that is the P2 rig, not two pedals.
>   Three units means three rigs, so **this dataset contains no measurement of absolute unit spread
>   and cannot be made to contain one.** `FeatureProfile`'s `kHfBudgetDb` should therefore **stay
>   tight** — build-plan §9.2's question resolves against widening it.
> - ⭐ **A free known-answer probe worth reusing.** Below the shelf zero every MODE has `Zs = R5`, so
>   every mode differential must read exactly 0.00 dB under ~200 Hz. It reads ≤0.20 dB (P2) and
>   ≤0.60 dB (P1) — the LF noise floor of the dataset, obtained with no reference capture. It is what
>   ruled out "NAM just can't do low frequencies" as the M4 explanation.
> - 📌 **Bookkeeping: build-plan §1's unit table had P1 and P2 swapped** (folders and `.nam` metadata
>   agree with the filenames on disk). Corrected. **P1 = thelamehorse @ 10:30, P2 = danielnguyen @ 2:30.**
> - ⚠ **The measured shelf zeros are ~7% below the drawn ones, consistently.** The zero depends only
>   on R5·C, and both branches imply the same R5 to 1.6%, so it is one common offset: either R5 ≈
>   3.85 kΩ or both caps run ~7% high. **`schematic.png` was re-read at high zoom — R5 is
>   unambiguously "3k6", so this is not a transcription error.** The cap ratio is confirmed (2.24
>   measured vs 2.20 drawn). Model the shelf from the measured (τ, K0) pair; this dataset cannot
>   separate R5 from C.
>
> ### NEXT
> Step 4b, refitting the JFET stage to K0 = 6.59 and the two measured time constants, then M5's H2
> slope for the shaper. After the stage is refitted, re-run `OSFidelity` and only THEN fit build-plan
> §9.3's per-mode low-OS shelf restore — it was blocked on M2, and the pole it targets has moved from
> the placeholder's ~18 kHz to a measured 12.6 kHz (Bright) / 26.8 kHz (Mid, past Nyquist at 48 kHz).

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
