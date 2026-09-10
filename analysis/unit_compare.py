#!/usr/bin/env python3
"""How the owner's pedal (P4) compares to the three NAM-modelled units, on the ONE axis that can
carry a unit-to-unit comparison at all.

    .venv/bin/python analysis/unit_compare.py

⭐⭐ THE MODE DIFFERENTIAL IS THE ONLY HONEST COMPARISON IN THIS DATASET, and circuit.md note #7's
M6 says so with numbers: the ABSOLUTE P1-vs-P2 response spans 13.8 dB at 18 kHz, which is the P2
rig, not two pedals -- three units means three rigs, and P4 adds a fourth. Within one unit the
modes differ in exactly one component, so rig gain, converter response, cable poles, the interface
loading and the unit's own everything-else all cancel in the ratio. M6's measured spread on that
differential is 0.33 dB RMS / 0.80 dB peak, and that IS the project's tolerance band.

⚠ P4 IS A TWO-POSITION VARIANT (BRIGHT + DARK, no MID), and its bypass cap is UNKNOWN -- a
different circuit variant, so it must not be assumed to be P1/P2's 22 nF. The comparison here is
therefore P4's bright-minus-dark against P1's and P2's bright-minus-dark, and the question it
answers is whether the owner's BRIGHT is the same physical branch. If P4's shelf zero lands near
1.86 kHz it is the 22 nF branch; near 4.17 kHz it is the 10 nF one, and circuit.md note #2's label
reasoning will have failed a second way.

⚠⚠ P2 IS POLARITY-INVERTED (note #9b) and that is INVISIBLE in a magnitude differential, which is
why it is usable here at all despite being disqualified for absolute response, polarity and its own
cable pole. A differential of two inverted captures is not inverted.
"""
import json, os, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import p4_corners as P

OUT = "analysis/reports/unit_compare.json"
BAND = (150.0, 18000.0)     # the shelf's own fit band (note #2)
M6_RMS, M6_PEAK = 0.33, 0.80   # the measured two-unit tolerance, for reference


def differential(unit_files, hi="bright", lo="dark"):
    """(f, hi-minus-lo in dB) -- rig-cancelling, so no deconvolution and no loading correction."""
    fh, mh = P.load_fr(unit_files[hi])
    fl, ml = P.load_fr(unit_files[lo])
    return fh, mh - np.interp(fh, fl, ml)


def main():
    caps = C.find_captures()
    byunit = {}
    for path, d in caps:
        # one take per (unit, mode): prefer the LARGEST pad, i.e. the most headroom and least
        # chance of the shelf being measured through any compression at all.
        k = (d["unit"], d["volume_clock"], d["mode"])
        prev = byunit.get(k)
        if prev is None or d["pad_db"] > prev[1]["pad_db"]:
            byunit[k] = (path, d)

    units = {}
    for (unit, clock, mode), (path, d) in byunit.items():
        units.setdefault((unit, clock), {})[mode] = path

    report, curves = {}, {}
    for (unit, clock), files in sorted(units.items()):
        if "bright" not in files or "dark" not in files:
            continue
        f, diff = differential(files)
        fit = P.fit_shelf(f, diff)
        m = (f >= BAND[0]) & (f <= BAND[1])
        curves[f"{unit}_V{clock:04d}"] = (f[m], diff[m])
        report[f"{unit}_V{clock:04d}"] = dict(
            unit=unit, volume_clock=clock, **fit,
            implied_cap_nf=1e9 * (1.0 / (2 * np.pi * fit["fz_hz"] * 3600.0)),
        )

    # pairwise agreement on the differential CURVE, level-normalised (M6's statistic)
    keys = sorted(curves)
    pair = {}
    grid = np.geomspace(*BAND, 400)
    interp = {k: np.interp(grid, *curves[k]) for k in keys}
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            r = interp[a] - interp[b]
            r = r - np.mean(r)
            pair[f"{a} vs {b}"] = dict(rms_db=float(np.sqrt(np.mean(r ** 2))),
                                       peak_db=float(np.max(np.abs(r))))

    json.dump(dict(shelves=report, pairwise=pair), open(OUT, "w"), indent=2, default=float)

    print("MODE SHELF (bright minus dark), fitted per unit -- rig-cancelling\n")
    print(f"  {'capture':16s} {'zero Hz':>9s} {'K0':>6s} {'plateau dB':>11s} {'resid dB':>9s}  implied C")
    for k, v in sorted(report.items()):
        print(f"  {k:16s} {v['fz_hz']:9.1f} {v['k0']:6.2f} "
              f"{20 * np.log10(v['k0']):11.2f} {v['residual_db']:9.3f}  {v['implied_cap_nf']:6.1f} nF")

    print("\nPAIRWISE AGREEMENT on the differential curve (level removed), 150 Hz - 18 kHz")
    print(f"  M6's measured two-unit band: {M6_RMS:.2f} dB RMS / {M6_PEAK:.2f} dB peak\n")
    for k, v in sorted(pair.items(), key=lambda kv: kv[1]["rms_db"]):
        flag = "ok " if v["rms_db"] <= M6_RMS and v["peak_db"] <= M6_PEAK else "OVER"
        print(f"  {flag} {k:40s} {v['rms_db']:6.3f} dB RMS  {v['peak_db']:6.3f} dB peak")
    print(f"\nwrote {OUT}")


if __name__ == "__main__":
    main()
