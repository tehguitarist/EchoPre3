#!/usr/bin/env python3
"""The VOLUME sweep -- the measurement this project has been blocked on since it began.

    .venv/bin/python analysis/volume_sweep.py [--self-test] [--mode dark|bright]

⭐⭐ WHAT THIS SETTLES, all of which were explicitly blocked on "a WITHIN-RIG volume sweep":
  * the taper exponent `p` (shipped 2.0; circuit.md note #20a's P1 and P3 both wanted 2.4-2.7)
  * the 3.92 dB as-drawn peak-to-full-CW fall-back against the maker's stated 1-2 dB (note #1's
    open discrepancy, invariant to every assumption tested and never explained)
  * the position of the volume peak (predicted Ra = 176 k = 35.3 % of the pot, i.e. 1-2 o'clock)
  * the C10 corner, whose three NAM points were 1:1 confounded with unit AND trainer (note #7 M4)

Every earlier attempt failed for ONE reason: volume was confounded with unit and rig, because the
three NAM captures were three pedals recorded by three people. This is one pedal, one rig, one
session, one calibration -- so the knob is finally the only thing varying.

⚠⚠ LEVELS ARE IN VOLTS OUT PER VOLT IN, not in dBFS. The play side (+12.20 dBu) and the record side
(+14.29 dBu) are NOT the same, so a capture's digital gain is 2.090 dB away from the pedal's actual
voltage gain. Getting that backwards is a 4.2 dB error in the control law. absolute_gain.py carries
the derivation and the bypass capture's known answer that verifies it.

⛔ 7:30 IS EXCLUDED FROM THE TAPER FIT. At Ra = 1.25 kOhm the network sits on its steepest slope, so
knob-setting error dominates: its two takes fit LF corners 8.9 % apart, where 9:00's two takes agree
to 0.1 %. CLAUDE.md predicted this for the LEVEL and it is true of the CORNER as well.
"""
import argparse, json, os, sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import p4_corners as P
import lf_pole_attribution as LFA
from absolute_gain import VOLTS_CORR, midband_db

OUT = "analysis/reports/volume_sweep.json"


def x_to_clock(x):
    mins = int(round((7.0 + 10.0 * float(x)) * 60))   # round FIRST, then split -- 12:60 otherwise
    return f"{mins // 60:d}:{mins % 60:02d}"


def network_db(x, p=2.0, c10=LFA.C10, f=1000.0):
    ra = LFA.POT * np.asarray(x, dtype=float) ** p
    out = []
    for r in np.atleast_1d(ra):
        out.append(np.abs(LFA.out_network([f], np.nan, c10=c10)[0]) if False else None)
    # out_network takes x and applies its own taper; call it with the equivalent x for THIS p
    xs = (np.atleast_1d(ra) / LFA.POT) ** (1.0 / LFA.TAPER_P)
    return np.array([20 * np.log10(np.abs(LFA.out_network([f], xi, c10=c10)[0])) for xi in xs])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--mode", default="dark", choices=("dark", "bright"))
    args = ap.parse_args()

    bypass_fr = None if args.self_test else P.load_fr(P.REF_BYPASS)[1]

    picks = {}
    for path, d in C.find_captures():
        if d["unit"] != "p4" or d["mode"] != args.mode:
            continue
        k = d["volume_clock"]
        if k not in picks or d["pad_db"] > picks[k][1]["pad_db"]:
            picks[k] = (path, d)

    rows = []
    for clock, (path, d) in sorted(picks.items()):
        tag = f"V{clock:04d}_pad{d['pad_db']:g}_{args.mode}"
        if args.self_test:
            path = P.render(d, f"p4_{tag}")
        f, m = P.pedal_fr(path, d, bypass_fr, undo_load=not args.self_test)
        pf, pm = P.load_fr(P.render(d, f"plug_{tag}"))
        gain = midband_db(f, m) + d["pad_db"] + (0.0 if args.self_test else VOLTS_CORR)
        plug = midband_db(pf, pm) + d["pad_db"]
        rows.append(dict(clock=clock, x=d["volume"], pad=d["pad_db"],
                         pedal_db=gain, plugin_db=plug, delta_db=gain - plug,
                         lf_hz=P.fit_lf(f, m)["fc_hz"],
                         lf_resid=P.fit_lf(f, m)["residual_db"]))
        print(f"  V{clock:04d} x={d['volume']:.3f}  pedal {gain:+7.3f} dB   plugin {plug:+7.3f} dB"
              f"   delta {gain - plug:+7.3f}   LF {rows[-1]['lf_hz']:6.2f} Hz")

    xs = np.array([r["x"] for r in rows])
    lv = np.array([r["pedal_db"] for r in rows])

    # --- fit the control law: taper p and C10, level free ------------------------------------------
    fit_m = xs > 0.1     # 7:30 excluded (docstring)

    def resid(q):
        mdl = network_db(xs[fit_m], p=q[0], c10=q[1] * 1e-9)
        r = lv[fit_m] - mdl
        return r - np.mean(r)

    # ⚠ C10 is DEGENERATE in a midband control law -- it only shapes the bottom, so a 1 kHz fit
    # returns whatever it started from at an identical residual (--self-test: 63.9 nF, 0.000 dB).
    # The taper is fitted here; C10 belongs to p4_component_fit.py, which fits the LF BAND.
    r1 = least_squares(lambda q: resid([q[0], LFA.C10 * 1e9]), [2.0], bounds=([0.5], [8.0]))
    p_fit, c10_fit = float(r1.x[0]), LFA.C10 * 1e9
    r2 = r1

    def stats(rr):
        return float(np.max(np.abs(rr))), float(np.sqrt(np.mean(rr ** 2)))

    print(f"\nCONTROL LAW, {int(fit_m.sum())} knob positions (7:30 excluded)\n")
    print(f"   {'model':34s} {'worst dB':>9s} {'RMS dB':>8s}")
    for lbl, rr in ((f"as drawn (p = {LFA.TAPER_P}, C10 = 100 nF)", resid([LFA.TAPER_P, 100.0])),
                    (f"p = {r1.x[0]:.2f} (fitted)", r1.fun)):
        w, s = stats(rr)
        print(f"   {lbl:34s} {w:9.3f} {s:8.3f}")

    # --- peak position and fall-back, measured -----------------------------------------------------
    grid = np.linspace(0.02, 1.0, 400)
    for lbl, p_, c_ in (("as drawn", LFA.TAPER_P, LFA.C10), ("fitted", p_fit, c10_fit * 1e-9)):
        mdl = network_db(grid, p=p_, c10=c_)
        i = int(np.argmax(mdl))
        print(f"\n   {lbl}: peak at x = {grid[i]:.3f} ({x_to_clock(grid[i])}), "
              f"Ra = {LFA.POT * grid[i] ** p_ / 1e3:.0f} k;  "
              f"fall-back to full CW = {mdl[i] - mdl[-1]:.2f} dB")
    imax = int(np.argmax(lv))
    end = "full CW" if xs[-1] > 0.97 else f"V{rows[-1]['clock']:04d} (NOT full CW -- "
    print(f"\n   MEASURED: peak at V{rows[imax]['clock']:04d} ({x_to_clock(xs[imax])}), "
          f"{lv[imax]:+.2f} dB;  {end}{'' if xs[-1] > 0.97 else 'the sweep stops short)'} "
          f"{lv[-1]:+.2f} dB;  fall-back so far = {lv[imax] - lv[-1]:.2f} dB")
    print("   (maker's published copy: peak +3 dB at 1-2 o'clock, 3-5 o'clock +1..+2 dB, "
          "i.e. 1-2 dB of fall-back)")

    json.dump(dict(mode=args.mode, rows=rows, taper_p=p_fit, c10_nf=c10_fit,
                   taper_p_c10_fixed=float(r1.x[0])), open(OUT.replace(".json", f"_{args.mode}.json"), "w"),
              indent=2, default=float)
    print(f"\nwrote {OUT.replace('.json', f'_{args.mode}.json')}")


if __name__ == "__main__":
    main()
