#!/usr/bin/env python3
"""Measure the pedal's OUTPUT IMPEDANCE directly, from a pair of captures into different loads.

    .venv/bin/python analysis/output_impedance.py [--self-test]

⭐⭐ THIS QUANTITY HAS NEVER BEEN MEASURED. circuit.md stage 3 computes it at 92-139 kOhm and three
separate things rest on that number: the VOLUME network's control law, the derived `ro` = 1.44 MOhm,
and the interface-loading correction applied to every capture in note #21. All of it was model, not
measurement, until this pair of takes.

THE METHOD, and why the unknown line-input gain does not matter. The line input needed its gain
maxed to give a usable signal, so its gain G and its impedance Zl are BOTH unknown -- and G is
confounded with the loading drop being measured. A bypassed capture through the same line input at
the same gain breaks that: bypass presents the reamp's near-zero output impedance, so the line
input's loading does nothing to it and that take measures G alone.

    bypass_line / bypass_inst                 = G                    (loading cancels: Zsrc ~ 0)
    pedal_line  / pedal_inst                  = G * L(Zout, Zl)
    => R(x) = (pedal_line/pedal_inst) / (bypass_line/bypass_inst) = L(Zout(x), Zl)

with  L = [Zl/(Zl+Zout)] * [(Zi+Zout)/Zi],  Zi = 1 MOhm (the instrument input, confirmed).
G has cancelled completely, so the maxed gain costs nothing.

⭐ AND IT IS OVERDETERMINED, which is the whole reason two knob positions were captured. Zl is
common to both takes while Zout(x) is not (102 kOhm at 10:30 against 59 kOhm at full CW). So fitting
ONE global Zl across BOTH positions and every frequency tests the impedance model against itself:
if a single Zl fits, the model's Zout is right; if each position needs its own, it is not. Same
discriminator that separated C10 from the taper in note #21.
"""
import argparse, json, os, sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import probe_analyse as PA
import captures as C
import lf_pole_attribution as LFA

D = "analysis/captures/probe"
ZI = 1.0e6                       # instrument input, confirmed 1 MOhm
OUT = "analysis/reports/output_impedance.json"
BAND = (100.0, 12000.0)
PAIRS = [("p4_V1030_dark_pad4p5", 0.35), ("p4_V1700_dark_pad1p5", 1.00)]
BYPASS = "p4_V1030_bypass"


def zout_model(f, x):
    """circuit.md stage 3's output impedance: drive node OUT with 1 A, read the volts."""
    ra = LFA.POT * x ** LFA.TAPER_P
    rb = LFA.POT - ra
    zd = 1.0 / (1 / LFA.R6 + 1 / LFA.RO)
    out = []
    for w in 2j * np.pi * np.asarray(f, dtype=complex) * LFA.C10:
        Y = np.array([[1 / zd + w, -w, 0],
                      [-w, w + 1 / LFA.R10 + 1 / ra + 1 / LFA.R9, -1 / LFA.R9],
                      [0, -1 / LFA.R9, 1 / LFA.R9 + 1 / (LFA.R8 + rb)]], dtype=complex)
        out.append(np.linalg.solve(Y, np.array([0, 0, 1], dtype=complex))[2])
    return np.array(out)


def sweep_db(path, fs, segs, ref):
    """Magnitude vs frequency from the tone ladder's quietest cells -- linear, well clear of the
    load line. The IMD and loud tone cells are deliberately NOT used: this is a LINEAR measurement
    and a compressing cell would report the pedal's gain change as a loading effect."""
    cap = PA.load(path, fs)
    lag = PA.align(cap, ref)
    out = {}

    def take(seg, f):
        c = PA.cell(cap, segs[seg], fs, lag)
        r = PA.cell(ref, segs[seg], fs)
        out.setdefault(f, []).append(PA.db(PA.amp(c, f, fs)) - PA.db(PA.amp(r, f, fs)))

    # every discrete tone the signal carries: the ladder's two, both cal tones, and the IMD pair.
    # Only QUIET cells -- this is a LINEAR measurement, and a compressing cell would report the
    # pedal's own gain change as a loading effect.
    for k in segs:
        if k.endswith("_repeat"):
            continue
        if k.startswith("tone_"):
            f, d = float(k.split("_")[1]), int(k.split("_")[2])
            if d <= -18:
                take(k, f)
        elif k == "cal_1k":
            take(k, 1000.0)
        elif k == "cal_220":
            take(k, 220.0)
        elif k.startswith("imd_"):
            if int(k.split("_")[1]) <= -16:
                take(k, 611.0)
    return {f: float(np.mean(v)) for f, v in out.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true",
                    help="synthesise the line takes from the model at a known Zl and recover it")
    a = ap.parse_args()
    m = json.load(open(PA.META))
    fs, segs = m["fs"], m["segments"]
    ref = PA.load(PA.SIG, fs)

    ZL_TRUE, G_TRUE = 10.7e3, -13.4        # self-test only
    data = {}
    for name, x in PAIRS + [(BYPASS, None)]:
        ipath = f"{D}/{name}.wav"
        if not os.path.exists(ipath) and name == BYPASS:
            # ⭐ No probe capture of the BYPASSED pedal through the INSTRUMENT input exists, but the
            # main-signal one does -- and the bypass path is linear and time-invariant, so its
            # frequency response is the same measured through any signal. Sample the main-signal
            # capture's Farina response at the probe's tone frequencies.
            import p4_corners as P4
            fm, mm = P4.load_fr("analysis/captures/p4_V1030_bypass.wav")
            want = {float(k.split("_")[1]) for k in segs
                    if k.startswith("tone_") and not k.endswith("_repeat")}
            want |= {220.0, 611.0, 1000.0}          # the cal tones and the IMD pair
            inst = {f: float(np.interp(f, fm, mm)) for f in sorted(want)}
        else:
            inst = sweep_db(ipath, fs, segs, ref)
        if a.self_test:
            line = {}
            for f, v in inst.items():
                z = zout_model([f], x)[0] if x is not None else 0.0
                L = (ZL_TRUE / (ZL_TRUE + z)) * ((ZI + z) / ZI)
                line[f] = v + G_TRUE + 20 * np.log10(abs(L))
        else:
            line = sweep_db(f"{D}/{name}_take2.wav", fs, segs, ref)
        data[name] = (inst, line, x)

    bi, bl, _ = data[BYPASS]
    freqs = sorted(f for f in bi if BAND[0] <= f <= BAND[1])
    G = np.array([bl[f] - bi[f] for f in freqs])
    print(f"line-input gain G, from the BYPASS pair (its loading cancels): "
          f"{np.mean(G):+.2f} dB, flat to {np.std(G):.3f} dB over {len(freqs)} tones\n")

    def resid(p):
        zl = p[0] * 1e3
        out = []
        for name, x in PAIRS:
            inst, line, _ = data[name]
            for i, f in enumerate(freqs):
                z = zout_model([f], x)[0]
                L = (zl / (zl + z)) * ((ZI + z) / ZI)
                out.append((line[f] - inst[f] - G[i]) - 20 * np.log10(abs(L)))
        return np.array(out)

    # ⭐⭐ A ROUTE THAT NEEDS NO BYPASS AND NO GAIN AT ALL. G is common to both knob positions, so
    # the RATIO of the two positions' line-to-instrument ratios cancels it exactly. One equation,
    # one unknown (Zl), using only the two pedal takes -- immune to the maxed line gain and to any
    # cross-signal assumption in the bypass reference.
    def resid_gfree(p):
        zl_ = p[0] * 1e3
        (n1, x1), (n2, x2) = PAIRS
        i1, l1, _ = data[n1]
        i2, l2, _ = data[n2]
        out = []
        for f in freqs:
            z1, z2 = zout_model([f], x1)[0], zout_model([f], x2)[0]
            L1 = (zl_ / (zl_ + z1)) * ((ZI + z1) / ZI)
            L2 = (zl_ / (zl_ + z2)) * ((ZI + z2) / ZI)
            out.append(((l1[f] - i1[f]) - (l2[f] - i2[f])) - 20 * np.log10(abs(L1 / L2)))
        return np.array(out)

    rg = least_squares(resid_gfree, [10.0], bounds=([0.5], [2000.0]))
    print(f"GAIN-FREE route (the two knob positions' ratio; G cancels, no bypass used):")
    print(f"   Zl = {rg.x[0]:.2f} kOhm     worst {np.max(np.abs(rg.fun)):.3f} dB, "
          f"RMS {np.sqrt(np.mean(rg.fun**2)):.3f} dB\n")

    r = least_squares(resid, [10.0], bounds=([0.5], [2000.0]))
    zl_k = float(r.x[0])
    zl = zl_k * 1e3          # OHMS from here on -- the fit works in kOhm and mixing the two
                             # silently returned 0.1 kOhm for a 101.8 kOhm known answer
    print(f"ONE GLOBAL line-input impedance across BOTH knob positions and {len(freqs)} tones:")
    print(f"   Zl = {zl_k:.2f} kOhm     worst residual {np.max(np.abs(r.fun)):.3f} dB, "
          f"RMS {np.sqrt(np.mean(r.fun**2)):.3f} dB")
    if a.self_test:
        print(f"   [self-test: true Zl was {ZL_TRUE/1e3:.2f} kOhm -> error "
              f"{100*(zl-ZL_TRUE)/ZL_TRUE:+.2f} %]")

    print("\nPER-POSITION fit (a single Zl must serve BOTH; needing different values would mean")
    print("the model's Zout is wrong, not that the load is):")
    per = {}
    for name, x in PAIRS:
        rr = least_squares(lambda p, n=name, xx=x: _one(p, n, xx, data, freqs, G),
                           [10.0], bounds=([0.5], [2000.0]))
        per[name] = float(rr.x[0])
        zm = abs(zout_model([1000.0], x)[0])
        print(f"   {name:30s} Zl = {rr.x[0]:7.2f} k   (model Zout at 1 kHz {zm/1e3:6.1f} k)"
              f"   resid {np.sqrt(np.mean(rr.fun**2)):.3f} dB")
    spread = max(per.values()) / min(per.values())
    print(f"\n   spread between positions: {spread:.3f}x  "
          + ("-> ONE load fits both; the Zout model is corroborated"
             if spread < 1.15 else "-> the two disagree; the Zout model is NOT corroborated"))

    print("\nMEASURED Zout, back-solved per position at the fitted Zl:")
    for name, x in PAIRS:
        inst, line, _ = data[name]
        zs = []
        for i, f in enumerate(freqs):
            d = 10 ** ((line[f] - inst[f] - G[i]) / 20.0)
            # d = (zl/(zl+z)) * ((ZI+z)/ZI) is LINEAR in z once cleared, not quadratic:
            #   d*ZI*(zl+z) = zl*(ZI+z)  ->  z*(d*ZI - zl) = zl*ZI*(1-d)
            # The quadratic form written first returned -0.0 kOhm on a self-test whose answer was
            # known by construction, which is exactly what that self-test is for.
            den = d * ZI - zl
            zs.append(zl * ZI * (1.0 - d) / den if abs(den) > 1e-9 else np.nan)
        zs = np.array(zs)
        band = [i for i, f in enumerate(freqs) if 200 <= f <= 5000]
        print(f"   {name:30s} {np.mean(zs[band])/1e3:6.1f} k measured  vs "
              f"{abs(zout_model([1000.0], x)[0])/1e3:6.1f} k modelled   "
              f"({100*(np.mean(zs[band])/abs(zout_model([1000.0],x)[0])-1):+.1f} %)")

    json.dump(dict(zl_kohm=zl_k, per_position=per, gain_db=float(np.mean(G))),
              open(OUT, "w"), indent=2, default=float)
    print(f"\nwrote {OUT}")


def _one(p, name, x, data, freqs, G):
    zl = p[0] * 1e3
    inst, line, _ = data[name]
    out = []
    for i, f in enumerate(freqs):
        z = zout_model([f], x)[0]
        L = (zl / (zl + z)) * ((ZI + z) / ZI)
        out.append((line[f] - inst[f] - G[i]) - 20 * np.log10(abs(L)))
    return np.array(out)


if __name__ == "__main__":
    main()
