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
> 1. ~~**Every JFET amplitude parameter is a placeholder**~~ — SUPERSEDED by step 4b below: `gm` and
>    both shelf τ are measured, and the rest of the amplitude parameters are derived from them.
>    `kInputRef` = 0.87 is still a declared ASSUMPTION (no bypass anchor can exist) and
>    `kOutputMakeup` = 1.0 is still UNCALIBRATED.
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
> - ⚠ **M3 input LP: confirmed by ONE unit, not two. ~~P3 corroborates~~ — REFUTED 2026-09-08, see
>   note #9.** P1 fits 6.7 kHz against the drawn 7.3 kHz. P2 fits 1.4 kHz. Cascading the pedal's own
>   7.3 kHz input pole with ONE free extra pole localises P2's: it needs **3.2 kHz at 0.05 dB
>   residual — 542 pF, an ordinary five-metre cable**, where P1 needs 30 kHz (52 pF, i.e. nothing).
>   ⛔ **P3's apparent 7.2 kHz is an ARTEFACT and must not be cited as corroboration**: P3 has only a
>   MID capture, and M3's estimator has no mode-shelf term, so it cannot recover the pole from a
>   bypassed mode at all (proved against the plugin, whose pole is 7300 Hz by construction: DARK
>   recovers 7295 Hz at 0.01 dB, MID returns 9e12 Hz at 1.14 dB). build-plan §4's "anchor absolute
>   response to P2/danielnguyen" is still reversed, but to: **anchor to P1 ALONE, use P2 and P3 for
>   the differential only** (a post-JFET pole cancels in the mode ratio, so P2 is still the *best*
>   differential capture in the set).
> - ⭐⭐ **That finding is really about the PEDAL, not about P2: its output impedance is 92–139 kΩ and
>   barely moves with VOLUME.** The wiper is grounded and R9 (110 kΩ) bridges to the jack, so the pot
>   cannot pull it down the way a normal divider would. **`docs/calibration-and-gain-staging.md` §4
>   ("output load: almost never worth modelling") therefore does NOT apply here** — its arithmetic
>   assumes ~6 kΩ, this is 15–23× that, and its "treble corner ~50 kHz" bullet becomes ~3 kHz with
>   500 pF. That section now carries an explicit exception.
> - ✅ **DECIDED 2026-09-08 — model an ideal source AND an ideal load; add neither a guitar source
>   impedance nor a cable capacitance.** Both open "decide explicitly" items (circuit.md stages 1 and
>   3) close the same way for the same reason: **the loading is already somewhere else, or nowhere at
>   all.** On the input, NAM's protocol reamps a digital file into the pedal, so the reference carries
>   no guitar loading, and in use a DI track already carries the guitar's cable and pickup resonance —
>   modelling it would double-count. On the output, the plugin feeds a DAW digitally, so there is no
>   cable to model, and baking one in would import one trainer's 542 pF as a permanent voicing while
>   P1 and P3 show 39–52 pF. **What this binds is step 9: A/B the top octave against P1 or P3 only,
>   and never move a plugin constant to close a null against P2 above ~2 kHz.**
> - ⛔ **VOLUME is ruled out as the cause of P2's rolloff, and the check is worth keeping** (P2 is the
>   only capture at a high volume setting, so it was a fair suspicion). Across all three captured
>   positions and every drain-drive assumption, the output impedance spans a factor of **1.5**; the
>   required load capacitance differs by a factor of **10**. Under the physical ~20 kΩ drive the sign
>   is backwards too — 2:30 has the lowest Zout, so a capacitive load predicts P2 should be the
>   brightest capture, and it is the darkest.
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
> ### STEP 4b IS DONE (2026-09-08). The measurements are in the stage — and putting them there found
> ### a 16.4 dB structural bug that every linear test in the suite passed straight through.
>
> Write-up `docs/build-plan.md` §10, reasoning in `src/dsp/JfetStage.h`, circuit consequences in
> `.claude/rules/circuit.md` note #8. Ten tests pass; CPU unchanged (2.02–2.08 % at the 4× default).
>
> **Fitted:** `gm` 813 µS → **1553 µS** (from M2's K0, not from circuit.md's ro-corrected 1.58–1.71 mS
> — that correction belongs to a model with a frequency-dependent Rout; this one folds Rout into a
> constant, and under that structure the model's own differential is exactly 1 + gm·R5, so the
> corrected value would MISS the measurement it was fitted to). Shelf τ from M1 (85.369 / 38.263 µs,
> ~7 % above drawn R5·C). `ro` 1.01 → 1.44 MΩ.
>
> ⭐ **A measured `gm` collapses the square-law self-bias solve to a ONE-parameter family**
> (`|Vp|/Vov = 1 + gm·R5/2 = 3.796`, `IDSS/Id = 14.41`), which turns the datasheet's useless 5:1
> spread into a real bracket: Vov ∈ [0.131, 0.447] V. Shipped at the IDSS = 5 mA end → **Id = 347 µA,
> Vd = 14.4 V, not the ~11 V mid-rail** circuit.md estimated from a nominal part.
>
> ⚠⚠ **THE BUG: degeneration suppresses distortion TWICE, not once.** `id2 = c·(u1²)/k(s)` — the
> squared term is filtered by the *same* 1/k(s) that set the drive — so **H2/H1 = A/(4·Vov·k²)**. The
> stage applied the shelf once and shipped that way. Against an exact per-sample implicit solve of
> `id = gm·g(vg − id·R5)`: shelf-only over-produced H2 by **+16.3/+16.1/+15.7 dB** at 0.2/0.5/0.8 V of
> gate swing; the corrected two-shelf structure lands at **−0.04/−0.25/−0.72 dB**. The stage now runs
> a second 1/k(s) instance on the shaper's nonlinear EXCESS only.
>
> ⭐ **The method lesson, alongside the phase-testing one above.** `ChainTest`'s mode differential
> exists to catch exactly this class of error and passed the entire time, because the differential is
> a **linear** measurement and the linear path was right. **A correct frequency response is not
> evidence that the nonlinear path is right.** Building the independent implicit solve took less time
> than the two magnitude tests that could never have found it.
>
> **Two consequences that are now predictions, not choices:**
> - **MODE moves distortion as k².** Fully bypassed, H2 rises **32.7 dB** re the fundamental versus
>   DARK. M5's "more compression in Bright" is its qualitative shape; the two-way pedal session should
>   measure it.
> - ⚠ **The reference renders were driven at ≤ 0.41 V/FS, ≥ 6.6 dB below `kInputRef` = 0.87** — a
>   BOUND, not an assumption (H2 goes as aEven × drive, aEven = 1/Vov, datasheet caps Vov). ➡ **Step 9
>   must A/B the harmonics at matched DRIVE, never at matched digital level.** This bound only exists
>   because of the bug fix: under the old structure kInputRef = 0.87 demanded Vov = 6.3 V, which the
>   22 V rail forbids — the impossibility was the first hint the structure was wrong.
>
> ⭐ **ADAA became free and is still off, for a different reason.** ADAA1 is linear in the map, so
> applying it to the excess alone cancels the two-point average on the linear path exactly: the
> 12 kHz cost went 2.99 dB → **0.00 dB** at 1×, and FeatureProfile's 1× verdict flipped to "FREE WIN".
> It stays off because the cost MOVED rather than vanished — at 1× it averages away **1.18 dB of
> WANTED H2** (1.29 dB at −6 dBFS), which would make the OS selector a voicing control, and 1× + ADAA
> is still beaten outright by plain 2× by 18.0 dB of alias floor for 0.83 pp of CPU. The old
> "genuinely marginal 2× row" is dissolved: ADAA at 2× now makes the floor slightly WORSE, because
> past 2× the floor is the decimation FIR's stopband (−105.8/−105.5/−105.4 dBc, flat), not fold-back.
>
> **Still deliberately not modelled** (build-plan §10.5): the load line (the drain enters triode at
> g = +0.370 V, 8× nearer than the modelled channel ceiling, reachable at ~+6.6 dB of input trim);
> `tanh²` versus the true parabola (4.5 %, 0.4 dB of H2 at 0 dBFS); `beta` = 0 (M5 fixes only its
> sign). The shaper's curvature remains degenerate 1:1 with the trainers' unknown reamp level.
>
> ### THE MODE SHELF'S DISCRETISATION IS FIXED (2026-09-08), and §9.3's per-mode restore DISSOLVES.
>
> Write-up `docs/build-plan.md` §11, algebra and measurements in `src/dsp/JfetStage.h`. Ten tests pass.
>
> ⭐⭐ **The 3.09 dB mode-to-mode spread in the 1× droop was not physics — it was the shelf's own
> plain-bilinear discretisation.** `1/k(s)`'s pole sits K0 = 6.6× above its zero, at 12.3 kHz (Bright)
> and **27.4 kHz (Mid, above Nyquist at 48 kHz)**; bilinear warps a past-Nyquist pole back down into
> the band, so the shelf plateaus early and the top octave reads as LIFTED. Computed from the
> coefficients alone that error is +1.17 dB (Bright) / +3.53 dB (Mid) at 48 kHz, against `OSFidelity`'s
> measured +1.14 / +3.08 — **agreement to 0.05 dB at every frequency**, so the spread was one line of
> code. The shelf is now matched to the analog magnitude at three frequencies (DC, Nyquist, and its
> own log-midpoint `fz·√K0`); a first-order section has exactly three degrees of freedom, so nothing is
> fitted. Measured spread **3.09 → 0.50 dB**, inside M6's own tolerance band, and the worst error
> against the analog shelf improves at EVERY factor (Mid: 3.529 → 0.459 dB at 1×, 0.048 → 0.013 at 8×).
> ➡ **`dsp.md`'s "one fixed shelf" premise holds after all; do NOT build the per-mode restore.**
>
> - ⛔ **Prewarping BOTH corners is WORSE than plain bilinear** (3.06 dB vs 1.17 at 18 kHz). Pinning
>   the two ends of a transition lets the curve between them bow out. Measured before it was believed.
> - ⚠ **The phase check looked like a trade-off and was not.** Raw phase error gets worse (−22.7° vs
>   −13.5° for Mid at 18 kHz), but it is a near-constant fractional sample of DELAY. Remove the
>   best-fit pure delay — which a sub-sample null aligns out anyway — and the new design is ~2× better
>   in phase too, at every rate. Taking the raw number at face value would have rejected the change.
> - ⚠⚠ **TWO TESTS WERE PASSING FOR THE WRONG REASON, and the correct model FAILED them.**
>   (a) `JfetStageTest` compared the shelf to the analytic prototype **at the bilinear-warped
>   frequency** — a tautology once bilinear is what the stage computes. It read 0.0000 dB while the
>   filter sat 3.5 dB off at base rate. (b) `ChainTest` asserted the plateau lands within 0.15 dB of
>   **K0, which is an ASYMPTOTE** the analog shelf is still 0.47 dB short of at the 80 kHz probe — so
>   it demanded an error, and the warp happened to supply one of the right size, cancelling to
>   0.06 dB. Both now compare against the circuit's own transfer function at the real frequency.
> - 📌 **The shelf had only ever been validated at 192 kHz (the 4× default), where the error is 6×
>   smaller.** New `JfetStageTest` section 1c sweeps the rate. ⚠ It also has to MEASURE the
>   instrument's own floor (DARK is a known-zero-phase probe, floor 0.06°), because past 4× the errors
>   under test are below it and the comparison is asymmetric — the bilinear column is closed-form, the
>   shipped column is measured.
>
> ### THE LOW-OS DROOP RESTORE IS IN (2026-09-08), derived rather than fitted — build-plan §12.
>
> `src/dsp/OsDroopRestore.h`, guarded by `tests/DroopRestoreTest.cpp`. Eleven tests pass.
>
> ⭐ **It is a DERIVATION.** The residual droop is entirely the input network's trapezoidal caps, and a
> trapezoidal-cap WDF *is* the bilinear transform of its own prototype — so the error is the network's
> analytic transfer function at the warped frequency over the same function at the real one, in closed
> form, predicting `OSFidelity`'s measured droop **to 0.01 dB in every cell**. The coefficients fall
> out of that at whatever (base rate, oversampled rate) the host supplies, so it self-scales to every
> sample rate and bypasses itself when the whole droop is under 0.005 dB. 📌 The analytic transfer
> function moved from `InputNetworkTest` into `InputNetwork.h` so there is ONE definition;
> `DroopRestoreTest` §1 asserts it still describes the real WDF tree (0.0000 dB). **That is the check
> that could rot silently** — without it the restore would correct a network that had changed and
> every other assertion would still pass.
>
> ⚠ **It sits BEFORE the JFET, at the oversampled rate — decided by measurement, not by reading
> `dsp.md`'s "one biquad at base rate" literally.** Post-chain placement corrects the linear path just
> as well but also boosts the HARMONICS, which never carried the droop. At 1×/48 kHz: post-chain moves
> the wanted H2 by **+0.63 dB** and costs **1.53 dB** of alias floor; pre-compensation costs 0.09 dB
> and 0.08 dB for the same response. Post-chain would have made the OS selector a voicing control,
> which is the one thing `OSFidelity` exists to prevent.
>
> ⚠⚠ **The design is deliberately BOUNDED, and the better-looking design was rejected.** The droop
> runs to −∞ at Nyquist, so chasing the top octave means inverting a near-Nyquist zero. That version
> was built: it tracks to **0.44 dB out to 20 kHz** — with **+29 to +37 dB of gain at Nyquist**,
> exactly where 1× (no decimation filter at all) puts its alias products. The shipped design pins its
> plateau to the droop at 19.2 kHz instead, so peak boost is ≤ 6.6 dB. ⛔ And the cap is not laziness:
> an exact two-point match is **infeasible for a first-order section in 20 of 24 (rate × factor) cells**,
> needing 15–45 dB where it works at all.
>
> Worst error vs the analog input network, 20 Hz–20 kHz at 48 kHz base: 1× **7.94 → 3.27 dB**,
> 2× **1.09 → 0.60**, 4× **0.245 → 0.141**, 8× **0.060 → 0.035**. At 1×, 12 kHz goes −1.07 → +0.03 dB
> and 18 kHz −5.03 → −1.62.
>
> ⚠ Both design frequencies are CAPPED at their 48 kHz values, because hearing does not scale with the
> sample rate: uncapped, a 192 kHz session designs the shelf around 48 and 76.8 kHz and then overshoots
> inside the audible band. `DroopRestoreTest`'s "never worse than doing nothing" check caught it at 8×.
>
> 📌 **Twice in one session an asymmetric comparison — closed form against measurement — failed a
> correct implementation.** Both `JfetStageTest` §1c and `DroopRestoreTest` §2 now MEASURE the
> correlation instrument's own floor (from a known-exact probe) instead of assuming it.
>
> ### NEXT
> Everything unblocked in the DSP chain is done. What remains is gated on data or on the user:
> - **Calibration (`kInputRef`, `kOutputMakeup`) is still unanchored** and cannot be fixed from the NAM
>   set — see §6 and limits L1/L2. `kOutputMakeup` is exactly 1.0.
> - **Step 9 reference validation** against P1/P3 (never P2 above ~2 kHz — that is its trainer's cable).
>   ⚠ A/B the harmonics at matched DRIVE, not matched digital level (§10.1's ≥ 6.6 dB bound).
> - **The two-way pedal's VOLUME sweep** (§7) is the only measurement that can settle the taper, the
>   3.9 dB vs 1–2 dB fall-back discrepancy, and `kInputRef`. Load-bearing, not contingent.

> ### FULL EVALUATION SWEEP RUN 2026-09-08 (FR + THD + PHASE, all seven captures, no new data).
>
> Report artifact: https://claude.ai/code/artifact/90256712-20e2-426e-96c1-d9bd20750bb8
> Raw: `analysis/reports/{comprehensive_data.json, phase_sweep.json, executive_summary.txt, dashboard.html}`.
> New script `analysis/phase_sweep.py` (phase was the one axis no report covered).
>
> **Headline: against P1 the model is close on every axis; against P2/P3 the CAPTURES are the outlier.**
> FR shape error vs P1 is **0.66–0.78 dB RMS with ZERO bands over 1.5 dB** above 40 Hz, in all three
> modes. Midband phase vs P1 is **within 1.4° from 200 Hz to 8 kHz**. Against P2/P3 the plugin runs
> +11 to +13 dB brighter at 16 kHz — but **the captures differ from EACH OTHER by the same amount in
> the same mode** (P3−P1 = −10.9 dB, P2−P1 = −13.1 dB at 16 kHz), and the plugin sits on P1's side.
>
> ⭐⭐ **A RECORDED CONCLUSION WAS REFUTED: P3 never corroborated the input pole.** circuit.md note #9
> has the proof. The M3 estimator was run on the PLUGIN, whose input corner is 7300 Hz by
> construction: in DARK it recovers 7295 Hz at 0.01 dB residual; in MID it returns 9e12 Hz at 1.14 dB,
> because the mode shelf's lift cancels the pole's rolloff and the fit runs away. P3 has only a MID
> capture, so its 7.2 kHz was its rig's HF loss coincidentally cancelling the MID shelf. **P1 (fitted
> in DARK) is the ONLY absolute anchor this dataset contains.** The general lesson is the project's
> own, one level up: *run every capture-side estimator on the plugin first, where the answer is known
> by construction* — that is what exposed a fit which had looked like independent confirmation.
>
> ⭐⭐ **ALL THREE P2 CAPTURES ARE POLARITY-INVERTED** relative to P1, P3 and the plugin. Midband phase
> reads +13.6 / −2.7 / +1.7° for P2 against ~−178° for everything else, and the fitted constant lands
> at ~−170° on all three. A single common-source stage MUST invert, so P1/P3/plugin are right and
> P2's chain flipped. It cancels in the mode differential, which is why phase-1 never saw it. **Third
> independent axis disqualifying P2's rig**, after the cable pole and the absolute response.
> ⚠ `null_depth()` gain-matches with a SIGNED least-squares scalar and reports |g|, so **P2's flip is
> invisible in its null number** — check `polarity()` first, always.
>
> ⭐ **The mode shelf is confirmed in PHASE, not just magnitude.** Mode-vs-dark differential phase
> residuals are 0.2–2.6° across 30 Hz–15 kHz, against a MEASURED instrument floor of 1.0–5.3° (below
> the shelf zero every mode sees Zs = R5, so the differential must read 0° — the phase twin of note
> #7's free known-answer probe). Three of the four differentials are AT or BELOW their own floor
> across the whole band. Only P1's mid above 12 kHz exceeds it (−4.1 to −7.0° vs a 2.75° floor).
>
> ⚠ **THD is not arbitrable from this dataset, and the sweep compares it the WRONG WAY.** Three units,
> one measurement (mid, 806 Hz, −6 dBFS): P1 0.58 %, P2 0.35 %, P3 2.89 %, plugin 0.54 % — an **8×
> spread between captures**, with the plugin inside it. P3's THD is flat with frequency, which the
> circuit forbids (distortion moves as k²), so it is floor. AND every figure is at matched DIGITAL
> LEVEL, which §10.1 says is exactly wrong for harmonics. Nothing here can fit the shaper.
>
> ⚠ **The one real gap against P1 is at the BOTTOM, not the top:** +2.4 dB @ 40 Hz, +2.9 dB @ 32 Hz,
> and −35° of phase at 50 Hz — same sign and size as note #7's C10-corner discrepancy, which was
> already ruled confounded. ⛔ Do NOT act on it; it needs the within-rig VOLUME sweep.
>
> 📌 **Null depth is −5.7 to −11.8 dB, and that is level, not shape.** The match gain is 9.6–17.6 dB
> (kOutputMakeup is still exactly 1.0), and an ESS weights the low octaves — where the plugin really
> does differ — equally with everything above. Linear-removed null is −28 to −32 dB.
>
> ⚠⚠ **FOUR HARNESS DEFECTS had to be fixed before the sweep would run, none of them in the model.**
> All four were template assumptions invalidated by phase 0a's test-signal redesign, and all four
> failed LOUDLY (0/7 captures analysed) rather than silently: (a) `comprehensive_report.py` hand-typed
> the sweep segment names (`sweep_drv_-18`…) instead of reading `A.sweep_segments()`; (b) it coerced
> every parsed setting with `float()`, which `unit`="p1" is not; (c) `short_id()` labelled all seven
> captures "D0.00" from a drive control this pedal does not have; (d) `report_audit.py` hard-coded
> `sweep_drv_-18` and described twin-T/bridged-T notches belonging to a different pedal. **The
> harness had never been run end-to-end on this pedal's signal.** Fixed, all deriving from the
> generator now. ⚠ **`python3` on this machine (3.14) has a broken numpy — use `.venv/bin/python`.**
>
> 📌 **STILL MISSING: the no-plugin null render (phase 0c).** The M0 loop-unity check has never run,
> so everything downstream inherits whatever error it would have caught. One bounce.

> ### ⭐⭐ P1's NAM INPUT CALIBRATION IS KNOWN (−12 dBu, owner-reported 2026-09-08). L2 IS PARTLY LIFTED.
>
> Analysis `analysis/harmonic_audit.py`, raw `analysis/reports/harmonic_audit.json`, reasoning in
> `src/dsp/JfetStage.h`. **No DSP constant was changed** — see the ⛔ list below for why.
>
> **The number.** NAM calibrates by playing a 1 kHz sine at 0 dBFS and measuring RMS volts at the jack
> into the gear, so −12 dBu fixes the trainer's level outright:
> `V/FS = 0.7746 × 10^(−12/20) × √2 = 0.2752 V per full scale` (the √2 converts the measured RMS of a
> full-scale SINE into the volts a sample of 1.0 represents). ✅ **It lands INSIDE the ≤ 0.41 V/FS
> bound §10.1 derived independently from H2** — two unrelated routes agreeing. It is **10.00 dB below
> `kInputRef` = 0.87**, so matched-drive A/B means feeding the plugin −10.00 dB.
> ⚠ It is NOT in the file metadata — **none of the seven `.nam` files carries `input_level_dbu` or
> `output_level_dbu`** (checked). The result rests entirely on that one external fact.
> 📌 This anchors the INPUT only. `kOutputMakeup` still has no anchor — that needs `output_level_dbu`.
>
> ⚠⚠ **AT THE CALIBRATED DRIVE THE MODEL'S H2 IS ~10 dB SHORT.** Over P1's 24 usable cells the deficit
> is **mean −9.47 dB, median −9.65, sd 2.98**. Since `H2/H1 = A/(4·Vov·k²)` is exactly inverse in Vov,
> that implies **Vov ≈ 0.150 V against the shipped 0.447 V** — near the bottom of `JfetStage.h`'s own
> admissible [0.131, 0.447] bracket. ⭐ **Verified end-to-end, not just algebraically:** a throwaway
> probe build at Vov = 0.1502 moved the same 24 cells to **mean −0.17 dB, median −0.44, sd 2.92**.
> Implied operating point: |Vp| = 0.570 V, Id = 117 µA, IDSS = 1.68 mA, **Vd = 19.4 V (was 14.4)**.
> Both endpoints are datasheet-admissible.
>
> ⛔ **NOT APPLIED, for four reasons that are all in `JfetStage.h`:** (1) the −12 dBu is a recollection
> and nothing in the data can re-derive it; (2) the sd of 2.98 dB is the CAPTURE's floor, not the
> fit's precision, so Vov is pinned only to ~a factor of 1.4; (3) it moves the drain 14.4 → 19.4 V,
> only 2.6 V under the rail, which **invalidates the load-line arithmetic** in that file; (4) the
> implied part is LOW-IDSS/LOW-pinchoff, contradicting the "cherry picked" reading that justified the
> IDSS = 5 mA end. ➡ Apply together with a re-derived load line, or wait for a second calibrated capture.
>
> ⭐ **A HARMONIC-DOMAIN KNOWN-ANSWER PROBE, free and reusable.** Below the shelf zero every MODE has
> `Zs = R5`, so **H2 in dBc must be IDENTICAL across modes** — the harmonic twin of note #7's 0.00 dB
> magnitude probe. Measured spread: **1.5–21.3 dB (typically 4–9)**. That is the harmonic error floor
> of these NAM models, obtained with no reference capture. **Any plugin-vs-capture H2 delta under
> ~6 dB is inside the measurement's own noise** — which is why the ~10 dB deficit is believable and
> why Vov cannot be pinned tighter than a factor of 1.4.
>
> ⚠⚠ **M5's "everything above H2 is the models' error floor" IS TOO STRONG — it was a blanket claim
> where the truth is per-capture.** Re-measured: **P1's H3 IS floor** (rises 0.75–1.81 dB/dB where a
> cubic needs 3.0; H4 ≥ H3 in 1–3 of every 4 cells). But **P2-bright and P3 are NOT floor**:
> P2-bright's H3 rises **2.65–2.74 dB/dB absolute (1.97 dB/dB in dBc, against the 2.0 a cubic
> requires) over 25 dB of level, with 0–1 inversions**, reaching −36.7 dBc; P3 reaches −41.2 dBc.
> **That is real third-harmonic content, and the model produces essentially NONE** (−115 to −150 dBc,
> which is just what a pure quadratic makes via its own feedback loop, `beta` = 0).
> ⛔ Still not fittable — P2's and P3's reamp levels are unknown, so `beta` inherits the same
> degeneracy `aEven` just escaped. **The cruel split: P1 is calibrated but its H3 is floor; P2/P3's H3
> clears its floor but they are not calibrated.** ➡ **Ask the P2/P3 trainers for their input_level_dbu
> — that is now the single highest-value question, and it is one message.**
> 📌 P2-bright's top cell has **H3 ABOVE H2**, which a square-law device cannot do — that cell is a
> harder nonlinearity than this shaper has, not a bigger cubic. Do not fit `beta` to it.
>
> 📌 **Answering "is the THD fine?": THD is fine and it is not the question.** THD is an RSS over
> H2..H7, so it is dominated by whichever order is largest and hides the per-order picture completely.
> The per-order audit is `analysis/harmonic_audit.py`; run it, not the THD table, when judging the shaper.

> ### ⭐⭐ COMPRESSION AUDIT 2026-09-08 — the model has EXACTLY NONE, and that is a truncation bug
>
> `analysis/compression_audit.py`, raw `analysis/reports/compression_audit.json`. Asked because
> compression is the cubic's SIGN; it turned into the most useful nonlinear instrument in the set.
>
> ⭐ **COMPRESSION IS THE BEST-CONDITIONED NONLINEAR MEASUREMENT THIS DATASET HAS.** Its known-answer
> floor (same probe as always: below the shelf zero all three modes have Zs = R5 and must compress
> identically) is **0.145 dB (P1) / 0.210 dB (P2)** — against **4–9 dB** for the harmonics. Reason is
> structural and worth reusing: **it is measured on the FUNDAMENTAL, not on a harmonic 40–60 dB down.**
> P3's compression signal is 2–3× its floor, so P3 is measurable where every harmonic was not.
>
> ⚠⚠ **THE PLUGIN'S COMPRESSION IS EXACTLY 0.000 dB AT EVERY BAND AND EVERY LEVEL.** The captures are
> not: P3 reads −0.42 dB (794 Hz) and −0.61 dB (3.1 kHz) at the top cell, P2-bright −0.18/−0.35, and
> **Bright compresses more than Dark**, which is the physically right sign. P1 sits under its own floor.
>
> ⭐⭐ **IT IS NOT A MISSING CUBIC — IT IS THE VOLTERRA TRUNCATION.** An independent exact solve of
> `id = gm·g(vg − id·R5)` with `g` a PURE square law plus its cutoff clamp (no `beta` at all) DOES
> produce compression and H3; the shipped shelf-on-the-excess approximation drops both. So `beta`
> should stay 0 — fitting one would be fitting a residual smaller than the floor. ⚠ The bare parabola
> turns over at `w = −Vov`, so any oracle written for this MUST carry the cutoff clamp; without it the
> solve goes non-monotone above ~0.4 V of gate swing and returns garbage (it did, first attempt).
>
> ⭐ **A SELF-CONSISTENCY CHAIN THAT ACTUALLY CLOSES, at Vov = 0.150 with nothing fitted:**
> | Route | Predicts | Measured | Error |
> |---|---|---|---|
> | P1's **known** −12 dBu drive → H2 | −41.0 dBc | −44.1 | **+3.1 dB** |
> | P1's known drive → compression | −0.022 dB | under its 0.145 floor | consistent |
> | P3's **compression** → drive → H2 | −25.1 dBc | −26.8 | **+1.7 dB** |
> | P3's compression → drive → H3 | −31.6 dBc | −41.2 | +9.6 dB |
> Two independent anchors (a calibration and a compression curve) agree on H2 inside the floor. The
> inferred rig levels are −3 to −5 dBu for P2/P3, i.e. 7–9 dB hotter than P1 — ordinary reamp levels,
> and exactly why their nonlinear data clears the floor and P1's does not.
> ⚠ P2-mid's compression (−0.137 dB) is BELOW P2's own 0.210 dB floor, so its inferred drive is not
> trustworthy — that is the row that misses by 12.6 dB. Do not read it as a model error.
>
> ⚠⚠ **THE TWO FIXES ARE COUPLED — DO NOT APPLY Vov ALONE.** Truncation error grows fast with
> curvature, so lowering Vov makes the missing compression much worse. At a 0 dBFS input (A_gate =
> 0.783 V) the exact solve compresses **−0.032 dB at the shipped Vov = 0.447 but −1.05 dB at Vov =
> 0.150**, with H3 at −57.8 vs **−24.5 dBc**. The model says 0.000 dB and −135 dBc in both cases.
> ➡ **Lowering Vov without replacing the truncation would ship a stage that is quantitatively wrong
> in its own normal operating range** — and note that range is exactly where users sit, since
> kInputRef = 0.87 is 10 dB above the trainers' level.
>
> ### ⚠ WHAT TO ASK FOR WHILE CAPTURING THE TWO-SETTING PEDAL (owner has it available, 2026-09-08)
> This capture can close nearly every open item, but only if these are recorded AT capture time:
> 1. ⭐⭐ **Both NAM calibration figures, written down: `input_level_dbu` AND `output_level_dbu`.**
>    The input one broke the aEven/level degeneracy for P1; **the output one is the only thing that can
>    ever anchor `kOutputMakeup`**, which is still exactly 1.0 and cannot be derived from anything we
>    hold. Note NONE of the seven existing `.nam` files carries either field — do not assume the file
>    will record it.
> 2. ⭐ **A BYPASSED capture through the identical rig.** This is build-plan §5 measurement #4 and the
>    dataset's oldest hole (limit L2). It pins `kInputRef` directly instead of by inference.
> 3. ⭐ **A VOLUME sweep, everything else fixed.** Still the only within-rig measurement that can settle
>    the taper (p ≈ 2.0), the 3.9 dB vs 1–2 dB fall-back, and the C10 corner — all three are currently
>    blocked on exactly this and nothing else.
> 4. ⭐⭐ **SET NAM's INPUT CALIBRATION TO −2 dBu. Not "hot", not "near P3" — exactly −2 dBu.**
>    `V/FS = 0.7746 × 10^(−2/20) × √2 = 0.8701`, which **IS `kInputRef` = 0.87 to three decimals.** At
>    that setting the capture's digital levels map 1:1 onto the plugin's, so matched-drive A/B becomes
>    matched-LEVEL A/B, the whole drive-offset axis disappears, and the aEven-versus-reamp-level
>    degeneracy cannot recur for this unit. −3 dBu is within 1 dB and −4 dBu within 2 dB if the rig
>    cannot hit it exactly; **−12 dBu, P1's setting, is 10 dB too quiet and is why P1's entire
>    nonlinear dataset is floor.**
>    ⚠ **This is also what fixes the COVERAGE hole, which is bigger than the floor problem.** At
>    −12 dBu even the signal's hottest cell (−1 dBFS) puts only 0.22 V on the gate, which is what a
>    user playing at **−11 dBFS** produces. Everything above that is extrapolation. Rhythm guitar
>    tracked at −12 dBFS average with a ~12 dB crest factor peaks at 0 dBFS = **0.78 V on the gate**,
>    which P1's rig would need +10 dBFS to reach — 11 dB off the top of the capture set. At −2 dBu the
>    existing signal covers the whole range with nothing left over.
> 5. Both switch positions, and the null/no-plugin render (still missing — M0 has never run).

> ### ⭐⭐ A REAL BUG FIXED 2026-09-08: `useIntegerLatency = true` WAS DEFEATING THE LINEAR-PHASE FIR
>
> `src/PluginProcessor.cpp` (constructor comment carries the numbers), guarded by a new section 4 in
> `tests/OSFidelity.cpp`. Eleven tests pass.
>
> **The chain was built with `juce::dsp::Oversampling(..., filterHalfBandFIREquiripple, true, true)`.
> The last argument is `useIntegerLatency`, and JUCE implements it by appending a FRACTIONAL-DELAY
> filter — which is not linear phase.** So the code was paying for a linear-phase FIR, with a comment
> three lines above explaining that step 9's sub-sample null depends on it, and then adding an allpass
> after it. Measured against the same chain at 8×, best-fit delay removed (i.e. dispersion, not latency):
>
> | factor | 18 kHz error, `true` | `false` |
> |---|---|---|
> | 1× | +7.2° | −10.0° |
> | 2× | +14.3° | −2.8° |
> | **4× (SHIPPED DEFAULT)** | **+52.2°** | **−0.6°** |
>
> ⭐ **The tell was NON-MONOTONICITY, and it is the reusable lesson.** Oversampling can only make the
> top octave more faithful, so error must FALL as the factor rises. It rose: 4× was 7× worse than 1×.
> Magnitude matched to **0.03 dB** at the same time — and a 0.03 dB magnitude difference cannot produce
> 52° in a minimum-phase filter, so the phase had to be coming from an allpass. **No magnitude test
> could ever have found this**, which puts it alongside the input-network inversion and the 16.4 dB
> distortion bug as the third defect on this project that only phase testing could see.
>
> **Against the real P1 capture at the 4× default**, the 15 kHz residual moves +18.7 → −5.1° (Bright)
> and +18.3 → −5.5° (Dark). Whole-band RMS residual **5.5–7.2° → 3.1–4.4°**.
>
> **What it costs:** `getLatencyInSamples()` is fractional again, so `setLatencySamples()` rounds and
> the host is misaligned by up to half a sample. That is a CONSTANT, frequency-independent offset —
> strictly better here than frequency-dependent dispersion, since step 9 nulls sub-sample anyway.
>
> ⚠ **`OSFidelity` section 4 guards it, and two things in that guard are worth not re-deriving:**
> (a) **phase must be UNWRAPPED before fitting a delay** — `std::arg` wraps to (−π, π] and one sample
> at 18 kHz is already 135°, so the first version reported 557° of "dispersion" that was really a
> wrap; (b) **1× is excluded on purpose** — it has no resampler, so the quantity does not exist there,
> and its bulk delay vs 8× is the oversampler's whole ~65 samples. `getLatencySamples()` cannot remove
> that inside the test either, because a factor change is applied at the START of the next block, so
> the value read right after `configure()` still describes the previous factor.
> **Verified the guard actually fails** when the flag is flipped back, rather than assuming it would.
>
> ### 📌 WHERE THE 1 dB / 5° TARGETS NOW STAND (owner's goals, 2026-09-08)
> - ✅ **Phase, 200 Hz–12 kHz: MET.** P1 reads within **2.4°** (Bright/Dark) and 6.2° (Mid) at the 4×
>   default. Whole-band RMS 3.1–4.4°, against a 5° target.
> - ⛔ **Phase below 200 Hz: BLOCKED, not a model error.** −6° at 200 Hz growing to −35° at 50 Hz.
> - ⛔ **Magnitude within 1 dB: BLOCKED for the same reason and by the same feature.** P1 is already
>   within ~1 dB from 40 Hz up; the miss is +2.4 dB at 40 Hz and +2.9 at 32 Hz.
> - ⭐⭐ **Both LF misses are ONE missing high-pass pole, and magnitude and phase independently agree on
>   its corner** — a genuinely new cross-check this session, since phase did not exist before:
>   | capture | fc from magnitude | fc from phase | joint fit residual |
>   |---|---|---|---|
>   | P1 | 28.7 / 26.5 / 28.8 Hz | 25.4 / 26.5 / 26.0 Hz | 0.09–0.20 dB, 5.1–5.5° |
>   | P2 | 19.1–20.0 Hz | 16.8–17.1 Hz | 0.07–0.10 dB, 3.1° |
>   | P3 | 18.2 Hz | 12.0 Hz | 0.20 dB, 2.2° |
>   ⛔ **But the three units give 27 / 18 / 13.5 Hz, so it CANNOT be fitted** — that is note #7's M4
>   confound reappearing on a second axis. Adding a 27 Hz high-pass would fit P1's rig. ➡ It needs the
>   bypassed capture and the VOLUME sweep, which is now the *only* thing standing between the model and
>   both stated targets. Everything else in the audio band is already inside them.

> ### ⭐⭐ PATH A IS IN (2026-09-08): the loop is SOLVED, not expanded. Compression and H3 now exist.
>
> `src/dsp/JfetStage.h`, guarded by a new section 8 in `tests/JfetStageTest.cpp`. New instrument
> `analysis/band_audit.py` (+ `analysis/reports/band_audit.json`). Write-up `docs/build-plan.md` §13,
> circuit consequences `.claude/rules/circuit.md` notes #12–#14. **All 11 tests pass, warning-free.**
> ⚠ **NO fitted constant changed** — `gm`, both shelf τ, `aEven`/`Vov`, `beta` = 0 are exactly as
> step 4b left them. Only the structure moved.
>
> **The gap note #11 identified is closed.** The stage now solves `id = gm·g(vGate − Zs(z)·id)` by
> Newton per sample. Compression and H3 are higher-order terms of the truncated expansion, so the old
> structure could not make them **at all** (exactly 0.000 dB of compression in every band at every
> level). Now: **−0.032 dB at 0 dBFS and −0.178 dB at +6 dB of trim, H3 −57.6 and −41.2 dBc** against
> the truncation's −79.5 / −60.9. `beta` stays 0 — the cubic is the LOOP's, not a fitted coefficient.
>
> ⭐⭐ **The one design decision that mattered: the discrete source one-port is DERIVED FROM THE
> SHIPPED SHELF**, by inverting `1/(1 + gm·Zs(z)) = H(z)`, not by discretising R5 ∥ C afresh. The
> small-signal response is therefore unchanged to **1.1e-11 dB / 4.8e-15°** over 3 modes × 4 rates ×
> 5 frequencies, so the mode differential, the three-point discretisation, the phase residuals and
> `OsDroopRestore`'s premise all stand untouched. ⛔ **The obvious alternative — discretising R5 ∥ C
> directly — would have silently reintroduced plain bilinear on a shelf whose pole is above Nyquist
> at base rate, i.e. the exact 3.5 dB error the previous session removed.** Two facts fall OUT of the
> algebra rather than being imposed: `Zs(z=1) = R5` to 1e-16 in every mode at every rate, and DARK
> collapses to the bare resistor with no state.
>
> ⭐ **Cross-implementation check:** `compression_audit.py`'s Python oracle, written weeks earlier,
> said −0.032 dB at 0.783 V. The C++ solve reads **−0.0324**. Neither can inherit the other's bug.
>
> ⚠ **Two intuitions were measured and both were WRONG.** (a) **A warm start is decisively worse than
> a cold one** — the closed-form linear root already inverts 85–97 % of the answer, while the previous
> sample's `w` carries the whole sample-to-sample change as error (worst residual 1.3e-02 cold vs
> 8.2e+00 warm at 384 kHz DARK). (b) **The residual is the wrong criterion for the iteration count**:
> two iterations leave an alarming-looking 13 mV at the loudest reachable state, but that is a
> ONE-SAMPLE transient at a discontinuity and the harmonic error there is 0.02 dB. Two is shipped.
>
> 📌 **CPU: 2.07 → 4.78 % at the 4× default, 3.51 → 8.95 % at 8× (2.3×).** Still comfortable, but
> `dsp.md`'s "the linear-phase FIR costs nothing worth recovering" was decided when the chain was 2 %.
> 📌 A fused shape-and-slope function to recover it measured **exactly free** (5.01 vs 5.05 %) — both
> inline on the same argument, so the compiler already shared the work — and was removed rather than
> kept. The cost is the iteration count, nothing else.
>
> ### ⚠⚠ AND THE THD QUESTION THAT PROMPTED IT: the deficit is the REFERENCE's, not the model's
>
> **Below ~100 Hz.** The captures read up to **50 % THD at 20 Hz**, 12–37 dB above the model. Three
> independent reasons none of it is the pedal: **H3 comes back ABOVE H2 at 20 and 31.5 Hz in every P1
> mode** (impossible for a square law); the known-answer probe reads **20.2 dB at 20 Hz where the
> circuit forces 0.00**; and 50 % THD cannot happen at ~0.14 V of gate drive into a 22 V rail. The
> mechanism is structural — **132 ms of receptive field is 2.6 periods at 20 Hz**, and the output
> high-pass being reproduced sits at 24–44 Hz. ⛔ **Do not fit anything to a capture below ~100 Hz.**
> ⚠ The floor probe measures mode SPREAD, so an error common to all three modes reads ZERO — and an LF
> receptive-field artefact is exactly that, since the high-pass is identical in every mode. **At low
> frequency the probe systematically under-reports**; its 8.3 dB at 50 Hz is a lower bound.
>
> **From 125 Hz up the deficit is real but FLAT** (P1-dark −7.0 to −11.6 dB across 125 Hz–1.6 kHz, no
> trend, mean −9.6 dB). That is note #10's `Vov` scalar, unchanged. ➡ **There is no missing
> frequency-dependent distortion mechanism to build.** The treble goes the same way: P1-dark's −18 dB
> at 8 kHz cannot be physical (in DARK `k` is constant, so H2 in dBc must not move with frequency) and
> its 5 kHz cell fails the order-inversion test.
>
> 📌 ⚠ **"Rises 2 dB/dB with level" does NOT establish that a harmonic is real.** P3's H2 rises at a
> clean 1.9–2.1 dB/dB in every band — and its THD is flat at ≈ −30 dBc from 20 Hz to 8 kHz, which the
> circuit forbids since distortion moves as k². Pair the level-slope test with a frequency-shape test.
> ⚠ `band_audit.py` applies P1's −12 dBu offset to EVERY capture, so **only P1's column is a valid
> matched-drive comparison**; P2/P3 carry an unknown ~8 dB (their rig levels are inferred, not known).
>
> ### ⚠⚠ The compression known-answer probe needs 3f below the shelf zero, not f
> Compression is third order, so it reads the loop at 3f. Measured on the plugin, the probe's own
> spread is 0.0015 dB at 94 Hz but **0.0585 dB at 797 Hz** (3f = 2391 Hz, past the 1864 Hz zero) —
> and `compression_audit.py`'s top known-zero band was 800 Hz. ✅ On this dataset it reaches nothing:
> dropping that row leaves both floors unchanged (0.145 P1 / 0.210 P2), so a lower band binds. Both
> figures are now reported. ⭐ A second effect hides under it: at the lowest probe frequency the
> model's spread stops falling at 0.0017 dB, which is **the fixed iteration count leaving a
> mode-dependent bias** (`Rd·gm` differs ~30× between Dark and Bright). Converging collapses it to
> 0.00005 dB. Harmless at 85× below the capture floor, but exactly what would later be mistaken for a
> real mode asymmetry.
>
> ### NEXT — and note #11's coupling is now discharged in one direction
> - ⭐ **`Vov` is the remaining compression gap and it is now applicable in principle.** The plugin
>   moves the right way on every axis (Bright compresses more than Dark, more at HF) but is 10–20×
>   short in magnitude. Path A across the admissible bracket at 0 dBFS: **−0.032 dB at the shipped
>   0.4469, −0.143 at 0.250, −0.808 at note #10's 0.1502, −1.100 at the 0.131 floor** (captures:
>   P2-bright −0.18..−0.35, P3 −0.42..−0.61). ⛔ **Still NOT applied** — three of note #10's four
>   reasons stand (the −12 dBu is a recollection; the floor pins `Vov` only to ~1.4×; it moves the
>   drain 14.4 → 19.4 V and **invalidates `JfetStage.h`'s load-line arithmetic**). Only the fourth,
>   the truncation, is discharged. ➡ Apply with a re-derived load line, or wait for a second
>   calibrated capture.
> - **Unchanged and still blocking:** `kOutputMakeup` = 1.0 with no anchor; the two-way pedal's VOLUME
>   sweep; the missing no-plugin null render (M0 has never run).

> ### ⭐⭐ THE THREE UNREAD MEASUREMENTS (2026-09-08). No new captures. `analysis/imd_and_floors.py`.
>
> Write-up `docs/build-plan.md` §14, circuit consequences `.claude/rules/circuit.md` note #15. Two
> helpers had sat uncalled in `analyze.py` since the harness was built, and no script had ever
> touched an `imd_guitar_*` segment.
>
> ⭐⭐ **THE REFERENCE IS NOISE-FREE, SO EVERY "FLOOR" IN THIS PROJECT IS SYSTEMATIC ERROR.** Noise
> floor **−99 to −132 dBFS**; repeatability **−114 to −135 dB**, measured against a byte-identical
> duplicate cell. So the 4–9 dB harmonic floor, the 0.145–0.210 dB compression floor and the
> 1.9–20.2 dB per-band THD floor are the NAM models' deterministic error, not noise. ⛔ **None of it
> will average down** — more cells or longer dwells buy nothing. And the mode-spread probe measures a
> **difference of two systematic errors**, which can partially cancel, so it under-reports for a
> second reason on top of its common-mode blindness.
>
> ⚠⚠ **The twin-tone segment cannot measure IMD: 660 = 3 × 220 exactly**, so every product lands on
> the 220 Hz harmonic grid and neither input tone is a clean amplitude reference. Fixing it needs a
> NEW segment with an inharmonic pair, the signal being append-only. Not worth a re-capture alone.
>
> ⭐ **What it could still do — an off-grid known-answer probe — came back clean.** A memoryless
> circuit can put energy only on that grid; every capture reads **−136.6 dBc** off it, which is the
> files' own quantisation floor. The references' error is wrong AMPLITUDES on the right bins, not
> spurious junk, and there is no sign of memory effects or aliasing in them.
>
> ⭐ **The instrument validates on the plugin first**: product level slopes come back
> **1.00 / 1.00 / 1.00 / 2.00 / 2.00**, exactly what second- and third-order products must do.
> Then the captures split the same way circuit.md note #10 did, from a different signal and different
> bins: **P1's slopes are 0.1–0.9 where 1.0/2.0 are required, so P1's twin-tone data is floor**, while
> P2-bright reaches 1.64 on a third-order product and P3 returns 1.00/1.03/1.02 on second-order ones.
> P1's second-order deficit of 6–15 dB matches the single-tone H2 deficit, so this corroborates the
> `Vov` story rather than adding leverage.
>
> ➡ **Net: no new fitting leverage; the cruel split is confirmed by a third route.** Asking the P2/P3
> trainers for their `input_level_dbu` remains the highest-value action, and it is one message.
>
> 📌 Also reconciled this session: circuit.md note #11's −1.05 dB and Path A's −0.808 dB at
> `Vov` = 0.150 are not in conflict. That oracle used a PURE parabola; the shipped shaper uses the
> `tanh²` bump, which saturates where the parabola does not. **That makes `tanh²` the largest
> remaining shaper approximation now the truncation is gone** — 23 % of the compression at the low
> `Vov`, 4.5 % at the shipped one. Replace it AT THE SAME TIME as `Vov`, not separately.

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
