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
amplitude parameter and it is not yet measured.

**Read before touching anything:**
- `CLAUDE.md` → the "Current step" block, and the NEXT STEPS block just above
  "Project-specific carry-forwards". Long, but every ⚠ in it is a failure that has already happened.
- `docs/build-plan.md` §16 (the device model) and §17 (the closed-form solve).
- `.claude/rules/circuit.md` notes #12–#15.

## Do these, in this order

**1. Fit `Vov` from P1, using two independent observables and checking they agree.**
P1 is the only calibrated capture (−12 dBu at the pedal jack). Its second-harmonic deficit is
8.9–11.6 dB over 125 Hz–800 Hz and clears its own per-band floor by 1.6–6.0×, so it is real signal.
Use the midband H2 deficit as one route and **compression above 3 kHz only** as the other — below
3 kHz P1's compression reads positive, which the circuit forbids, so that region is floor regardless
of its magnitude.
⚠⚠ Do **not** claim a √N improvement from the 24 cells. The floors are systematic model error, not
noise (`circuit.md` #15), so they do not average down. Expect to pin `Vov` to about a factor of 1.5,
not to the owner's 5 % target.

**2. Explain the 4–8 kHz frequency-response dip.** `analysis/goal_check.py` reports the plugin
0.5–0.67 dB dark at 4064 / 5120 / 6451 / 8127 Hz against P1, in all three modes. This is inside the
core target band, is not the low-frequency confound, and has never been investigated.
⚠ The obvious candidate is ruled out by sign: P1 fits a 6.7 kHz input pole against the 7.3 kHz
shipped, and a lower real corner would make the plugin *brighter* there, not darker.

**3. Reconcile the two phase figures before tuning anything to either.** Whole-band RMS agrees
between `phase_sweep.py` (3.14–4.43°) and `goal_check.py` (3.56–4.12°). But the recorded "within
2.4° over 200 Hz–12 kHz" and `goal_check`'s 6.66–8.22° over the same band do not, and it is not yet
known which is right (fit window, weighting, or 4× vs 8×). Do this **after** item 2 — a
minimum-phase magnitude dip carries phase with it, so they may be one finding.

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
- ⚠ **Do not fit anything to a capture below ~100 Hz**, or to P1's harmonics above H2, or to P2
  above ~2 kHz. All three are the reference's own error.
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
