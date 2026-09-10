#!/usr/bin/env python3
"""
Attribute the plugin-vs-capture LOW-FREQUENCY excess (circuit.md note #19).

Every one of the seven NAM captures has less bass than the plugin. This asks WHERE that
high-pass lives, by exploiting the one lever the dataset has: the three units sit at three
VOLUME settings, and the pedal's own output high-pass (C10 into the volume network) moves
with the knob while anything in the capture chain does not.

Candidates tested, each as ONE global value across all seven captures:
  * C10 smaller             -- volume-DEPENDENT pole inside the pedal
  * R10 smaller             -- volume-dependent, but shares node E with Ra
  * resistive load at OUT   -- the training rig's input impedance
  * drain Norton impedance  -- a resistance at the other end of C10
  * an input-side high-pass -- volume-INDEPENDENT, i.e. ahead of the pedal

Result: a per-rig input-side high-pass at 20-30 Hz describes all seven at their own error
floor with the pedal left exactly as drawn. See circuit.md note #19 before acting on this.

Run:  .venv/bin/python analysis/lf_pole_attribution.py
Reads analysis/reports/hf_shape_fit.json (capture-minus-plugin curves, 8x OS).
"""
import json, os, re

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC  = os.path.join(HERE, "reports", "hf_shape_fit.json")
OUT  = os.path.join(HERE, "reports", "lf_pole_attribution.json")

# circuit.md stage 3 / CircuitValues.h.
# ⚠⚠ TAPER_P AND C10 ARE PARSED FROM THE HEADER, NOT TYPED. Six analysis scripts route their output
# network through this module, so a hardcoded copy here is a second definition of a shipped constant
# -- and when the taper moved 2.0 -> 2.30 every one of them would have silently kept modelling the
# old plugin while comparing against renders of the new one. That is not a hypothetical: the LF and
# midband taper fits are BOTH reparameterisations off TAPER_P (`x ** (taper / TAPER_P)`), so a stale
# copy would have returned a confident, well-fitting, wrong exponent. Same rule as OsDroopRestore's
# "one definition" and p4_corners.plugin_known(): read the source of truth.
_SRC_H = open(os.path.join(HERE, "..", "src", "dsp", "CircuitValues.h")).read()


def _const(name):
    m = re.search(rf"{name}\s*=\s*([0-9.eE+-]+)", _SRC_H)
    if m is None:
        raise RuntimeError(f"{name} not found in CircuitValues.h -- the header was restructured")
    return float(m.group(1))


R6, R10, R9, R8, POT = 22e3, 240e3, 110e3, 110e3, 500e3
C10 = _const("kC10")
TAPER_P = _const("kVolumeTaperP")
RO = 1.44e6                      # JfetStage ro
R4, C4 = 1e6, 22e-9              # input high-pass, 7.2 Hz as drawn
F_IN = 1.0 / (2 * np.pi * R4 * C4)

# VOLUME positions as fractions of a 7->5 o'clock (300 degree) sweep.
KNOB = {"p1": 0.35, "p2": 0.75, "p3": 0.30}     # 10:30, 2:30, 10:00
FIT_BAND = (25.0, 800.0)


def out_network(f, x, c10=C10, r10=R10, rl=np.inf, zd=None):
    """Node-E / node-OUT solve, driven by the drain Norton current. Returns V_out.

    `rl` is the load at the jack. It may be a scalar (a resistive load, or np.inf for unloaded) OR
    an ARRAY the same length as `f` -- which is what a CAPACITIVE load needs, since a cable's
    impedance is frequency-dependent. goal_check.py uses the array form for the interface's
    1 MOhm shunted by the measured ~99 pF of cable (circuit.md note #21).
    """
    f = np.atleast_1d(np.asarray(f, dtype=complex))
    ra = POT * x ** TAPER_P
    rb = POT - ra
    if zd is None:
        zd = 1.0 / (1 / R6 + 1 / RO)
    rl_arr = np.broadcast_to(np.asarray(rl, dtype=complex), f.shape)
    out = []
    for y, zl in zip(2j * np.pi * f * c10, rl_arr):
        Y = np.array([[1 / zd + y, -y, 0],
                      [-y, y + 1 / r10 + 1 / ra + 1 / R9, -1 / R9],
                      [0, -1 / R9, 1 / R9 + 1 / (R8 + rb) + 1 / zl]], dtype=complex)
        out.append(np.linalg.solve(Y, np.array([1, 0, 0], dtype=complex))[2])
    return np.array(out)


def hp_db(f, fc):
    return 20 * np.log10(np.abs(1j * np.asarray(f) / (1j * np.asarray(f) + fc)))


def shape_err(target_db, model_db):
    """Worst |residual| after removing the constant offset -- this is a SHAPE test, and the
    dataset carries no absolute level anchor (build-plan limit L2)."""
    r = target_db - model_db
    return float(np.max(np.abs(r - np.mean(r)))), float(np.sqrt(np.mean((r - np.mean(r)) ** 2)))


def load():
    d = json.load(open(SRC))
    caps = {}
    for k, v in d["captures"].items():
        f = np.array(v["freqs"])
        m = (f >= FIT_BAND[0]) & (f <= FIT_BAND[1])
        f = f[m]
        # the capture's own LF shape = the plugin's, plus the measured difference
        tgt = 20 * np.log10(np.abs(out_network(f, KNOB[v["unit"]]))) \
              + np.array(v["capture_minus_plugin_db"])[m]
        caps[k] = dict(f=f, tgt=tgt, x=KNOB[v["unit"]], unit=v["unit"], mode=v["mode"])
    return caps


def main():
    caps = load()
    report = {"source": os.path.basename(SRC), "fit_band_hz": list(FIT_BAND),
              "knob_fractions": KNOB, "candidates": {}, "per_rig_input_hp": {}}

    def plain(c):
        return 20 * np.log10(np.abs(out_network(c["f"], c["x"])))

    base = {k: shape_err(c["tgt"], plain(c)) for k, c in caps.items()}
    report["as_drawn"] = {k: dict(worst_db=v[0], rms_db=v[1]) for k, v in base.items()}
    print(f"as-drawn, worst over all seven: {max(v[0] for v in base.values()):.2f} dB\n")

    models = {
        "C10 (volume-dependent)":   (lambda th, c: 20*np.log10(np.abs(out_network(c["f"], c["x"], c10=th))),
                                     np.linspace(35e-9, 100e-9, 500), lambda v: f"{v*1e9:.1f} nF"),
        "R10 (volume-dependent)":   (lambda th, c: 20*np.log10(np.abs(out_network(c["f"], c["x"], r10=th))),
                                     np.logspace(4, 5.9, 400), lambda v: f"{v/1e3:.1f} kOhm"),
        "load at OUT":              (lambda th, c: 20*np.log10(np.abs(out_network(c["f"], c["x"], rl=th))),
                                     np.logspace(3.5, 7, 400), lambda v: f"{v/1e3:.1f} kOhm"),
        "drain Norton impedance":   (lambda th, c: 20*np.log10(np.abs(out_network(c["f"], c["x"], zd=th))),
                                     np.logspace(2, 6, 400), lambda v: f"{v/1e3:.2f} kOhm"),
        "input HP (volume-INDEP)":  (lambda th, c: plain(c) + hp_db(c["f"], th) - hp_db(c["f"], F_IN),
                                     np.linspace(5, 120, 600), lambda v: f"{v:.1f} Hz"),
    }

    print("ONE global value for all seven captures:")
    for name, (fn, grid, fmt) in models.items():
        best = min(((max(shape_err(c["tgt"], fn(th, c))[0] for c in caps.values()), th) for th in grid))
        # per-capture spread of the INDEPENDENTLY fitted value -- a single component must not need
        # a different value at each volume setting.
        per = [min(((shape_err(c["tgt"], fn(th, c))[0], th) for th in grid))[1] for c in caps.values()]
        spread = max(per) / min(per)
        report["candidates"][name] = dict(global_value=best[1], global_worst_db=best[0],
                                          per_capture_values=per, per_capture_spread=spread)
        print(f"  {name:26s} {fmt(best[1]):>11s}  worst {best[0]:5.2f} dB   "
              f"per-capture spread {spread:.2f}x")

    print("\nPER-RIG input-side high-pass, pedal left exactly as drawn:")
    fn, grid, _ = models["input HP (volume-INDEP)"]
    for k, c in caps.items():
        w, th = min(((shape_err(c["tgt"], fn(t, c))[0], t) for t in grid))
        report["per_rig_input_hp"][k] = dict(fc_hz=th, worst_db=w)
        print(f"  {k:22s} {th:5.1f} Hz   worst {w:.2f} dB")

    json.dump(report, open(OUT, "w"), indent=1)
    print(f"\nwrote {OUT}")


if __name__ == "__main__":
    main()
