# Echo Pre 3 — project memory

> Echo Pre 3 is a circuit-level emulation of the Echoplex EP-3 preamp (per the Chase Tone Secret
> Preamp schematic), built as an AU/VST3 plugin with JUCE 8+ and `chowdsp_wdf`.
> Author: Leigh Pierce. **Version 1.0.0 — complete and calibrated.**

## Where things stand

✅ **Done.** Every constant in the model is measured rather than assumed, including both absolute
level anchors (`kInputRef`, `kOutputMakeup`) and the VOLUME taper. Against the owner's own unit —
rig deconvolved, interface loading undone, matched drive — the model meets its frequency-response,
phase, absolute-level and per-band THD targets; the README carries the table. 13 tests pass,
`auval` passes, the build is warning-free.

**Three things rest on thinner evidence, documented rather than hidden:**

- the transistor's **triode branch** is constrained by one capture cell (the rig coupled drive depth
  to recorded level, so only one knob setting reaches that region — circuit.md note #27 §4);
- **MID** is scaled from the three-position units, since the calibration unit has no MID position;
- the test signal's **twin-tone segment cannot measure intermodulation at all** (660 = 3 × 220, so
  every product lands on the harmonic grid), which needs a new segment and therefore a new session.

⛔ **No more captures are coming — the rig is gone.** Anything needing one is recorded as such, not
left as an open question. `docs/capture-dataset.md` says what exists and what it cannot answer.

⚠ **One accepted trade, not a defect:** BRIGHT runs up to ~1.5 dB bright at 6.5–10 kHz and makes
~1.3 dB less H2 than the calibration unit, because the model is deliberately voiced to the newer
three-position units. Reversing it is one constant (`gm`) plus a re-run of `absolute_gain.py`. The
decision, its evidence and its full price are circuit.md notes **#28** and **#30**. **Do not move
`gm` to close any of it.**

## Where to look for what

| question | file |
|---|---|
| what is the circuit, and what does a constant measure | `.claude/rules/circuit.md` — **the single source of truth** |
| what was done, in what order, and why | `docs/build-plan.md` (§19 is the plan of record; the dated log runs §1–§18 then continues at §20) |
| what the captures are and what they cannot answer | `docs/capture-dataset.md` |
| how to measure a model against a reference without fooling yourself | `docs/measurement-discipline.md` |

⚠⚠ **Record a new circuit fact in `circuit.md`, not here.** This file used to restate circuit values
and went stale and actively wrong doing it — it carried a MODE cap mapping that a measurement had
already reversed, and said `kInputRef` "cannot be measured" long after it was measured. Where this
file and `circuit.md` disagree, **`circuit.md` wins.**

## Quick reference

```
Build:    cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
AU:       cmake --build build --target EchoPre3_AU   (auto-installs; bump VERSION to force a Logic rescan)
Tests:    ctest --test-dir build --output-on-failure
Host:     auval -v aufx Ep3p Lprc
Analysis: .venv/bin/python analysis/<script>.py      # run from the repo root
Format:   clang-format -i <NEW files only>           # see the warning below
```

⚠ **`python3` on this machine (3.14) has a broken numpy — use `.venv/bin/python`.**

⚠ **This tree is HAND-formatted and `clang-format` cannot reproduce it under any config.** A blanket
run reformats the whole codebase (it did once: ~800 lines of pure churn across six files, burying
the real diff). The shipped `.clang-format` has been corrected to describe the code that actually
exists, which roughly halves the churn — but aligned trailing comments and aligned constant tables
are deliberate and it will always reflow them. **Run it on NEW files only, then check the diff.**

## Schematics and agents

`schematics/schematic.png` is the primary source, with crops indexed in `circuit.md`. Load them when
verifying a circuit detail — but note the standing finding: **re-reading a component's value rarely
finds a new bug; re-solving a derived quantity often does.** A full re-verification pass found four
wrong derived numbers and zero wrong values.

Use the **`schematic-checker`** agent when a value or topology is in doubt, and **`dsp-validator`**
after a DSP stage change. Both read `circuit.md`/`dsp.md`, so keeping those current keeps the agents
useful with no extra setup.

@.claude/rules/circuit.md
@.claude/rules/dsp.md
@.claude/rules/architecture.md
@.claude/rules/ui.md
@.claude/rules/build.md

## Delegation & model tiering

Plan with a high-end model, delegate execution down to cheaper ones — reserve the expensive
reasoning for the step that is actually hard to get right.

- **Planning** (build-sequence ordering, schematic-topology judgement, deciding what a session
  should tackle) — a top-tier model at high effort.
- **Important thinking** (circuit/DSP correctness: the `schematic-checker` and `dsp-validator`
  agents, anything cross-checking values, topology or taper against `circuit.md`/`dsp.md`) — a
  strong reasoning model at high effort. Both agents' frontmatter is pinned to this tier; **don't
  downgrade them to save cost**, they are exactly the category this policy protects.
- **Routine work** (mechanical edits, scaffolding, formatting, running builds/tests) — a fast
  mid-tier model at medium effort.

Re-evaluate the concrete model names as new ones ship; the principle (plan high, validate high,
execute routine work cheap) is what should persist.

## Essential reading

- **`docs/measurement-discipline.md`** — the analysis-side traps: instruments and known answers,
  mutation guards, thresholds, aggregates, fits and degeneracy, screening mechanisms, staleness.
  Every entry is a failure mode that has inverted a conclusion **already written down as fact**.
  Re-read the relevant section before building an instrument, trusting an aggregate, or shipping a
  constant. **This is where this project's method lessons were distilled to.**
- **`docs/nonlinear-component-modeling.md`** — the parts `chowdsp_wdf` has no element for, and the
  structural traps. ⭐ Its §2 "a degenerated common-source stage is a *current* source" is the single
  most load-bearing paragraph for this pedal.
- **`docs/calibration-and-gain-staging.md`** — `kInputRef`/output-makeup calibration, taper floors,
  internal-vs-output clipping, the VU idle gate. ⚠ Its §4 ("output load: almost never worth
  modelling") carries an explicit exception for this pedal, whose output impedance is 15–23× the
  value that section assumes.
- **`docs/validation-and-capture.md`** — how to measure closeness to the real pedal, and how to
  capture it so the measurement is trustworthy.
- **`docs/ui-peripheral-spec.md`** — the visual spec for the reusable UI elements.
- **`analysis/`** — the harness: `gen_test_signal.py`, `analyze.py`, `captures.py`, `p4_corners.py`
  (the capture-side corrections, one definition each), plus per-question instruments. ⭐ Nearly all
  have a `--self-test` that puts a plugin render where the capture goes and checks the estimator
  recovers a known answer. **Run it before trusting a number.**

## Source map

```
src/
  PluginProcessor.{h,cpp}   Entry point, APVTS, gain staging, oversampling, bypass, trim link.
                            ⚠ processBlock SLICES the buffer while VOLUME moves and calls
                            processChunk per slice; slicing is skipped when VOLUME is static,
                            which is what keeps every render and null bit-for-bit stable.
  PluginEditor.{h,cpp}      Three-column layout, side-panel trims + VU, bottom strip (OS, trim
                            link, LOAD, version, UI size), display-clamped resizable scaling.
                            ⚠ The strip's width budget is tuned — re-do the arithmetic if
                            anything is added, and check the headless render at 0.5x AND 2.5x.
  ui/PedalFace.{h,cpp}      The centre face: VOLUME knob, three-position EQ switch, LED,
                            footswitch. The one per-pedal piece.
  dsp/
    CircuitValues.h         Component values and the shipped taper.
    InputNetwork.h          Input LPF / gate-bias network. Holds the ONE definition of its
                            analytic transfer function, which OsDroopRestore is derived from.
    JfetStage.h             The 2N5457 stage: measured transfer law solved implicitly against
                            its source network AND its VOLUME-dependent drain load. Carries the
                            reasoning for every device constant.
    OutputNetwork.h         The coupled output/VOLUME network — a tree, not a divider.
    OsDroopRestore.h        Derived (not fitted) low-OS top-octave shelf.
    EchoPreDsp.h            Wires the stages into the chain.
  utils/TaperUtils.h        Pot taper curves.

tests/                      13 CTest targets: per-stage transfer functions and phase, the JFET
                            solve against independent oracles, bypass clicks, VOLUME automation,
                            oversampling fidelity, CPU, a full control sweep, a UI snapshot.
analysis/                   Offline render tool + the Python harness.
schematics/                 Source images.
```

## Build sequence — all ten steps complete

Kept because the tests cite it ("build sequence step 4") and because the ORDER is the reusable part:
each step gated the next, and skipping ahead is what the sequence exists to prevent.

| # | step | where it landed |
|---|---|---|
| 1 | Schematic analysis → fill `circuit.md`, then triage the parts list for anything not WDF-native | `circuit.md`; exactly one part (Q1) needed an external model |
| 2 | CMake scaffold, APVTS, AU/VST3 loading in a DAW | — |
| 3 | `chowdsp_wdf` smoke test: RC lowpass, −3 dB point within 1 % | `WdfSmokeTest` (998.6 Hz vs 1000) |
| 4 | Stage-by-stage DSP, validated at each step | `InputNetworkTest`, `OutputNetworkTest`, `JfetStageTest` |
| 5 | Switch topologies, verified independently | ⭐ needed **no** scattering matrices — MODE folds analytically into the source impedance, so it is three coefficient sets |
| 6 | Oversampling (+ ADAA) on the nonlinear stage | `OSFidelity`; ADAA was later **removed** — the map became 2-D, so ADAA1's derivation no longer applies |
| 7 | Full-chain integration and level calibration | `ChainTest`; `kInputRef`/`kOutputMakeup` anchored from the capture session |
| 8 | UI | `PluginEditor`, `PedalFace`, `UISnapshot` |
| 9 | Reference validation against real captures | `analysis/goal_check.py` and the per-question instruments |
| 10 | Final sweep: all controls full range, no instability, clicks or NaN/Inf | `FullSweepTest` — 166 configurations, 1.36 M samples |

⛔ **Step 10's standing caveat:** output above 0 dBFS at extreme drive + volume is **faithful, not a
fault** — this pedal really does boost ~10 dB into a high-impedance input, and the output trim
manages it. `FullSweepTest`'s bound catches a divergence, not loudness.

## The findings worth not re-deriving

An index, not a retelling — each line points at where the reasoning and the numbers live. These are
the ones that cost real time or reversed a recorded conclusion.

**Circuit and topology**

- The MODE switch's lever→cap mapping is the **reverse** of what the labels suggest, and an aesthetic
  argument about which band a filter emphasises lost to a two-parameter fit — note **#2**.
- The VOLUME network is deliberately **non-monotonic** and must not be "fixed" into a divider; its
  output impedance is 59–102 kΩ and *moves with the knob*, which is why an output load **tilts** the
  control law rather than just lowering it — validation note #1, note **#31**.
- The maker's published control figures were **not** wrong; three of them were "refuted" against an
  open-circuit model, and one 68 kΩ load reconciles all of them — note **#31**.
- `R7` does not exist anywhere on the drawing. Treat the gap as an open question, not as noise, and
  **do not add a speculative part** — the designator census in `circuit.md`.

**The device model**

- **The transfer law is not square.** m = **1.60**, fitted on H2 at one frequency, with five
  unfitted observables improving and their offsets going to zero — note **#26**.
- Degeneration suppresses distortion **twice**, worth 16.4 dB, and every linear test passed the whole
  time. **A correct frequency response is not evidence that the nonlinear path is right** — note #8.
- The stage clips at **cutoff** first, not triode. The contrary claim survived three notes because a
  mis-converted triode figure coincided with the correct cutoff figure — note **#26a**.
- ⛔ A confident two-route `Vov` fit was **refuted by 13–20 dB** once a capture reached the
  nonlinearity, because both routes read the same floor. **A fit whose input is floor returns a
  confident number with a good residual** — notes #16, **#22**.

**Method — the traps this project was actually bitten by**

All of these are generalised in `docs/measurement-discipline.md`; the project-specific instances:

- **Phase testing caught defects that magnitude testing passed straight through**: an inverted input
  network (magnitude perfect at every frequency), `useIntegerLatency` appending an allpass after the
  linear-phase FIR, and the mode shelf's discretisation. ⭐ The tell for the second was
  **non-monotonicity** — oversampling can only improve the top octave, and 4× was 7× worse than 1×.
- **Run every capture-side estimator on the PLUGIN first**, where the answer is known by
  construction. That is what exposed a fit which had looked like independent confirmation — note #9.
- **Fit a model and report the residual; never threshold.** Nothing on this pedal reaches a plateau
  inside the audio band, so every "−3 dB below the plateau" reading was wrong, one of them inverting
  a verdict — note #7.
- **Asymmetric comparisons** — closed form against measurement — failed a *correct* implementation
  three times. Measure the instrument's own floor from a known-exact probe rather than assuming it.
- **A vacuous test reads as a perfect result.** An exactly-zero self-comparison is a hook that does
  nothing; a convergence check whose two configurations were identical passed a known-broken build.
- **A known-answer probe has a validity condition.** One was read over bands where its own premise
  fails, overstating a floor 7× and nearly ruling out a measurable target — note **#32**.
- **One definition of a shipped constant.** Stale copies of `TAPER_P` and `ro` silently biased six
  analysis scripts each. They parse the headers now. ⭐ And a **load-independent** disagreement is a
  constant; a load-dependent one is the topology — that distinction located one in a single step.
- **Cache keys must hash the command, not a summary of it.** Two successive faults: a key that was a
  display tag, then one that omitted the plugin binary — so a re-run after a constant changed
  compared new captures against the old plugin. ⭐ Caught by a column that did not move.
- ⚠ **Compare capture mtimes to report mtimes before trusting a prior fit.** `*.wav` is gitignored,
  so git cannot show that three captures landed after the fits that quoted them.
