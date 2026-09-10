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
> 4. ⭐⭐ **SET NAM's INPUT CALIBRATION TO +12.2 dBu — i.e. reamp at UNITY, no attenuation.**
>    ⚠ SUPERSEDES an earlier "−2 dBu" in this file, which was correct only while `kInputRef` was
>    0.87. The rule is `input_level_dbu = 20·log10(kInputRef / 1.0955)`, and `kInputRef` is moving to
>    **4.4626 V/FS** — the value a well-recorded guitar metering −12 dBFS RMS at ~0.78 V implies, and
>    the owner's own tracking calibration. `20·log10(4.4626/1.0955) = +12.20 dBu`, which is exactly
>    the interface's output at full scale, so **the reamp box should pass unity rather than attenuate.**
>    ✅ This puts the capture's digital levels 1:1 onto the plugin's, and the existing test signal then
>    spans 0.022 V to 3.98 V at the pedal — from clean right through the load line, which is precisely
>    the range no existing capture covers.
>    (Historic, for the arithmetic: `V/FS = 0.7746 × 10^(dBu/20) × √2`. At
>    −12 dBu, P1's setting, V/FS is 0.2752 — 24.2 dB below the interface, i.e. an ordinary reamp
>    attenuation, and why P1's entire nonlinear dataset is floor.)
>    ⚠ If the rig cannot hit +12.2 exactly, note the actual figure and the offset is arithmetic.
>    ⛔ Do NOT attenuate "to be safe": the whole point is to reach the loud end.
>    ⚠ **This is also what fixes the COVERAGE hole, which is bigger than the floor problem.** At
>    P1's −12 dBu even the signal's hottest cell (−1 dBFS) puts only 0.22 V on the gate. Under the new
>    calibration that is what a user playing at **−25 dBFS** produces, so the ENTIRE existing dataset
>    describes quiet playing and nothing else. Rhythm guitar at −12 dBFS average with a ~12 dB crest
>    peaks at 0 dBFS = **4.0 V at the gate**, and the drain enters triode at a ~1.6 V gate swing, i.e.
>    at **−6.7 dBFS** — so the top 6.7 dB of every normal take is in the load-line region that no
>    existing capture reaches and that the model did not implement. Capturing at +12.2 dBu is what
>    puts that region on the record.
> 5. Both switch positions, and the null/no-plugin render (still missing — M0 has never run).
> 6. ⭐ **A DI of the guitar through the same interface input, at a noted gain setting.** One extra
>    pass while the gear is already set up, and it is the ONLY thing that pins `kInputRef` without
>    inference: the DI's peak dBFS plus the interface's known calibration gives the guitar's actual
>    peak volts, which IS `kInputRef`.
>    ⚠⚠ **And write down WHERE each level is measured.** The interface's +12.2 dBu at 0 dBFS and the
>    pedal jack's −12 dBu are the SAME rig with a 24.2 dB reamp box between them. Both are correct;
>    confusing them is a 24 dB error that would invert every harmonic conclusion on record.

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

> ### ⭐⭐ THE DEVICE MODEL (2026-09-09): kInputRef, the load line, and the shaper that had to go
>
> `src/dsp/JfetStage.h` + `OutputNetwork.h` + `EchoPreDsp.h`; guarded by `JfetStageTest` section 8.
> Write-up `docs/build-plan.md` §16. **All 11 tests pass, warning-free; `auval` passes.**
>
> **`kInputRef` 0.87 → 4.4626 V/FS** — a well-recorded guitar metering −12 dBFS RMS measures ~0.78 V
> RMS, and a full-scale sine at that calibration is 3.156 V RMS = 4.4626 V peak. Owner's own tracking
> calibration, and the one the capture session will use.
>
> ⭐⭐ **The calibration did not cost anything; it changed WHAT HAD TO BE MODELLED.** The drain enters
> triode at a **1.691 V gate swing** — a property of the rail, R6 and the bias point, unchanged by any
> of this. At the old calibration that was **+7.5 dBFS** (unreachable without trim, correctly
> deferred); at the new one it is **−7.5 dBFS**, the top 7.5 dB of every normal take. So the load line
> had to be built — and building it forced the fitted shaper out, because that shaper's even bump
> ALONE asymptotes at 347 µA against the load line's ~542 µA ceiling. `JfetStage.h` had recorded that
> impossibility in advance and was right.
>
> **What replaced it: the device's own equations.** Shichman-Hodges with cutoff and triode, solved
> implicitly against the source one-port AND the drain load. `aEven`, `bumpScale`, `beta`, `limitPos`,
> `limitNeg` are gone; **`Vov` is the single amplitude parameter.** The stage is finally shaped like a
> preamp — clean to −12 dBFS (0.06 dB compression, H2 −36.6 dBc), gritting at −6 (0.65 dB, −22.0),
> into triode from −3 (2.4 dB) and odd-dominant at 0 dBFS (H3 −12.9 above H2 −21.3), which is what a
> stage clipping against cutoff and triode must do.
>
> ⚠⚠ **PLAIN NEWTON DOES NOT WORK ON THE SQUARE LAW.** `F'` runs 1 in cutoff to ~40 in saturation, and
> it **cycles** — measured, a period-3 orbit stuck near 1e-2 A at any iteration count. A warm start is
> worse (stuck at 2.7e-2 A). It needs a **safeguarded Newton**, bracket `lo = −Id0` (cutoff) and
> `hi = (Vds_q − vOff)/(zLoad + Rd)` (drain bottomed), ~980 µA wide.
> ⚠ **The bracket test must be NON-STRICT** — the root sits exactly ON an end whenever the device
> cuts off or the drain bottoms, which is what loud half-cycles do; with strict tests a converged
> iterate is rejected and MORE iterations make it WORSE. Measured.
> ⚠⚠ **Never "improve" the final iterate with `i = I(...) − Id0`** — that is a fixed-point step and
> `|dI/di|` reaches 8.8, so it MULTIPLIES the error. The old structure did it harmlessly; against the
> square law it sent a 3 V input to 9.5 mA, past IDSS.
>
> **kSolveIters = 8, chosen on HARMONICS not the residual.** Worst cell (Mid, 48 kHz) H2 error vs a
> 40-iteration solve: 3 iters −14.52 dB, 4 −6.55, 5 −2.52, 6 −1.04, **8 = 0.00**. ⭐ Ordinary playing
> (−12 dBFS) is exact at THREE; only the load-line region needs eight. ⛔ A hard input discontinuity is
> NOT the driver — refuted: bandlimited and stepped tones converge at the same rate.
>
> 📌 **CPU 4.78 → 5.74 % at 4×, 8.95 → 10.97 % at 8×. A 20 % rise, not the tripling an intermediate
> reading showed.** ⚠⚠ That reading was a **stale-parameter bug in my own probe** — exactly what
> `ProbeHarness`'s `Setup` comment warns about. `setSolveIters` was applied conditionally, so a
> "shipped" run inherited the previous count: PerfBenchmark's shipped column read 11.71 % against the
> 20-iteration column's 11.76 %, i.e. it measured 20 twice. `setSolveIters(0)` now RESTORES the
> shipped count. **Re-measure before believing a CPU regression.**
>
> ⚠ **FeatureProfile under-discriminates here and now says so.** Its columns are chain-level and
> largely blind to convergence — an earlier cut read **2 iterations as "converged, buys nothing"**
> when a stage-level probe put its H2 14.5 dB out. `JfetStageTest` 8c is the authority; the profile is
> kept for its CPU column, which is exact.
>
> ✅ **Preserved:** small-signal response identical to **1.6e-10 dB / 4.7e-10°** over 3 modes × 4 rates
> × 5 frequencies; `Zs(z=1) = R5` exactly; compression still matches `compression_audit.py`'s Python
> oracle (−0.0321 vs −0.032). ⭐ And §14's mode-dependent iteration bias is **gone** (0.000044 dB at
> the shipped count and fully converged alike).
>
> ⛔ **Given up: ADAA.** The map is now 2-D (`I(Vov_i, Vds_i)`), so ADAA1's derivation does not apply
> and no closed-form antiderivative of the composite exists. It was already off at every factor and
> measured as beaten by simply raising the factor. Removed from `JfetStage`, `EchoPreDsp`,
> `PluginProcessor`, `ProbeHarness`, `OSFidelity`, `PerfBenchmark` and `FeatureProfile` rather than
> left as a no-op. `PluginProcessor.h` records where it could go back if the profile ever asks.
>
> 📌 **The drain load is now VOLUME-dependent**, pushed from `OutputNetwork::drainNodeImpedance()`
> once per block. It runs 9.8–17.9 kΩ across the rotation, which moves the triode onset by ~4 dB — a
> fixed constant would have put the load line in the wrong place at one end of the knob.
>
> ### NEXT
> - ⭐ **`Vov` is now the only amplitude parameter and every blocker on moving it is discharged** —
>   the truncation (note #12), the shaper's `tanh²` (gone), and the load line (built). What remains is
>   that it is degenerate with the trainers' reamp level, which the +12.2 dBu capture is designed to fix.
> - **Unchanged:** `kOutputMakeup` = 1.0 with no anchor; the VOLUME sweep; the missing M0 null render.

> ### 📋 NEXT STEPS (2026-09-09) — what P1 alone can still do, and what it cannot
>
> Asked directly: can P1's calibration carry the harmonics/THD/compression and the FR/phase targets,
> or is the owner's capture required? Answered per axis, with the reason.
>
> **1. ⭐ Vov CAN be fitted from P1 now, to about a factor of 1.5 — do it.** P1's H2 deficit is
> 8.9–11.6 dB across 125 Hz–800 Hz and **clears its own per-band floor by 1.6–6.0×**, so it is a real
> measurement rather than noise. It already indicates `Vov ≈ 0.15` against the shipped 0.447.
> ⚠⚠ **But do NOT expect √N from the 24 cells.** Note #15 established these floors are SYSTEMATIC
> rather than noise, so averaging over cells does NOT reduce them — the pinning stays near the
> single-cell floor. That kills the 0.61 dB standard error a naive sd/√24 would claim, and with it any
> hope of the 5 % target from this dataset.
> ⭐ **Use TWO independent P1 observables and check they agree**: the midband H2 deficit, and
> compression **above 3 kHz only** (below that P1's compression reads POSITIVE, i.e. expansive, which
> the circuit forbids — that region is floor despite exceeding the floor's magnitude). Agreement
> between two unrelated routes on the same parameter is the strongest evidence available here.
> ⛔ **What P1 structurally cannot give:** anything in the load-line region (its hottest cell puts
> 0.25 V on the gate; triode starts at 1.69 V — a factor of 6.8 away), and any absolute level, so
> `kOutputMakeup` stays unanchored. Both need the +12.2 dBu session.
>
> **2. ⭐ The 4–8 kHz FR dip is actionable NOW and has never been investigated.** `goal_check.py`
> puts the core-band misses in two clusters, and only one of them is the known confound:
>   * 80 / 101 / 127 Hz, **+0.6 to +1.06 dB** — the missing LF high-pass pole. ⛔ Still confounded
>     (three units give 27 / 18 / 13.5 Hz), still needs the within-rig VOLUME sweep, still must not be
>     fitted to P1's rig.
>   * 4064 / 5120 / 6451 / 8127 Hz, **−0.5 to −0.67 dB**, present in all three modes — **NOT the LF
>     story and not yet explained.** ⚠ Note the sign rules out the obvious candidate: P1 fits a
>     6.7 kHz input pole against the 7.3 kHz shipped, and a LOWER real corner would make the plugin
>     BRIGHTER there, not darker. This is the single most actionable FR item.
>
> **3. Phase: reconcile the instruments before touching the model.** Whole-band RMS agrees between
> `phase_sweep.py` (3.14–4.43°) and `goal_check.py` (3.56–4.12°), so there is no instrument problem
> there. But the recorded "within 2.4° over 200 Hz–12 kHz" and `goal_check`'s 6.66–8.22° over the same
> band do not agree, and one of them is measuring something else (fit window, weighting, or 4× vs 8×).
> ⚠ Settle that first — acting on the larger figure without knowing which is right would be tuning to
> a measurement artefact. ⭐ And do it AFTER item 2: a minimum-phase magnitude dip at 4–8 kHz carries
> phase with it, so the two may be one finding.
>
> **4. Everything else waits for the capture**, and the list is unchanged: `kOutputMakeup` has no
> other possible route, the VOLUME sweep is the only within-rig measurement that can settle the taper
> and the LF pole, and the M0 null render has still never run.
>
> 📌 **On component tolerance (owner asked whether the targets are too tight): they are not.**
> Two real pedals agree to **0.33 dB RMS / 0.80 dB peak** on the rig-cancelling mode differential —
> the only genuine unit-to-unit figure in the dataset. A ±5 % resistor tolerance on R5 is worth only
> **±0.07 dB** of stage gain, because the degeneration factor is dominated by the product `gm·R5`
> rather than by either term; what it does move is corner FREQUENCIES by the same ±5 %, worth a few
> tenths of a dB near a corner. So ±0.5 dB is comfortable in the midband and sits at about the
> tolerance level near the band edges. ⚠ The measured 7 % offset in the shelf zeros is exactly this
> effect, and it is already absorbed because the model uses the measured τ rather than a computed
> `R5·C`. ⚠ **But the fit is to P1 specifically**, so hitting ±0.5 dB against P1 does not guarantee
> ±0.5 dB against the owner's own unit — that is what their capture answers.

> ### ⭐⭐ ALL THREE OF THAT LIST ARE NOW DONE (2026-09-09). Two of them reversed a recorded belief.
>
> Instruments: `analysis/vov_fit.py`, `analysis/hf_shape_fit.py`, `analysis/phase_reconcile.py`
> (+ their JSON in `analysis/reports/`). Detail in `.claude/rules/circuit.md` notes **#16–#18** and
> `src/dsp/JfetStage.h`. **All 11 tests pass, warning-free. NO DSP constant changed.**
> New: `OfflineRender --vov V`, a measurement flag beside `--input-scale` — every other quantity in
> the device model is derived from `Vov`, so it cannot be swept from outside the stage.
>
> **1. ⭐⭐ `Vov` IS FITTED, TWICE, AND THE TWO ROUTES AGREE — note #16.** Route A (H2 in dBc,
> 125–800 Hz, 60 cells) gives **0.126**; route B (compression GROWTH over the top 10 dB, 5–8 kHz —
> a different ORDER off a different signal) gives **0.165**. A factor of **1.31** apart, inside the
> ~1.5 the floor allows, bracketing note #10's independent 0.150. The shipped **0.4469 is outside
> both by ~3×**. Both routes recover an off-grid `Vov` = 0.2200 from a plugin render in `--self-test`
> before any capture is read.
> - ⚠⚠ **Route B only works on the INCREMENT.** `comp_db` carries the reference's level-INDEPENDENT
>   gain error as compression that was never there (P1-dark: −0.089/−0.087/−0.088 dB over the top
>   10 dB — flat). Differencing across level cancels it and drops the route's floor **0.145 →
>   0.013 dB**, the latter measured as the worst DARK increment.
> - ⚠⚠ **And do not pool by median across modes.** DARK has no leverage (`Zs = R5` at every
>   frequency), so a median sits on it: pooled that way route B's statistic moved **0.01 dB across
>   the whole admissible range** and read as a dead route. It is not — bright's 8 kHz cell spans
>   0.85 dB. **The statistic was flat, not the observable.**
> - ⛔ **STILL NOT APPLIED, and note #10's reason 3 is discharged and replaced by a stronger one.**
>   The load line does NOT move (triode onset 1.691 → 1.737 V; lowering `Vov` raises `Vds_q` by
>   almost as much as it lowers the current). **CUTOFF moves, and overtakes it**: first clipping goes
>   from triode at −7.5 dBFS to **cutoff at −12.2 dBFS**, i.e. the stage's clipping MECHANISM
>   changes. That is a qualitative voicing change decided by a parameter pinned to a factor of
>   1.3–1.5, from the one capture whose harmonic data clears its own floor by 4.7 dB. Reasons 1, 2
>   and 4 stand. ➡ The +12.2 dBu session measures the mechanism directly instead of inferring it.
>
> **2. ⛔⭐⭐ THE 4–8 kHz DIP IS P1's RIG, AND P2 CONFIRMS THE 7.3 kHz INPUT POLE — note #17.**
> It is not a dip: as a whole curve the error runs **+3.4 dB at 25 Hz → 0 near 1 kHz → −0.6 dB at
> 5 kHz → +0.95 dB at 16 kHz**, and those four cells are its bottom. Modelling the capture as the
> plugin × a high-pass × two low-passes: **P2 returns 3.2 kHz + 7.4 kHz at a 0.021–0.034 dB residual
> in ALL THREE MODES** — the pedal's own `R3 ∥ R4` into `C3` pole, recovered from a capture with no
> prior, to within 2 %, plus note #7's 3.2 kHz cable pole. P3 accommodates 7300 Hz freely.
> **P1 cannot be described by any cascade containing a 7.3 kHz pole** — and since an extra pole can
> only DARKEN, P1 is *brighter* at 4–8 kHz than the circuit as drawn can be. ➡ **The plugin is not
> too dark; P1 is too bright. Do not touch `C3`, `R3` or the input network.**
> - ⭐ **The estimator is not shelf-blind because it fits capture-against-PLUGIN**, so the mode shelf
>   appears on both sides and cancels — exactly the term note #9 found missing from M3. Evidence:
>   P2's fitted poles are mode-independent to 3 %.
> - ⚠ **P1 is the noisiest model in the set and is also the designated absolute anchor.** Residual
>   ranking (P2 0.02 ≪ P3 0.22 ≈ P1 0.19–0.26) matches note #2's shelf-fit ranking exactly, from an
>   unrelated fit. P1's best model still leaves a structured +0.3 dB hump at 3–5 kHz — the same size
>   as the miss. `goal_check.py` now says so in its own docstring.
> - ⚠⚠ **A BOUND THE FIT SITS ON IS NOT A FIT.** Flooring the "extra" pole at 8 kHz — on the
>   reasoning that it must sit above the pedal's own — put P2 exactly on that bound in all three
>   modes at a 2.1 dB residual and hid the whole result. The two pole terms enter the model
>   identically, so neither is "the input pole" until something outside the algebra says so.
>
> **3. ✅ THE PHASE FIGURES RECONCILE; THE RECORDED "2.4°" WAS MISLABELLED — note #18.** One residual
> curve, re-read under each differing choice: the recorded figure is a max over **six hand-listed
> report frequencies from 500 Hz up**, and the same instrument reads **−5.85 to −6.47° at its own
> 200 Hz report point**, which the claim's band label includes and its arithmetic did not. A max over
> interpolated points is not a max over a band. `goal_check`'s statistic is the one the "within 5°
> across all bands" target asks for. 📌 The OS factor was never it — 4× → 8× moves 0.2°.
> ✅ **And the miss is entirely at the bottom:** 200–500 Hz reads 6.8–8.0°, while 500 Hz–2 kHz reads
> 1.6–2.6°, 2–8 kHz 1.4–2.3° and 8–12 kHz 1.8–4.3°. The 5° target is MET above 500 Hz. What fails is
> the tail of the missing LF pole, already confounded and already blocked on the VOLUME sweep.
> ⛔ Note #17 also removes the minimum-phase companion that was suspected: there is no model-side
> magnitude dip at 4–8 kHz for phase to be carrying.
>
> ### NEXT — everything left is gated on the owner's capture session, and on nothing else
> - **Apply `Vov`** once a capture reaches the load-line region and can show which mechanism clips
>   first. The fit is done; only the decision is outstanding.
> - **`kOutputMakeup` = 1.0, unanchored.** Needs `output_level_dbu` and has no other route.
> - **The VOLUME sweep** still owns the taper, the 3.9 vs 1–2 dB fall-back, and the LF pole — which
>   is now the ONLY thing standing between the model and both stated targets, on both axes.
> - **The M0 no-plugin null render has still never run.**
> `CLAUDE.md`'s capture list above is unchanged and still correct.

## Project-specific carry-forwards

> ⚠⚠ **This section used to restate circuit facts and NAM-dataset caveats in place. DON'T.**
> Circuit values, topology and every measured constant (kInputRef, kOutputMakeup, gm, the VOLUME
> taper, the MODE cap mapping, etc.) live in `.claude/rules/circuit.md` and ONLY there — it is the
> single source of truth for "what is the circuit and what have we measured". A second copy here
> went stale and actively wrong (it carried the pre-note-#2 MODE mapping, after note #2 had already
> reversed it, and it said `kInputRef` "cannot be measured" long after note #21 measured it). Do not
> recreate that problem: record a new circuit fact in circuit.md, not here.
>
> The session-by-session **plan** (what to do next, in what order) lives in `docs/build-plan.md`.
> This file (`CLAUDE.md`) is project memory: who/why, the build sequence checklist above, and the
> chronological log below of how the project got here — read it for the REASONING behind a decision
> (method lessons, traps already found, why a value was chosen), not for the current value of
> anything. Where this log and circuit.md or build-plan.md disagree, circuit.md/build-plan.md win —
> they are kept current in place; this log is append-only history and is never retroactively edited
> to match.


> ### ⛔⭐⭐ THE LF BASS EXCESS (2026-09-09) — circuit.md note #19
> ⚠⚠ **PARTLY SUPERSEDED LATER THE SAME DAY — read the block below this one before acting on
> it.** The reamp-transformer mechanism named here was REFUTED by measurement (note #20), and
> the claim that the VOLUME settings discriminate is an overclaim (note #19a). The elimination
> of the loading explanations and of R10 stands.
>
> `analysis/lf_pole_attribution.py`. **No constant changed, and none should be.** Asked whether the
> uniform "plugin has more bass than every capture" was a consistent circuit error worth fixing now.
>
> ⭐⭐ **The discriminator was free and unused: the three units sit at three VOLUME settings**, and the
> pedal's own output high-pass moves with the knob while anything in the capture chain does not. Fit
> each candidate as ONE global value across all seven and watch whether it needs a different value per
> capture. As drawn the worst error is 2.59 dB. **The rig's input impedance and the drain impedance
> both run to their sweep bounds and still fit 2–3× worse — the loading explanations are dead.** R10
> needs a 3× different value per volume setting, so it is not one component.
>
> ⭐ **What survives is a PER-RIG high-pass AHEAD of the pedal: 29 Hz (P1) / 21 Hz (P2) / 20 Hz (P3),
> the three modes within a rig agreeing to 5 %, residuals 0.09–0.38 dB — the models' own error
> floor, with the pedal left exactly as drawn.** Physical candidate: the reamp transformer every NAM
> training chain contains. Right sign, right decade, common to all three protocols, varies by box and
> level, and ahead of the pedal so it is volume-independent — the axis that fits best.
>
> ⛔ **Do NOT change C10.** The two in-pedal candidates that fit at all need **38 % (C10) or 3.3× (the
> input HP)** changes to components legible at high zoom. When two structurally different in-pedal
> explanations each demand an implausible value and fit no better than one out-of-pedal explanation
> that demands nothing, the common factor is outside the pedal. Same refusal as P2's cable
> capacitance and the guitar source impedance, same reason: **the plugin's signal path has no
> transformer in it.**
>
> ⚠⚠ **A low NAM `ESR` (≈3e-4 here) does not argue against this.** ESR measures the model against its
> OWN training target — the recorded output of that rig. A perfect model of a rig containing a reamp
> transformer has ESR ≈ 0. It confirms the models faithfully reproduce what was recorded, which is
> what makes these corner fits trustworthy as measurements of the RIG; it is silent on whether the rig
> was flat. It is also broadband and energy-weighted, so 2 dB at 25 Hz barely registers in it anyway.
> ⛔ **Cable capacitance cannot do it either, on structure alone** — a shunt C at the output is a
> LOW-pass. Removing bass needs a series element. Wrong axis.
>
> ➡ **Capture-session consequence: the bypassed pass (ask #2) is now the DIRECT test**, not a general
> anchor. Capture bypassed through the identical reamp chain and its LF rolloff IS the rig's
> high-pass, measured rather than inferred. If the owner's rig lands in the same 20–30 Hz decade that
> is confirmation, and the right response is to **deconvolve the rig out of the reference**, not to
> add a pole to the pedal. 📌 The VOLUME sweep is no longer load-bearing for the LF magnitude/phase
> misses (notes #9d, #18) — those are blocked on the bypassed capture now. It remains load-bearing for
> the taper, the 3.9 vs 1–2 dB fall-back, and M4's C10 corner.

> ### ⭐⭐ THE LF STORY, SETTLED AS FAR AS THIS DATASET ALLOWS (2026-09-09) — notes #19a, #20, #20a
>
> Instruments: `analysis/lf_pole_attribution.py`, `analysis/lf_mechanism_probe.py` (+ their JSON).
> **NO DSP constant changed, and none should be until the capture lands.** Four rounds, each one
> reversing or narrowing the last — the corrections matter more than the conclusion.
>
> **The observation.** All seven captures have less bass than the plugin, uniformly in SIGN: 1.6–3.5 dB
> at 25 Hz, per-rig corner 20–30 Hz, the three modes within a rig agreeing to 5 %. Above 100 Hz P2 and
> P3 are already within 0.12 dB, so the entire effect lives below ~80 Hz.
>
> **⛔ What is ELIMINATED (note #19).** The rig's input impedance loading the output and the drain
> Norton impedance both run to their sweep bounds and still fit 2–3× worse. R10 needs a 3× different
> value per volume setting, so it is not one component. ⛔ **Cable capacitance cannot do it on
> structure alone** — a shunt C at the output is a LOW-pass; removing bass needs a series element.
>
> **⚠⚠ What was OVERCLAIMED and corrected (note #19a).** Note #19 said the three VOLUME settings
> discriminate. They discriminate against the above and NOT against the one that matters. Profiling
> the worst residual against a pinned C10, each rig allowed its own free pole, is **FLAT from 100 nF
> to 70 nF — leaving C10 exactly as drawn costs 0.041 dB.** ➡ The correct statement is **"no
> measurement here assigns any of this to the pedal"**, not "it is the rigs". Three volume settings
> are not enough leverage because R10 = 240 kΩ sits across node E and compresses the swing the knob
> produces. 📌 "Three independent rigs agree" is also weaker than it feels: three of three sharing a
> sign is p ≈ 0.25. It rules out arbitrary scatter, correctly, but not *which* common cause.
>
> **⭐⭐ What MEASUREMENT then settled (note #20), refuting my own hypothesis.** Two properties of the
> pole are readable from the captures we already hold, and `--self-test` passes first (plugin in the
> capture's place → no pole found, spread 1.00×, residual 0.003–0.012 dB):
> - **It is LINEAR.** Across the signal's four sweeps spanning 25 dB, the fitted corner moves
>   **1.01× (P2) to 1.18× (P1)**. ⛔ That refutes note #19's reamp transformer outright — core
>   saturation moves the corner with flux — and rules out any *nonlinear* neural artefact.
>   ⚠ Exclude the −6 dBFS row: the pedal distorts there and the two sides do not distort identically
>   while `Vov` and `kInputRef` are unsettled (P1's fitted order jumps to 1.8–2.1 there; P2's does not
>   move, which is itself the tell).
> - **It is ONE first-order pole.** 2-pole fits worse in all seven. Capture corners **33.8 / 23.5 /
>   34.4 Hz** for P1 / P2 / P3.
> - ⚠⚠ **A SHELF BEATING A LONE POLE IS THE EXPECTED SHAPE, NOT A FINDING.** The comparison is
>   capture-MINUS-PLUGIN and the plugin has its own LF pole, so the difference of two first-order
>   poles is already a shelf. The first cut fitted a free fractional exponent, got 0.30–0.66, and
>   nearly recorded "gentler than any RC can be". **Read the shelf's POLE as the capture's corner and
>   its ZERO as the plugin's.**
> - ⭐ **Free validation of the output network:** that zero must track VOLUME, and it does — 17 / 10 /
>   25 Hz fitted against 24.8 / 13.3 / 28 computed. The consistent downward bias is expected, since
>   the plugin has TWO LF poles and a one-pole summary of the pair must sit below the output one.
>
> ➡ **NET: a real, linear, single first-order pole — an ordinary coupling capacitor, somewhere.**
> With note #18's magnitude/phase agreement on its corner (a minimum-phase signature a neural
> artefact need not have), the balance moves AWAY from both the transformer and the receptive field.
> ⛔ It still does not say WHERE: as a per-rig pole ahead of the pedal it needs 29 / 21 / 20 Hz; as
> the pedal's own corner scaled up, ×1.36 / 1.77 / 1.23. **Same 1.45× spread either way.**
>
> **⛔ Is it a VOLUME effect? (note #20a).** P1 and P3 sit 30 min apart on the knob and measure the
> same corner; P2 sits far away and reads lower — exactly the pattern an in-pedal pole would make.
> ⚠⚠ **But the pair that agrees is the pair with no leverage.** Two points that close cannot
> determine a slope, so ANY common cause puts them together. All the leverage is in P2, which would
> have to have been captured at **10:31 rather than 2:30**. Back-solved taper exponents **2.70 / 7.24
> / 2.39** — independently reproducing note #7's M4 (2.5 / 3.2 / 7.8) from a different estimator on a
> different band, outlier included.
> - ⭐ **Earned anyway: P1 and P3 both want a taper slightly steeper than shipped** (p ≈ 2.4–2.7 vs
>   2.0), two units, two rigs, two trainers. Weak, right sign, carry it into the VOLUME sweep. ⛔ Do
>   not move `p` now — 2.0 is what puts the volume peak at the maker's stated 1–2 o'clock.
> - ⭐ **Free by-product: validation note #3's rotation direction is corroborated.** Reversed wiring
>   fits 17.5 Hz RMS against **9.3 Hz** for the shipped sense. **CW = louder stands.**

> ### ⚠⚠ THE OWNER'S PEDAL IS A TWO-POSITION UNIT: BRIGHT + DARK, NO MID (confirmed 2026-09-09)
>
> Known in outline (`docs/build-plan.md` §7 calls it "the two-way pedal"); now pinned to *which* two.
> This is the best possible pair and it costs less than it looks, but three things follow.
>
> ✅ **Nothing structural is lost.** **DARK is the unbypassed reference every differential needs**, and
> one bypassed branch is enough for all of it: `K0 = 1 + gm·R5` comes from a single branch's plateau
> (note #7 measured it twice and the two branches agreed to 0.37 dB), and every known-answer probe in
> this file — magnitude (#7), harmonic (#10), compression (#11, #14) — needs only that two modes must
> agree below the shelf zero. **All of them still work with two positions.**
>
> ⚠ **Do NOT assume their BRIGHT is C1 = 22 nF.** It is a different circuit variant, so its bypass cap
> is unknown. **Fit its shelf zero from the capture** (note #2's two-parameter shelf, which returns
> ≤ 0.25 dB residuals) and compare against the 1.86 kHz P1/P2 measured. If it comes back at ~4.2 kHz
> their "bright" is actually the 10 nF branch, and the label reasoning that already failed once in
> note #2 will have failed a second way.
>
> ⚠ **MID's τ = 38.263 µs cannot be validated by this capture and must stay inferred.** It rests on
> P1/P2 alone. If their unit's own branch measures a τ offset from P1/P2's (note #2's ~7 % story),
> **scale MID by the measured CAP RATIO rather than transplanting an absolute value** — the ratio is
> the robust quantity (2.24 measured vs 2.20 drawn, and both branches imply the same R5 to 1.6 %),
> the absolute is not.
>
> ⚠⚠ **AND A DECISION TO MAKE DELIBERATELY, NOT BY DRIFT: which unit is the model OF?** Every fitted
> constant today (`gm`, both shelf τ, `Vov`'s bracket) comes from P1/P2. If the owner's unit disagrees
> — and M6 puts genuine unit-to-unit spread at only 0.33 dB RMS on the mode differential, so a large
> disagreement would be a variant difference rather than tolerance — **retuning to their unit means
> the MID position becomes a mix of two different pedals.** Decide it once, in writing, before moving
> any constant. ➡ Default recommendation: **model the owner's unit for everything their capture can
> measure, and keep MID as P1/P2's branch scaled by their measured ratio**, with the mixing recorded.
>
> 📌 `CLAUDE.md`'s capture-session ask list is otherwise unchanged and still correct. Ask #5's "both
> switch positions" now means *the* two positions, and the missing M0 no-plugin null render, the
> bypassed pass, the VOLUME sweep and both `input_level_dbu` / `output_level_dbu` figures are all
> still outstanding and all still load-bearing.

> ### ⭐⭐ CAPTURE SESSION IN PROGRESS (2026-09-10). The RIG IS MEASURED AND CLEAN. Raw WAVs, not NAM.
>
> Owner is capturing their own unit (P4, the two-position BRIGHT/DARK variant) as this is written.
> Checklist and full reasoning: `docs/capture-session-checklist.md`. Instrument:
> `analysis/check_capture.py <file> [--loop]` — run it on each capture; it checks truncation,
> alignment, polarity, clipping, banded FR, noise, per-order distortion and calibration, and it
> **measures its own distortion floor first** (note #9's rule) because raw THD on the quietest sweep
> read 1.72 % purely from a fixed-amplitude artefact being divided by a small fundamental.
>
> ⭐⭐⭐ **THE HEADLINE: THESE ARE RAW CAPTURES OF THE TEST SIGNAL, NOT NAM MODELS.** Every floor this
> project has fought — the 4–9 dB harmonic floor, the 0.145 dB compression floor, the 1.9–20.2 dB
> per-band LF THD floor — is NAM model error, and note #15 established it is **systematic**, so it
> never averaged down. It is simply gone. `Vov` was pinned no tighter than a factor of 1.3–1.5
> (note #16) **because of that floor and nothing else.**
>
> ### ⭐⭐ THE TWO REFERENCE CAPTURES ARE IN AND BOTH PASS — and they settle notes #19/#19a/#20
>
> `loop_V0000_none.wav` (no pedal in circuit) and `p4_V1030_bypass.wav` (pedal in, bypassed).
>
> ⭐⭐ **THE RIG HAS NO LOW-FREQUENCY POLE.** 20–40 Hz reads −0.006..+0.065 dB (loop) and
> −0.053..+0.018 dB (bypass), and bypass-minus-loop is −0.047 dB at 20 Hz. ➡ **The real, linear,
> single ~20–35 Hz pole present in ALL SEVEN NAM captures is NOT in this signal path.** So when the
> P4 pedal captures are analysed: **an LF pole there belongs to the PEDAL** — change `C10` (or
> whatever carries it) with evidence at last — **or there is none, and it was the other three
> trainers' rigs.** Either way note #19a's "unidentifiable" is resolved by this dataset.
> 📌 The 7:30 captures are the leverage for exactly this — see below.
>
> ✅ **The bypass is genuine TRUE BYPASS: nulls at −70.8 dB against the loop, 0.008 dB of insertion
> loss.** No buffer, no bypass-path component to model. ⇒ checklist §2c's input-loading correction
> applies ONLY to the ACTIVE captures, where the source becomes the pedal's ~130 kΩ.
>
> ⚠⚠ **USE THE BYPASS CAPTURE AS THE DECONVOLUTION REFERENCE, NOT THE BARE LOOP.** They are not
> interchangeable: bypass-minus-loop is +0.37 dB at 18 kHz, best described as **the loop carrying one
> extra pole at 48.4 kHz** (0.079 dB residual). A bypassed pedal cannot ADD treble, so the loop path
> had a cable the bypass path did not. **The bypass path shares its cabling with every pedal
> capture; the loop does not.** Deconvolving against the loop imports 0.37 dB of HF error.
> The loop's only remaining job is the M0 unity check, which it passes.
>
> ⚠ **The chain has real HF droop that MUST be deconvolved** — about −0.53..−0.75 dB over 8–20 kHz on
> the bypass reference. That is the same size as the project's entire 1 dB target, so it is not
> optional. Ordinary converter filtering; harmless once removed.
>
> ⭐⭐ **CALIBRATION — `kOutputMakeup` CAN FINALLY BE ANCHORED.** `output_level_dbu` = **+14.30 dBu**
> (loop) and **+14.29 dBu** (bypass), 0.01 dB apart, so the play side did not drift between takes.
> Record full scale ≈ **4.02 V RMS**. `input_level_dbu` = **+12.20 dBu** (`kInputRef` = 4.4626 V/FS).
> ✅ **CONFIRMED by the owner 2026-09-10:** the play side IS set to **1.7745 V RMS at −5 dBFS /
> 220 Hz**, so `input_level_dbu` = +12.20 dBu and `kInputRef` = 4.4626 V/FS stand. (220 Hz rather
> than 1 kHz because the meter, a Jaycar QM1529, is specified only to 400 Hz; accuracy at that
> reading is ±0.135 dB, well inside anything that matters here.)
> ✅ **CONFIRMED: the interface input is 1 MΩ**, so checklist §2c's loading correction is now a known
> quantity rather than an open question — table below.
> 📌 Noise floor −99.5 / −99.8 dBFS RMS. H2 clearance over the pedal's expected output is 16–71 dB on
> every cell but the quietest sweep, which is the linear FR reference and reads no harmonics.
>
> ### 📌 `analysis/captures.py` CHANGED — behaviour a future session must not be surprised by
> - Mode token now accepts **`none`** (nothing in circuit) and **`bypass`** (pedal in, footswitch
>   bypassed) beside bright/dark/mid. Naming the loop "dark" was actively misleading.
> - ⚠⚠ **`find_captures()` now EXCLUDES reference captures by default.** Every comparison script
>   renders the plugin per capture and diffs it, which is meaningless for a file with no pedal in it
>   and would fail SILENTLY as a mysterious outlier. Pass `include_reference=True`, or use
>   `find_reference_captures()`.
> - `render_args()` **raises** on mode `none` rather than inventing a setting, and emits `--bypass`
>   for mode `bypass` (so that pair should null).
>
> ### ⭐ The 7:30 bonus capture: read its CORNER, never its LEVEL
> `p4_V0730_{bright,dark}` (x = 0.05, Ra = 1.25 kΩ at p = 2.0) puts the C10 corner at **67.6 Hz**,
> against 12.9–39.8 Hz for the rest of the matrix — **span 3.09× → 5.25×**, and easy to fit where the
> sweep has energy. That corner is robust to knob error (±10 min → 65.1–69.6 Hz, ±3 %).
> ⚠⚠ **Its LEVEL is NOT taper leverage, despite looking like the best in the set.** The 27 dB spread
> across plausible exponents is matched by ±5.7 dB of knob-position sensitivity, giving a worse ratio
> (≈4.7) than 10:30's (≈15). Sensitivity to the exponent goes as `ln(x)·dp`, to position as `p·dx/x`,
> and `dx/x` blows up faster than `ln(x)` grows. ➡ **Fit the taper from the MIDDLE of the sweep.**
> ⛔ A level mismatch at 7:30 is not a taper error.
>
> ### ➡ NEXT SESSION — the order to work in, once the captures land
> 1. `check_capture.py` on every file. ✅ Both calibration questions are already ANSWERED (above) —
>    nothing to confirm with the owner.
>
> ⭐ **THE INPUT-LOADING CORRECTION, at the confirmed Zin = 1 MΩ. ADD these dB to each capture to
> undo the interface.** ⚠ It is NOT a single scalar: it moves with the knob (0.341 dB across the
> sweep at 1 kHz) *and* with frequency (up to 0.120 dB within one position), because the pedal's own
> output impedance does both. Generate it per capture from the network model rather than typing these
> in — `analysis/lf_pole_attribution.py`'s `out_network(f, x, rl=1e6)` against `rl=inf` is the source.
>
> | knob | 20 Hz | 50 Hz | 100 Hz | 1 kHz | 10 kHz | 20 kHz |
> |---|---|---|---|---|---|---|
> | 7:30 | +0.781 | +0.781 | +0.781 | +0.780 | +0.780 | +0.780 |
> | 9:00 | +0.862 | +0.843 | +0.832 | +0.826 | +0.825 | +0.825 |
> | 10:30 | +0.940 | +0.873 | +0.851 | +0.842 | +0.842 | +0.842 |
> | 12:00 | +0.956 | +0.864 | +0.842 | +0.834 | +0.834 | +0.834 |
> | 13:30 | +0.924 | +0.831 | +0.811 | +0.804 | +0.804 | +0.804 |
> | 15:00 | +0.846 | +0.764 | +0.748 | +0.742 | +0.742 | +0.742 |
> | 17:00 | +0.552 | +0.512 | +0.504 | +0.501 | +0.501 | +0.501 |
>
> ⚠⚠ **The 0.341 dB of knob-dependence is the part that matters** — a constant would be absorbed
> harmlessly by `kOutputMakeup`, but a knob-dependent one lands directly in the VOLUME taper fit,
> which is step 5. ⛔ Do not fit the taper before applying this.
> 2. **Deconvolve `p4_V1030_bypass.wav` out of every pedal capture**, then apply the §2c loading
>    correction. Only then compare anything.
> 3. **The LF pole**, using the 7:30 corner — the question this session was built to answer.
> 4. **`kOutputMakeup`**, from `output_level_dbu` = +14.29 dBu. It has been exactly 1.0 and
>    unanchored since the project began and this is its only possible route.
> 5. **The VOLUME taper** (`p` = 2.0 shipped; note #20a's P1 and P3 both wanted 2.4–2.7), the
>    **3.92 dB vs the maker's 1–2 dB fall-back**, and the **volume peak position** (predicted at
>    Ra = 176 k = 35 % of the pot, i.e. 1–2 o'clock).
> 6. **`Vov`** — fitted to 0.126–0.165 (note #16) but never applied, because the NAM harmonic floor
>    pinned it only to ~1.4× and because it swaps the stage's clipping MECHANISM (cutoff vs triode).
>    ⭐ With raw captures reaching the load line at last, **measure which one clips first** instead of
>    inferring it. That was note #16's stated condition for applying the fit.
> 7. **MID stays inferred** — P4 has no MID position. Scale it by the measured CAP RATIO (2.24), not
>    by an absolute τ, and decide deliberately (CLAUDE.md's two-position block) which unit the model
>    is OF before moving any constant.

> ### ⚠⚠ THE MATRIX IS CAPTURED WITH THE SOURCE PADDED **12 dB** — the pedal overruns the interface
> ⚠ **REVISED from 9 dB later the same day: 9 was not enough (13:30 still clipped, and a 10:30 bright
> retake landed at −0.01 dBFS). The model UNDER-PREDICTS the real pedal's output by ~4 dB, because
> `kOutputMakeup` is exactly 1.0 and unanchored — the plugin's absolute level has never been
> calibrated. Set the pad from the METER, not from any predicted table. `input_level_dbu` = +0.20 dBu
> for the padded matrix.** Everything below still describes why the pad exists; only the figure moved.
>
> Discovered mid-session 2026-09-10, at 10:30. **The pedal's own boost puts it over the converter at
> every VOLUME position from 9:00 up**, worst around the volume peak. Predicted and then confirmed:
>
> | | dark | bright |
> |---|---|---|
> | predicted recorded peak at 10:30 | +3.60 dBFS | +6.08 dBFS |
> | worst over the whole matrix (≈13:30) | +4.66 | **+7.14** |
>
> The owner independently estimated "at least 3 dB over", which is the dark figure — ⭐ **the model
> predicted the owner's rig before the capture existed.** BRIGHT is the harder case because the mode
> shelf's HF lift reaches ~16 dB.
>
> ⛔ **It is NOT a gain-knob problem.** BRIGHT needs ~11.4 V peak and the SSL's instrument input stage
> tops out at 6.16 V (+15 dBu), so **no gain setting can accept it.** The converter limit (0 dBFS =
> 5.68 V peak at `output_level_dbu` = +14.29) and the analog limit are only 0.7 dB apart.
>
> ⭐ **The fix is a DIGITAL pad on the playback, not the output knob** — the analog calibration stays
> at 1.7745 V / −5 dBFS / 220 Hz and the offset is exact and repeatable. Turning the knob would force
> a re-measurement and introduce an unknown.
>
> ### 📌 `_pad<N>` FILENAME SUFFIX — the drive is now a dimension of the dataset
> `p4_V1030_bright_pad9.wav` = 9 dB of digital attenuation on the played signal. **No suffix means
> pad 0**, so every pre-existing capture keeps its meaning. `_pad7p5` for 7.5 dB (`p` = decimal point).
> ⭐ `render_args()` feeds the figure to OfflineRender's **`--input-scale`** (which scales the SIGNAL
> ahead of the processor and is unbounded — `--input-trim` is a [−12,+12] plugin control and would be
> the wrong knob), so **plugin-vs-capture comparisons are MATCHED-DRIVE automatically**. That is what
> circuit.md note #8 makes binding for anything harmonic, and it no longer has to be remembered.
> ⇒ `input_level_dbu` is **+3.20 dBu** for the padded matrix and **+12.20 dBu** for the pad-0 pair.
>
> ### ⭐⭐ THE LOAD LINE LIVES IN THREE PAD-0 CAPTURES, AND ONLY THERE
> ⚠ At pad 12 the matrix has **NO load-line coverage** (a pad eats the 6.5 dB 1:1, leaving −5.5), so
> the stage never leaves its linear region there. The nonlinear dataset is exactly:
> **`p4_V0730_bright` (−10.83 dBFS), `p4_V0730_dark` (−16.37), `p4_V0900_dark` (−2.46)** — all pad 0,
> all clean. ⛔ **Do not overwrite them.** ⭐⭐ `p4_V0900_dark` matters more than it looks: the drain
> load runs 9.8–17.9 kΩ across the rotation and moves triode onset ~4 dB, so 9:00 gives a SECOND
> drain-load point and turns that caveat into a measurement. 📌 One optional capture completes it —
> `p4_V0900_bright_pad4.wav`, which still leaves +2.5 dB of coverage.
> 📌 Folder layout: everything lives in `captures/`, distinguished by the `_pad` suffix; only
> `clipped/` is separate — any OTHER subfolder hides its contents from `find_captures()` silently.
> ⚠ `*.wav` is gitignored: captures are NOT recoverable from version control.
>
> ### (superseded heading kept for the reasoning below)
> `p4_V0730_{bright,dark}.wav` are at FULL drive and clean (−10.83 / −16.37 dBFS peak). **The VOLUME
> control sits AFTER the JFET stage**, so the drive into the transistor is identical at every knob
> position — 7:30 exercises the load line completely while recording ~16 dB quieter. ⛔ **Do not
> overwrite them**; the padded matrix has NO load-line coverage (triode onset moves to −1.5 dBFS
> against a −1 dBFS hottest cell).
> ⚠ **Caveat to carry, not to solve:** the drain load varies 9.8–17.9 kΩ with VOLUME and moves the
> triode onset by ~4 dB, so 7:30 samples the load line at ONE end of that range. That is also a
> testable prediction for later.
>
> ### ✅ THE REFERENCE CAPTURES DO NOT NEED REDOING — measured, not assumed
> A digital pad only changes level, so the question is whether the chain is level-dependent. The four
> sweeps span 35 dB and answer it directly. Bypass reference, shape re midband:
>
> | level change | shape difference |
> |---|---|
> | 10 dB down | 0.018 dB RMS |
> | 20 dB down | 0.075 dB RMS |
> | 35 dB down | 0.246 dB RMS |
>
> The error tracks a **fixed measurement floor** (it grows as the level falls) rather than a real
> level dependence. At 9 dB the contribution is **~0.02 dB**. ⚠ When deconvolving, use the
> reference's −6 or −16 dBFS sweep, never its quietest.
>
> ### ⚠⚠ A BUG IN `check_capture.py` THAT WOULD HAVE CONDEMNED EVERY REAL CAPTURE
> It flagged any inverted file as BAD. **Stage 2 is a single common-source JFET stage and MUST
> invert** (circuit.md stage 2), so a genuine pedal capture reads **−1** and only a loop or bypassed
> capture reads **+1**. The expectation is now taken from the mode token. Verified across all files:
> every P4 pedal capture −1, both references +1, and P2 +1 — which is note #9b's known rig fault,
> recovered here from a third route.
>
> ### 📌 `analysis/captures/clipped/` — quarantine, with a README
> `p4_V0900_bright` (11 k clipped samples) and `p4_V1030_bright` (291 k) from before the pad, plus
> `aborted_take_Audio-Bus256.wav`, a 136 s partial that landed under the DAW's default BUS name.
> ⛔ Clipped captures are **not partially usable**: flat-topped peaks look exactly like the pedal
> saturating, and saturation is what the load-line work measures — the artefact mimics the signal.
> ⚠ **An unparseable filename in `captures/` makes `find_captures()` RAISE and takes every analysis
> script down with it.** The DAW names exports after the bus, not the take; rename on export.

> ### ⭐⭐ P4 IS A MEASURABLY DIFFERENT UNIT (2026-09-10) — and the OWNER'S DECISION is recorded here
>
> Instruments and all numbers: `.claude/rules/circuit.md` note #21, plus `analysis/unit_compare.py`
> and `analysis/reports/unit_compare.json`. **No DSP constant has moved yet.**
>
> **⭐ P4's BRIGHT IS the 22 nF branch — fitted, not assumed.** Shelf zero **1919–1950 Hz**, implied
> cap 22.7–23.0 nF, against P1/P2's 1792–1884 Hz. The two-position block said this had to be fitted
> because P4 is a different circuit variant with an unknown bypass cap; it is the same branch.
> 📌 P4's zero sits ~4 % above drawn where P1/P2 sit ~7–9 % above, so note #2's "consistently 7 %
> low" is P1/P2's parts bin rather than a systematic R5 error — weak evidence for cap tolerance over
> R5, from one extra unit.
>
> **⭐⭐ P4's JFET IS ~25 % WEAKER, and this is the one parameter where the units genuinely differ:**
>
> | unit | K0 | implied gm | shelf zero | fit residual |
> |---|---|---|---|---|
> | **P4 (owner), 9:00** | **5.19** | **1165 µS** | 1950 Hz | 0.027 dB |
> | **P4 (owner), 10:30** | **5.06** | **1127 µS** | 1919 Hz | 0.042 dB |
> | P1 | 6.31 | 1475 µS | 1884 Hz | 0.216 dB |
> | P2 | 6.91 | 1640 µS | 1792 Hz | 0.057 dB |
> | **shipped model** | 6.59 | 1553 µS | 1864 Hz | — |
>
> Agreement on the whole differential CURVE, level removed (M6's band is 0.33 dB RMS / 0.80 peak):
> **P4 9:00 vs P4 10:30 = 0.049 dB RMS / 0.178 peak** (the same pedal twice — a repeatability floor
> this project has never had), P1 vs P2 = 0.232 / 0.812, **P4 vs P1/P2 = 0.405–0.631 / 1.46–2.05**.
> So P4 sits ~2× outside the two-NAM-unit band and ~10× outside its own repeatability. It is a real
> unit difference: **the model is currently ~1.9 dB too bright at 10 kHz against the owner's pedal**
> in BRIGHT, and unaffected in DARK.
> ⚠ Attribution caveat: K0 is the product `gm·R5`, and P4's shelf zero implies its `R5·C` is ~5 %
> smaller. If that whole 5 % is R5 rather than cap tolerance, P4's `gm` is 18 % below P1's rather
> than 22 %. This dataset cannot split R5 from C (note #2's degeneracy), but the sign and rough size
> hold either way.
>
> ### ⛔⭐⭐ THE DECISION, MADE BY THE OWNER 2026-09-10 — write it here, do not let it drift
> CLAUDE.md's two-position block says to decide **once, in writing, before moving any constant**
> which unit the model is OF. Decided:
>
> **Voice to the NEWER three-position units (P1/P2). Use P4 as the comprehensive MEASUREMENT
> baseline and extrapolate from it.** The reasoning is that P4 has a measured, near-flat rig, both
> calibration figures written down, a complete knob rotation in one session, and no NAM model error
> — so it is by far the best instrument — while the intended product is the newer variant.
>
> ➡ **What that means parameter by parameter, because the two roles are separable and it matters
> which is which:**
> - ⭐ **`gm` and the shelf τ come from P1/P2** (the voicing). They are pinned by the mode
>   differential, which is rig-free, so P4's superior rig buys nothing here. Keep gm ≈ 1553 µS.
> - ⭐ **Everything structural comes from P4**: the taper (p ≈ 2.3), C10 (as drawn), `kOutputMakeup`,
>   the load line, the LF corner, the output-loading correction. These are shared between variants
>   and P4 is the only unit that can measure them at all.
> - ⚠⚠ **`kOutputMakeup` is the one that couples the two roles.** It and `gm` are both level scalars
>   in the DARK path. The measured **+1.332 dB** is against the model AS CURRENTLY VOICED (gm =
>   1553). P4's own weaker JFET makes it ~0.46 dB quieter than a P1/P2-gm unit, so a model voiced to
>   P4 would need ~+1.79 dB instead. **Fit `gm` first from the differential, then the makeup.**
> - ⚠ **MID stays inferred and is now doubly so** — P4 has no MID position, and the model's MID will
>   carry P1/P2's τ alongside P1/P2's gm, which is at least self-consistent under this decision.
> - 📌 Recording the mixing explicitly: under this decision **no position is a blend of two pedals**,
>   because both the gain parameters and both shelf τ come from P1/P2 together. That is a cleaner
>   outcome than the two-position block feared.
>
> ### 📌 THE DRIVE EACH CAPTURE ACTUALLY DELIVERS — pad 12 does NOT reach playing level
> Raised by the owner and confirmed. Gate volts by pad and sweep (`kInputRef` 4.4626, 0.90 divider;
> triode onset 1.691 V; real playing −12..−6 dBFS = 1.01..2.01 V at the gate):
>
> | pad | sweep_clean | sweep_−26 | sweep_−16 | sweep_−6 | the −6 cell equals a user playing at |
> |---|---|---|---|---|---|
> | 0 | 0.036 V | 0.201 V | 0.637 V | **2.013 V** (past triode) | **−6.0 dBFS** |
> | 4.5 | 0.021 V | 0.120 V | 0.379 V | 1.199 V | −10.5 dBFS |
> | 12 | 0.009 V | 0.051 V | 0.160 V | 0.506 V | **−18.0 dBFS** |
>
> ⚠⚠ **So the pad-12 matrix tops out 6–12 dB BELOW real playing and cannot fit anything nonlinear.**
> The nonlinear dataset is exactly the pad-0 takes at their `sweep_-6` / `sweep_-16` cells
> (`p4_V0730_bright`, `p4_V0730_dark`, `p4_V0900_dark`) plus **`p4_V0900_bright_pad4p5`**, whose
> −6 cell lands at −10.5 dBFS equivalent — the mid-drive point the matrix otherwise lacks.
> ✅ **The LINEAR fits are unaffected, and that was checked rather than assumed**: the 9:00 DARK LF
> corner moves 1.9 % across a 47 dB drive span ending past triode (note #21).
>
> ### ⚠⚠ THREE HARNESS DEFECTS FIXED, one of which reported a stale render as a measurement
> - **The plugin-render cache was keyed on a display TAG, not on the render arguments**, so once a
>   capture's settings changed it silently served the previous render. `volume_sweep.py --self-test`,
>   where both sides ARE the same render and the answer must be exactly 0.000, reported **−1.382 dB**
>   — which reads as a real measurement. It is keyed on a hash of the argument list now.
> - **`captures.py::find_captures()` raised on any unparseable filename** and took every analysis
>   script down with it; a DAW take suffix (`p4_V1330_dark_pad12_1.wav`) was enough. It now SKIPS
>   such files and prints a warning naming them to stderr on every run — loud, because a capture the
>   harness cannot see is one that silently does not exist.
> - ⚠ **`check_capture.py`'s "H2 clearance" column is meaningless on an ACTIVE capture** and reads
>   BAD when the pedal agrees with the model. It subtracts the measured H2 from the model's expected
>   H2, which is a headroom check for a REFERENCE capture, where the measured H2 is chain noise. On a
>   pedal capture the measured H2 *is* the pedal's. Ignore that column on active captures; the H2 dBc
>   figures themselves are fine. (Not yet fixed.)

> ### ⭐⭐ SESSION 2026-09-10 (part 2): THE PROBE CAPTURES. `Vov` REVERSED, Zout MEASURED.
>
> Full numbers in `.claude/rules/circuit.md` notes **#21, #21a, #22**. **No DSP constant changed.**
> New: `analysis/gen_probe_signal.py` + `probe_signal_48k.wav` (56 s, self-contained),
> `analysis/probe_analyse.py`, `analysis/probe_compare.py`, `analysis/output_impedance.py`,
> `analysis/p4_corners.py`, `analysis/unit_compare.py`, `analysis/p4_component_fit.py`,
> `analysis/volume_sweep.py`. **Every one has a passing `--self-test` against plugin renders.**
>
> **The capture set is COMPLETE and the rig is torn down.** 28 main-signal captures (both modes,
> 7:30 → 17:00, plus loop and bypass references) and 15 probe captures (P4 at six settings including
> two into a 10 kΩ line input, plus NAM probe renders of P1/P2/P3).
>
> ### What is now MEASURED that never was
> | quantity | was | now |
> |---|---|---|
> | VOLUME taper `p` | 2.0 assumed | **2.28–2.33**, two independent bands |
> | peak-to-full-CW fall-back | 3.9 model vs "1–2 dB" maker | **3.86 measured vs 3.91 model** |
> | `kOutputMakeup` | 1.0, unanchored since day one | **1.1658 (+1.332 dB)**, sd 0.160 |
> | output impedance | computed, never measured | **99.8 k / 60.8 kΩ**, model off by 2.0 / 2.3 % |
> | P4's `gm` (K0) | — | **K0 = 5.06–5.19**, vs P1 6.31, P2 6.91, model 6.59 |
> | drift + knob repeatability | unknown | **≤ 0.061 dB** at a normal knob position |
> | `Vov` | NAM fit said 0.126–0.165 | **REFUTED — it is ~0.94 at P4's own `gm`** |
>
> ⭐ **On tolerance (the owner's point, and it is the right frame): the unit's resistors are carbon,
> so 2–5 % between one unit and nominal is expected.** That reframes several results as agreement
> rather than discrepancy — the 2.0/2.3 % Zout errors are essentially exact, and the ~5–9 % offsets
> in the measured shelf zeros (R5·C) across units sit at the edge of ordinary tolerance. ⛔ It does
> NOT explain P4's 25 % lower K0: that is `gm`, a JFET parameter with a 5:1 datasheet spread, not a
> resistor.
>
> ### ⛔⭐⭐ THE ONE CONSTANT THE PROJECT WAS ABOUT TO GET WRONG
> Note #16 fitted `Vov` twice from the NAM set (0.126 and 0.165) and deferred applying it pending a
> capture that reaches the load line. That capture exists now and **refutes the fit: applying it
> would have made H2 13–20 dB wrong.** The shipped 0.4469 is only 3.4 dB out and has the LOWEST
> scatter of any value tried. ⭐ The cause was already on the record and was reasoned past: note #7's
> M5 says P1's H2 does not move with level at all, i.e. it is floor — and route A fitted P1's H2
> anyway because the deficit cleared its per-band floor in magnitude. **A fit whose input is floor
> returns a confident number with a good residual.**
>
> ### ➡ NEXT SESSION — the model is now the bottleneck, not the data
> Everything below is unblocked. Work in this order.
>
> 1. ⭐⭐ **Decide `gm` under the recorded decision** (voice to the newer P1/P2 units, measure with
>    P4). The differential is rig-free, so P1 = 1475 µS and P2 = 1640 µS are both usable; shipped is
>    1553 µS, the mean of the two. **Probably no change — but write down that it was checked.**
> 2. ⭐⭐ **Apply `kOutputMakeup` = 1.1658.** Its only coupling is to `gm` (both are level scalars in
>    the DARK path), so do it after step 1 and re-derive if `gm` moves. This is the first anchor the
>    constant has ever had and there will not be another.
> 3. ⭐⭐ **Apply the taper `p` = 2.3.** Two independent bands agree; the shipped 2.0 costs 2.08 dB
>    worst / 0.96 dB RMS on the control law. ⚠ Re-check that the volume peak still lands in the
>    maker's 1–2 o'clock: at p = 2.33 the fitted peak is 13:28, so it does.
> 4. ⭐⭐ **Add a `--gm` flag to `OfflineRender`, then model the clipping ONSET.** See circuit.md
>    note **#22a**: the `Vov` sweep goes NON-MONOTONE above ~0.7 (sd 1.45 → 4.56 → 12.71) because it
>    holds P1/P2's `gm` while sweeping, and the bias point collapses — at `Vov` = 1.2 the quiescent
>    Vds is NEGATIVE. `gm` and `Vov` are one square-law family and must be swept together. At P4's
>    measured gm = 1146 µS the same `Vov` = 0.93 is healthy (IDSS exactly 5 mA, Vds = 8.4 V).
>    ⚠ Separately, the H2 delta grows with drive (−2.5 dB at the −12 cell, −6.2 at −4), so no single
>    curvature scalar closes it — the clipping ONSET is misplaced too. `probe_compare.py` is the
>    instrument for both.
> 5. **Then re-run the full evaluation** (`goal_check.py`, `phase_sweep.py`) against P4 and re-read
>    the 1 dB / 5° targets. Both were previously blocked on the LF pole, which note #21 dissolves:
>    C10 is as drawn and the LF error was the taper.
>
> ⚠ **Standing traps for whoever picks this up:**
> - **`probe_compare.py` must stay `--unit p4`.** The probe directory also holds NAM renders whose
>   rigs ran at unknown levels, so their drive is not matched; pooling them took the shipped-`Vov`
>   mean from −3.4 dB to −10.8 with an sd of 10.4 and looked like a real result.
> - **7:30 is excluded from everything except load-line depth.** Its knob-setting error is 6.4 dB
>   (dark) and 10.4 dB (bright), measured — not its level only, its CORNER too.
> - **BRIGHT above 9:00 has no load-line data and never can**: the pedal's output clips the
>   converter before the JFET reaches triode. That is the rig's ceiling, not a missing capture.
> - **MID stays inferred** — P4 has no MID position. Scale by the measured cap RATIO (2.24), never
>   by an absolute τ.

> ### ⭐⭐ SESSION 2026-09-11: THREE CONSTANTS APPLIED. Full detail in circuit.md note **#23**.
>
> The handover's steps 1–4 are done. **All 11 tests pass, warning-free.**
>
> | constant | was | now | source |
> |---|---|---|---|
> | `gm` | 1.5531069 mS | **unchanged** | re-checked against a 2nd estimator; means agree to 0.021 dB |
> | `kVolumeTaperP` | 2.0 (marketing copy) | **2.30** | P4's VOLUME sweep, two independent bands |
> | `kOutputMakeup` | 1.0 (UNANCHORED) | **1.1562 (+1.261 dB)** | P4, DARK only, sd 0.175 dB |
> | `kInputRef` | 4.4626, comment said "ASSUMPTION" | 4.4626, **comment corrected to MEASURED** | owner-confirmed +12.20 dBu |
>
> ⭐ **`kOutputMakeup` is the headline: it has been exactly 1.0 and unanchored since the project
> began and there was never another possible route to it.** ✅ Verified by re-measurement — with the
> constant applied, the same instrument reads **+0.000 dB**, scatter unchanged.
>
> ⭐⭐ **The taper confirmed itself through a quantity it does not appear in.** `kOutputMakeup` must
> be a CONSTANT across the knob. At p = 2.0 its per-position scatter was **2.73 dB**; at p = 2.30 it
> is **0.42 dB**. A 6.5× collapse in the scatter of something that must not vary is stronger evidence
> than either taper fit's own residual.
>
> ### ⚠⚠ FOUR HARNESS FAULTS, AND TWO WOULD HAVE CORRUPTED THESE VERY NUMBERS
> 1. ⚠⚠ **The render cache did not include the plugin BINARY.** Keys were a hash of the render
>    ARGUMENTS — itself a fix for a key that had been a display tag. Same fault one level up: change
>    a constant, rebuild, re-run, and the args are identical, so every script compares new captures
>    against the OLD plugin. ⭐ **Caught by a column that did not move** — after the taper changed,
>    the plugin levels came back byte-identical. Fixed via `captures.render_bin_key()`.
>    **Any future measurement flag must go into that key too.**
> 2. ⚠⚠ **The fits the handover quoted were STALE**: three captures (`p4_V0800_dark`,
>    `p4_V0800_bright`, `p4_V1030_dark_pad4p5`) landed after the reports. `*.wav` is gitignored, so
>    git cannot show this. ➡ **Compare capture mtimes to report mtimes before trusting a prior fit.**
> 3. ⚠⚠ **8:00 is x = 0.100 EXACTLY and two scripts disagreed on whether that is excluded.**
>    Including it moved the LF taper 2.281 → 2.201 and the joint C10 fit to 114.3 nF (14 % over
>    nominal), reopening note #21's closed C10 question with a wrong answer. ⭐ It is excluded on its
>    **residual** (0.185 dB RMS against 0.020–0.087 elsewhere; dropping it halves the pooled RMS),
>    not on leverage. Physical cause: the control law moves **±2.98 dB per ±10 min** of knob error at
>    8:00, against ±1.02 at 9:00. Threshold is `x <= 0.1` in all three scripts now.
> 4. ⚠ **`TAPER_P` was a second definition** of a shipped constant in `lf_pole_attribution.py`, and
>    both taper fits are reparameterisations off it. It parses `CircuitValues.h` now.
>
> ### ⭐ `OfflineRender --gm S` IS IN, WITH A BIAS-POINT GUARD
> It sits beside `--vov`, and setting it rebuilds the shelf (so a `gm` sweep moves the model's mode
> differential to match the unit under comparison — required to fit `Vov` against P4). ⭐⭐ It now
> prints the implied operating point and **refuses** a pair whose quiescent Vds is negative, so
> note #22a's trap cannot recur silently: shipped `gm` with `Vov` = 1.2 exits 1 and writes no file;
> `Vov` = 0.93 warns twice; **P4's gm = 1146 µS with `Vov` = 0.93 gives IDSS 4.999 mA and
> Vds_q 8.36 V**, reproducing note #22a's prediction from the flag itself.
>
> ### ⭐⭐ AND HANDOVER STEPS 4–5 ARE DONE TOO — circuit.md notes **#24** and **#25**
>
> **`Vov` STAYS at 0.4469, and it is now MEASURED rather than defaulted.** Swept as a PAIR with `gm`
> pinned at P4's measured 1146 µS, the sweep is **monotone across the whole range** — confirming
> note #22a's diagnosis that the old tail was a collapsing bias point. Three criteria give three
> answers spanning 2× (mean H2 zero at 0.78, scatter minimum 0.60, drive-slope zero ~0.40); converted
> back through `Vov·K0²` the mean-zero answer is **0.47, within 4 % of the shipped value**.
> ⛔ **The drive dependence CHANGES SIGN** between the shipped pair and P4's, so the clipping onset is
> a fittable two-parameter problem — but no single `Vov` closes both the mean and the slope.
> ➡ **The model's H2-vs-drive CURVE has the wrong SHAPE, not the wrong scale.** That is the next
> modelling job, and `Vov` must not absorb it.
>
> **`goal_check.py` NOW ANCHORS TO P4** (`--unit`, default p4) with the rig deconvolved, the
> interface load undone in magnitude AND phase, matched drive, and a new **absolute-level** section
> that only became possible once `kOutputMakeup` was anchored.
>
> | target, 8× OS, against the owner's own pedal | result |
> |---|---|
> | FR ±0.5 dB, 80 Hz–12 kHz | ✅ **DARK passes 9:00–17:00 except 13:30**, at 0.04–0.22 dB core worst |
> | phase ±5° | ✅ **worst 3.90° over 200 Hz–12 kHz**; every DARK row passes 20 Hz–20 kHz |
> | absolute level ±0.5 dB | ✅ **all 15 captures pass**; dark mean −0.039 dB |
>
> ⚠⚠ **THE ~99 pF CAPTURE-SIDE CABLE LOAD IS NOT OPTIONAL IN EITHER INSTRUMENT, and omitting it
> reads as a knob-dependent MODEL defect.** The bypass deconvolution structurally cannot remove it
> (note #21). Without it `goal_check` reported HF misses growing with VOLUME to +2.69 dB, and
> `phase_sweep` reported P4 at **17–23° RMS against P1's 2.7–4.0°**. With it, `phase_sweep`'s P4 DARK
> rows read **0.13–1.88° RMS**, ~10× better than P1. ⛔ CAPTURE-side only — the plugin still ships no
> load capacitance, because its output goes to a DAW digitally.
> ⭐ Both corrections live in `p4_corners` as ONE definition each. Two free checks confirm the
> refactor: `goal_check`'s output is byte-identical through the shared function, and the **mode
> differential did not move** (it is a within-unit ratio, so the rig must cancel in it).
>
> ### ➡ NEXT
> 1. **Model the clipping ONSET.** This is the one substantive modelling gap left. The tooling is in
>    place and note #24 maps the residual: at P4's gm the drive slope runs +0.14 dB (`Vov` 0.45) →
>    +4.99 (0.77) while the shipped pair reads −1.06, so the sign change brackets the answer.
> 2. **BRIGHT's 6.5–10 kHz miss is the recorded voicing decision** (P4's K0 5.06–5.19 vs the shipped
>    6.59), not a defect. ⛔ Do not move `gm` to close it.
> 3. **MID stays inferred** (P4 has no MID position) — scale by the measured cap RATIO 2.24.

> ### ⭐⭐ SESSION 2026-09-10 (part 3): THE TRANSFER LAW IS NOT SQUARE. m = 1.60, and |Vp| replaces Vov.
>
> Full detail in `.claude/rules/circuit.md` note **#26** (with #26a–#26d); reasoning in
> `src/dsp/JfetStage.h`. New instrument `analysis/onset_fit.py`. **All 11 tests pass, warning-free.**
> This closes note #24's one remaining modelling gap — "the H2-versus-drive CURVE has the wrong
> SHAPE, not the wrong scale" — and the answer was the device law itself, not any of its constants.
>
> | constant | was | now |
> |---|---|---|
> | transfer-law exponent | 2.0 (Shichman-Hodges, implicit) | **1.60, MEASURED** |
> | amplitude parameter | `vov` = 0.4469 V | **`vp` = 1.942 V** (Vov is derived: 0.4321) |
> | `ro` | 1.4407 MΩ | 1.1921 MΩ (re-derived at the new Id0; worth 0.03 dB) |
> | `gm`, both shelf τ, taper, `kOutputMakeup`, `kInputRef` | — | **all unchanged** |
>
> ⭐⭐ **THE INSTRUMENT IS THE REUSABLE PART.** In DARK the source one-port is the bare resistor with
> NO STATE, so the whole stage is MEMORYLESS: one period of gate sine through the solve is the exact
> steady-state spectrum. That oracle reproduces the shipped plugin at 8× to **0.01 dB** and runs in
> 6 ms per cell against ~40 s for a render — which is what made a two-parameter family explorable at
> all. Every `--vov`/`--gm` sweep before this was ~8 renders per parameter point.
>
> **The result, over 64 probe cells (both modes, 220 and 3150 Hz, every knob position):**
>
> | model | vs P4 at P4's own gm | vs P4 at the SHIPPED gm |
> |---|---|---|
> | square law, as shipped before | mean −7.96 dB, sd 2.73 | mean −3.36, sd 1.36 |
> | **m = 1.60** | **mean −0.44 dB, sd 0.63** | **mean +1.26, sd 1.53** |
>
> ⭐ **Compression came along for free and was NEVER FITTED**: at 10:30 DARK the top cell reads
> −0.326 dB against the pedal's −0.285 (delta +0.04) where the square law read −0.802 (delta +0.52).
>
> ### The three things worth not re-deriving
> 1. ⛔ **Drain-side mechanisms were eliminated by the data, free.** The residual at a given GATE
>    DRIVE is the same across a 15× range of drain load (≤ 0.07 dB), and anything acting through the
>    load line must scale with it. A softened cutoff was swept and is best at exactly zero.
> 2. ⭐ **The fit is well posed because three observables pin three parameters separately.** Cutoff
>    onset is EXACTLY |Vp| in gate volts whatever gm and m are; small-signal curvature then pins m;
>    gm is already measured from the rig-free mode differential. ⛔ And the square law cannot be
>    rescued by moving gm — it needs K0 = 6.79 against P4's measured 5.126, a 2.4 dB error in a
>    quantity fitted to 0.03 dB.
> 3. ⭐⭐ **The evidence that it is structure, not a spent degree of freedom.** Fitted on H2 at 220 Hz
>    ONLY; all five other observables improve and their OFFSETS go to zero (H3, a 14× different
>    frequency, and compression on the fundamental). And it makes |Vp| AGREE across disjoint capture
>    subsets — pad-0 vs padded, 32 % apart under the square law, 6 % apart with m free.
>
> ### ⚠⚠ A RECORDED CLAIM WAS WRONG: THE STAGE CLIPS AT CUTOFF FIRST, NOT TRIODE
> `triodeOnsetGateVolts()` converted gate-source drive to gate volts with the SMALL-SIGNAL K0, which
> is meaningless at triode onset. It read 1.691 V where the truth is ~2.4 V — and 1.691 V is very
> nearly the true CUTOFF onset (1.696 V), so **"triode is entered first, at −7.5 dBFS", repeated
> through notes #16 and #22 and into the capture-session planning, was a mis-converted triode figure
> that coincided with the correct cutoff figure.** Fixed; `cutoffOnsetGateVolts()` now exists and is
> exactly `|Vp|`.
>
> ### 📌 Implementation notes
> - **|Vp| replaces Vov as the parameter and retires note #22a's trap BY CONSTRUCTION.**
>   `Id0 = gm·|Vp|/(m + gm·R5)` is bounded above by `|Vp|/R5` for any gm, so the bias point can no
>   longer collapse. `OfflineRender`'s guard is now belt-and-braces. New flags `--vp` and
>   `--exponent`; `--vov` still works and still means the quiescent overdrive.
>   ⭐ To reproduce the old model for a same-cells before/after, pass **`--exponent 2.0 --vov 0.4469`**.
>   ⚠⚠ **`--vov`, NOT `--vp`** — and the difference bit once already. The old model stored Vov
>   directly, so its Vov did not move when gm did; |Vp| does. The two spellings agree only at the
>   shipped gm: at P4's 1146 µS, `--vp 1.6962` means Vov = 0.554 rather than 0.447 and reports a
>   4.95 dB deficit where the real old model reports 8.0. **A reproduction has to be written in the
>   parameterisation the thing being reproduced actually used.**
> - **The closed-form solve is gone from production** (it is exact only at m = 2) and is kept as the
>   m = 2 oracle. Production is a one-unknown **Halley** solve in saturation: `u + c·u^m = P`, three
>   steps, exact to 1.6e-18 A against a 300-step bisection. ⚠⚠ Its worst case is at the CUTOFF KNEE,
>   not at full drive — the first version was checked over 0.05–4.016 V and read exact.
> - ⚠⚠ **A real bug the bisection oracle caught:** the triode fallback bracketed `[iSat, B/R]`, which
>   is inverted whenever the saturation root exceeds the drain-bottomed current — what every loud
>   half-cycle does. It read 980 µA against the oracle's 458 at a 0 dBFS peak.
> - ⚠ **And a test was briefly VACUOUS**: 8c(b) swept an iteration count that only the oracle honours,
>   so it printed 0.0000 dB in every row while comparing the shipped path with itself. It now
>   compares against a different algorithm — and **measures that reference's own floor first**,
>   because here the reference is the LESS accurate side.
> - 📌 **CPU 2.3 % → 6.7–7.5 % at the 4× default, 12.8–14.4 % at 8×.** It is all `std::pow`, three
>   per sample. `exp2((m−1)·log2(u))` measured 6.3 ns against `pow`'s 9.2 — about 1.4 points — and was
>   NOT taken: it costs a digit or two of the machine-precision agreement and the profile does not
>   flag 7 %.
>
> ### ➡ NEXT
> - ⭐⭐ **A voicing decision for the owner, now quantified.** At the shipped gm the model makes about
>   2.4 dB LESS H2 than their own pedal at small signal. That is the recorded "voice to P1/P2"
>   decision, the same class as BRIGHT's 6.5–10 kHz miss — but it is now a measured number rather
>   than an unknown, and reversing it is one constant (`gm` → ~1146 µS).
> - **Compression**: largely closed as a by-product; re-read it against the captures before treating
>   it as a separate job.
> - **The band edges** (below 200 Hz and above 12 kHz) are untouched by this session.
> - **MID stays inferred** — P4 has no MID position. Scale by the measured cap RATIO 2.24.

> ### ⭐⭐ SESSION 2026-09-11: REVALIDATION AFTER THE TRANSFER-LAW CHANGE. Full detail: circuit.md #27.
>
> Note #26 replaced the device law in the previous commit. **Every downstream validation and the
> `kOutputMakeup` anchor had been measured under the OLD square law**, so the shipped model had never
> been checked end to end. This session is that check. **NO constant changed, and none should.**
> All 11 tests pass, warning-free.
>
> | what | result |
> |---|---|
> | `kOutputMakeup` = 1.1562 | ✅ **re-measured +0.019 dB, sd 0.178** — the whole device-law change is 1/10 of the scatter. STAYS. |
> | linear FR / phase / level | ✅ **identical to note #25** (DARK core 0.03/0.07 at 10:30, phase worst 3.90°, all 15 levels PASS) |
> | compression | ✅ **closed: median −0.024 dB**, 14 of 16 cells within ±0.29 dB |
> | H2 vs P4, shipped params | ✅ **mean +1.26 dB, sd 1.53** — reproduces note #26c's ORACLE through a real plugin render |
>
> ⭐ **Two free cross-checks fell out.** (a) `goal_check`'s absolute-level DARK mean moved −0.039 →
> −0.021 dB, i.e. **+0.018 against `absolute_gain`'s +0.019** — two instruments, one millidB. (b) A
> nonlinear re-derivation leaving every linear target bit-for-bit unmoved is the evidence it was
> confined to the nonlinear path; note #26's preservation claim was measured at the STAGE, this is the
> whole chain against real captures.
>
> ⚠ **Compression's RAW column is not the statistic.** The captures read POSITIVE (expansive) by
> 0.06–0.19 dB at mid levels, which the circuit forbids — note #16's level-independent reference gain
> error. Use the INCREMENT across level, which cancels it.
>
> ### ⛔⭐⭐ THE ONE FINDING: THE TRIODE BRANCH RESTS ON A SINGLE CAPTURE CELL
>
> Both cells that break the compression table are `p4_V1700_dark_pad1p5` (plugin over-compresses
> 0.67/0.59 dB, under-produces H2 6.2/5.3 dB). ⚠⚠ **Not an outlier among comparable cells — it has no
> comparables.** The rig ANTI-CORRELATES drive depth with drain load, because VOLUME sets both the
> load line and the recorded level: 7:30–9:00 reach 3.58 V at a 0.5–7.6 kΩ load (triode onset 3.8–9.6 V,
> unreachable), 10:30–13:30 sit at 13.6–17.6 kΩ but only 1.9–2.1 V. **17:00 is the only cell that
> enters triode** (3.01 V against a 2.05 V onset at 17.8 kΩ).
>
> ⚠⚠ **And it is the largest residual in the m-fit that shipped, absorbed into an sd.** In
> `onset_fit.json`'s own cells its delta is **+6.196 dB against a next-worst 2.82**, and dropping it
> takes the scatter **sd 1.358 → 0.966** — one cell in twenty carrying 29 %. ➡ So note #26's `m` and
> `|Vp|` are a **SATURATION-branch fit**, and the triode branch (forced by the solve's guarantees, not
> chosen) has **essentially no measurement support**.
>
> ⛔ **Do NOT fit it — one cell cannot constrain a branch**, and both observables move the same way
> (plugin clipping harder), so a curvature constant would absorb a structural error. That is note
> #22's recorded failure mode.
> ⚠ **A deeper cell cannot be captured at that knob setting**: the file peaks at **−1.18 dBFS**, and
> only the pedal's own −1.52 dB of compression keeps it inside the converter. Same structural ceiling
> already recorded for BRIGHT above 9:00, reached from the other side.
> ⭐ **It matters in play**: at 17:00 a user at 0 dBFS puts 4.02 V on the gate against a 2.05 V onset.
>
> ➡ **THE ONE THING TO ADD TO ANY FUTURE CAPTURE SESSION: a known RESISTIVE PAD ON THE PEDAL'S OUTPUT,
> not the input.** Input padding cannot work — it lowers drive and recorded level together, which is
> what created the anti-correlation in the first place. An output pad decouples them. A known load is
> already handled (`_load10k` measures `Zout` that way; `loading_correction_complex` undoes a load in
> magnitude and phase). ⚠ Record its value — it loads a 59–102 kΩ source, so it must be modelled.
>
> ### 📌 MID is resolved as NO ACTION
> `tauBright`/`tauMid` = 2.2311, between the drawn 2.20 and the measured 2.24, and **both values are
> P1/P2's own fits — the unit the model is voiced to.** The "scale by the cap ratio 2.24" rule bites
> only if the model is retuned to P4, which the recorded decision declines.
>
> ### ➡ NEXT
> 1. ⭐⭐ **The voicing decision is the owner's and is now fully quantified.** At the shipped `gm` the
>    model makes **+1.26 dB less H2** than their own pedal (note #26c's −2.39 dB predicted at
>    small signal; BRIGHT also reads up to +1.51 dB bright at 6.5–10 kHz). Reversing it is ONE
>    constant, `gm` → ~1146 µS. ⚠ `kOutputMakeup` must then be re-measured — it and `gm` are both
>    level scalars in the DARK path.
> 2. **The triode branch**, if and only if an output-padded capture ever exists. Not fittable now.
> 3. **The band edges, and this is BETTER than note #25 recorded.** ⭐ DARK now passes the FULL
>    20 Hz–20 kHz ±1.0 dB target — **zero bands over** — at 9:00, 10:30, 12:00, 15:00 and 17:00, where
>    note #25 only ever claimed the 80 Hz–12 kHz core. The LF story really is closed by note #21's
>    taper and C10: 20 Hz reads −0.16 to −0.24 dB at those positions. What still misses is **8:00 dark
>    marginally (−1.08 dB at 20 Hz)**, 13:30 in the core HF (0.64), and BRIGHT above ~6.5 kHz, which is
>    the voicing decision. 7:30 and 8:00 remain excluded for knob-slope error (note #23). ➡ There is no
>    general LF defect left to chase.

### 🗺️ Current plan

> The session-by-session plan — priority order, what's parked, what's explicitly out of scope —
> lives in `docs/build-plan.md`'s final section, kept current in place. This file stays the
> chronological log; don't duplicate the plan here. See `docs/build-plan.md` §19.
