# Handover prompt — Echo Pre 3

> Paste this into a fresh session to pick the work up. It is an entry point, not a summary: the
> detail lives in the files it points at, and those are the authority wherever they disagree with
> this page.

---

You are continuing work on **Echo Pre 3**, a circuit-level emulation of the Echoplex EP-3 preamp
(Chase Tone Secret Preamp schematic) as an AU/VST3 plugin in JUCE 8 + `chowdsp_wdf`.

## Where it stands

The DSP chain is complete and host-verified. All 11 tests pass via `ctest`, the build is
warning-free, and `auval -v aufx Ep3p Lprc` passes. Steps 1–8 of the build sequence are done.

The JFET stage models the device itself — Shichman-Hodges square law with cutoff and triode regions,
solved **in closed form** against both the source one-port and the drain load line. There is no
iteration, no fitted shaper, and no transcendental in the signal path. `Vov` is the single remaining
amplitude parameter. It is now MEASURED (two agreeing routes, 2026-09-09) and deliberately not
applied — the reason is a clipping-mechanism change, not a precision problem. See below.

**Read before touching anything:**
- `CLAUDE.md` → the "Current step" block, and the NEXT STEPS block just above
  "Project-specific carry-forwards". Long, but every ⚠ in it is a failure that has already happened.
- `docs/build-plan.md` §16 (the device model) and §17 (the closed-form solve).
- `.claude/rules/circuit.md` notes #12–#18.

## What the last session did (2026-09-09)

The three items this page used to list are **done**, and two of them reversed a recorded belief. Full
detail in `.claude/rules/circuit.md` notes **#16–#18** and the block at the end of `CLAUDE.md`'s
"Current step". **No DSP constant changed; all 11 tests pass.**

1. **`Vov` is fitted, twice, and the routes agree.** H2 in dBc over 125–800 Hz gives 0.126;
   compression growth over the top 10 dB at 5–8 kHz gives 0.165. A factor of 1.31 apart, against a
   shipped 0.4469. It is still **not applied** — see below.
2. **The 4–8 kHz "dip" is P1's rig, not the model.** P2 recovers the pedal's own 7.3 kHz input pole
   at a 0.02 dB residual in all three modes. P1 cannot be described by any cascade containing one,
   and is *brighter* there than the drawn circuit can be. Do not touch the input network.
3. **The two phase numbers reconcile.** The recorded "2.4° over 200 Hz–12 kHz" was a max over six
   report frequencies starting at 500 Hz. The 5° target is met above 500 Hz; the whole miss is
   200–500 Hz, which is the tail of the known LF pole.

New instruments: `analysis/vov_fit.py`, `analysis/hf_shape_fit.py`, `analysis/phase_reconcile.py`,
each with a `--self-test` or known-answer rung that runs on the plugin first. New CLI flag
`OfflineRender --vov V`, a measurement flag beside `--input-scale`.

## Do these, in this order

**1. Nothing in the model is blocked any more.** Every remaining item needs data the dataset cannot
contain. If you are picking this up before the capture session, the highest-value action is still a
single message: **ask the P2 and P3 trainers for their `input_level_dbu`.** P2 and P3 are the units
whose nonlinear data clears its floor, and P1 is the unit that is calibrated; that split is what
blocks the cubic, and one of those two numbers closes it.

**2. When the +12.2 dBu capture lands, apply `Vov` — but decide it on the clipping MECHANISM, not on
the parameter.** The reason it is still unapplied is not precision. It is that the fitted value swaps
which region clips first: at the shipped 0.4469 the drain enters triode at −7.5 dBFS and cuts off at
−2.7; at 0.150 it cuts off at −12.2 and triode is never reached. The load line itself barely moves.
A capture that reaches the load-line region shows which one the real pedal does, directly.

**3. Then `kOutputMakeup`, the VOLUME sweep, and the M0 null render**, in that order — see the last
section of this page.

## The traps, so they are not re-learned

- ⚠⚠ **Run every capture-side estimator on the PLUGIN first**, where the answer is known by
  construction. That is what exposed a fit which had looked like independent confirmation.
- ⚠⚠ **A correct frequency response is not evidence the nonlinear path is right.** Two structural
  distortion bugs on this project passed every linear test.
- ⚠⚠ **Phase testing has caught three defects magnitude testing structurally could not.** Check both.
- ⚠⚠ **Choose thresholds and parameters on the quantity that matters, not on a proxy.** The Newton
  residual, the alias floor and THD have each given a confidently wrong verdict here.
- ⚠ **Fit a model and report the residual; never threshold.** Nothing on this pedal reaches a
  plateau inside the audio band.
- ⚠ **Say WHERE in the chain a level is measured.** dBu at the interface and dBu at the pedal jack
  differ by the reamp box — 24.2 dB on this rig.
- ⚠⚠ **A bound the fit rests on is not a fit, and a flat STATISTIC is not a flat observable.** Both
  hid a real result this session — one behind a plausible-sounding bound, one behind a median taken
  across cells with wildly unequal sensitivity.
- ⚠ **Difference out a contaminating constant rather than widening the tolerance around it.** Reading
  compression as a growth-with-level instead of an absolute took its floor from 0.145 to 0.006 dB.
- ⚠ **Do not fit anything to a capture below ~100 Hz**, or to P1's harmonics above H2, or to P2's
  absolute LEVEL, POLARITY or top octave. All are the reference's own error.
  ⚠ But note #17 narrows the last one: P2's HF is fully described as the pedal's own 7.3 kHz pole
  times a 3.2 kHz cable pole, to 0.02 dB, so it is the best capture in the set for pole STRUCTURE —
  and P1, the designated absolute anchor, is the noisiest model in the set.
- ⚠ `python3` on this machine has a broken numpy — use `.venv/bin/python`.
- ⚠ This tree is hand-formatted; run `clang-format` on **new files only**.

## Blocked on the owner's capture session, and only on that

`kOutputMakeup` is still exactly 1.0 with no anchor and no other possible route. The VOLUME sweep is
the only within-rig measurement that can settle the taper, the 3.9 vs 1–2 dB fall-back and the
low-frequency pole. The M0 no-plugin null render has never run. And nothing in the dataset reaches
the load-line region, which is where the pedal's character lives at real playing levels.

`CLAUDE.md`'s capture list has the six things to record at capture time. The load-bearing one:
**set NAM's input calibration to +12.2 dBu — reamp at unity, do not attenuate.** That makes the
capture's digital levels map 1:1 onto the plugin's.
