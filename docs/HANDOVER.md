# Handover prompt — Echo Pre 3, after the 2026-09-10 capture session

Paste the block below into a fresh session.

---

I'm continuing work on Echo Pre 3, a circuit-level emulation of the Chase Tone Secret Preamp
(Echoplex EP-3 preamp) built with JUCE 8 and chowdsp_wdf. Read `CLAUDE.md` and
`.claude/rules/circuit.md` first — in particular circuit.md notes **#21, #21a and #22** and the
final CLAUDE.md session block, which together describe where the project actually stands.

**The situation.** The capture campaign is finished and the rig is gone. There are 28 main-signal
captures of the owner's own pedal (P4) covering both MODE positions across the full VOLUME rotation,
plus loop and bypass references, plus 15 captures of a short self-contained probe signal including
two into a 10 kΩ line input. Seven NAM-model renders of three other units are also on disk. The rig
was measured and is near-flat, and both play-side and record-side calibrations are known, so for the
first time the data is better than the model. **Data is no longer the bottleneck; the model is.**

**The recorded decision on which unit the model is of:** voice to the NEWER three-position units
(P1/P2), use P4 as the measurement baseline. That splits cleanly — `gm` and the shelf time constants
come from P1/P2 because the mode differential is rig-free, and everything structural (taper, C10,
`kOutputMakeup`, the load line, output impedance) comes from P4 because only P4 can measure it.

**Do these, in order. All are unblocked.**

1. Decide `gm` under that decision. P1 gives 1475 µS, P2 1640 µS, shipped is 1553 µS. Probably no
   change, but record that it was checked.
2. Apply `kOutputMakeup` = 1.1658 (+1.332 dB). It couples only to `gm`, so do it after step 1. This
   is the first anchor the constant has ever had and there will not be another.
3. Apply the VOLUME taper `p` = 2.3 (measured 2.28 from the LF corners and 2.33 from the midband
   control law). The shipped 2.0 costs 2.08 dB worst and 0.96 dB RMS on the control law.
4. Model the clipping ONSET. The H2 error grows with drive (−2.5 dB at the −12 dBFS cell, −6.2 at
   −4), so no single curvature scalar closes it. `analysis/probe_compare.py --vov` is the instrument.
5. Re-run `goal_check.py` and `phase_sweep.py` against P4 and re-read the 1 dB / 5° targets.

**Things that will waste your time if you don't know them.**

- **Do NOT apply circuit.md note #16's fitted `Vov` of 0.126–0.165.** Note #22 refutes it with
  calibrated matched-drive captures: it would make H2 13–20 dB wrong. The shipped 0.4469 is 3.4 dB
  out and has the lowest scatter of any value tried.
- **`analysis/probe_compare.py` must stay `--unit p4`.** The probe directory also holds NAM renders
  whose rigs ran at unknown levels, so their drive is not matched.
- **VOLUME 7:30 is excluded from everything except load-line depth.** Its knob-setting error is
  6.4 dB in dark and 10.4 dB in bright, measured.
- **BRIGHT above 9:00 has no load-line data and never can** — the pedal's output clips the converter
  before the JFET reaches triode.
- **MID stays inferred.** P4 has no MID position; scale it by the measured cap ratio of 2.24, never
  by an absolute time constant.
- The unit's resistors are carbon, so 2–5 % between one unit and nominal is expected. Read the
  2.0–2.3 % output-impedance errors as agreement, not discrepancy.

**Method rules this project has been bitten by, all with concrete instances in circuit.md.**
Run every capture-side estimator on the PLUGIN first, where the answer is known by construction.
Fit a model and report the residual rather than thresholding. Check phase as well as magnitude —
three real bugs here were invisible to magnitude. Measure the instrument's own floor before reading
anything through it, and bound a fit at BOTH ends. And note #22's newest one: **a fit whose input is
floor returns a confident number with a good residual**, so a clean fit is not evidence the data
carried signal.

All 11 tests pass via `ctest --test-dir build`. `python3` on this machine has a broken numpy — use
`.venv/bin/python`.
