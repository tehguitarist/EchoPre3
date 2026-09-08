#!/usr/bin/env python3
"""What shape is the plugin-versus-capture frequency-response error, and what mechanism makes it?

`goal_check.py` reports the plugin 0.5-0.67 dB dark at 4064 / 5120 / 6451 / 8127 Hz against P1, in
all three modes, and calls it a dip in the core band. It is not a dip. Printed as a whole curve it
is one continuous shape: the plugin runs high below ~1 kHz, crosses over, reaches a broad minimum
around 5 kHz, recovers, and is BRIGHTER than the capture by ~1 dB at 16 kHz. The 4-8 kHz cells are
the bottom of that curve, not a feature of their own.

⚠ THE SIGN RULES OUT THE OBVIOUS CANDIDATE, and it rules out its opposite too. P1's M3 fit put the
input pole at 6.7 kHz against the 7.3 kHz shipped, and a LOWER real corner would make the plugin
brighter at 5 kHz, not darker -- so that is not it. But raising the corner alone does not work
either: a single-pole difference is monotone in frequency and saturates, and this curve turns over.
Any mechanism has to produce a lift that peaks and then falls.

WHAT IS FITTED. The capture is modelled as the plugin times three physical terms, and the residual
of each nested model is reported so a reader can see which term is actually load-bearing:

    R(f) = 20*log10(cap/plug) = HP(f, fhp) + [L(f, fc) - L(f, 7300)] + L(f, fx) + c

    fhp  a high-pass the capture has and the plugin does not (the known LF story: the C10/VOLUME
         corner, circuit.md note #7's M4, confounded across units and NOT to be fitted into the model)
    fc   the capture's input low-pass, against the 7300 Hz the plugin has by construction
    fx   one extra low-pass in the capture's chain (note #7 fitted 30.2 kHz = 52 pF for P1)
    c    a level offset, since kOutputMakeup is unanchored and every comparison here reads SHAPE

⚠⚠ KNOWN-ANSWER FIRST (circuit.md note #9). `--self-test` puts a PLUGIN render where the capture
goes. The fit must then return fc = 7300 Hz, fx and fhp off the ends of their ranges, and a residual
at the numerical floor. A capture cannot tell you its estimator is blind; a render whose truth you
set by construction can, and on this project that check has already overturned a published result.

Run from the repo root:
    .venv/bin/python analysis/hf_shape_fit.py [--os 8] [--self-test]
Writes analysis/reports/hf_shape_fit.json
"""
import argparse, json, os, subprocess, sys, tempfile
from datetime import datetime, timezone

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C

OUTPUT_JSON = "analysis/reports/hf_shape_fit.json"
# The plugin's input low-pass, by construction: R3 || R4 = 99.0 k into C3 = 220 pF.
# InputNetwork.h is the authority; this is the number the fit has to recover in --self-test.
PLUGIN_INPUT_POLE_HZ = 1.0 / (2.0 * np.pi * (110e3 * 1e6 / 1.11e6) * 220e-12)
FIT_LO, FIT_HI = 25.0, 17000.0
# Bounds, in Hz. fhp is allowed to run to its ends, which is how the fit says "not needed".
#
# ⚠⚠ fc AND fx GET THE SAME RANGE, AND THE FIRST VERSION OF THIS SCRIPT DID NOT. It floored fx at
# 8 kHz on the reasoning that the "extra" pole would sit above the pedal's own -- and P2's fit then
# sat exactly on that bound in all three modes, reporting 3.1 kHz + 8 kHz(bound) at a 2.1 dB
# residual. Freed, the same data returns 3.2 kHz + 7.4 kHz at 0.02 dB: the pedal's own input pole,
# recovered from a capture with no prior. A parameter resting on its bound is not a fit, and the
# bound had encoded an assumption about which pole was which that the algebra does not support --
# the two terms enter the model identically.
BOUNDS = {"fhp": (1.0, 200.0), "fc": (2000.0, 400000.0), "fx": (2000.0, 400000.0)}


def lp_db(f, fp):
    """Magnitude of a first-order low-pass, in dB."""
    return -10.0 * np.log10(1.0 + (f / fp) ** 2)


def hp_db(f, fp):
    """Magnitude of a first-order high-pass, in dB."""
    return 10.0 * np.log10(1.0 / (1.0 + (fp / f) ** 2))


def model(f, fhp, fc, fx, c):
    return hp_db(f, fhp) + lp_db(f, fc) - lp_db(f, PLUGIN_INPUT_POLE_HZ) + lp_db(f, fx) + c


def fit(f, r, free):
    """Least-squares fit of `model` to r(f), with the terms not in `free` pinned to inert values.

    Pinned values are the ones that make their term vanish: fhp -> 0 (no high-pass), fc -> the
    plugin's own pole (no difference), fx -> infinity (no extra pole)."""
    inert = {"fhp": 1e-6, "fc": PLUGIN_INPUT_POLE_HZ, "fx": 1e12}
    names = [n for n in ("fhp", "fc", "fx") if n in free]
    x0 = [np.log(np.sqrt(BOUNDS[n][0] * BOUNDS[n][1])) for n in names] + [0.0]
    lo = [np.log(BOUNDS[n][0]) for n in names] + [-20.0]
    hi = [np.log(BOUNDS[n][1]) for n in names] + [20.0]

    def resid(x):
        p = dict(inert)
        for n, v in zip(names, x[:-1]):
            p[n] = float(np.exp(v))
        return model(f, p["fhp"], p["fc"], p["fx"], x[-1]) - r

    sol = least_squares(resid, x0, bounds=(lo, hi))
    p = dict(inert)
    for n, v in zip(names, sol.x[:-1]):
        p[n] = float(np.exp(v))
    p["c"] = float(sol.x[-1])
    p["rms"] = float(np.sqrt(np.mean(sol.fun ** 2)))
    p["worst"] = float(np.max(np.abs(sol.fun)))
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--residuals", action="store_true",
                    help="print the best model's per-band residual, to show whether it is structured")
    args = ap.parse_args()

    orig = A.load(A.ORIG)
    ref_seg = A.seg_of(orig, "sweep_clean", settled=False)
    tmp = tempfile.mkdtemp(prefix="hfshape_")
    print(f"HF shape fit | OS {args.os}x | plugin input pole {PLUGIN_INPUT_POLE_HZ:.0f} Hz "
          f"(by construction)")
    if args.self_test:
        print("  ⚠ SELF-TEST: a plugin render stands in for the capture. fc must come back at the")
        print("    pole above, fhp and fx must run to their inert ends, residual at the floor.")

    results = {}
    for path, parsed in C.find_captures():
        name = os.path.splitext(os.path.basename(path))[0]
        out = os.path.join(tmp, name + ".wav")
        subprocess.run([args.bin, A.ORIG, out, "--os", str(args.os)] + C.render_args(parsed),
                       check=True, capture_output=True)
        ren, _ = A.align(A.load(out), orig)
        if args.self_test:
            cap = ren
        else:
            cap, _ = A.align(C.load_capture(path), orig)

        f, mc, nc = A.band_fr(A.seg_of(cap, "sweep_clean", settled=False), ref_seg, frac=3)
        _, mr, nr = A.band_fr(A.seg_of(ren, "sweep_clean", settled=False), ref_seg, frac=3)
        # band_average interpolates empty bands; never fit one.
        keep = (nc > 0) & (nr > 0) & (f >= FIT_LO) & (f <= FIT_HI)
        fv = f[keep]
        r = (mc - mr)[keep]                      # capture MINUS plugin: what the capture has extra

        # ⚠ fc and fx enter the model identically (two first-order low-passes in cascade), so the
        # last two rows do NOT separately identify "the input pole" from "an extra pole" -- they say
        # how many poles the capture's HF needs, and where. The middle two rows are the nested
        # one-pole models, and the gap between them and the last row is the whole finding.
        models = {
            "offset only":        fit(fv, r, ()),
            "+ LF high-pass":     fit(fv, r, ("fhp",)),
            "+ move input pole":  fit(fv, r, ("fhp", "fc")),
            "+ one EXTRA pole":   fit(fv, r, ("fhp", "fx")),
            "+ two HF poles":     fit(fv, r, ("fhp", "fc", "fx")),
        }
        results[name] = {"unit": parsed["unit"], "mode": parsed["mode"],
                         "freqs": [float(x) for x in fv], "capture_minus_plugin_db": [float(x) for x in r],
                         "models": models}

        print(f"\n--- {name} ---")
        print(f"{'model':>18} {'fhp Hz':>8} {'lower pole':>11} {'upper pole':>11} {'offset':>8} "
              f"{'RMS dB':>8} {'worst':>7}")
        for k, p in models.items():
            # Sorted: fc and fx are interchangeable, so "which is which" is not a fitted fact.
            lo_p, hi_p = sorted((p["fc"], p["fx"]))
            print(f"{k:>18} {p['fhp']:>8.1f} {lo_p:>11.0f} "
                  f"{hi_p:>11.0f} {p['c']:>+8.2f} {p['rms']:>8.3f} {p['worst']:>7.3f}")
        if args.residuals:
            best = models["+ two HF poles"]
            res = r - model(fv, best["fhp"], best["fc"], best["fx"], best["c"])
            print("   residual of the best model, dB per 1/3-oct band:")
            print("   " + "  ".join(f"{x:.0f}:{y:+.2f}" for x, y in zip(fv, res)))

    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json.dump({"generated": datetime.now(timezone.utc).isoformat(), "os_factor": args.os,
               "self_test": bool(args.self_test),
               "plugin_input_pole_hz": float(PLUGIN_INPUT_POLE_HZ),
               "fit_band_hz": [FIT_LO, FIT_HI], "captures": results},
              open(OUTPUT_JSON, "w"), indent=2)
    print(f"\nwrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
