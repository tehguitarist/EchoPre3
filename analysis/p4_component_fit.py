#!/usr/bin/env python3
"""Assign P4's two corner discrepancies to COMPONENTS, using the knob as the discriminator.

    .venv/bin/python analysis/p4_component_fit.py [--self-test]

⭐⭐ WHY THIS WORKS NOW AND FAILED ON THE NAM SET. circuit.md note #19a's profile was flat from
100 nF to 70 nF -- leaving C10 as drawn cost 0.041 dB -- so the pedal-side term was UNIDENTIFIABLE.
Two things have changed and both are necessary:

  1. THE RIG IS MEASURED AND HAS NO LF POLE. Bypass-minus-loop is -0.047 dB at 20 Hz, and the
     bypass reference shares its cabling with every pedal capture. On the NAM set the rig was the
     competing hypothesis and could not be ruled out; here it is measured out.
  2. THE KNOB SPANS 10x IN Ra, WITHIN ONE RIG. The NAM set had three volume points across three
     rigs and three trainers, so volume was 1:1 confounded with unit (note #7's M4). These are one
     pedal, one rig, one session -- so a corner that moves with the knob and a corner that does not
     are finally separable.

Two candidates, fitted as ONE global value each across every usable knob position:

  C10        a coupling capacitor: scales EVERY corner by the same factor, knob-independent.
  taper p    wrong Ra at each x: scales corners by a knob-DEPENDENT factor, and is pinned
             elsewhere by the volume peak landing at the maker's 1-2 o'clock (p ~= 2.0).

  Cload      at the output: the pedal drives 92-139 kOhm (circuit.md stage 3), so an ordinary
             cable/input capacitance puts a pole in the audio band -- and the BYPASS REFERENCE
             CANNOT REMOVE IT, because in bypass the source is the interface's own low output
             impedance, not the pedal's 120 kOhm. Deconvolution divides out the series chain, not
             a source-impedance-dependent load.

⛔ 7:30 IS EXCLUDED, and this reverses CLAUDE.md's "read its CORNER, never its LEVEL". Its two takes
fit 68.79 and 63.19 Hz -- 8.9 % apart -- where 9:00's two takes agree to 0.1 %. At Ra = 1.25 kOhm the
network is on its steepest slope, so the physical knob-setting error at the very bottom of the
rotation is larger than the +-10 min the level warning assumed. The corner is knob-error dominated
there too, not just the level.
"""
import argparse, json, os, sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import p4_corners as P
import lf_pole_attribution as LFA

OUT = "analysis/reports/p4_component_fit.json"
MODE = "dark"
LF_BAND = (15.0, 400.0)
HF_BAND = (3000.0, 19000.0)


def net_db(f, x, **kw):
    return 20 * np.log10(np.abs(LFA.out_network(f, x, **kw)))


def zout(f, x):
    """Pedal output impedance: drive node OUT with 1 A and read the volts (reciprocity)."""
    ra = LFA.POT * x ** LFA.TAPER_P
    rb = LFA.POT - ra
    zd = 1.0 / (1 / LFA.R6 + 1 / LFA.RO)
    out = []
    for y in 2j * np.pi * np.asarray(f, dtype=complex) * LFA.C10:
        Y = np.array([[1 / zd + y, -y, 0],
                      [-y, y + 1 / LFA.R10 + 1 / ra + 1 / LFA.R9, -1 / LFA.R9],
                      [0, -1 / LFA.R9, 1 / LFA.R9 + 1 / (LFA.R8 + rb)]], dtype=complex)
        out.append(np.linalg.solve(Y, np.array([0, 0, 1], dtype=complex))[2])
    return np.array(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--mode", default="dark", choices=("dark", "bright"),
                    help="BRIGHT is a free cross-check at LF: C10 sits after the stage, and below "
                         "the 1.9 kHz shelf zero every mode has Zs = R5, so it must return the "
                         "same C10. It is NOT usable for the HF fit -- the model's K0 is wrong for "
                         "this unit, and that error lives in exactly the HF band.")
    args = ap.parse_args()
    global MODE
    MODE = args.mode

    bypass_fr = None if args.self_test else P.load_fr(P.REF_BYPASS)[1]

    # one DARK capture per knob position, 7:30 excluded (see the docstring)
    picks = {}
    for path, d in C.find_captures():
        if d["unit"] != "p4" or d["mode"] != MODE or d["volume_clock"] <= 730:
            continue
        k = d["volume_clock"]
        if k not in picks or d["pad_db"] > picks[k][1]["pad_db"]:
            picks[k] = (path, d)
    if len(picks) < 2:
        sys.exit("need at least two knob positions above 7:30")

    data = {}
    for clock, (path, d) in sorted(picks.items()):
        tag = f"V{clock:04d}_pad{d['pad_db']:g}_dark"
        if args.self_test:
            path = P.render(d, f"p4_{tag}")
        f, m = P.pedal_fr(path, d, bypass_fr, undo_load=not args.self_test)
        pf, pm = P.load_fr(P.render(d, f"plug_{tag}"))
        data[clock] = dict(f=f, cap=m, plug=np.interp(f, pf, pm), x=d["volume"])
        print(f"  loaded V{clock:04d}  x={d['volume']:.3f}  (pad {d['pad_db']:g})")

    report = {"self_test": args.self_test, "knobs": {str(k): v["x"] for k, v in data.items()}}

    # --- 1. LF: is one global C10 enough, or does it need the taper? -------------------------------
    def lf_resid(p, c10=None, taper=None, rig_hp=None):
        """Residual of capture minus (plugin with one trial change), level removed, pooled.

        `rig_hp` is the competing OUT-OF-PEDAL hypothesis: an extra high-pass somewhere in the
        capture chain, which is a fixed corner in Hz and therefore KNOB-INDEPENDENT. That is what
        makes it separable from C10 here and what made it inseparable on the NAM set, where the
        three volume points were 1:1 confounded with three different rigs (circuit.md note #7 M4).
        """
        out = []
        for clock, v in data.items():
            f = v["f"]
            m = (f >= LF_BAND[0]) & (f <= LF_BAND[1])
            kw = {}
            if c10 is not None:
                kw["c10"] = c10
            x = v["x"] if taper is None else v["x"] ** (taper / LFA.TAPER_P)
            # model = plugin, with the output network swapped for the trial one
            mdl = v["plug"][m] - net_db(f[m], v["x"]) + net_db(f[m], x, **kw)
            if rig_hp is not None:
                mdl = mdl + P.hp_db(f[m], rig_hp)
            r = (v["cap"][m] - mdl)
            out.append(r - np.mean(r))
        return np.concatenate(out)

    def worst(r):
        return float(np.max(np.abs(r))), float(np.sqrt(np.mean(r ** 2)))

    rows = {}
    rows["as drawn"] = worst(lf_resid([]))
    rc = least_squares(lambda p: lf_resid([], c10=p[0] * 1e-9), [100.0], bounds=([20.0], [300.0]))
    rows[f"C10 = {rc.x[0]:.1f} nF"] = worst(rc.fun)
    rt = least_squares(lambda p: lf_resid([], taper=p[0]), [2.0], bounds=([0.5], [8.0]))
    rows[f"taper p = {rt.x[0]:.2f}"] = worst(rt.fun)
    rb = least_squares(lambda p: lf_resid([], c10=p[0] * 1e-9, taper=p[1]), [100.0, 2.0],
                       bounds=([20.0, 0.5], [300.0, 8.0]))
    rows[f"C10 = {rb.x[0]:.1f} nF + p = {rb.x[1]:.2f}"] = worst(rb.fun)
    rr = least_squares(lambda p: lf_resid([], rig_hp=p[0]), [25.0], bounds=([1.0], [200.0]))
    rows[f"rig high-pass = {rr.x[0]:.1f} Hz (OUT of pedal)"] = worst(rr.fun)
    rj = least_squares(lambda p: lf_resid([], c10=p[0] * 1e-9, rig_hp=p[1]), [100.0, 20.0],
                       bounds=([20.0, 1.0], [300.0, 200.0]))
    rows[f"C10 = {rj.x[0]:.1f} nF + rig {rj.x[1]:.1f} Hz"] = worst(rj.fun)
    report["lf_rig"] = dict(rig_hp_hz=float(rr.x[0]), joint_c10_nf=float(rj.x[0]),
                            joint_rig_hz=float(rj.x[1]))
    report["lf"] = {k: dict(worst_db=v[0], rms_db=v[1]) for k, v in rows.items()}
    report["lf_fit"] = dict(c10_nf=float(rc.x[0]), taper_p=float(rt.x[0]),
                            joint_c10_nf=float(rb.x[0]), joint_p=float(rb.x[1]))

    print(f"\nLF ({LF_BAND[0]:.0f}-{LF_BAND[1]:.0f} Hz), one GLOBAL value across "
          f"{len(data)} knob positions\n")
    print(f"   {'model':34s} {'worst dB':>9s} {'RMS dB':>8s}")
    for k, (w, r) in rows.items():
        print(f"   {k:34s} {w:9.3f} {r:8.3f}")

    # --- 2. HF: one global load capacitance? -------------------------------------------------------
    def hf_resid(cpf):
        out = []
        for clock, v in data.items():
            f = v["f"]
            m = (f >= HF_BAND[0]) & (f <= HF_BAND[1])
            load = 1.0 / (1 + 2j * np.pi * f[m] * cpf * 1e-12 * zout(f[m], v["x"]))
            r = v["cap"][m] - (v["plug"][m] + 20 * np.log10(np.abs(load)))
            out.append(r - np.mean(r))
        return np.concatenate(out)

    hf = {"as drawn (no load)": worst(hf_resid(0.0))}
    rl = least_squares(lambda p: hf_resid(p[0]), [80.0], bounds=([0.0], [2000.0]))
    hf[f"Cload = {rl.x[0]:.0f} pF"] = worst(rl.fun)
    report["hf"] = {k: dict(worst_db=v[0], rms_db=v[1]) for k, v in hf.items()}
    report["hf_fit"] = dict(cload_pf=float(rl.x[0]))
    print(f"\nHF ({HF_BAND[0]/1000:.0f}-{HF_BAND[1]/1000:.0f} kHz), one GLOBAL value, DARK only\n")
    print(f"   {'model':34s} {'worst dB':>9s} {'RMS dB':>8s}")
    for k, (w, r) in hf.items():
        print(f"   {k:34s} {w:9.3f} {r:8.3f}")
    print(f"\n   pedal Zout at 10 kHz: "
          + ", ".join(f"V{c:04d} {abs(zout([1e4], v['x'])[0])/1e3:.0f}k" for c, v in sorted(data.items())))

    json.dump(report, open(OUT.replace(".json", f"_{MODE}.json"), "w"), indent=2, default=float)
    print(f"\nwrote {OUT.replace(chr(46) + chr(106) + chr(115) + chr(111) + chr(110), '_' + MODE + chr(46) + 'json')}")


if __name__ == "__main__":
    main()
