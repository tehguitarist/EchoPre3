#!/usr/bin/env python3
"""Is the captures' extra LOW-FREQUENCY pole a LINEAR element, and what ORDER is it?

circuit.md notes #19/#19a establish that every capture has ~20-30 Hz of high-pass the plugin does
not, and that the three VOLUME settings cannot say whether it lives in the pedal or ahead of it
(pinning C10 as drawn costs 0.041 dB). Two questions are still answerable WITHOUT new captures, and
neither needs the level calibration that blocks everything else:

  TEST 1 -- LEVEL INVARIANCE.  The signal carries four full-range sweeps at -41/-26/-16/-6 dBFS.
      Measure capture-minus-plugin at each, so the pedal's own compression cancels. A resistor and
      a capacitor do not know the signal level, so a LINEAR pole (the pedal's own C10/volume
      network, or an RC anywhere in the capture chain) must give the SAME corner at all four. A
      transformer's LF response moves with flux, i.e. with level and inversely with frequency, and
      a neural model's LF error has no obligation to be level-invariant at all.

  TEST 2 -- SHAPE/ORDER.  A single RC is exactly first order (6 dB/octave, +45 deg at fc). A transformer
      rolls off faster than first order, and two cascaded RCs would too. The sweeps start at 10 Hz,
      which is BELOW every fitted corner, so there is real data under the knee to see this in.
      ⚠⚠ THE COMPARISON IS AGAINST THE PLUGIN, WHICH HAS ITS OWN LF HIGH-PASS, so the difference of
      two first-order poles is ALREADY a first-order SHELF -- flat above, levelling off below at
      20*log10(fc_plugin/fc_capture). A shelf beating a lone pole is therefore the EXPECTED result
      and is NOT evidence of anything exotic; the shelf's POLE is the capture's own corner and its
      ZERO should track the plugin's, which moves with VOLUME. Read it that way round.

⚠⚠ KNOWN-ANSWER FIRST (circuit.md note #9, this project's standing rule). --self-test puts a PLUGIN
render where the capture goes. The plugin's LF is a pair of linear RCs by construction, so the
self-test MUST return level-invariant corners and order ~1. If it does not, the instrument is
measuring itself and no capture result from it means anything.

⚠ The -6 dBFS sweep drives the pedal into audible distortion, so its raw transfer carries harmonic
contamination. That contaminates both sides equally only if the plugin distorts identically, which
it does not (kInputRef and Vov are unsettled). Its row is reported but flagged; the -41/-26/-16
rows span 25 dB with the pedal essentially clean and are the load-bearing ones.

Run from the repo root:
    .venv/bin/python analysis/lf_mechanism_probe.py [--os 8] [--self-test]
Writes analysis/reports/lf_mechanism_probe.json
"""
import argparse, json, os, subprocess, sys, tempfile
from datetime import datetime, timezone

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C

OUTPUT_JSON = "analysis/reports/lf_mechanism_probe.json"
LF_LO, LF_HI = 11.0, 300.0        # under every fitted corner, up to where the LF term has died
NORM_LO, NORM_HI = 300.0, 3000.0  # midband anchor: removes level offset AND the pedal's compression
DIRTY_LEVEL_DB = -6               # the one sweep where the pedal is audibly distorting


def hp_db(f, fc, order):
    """|H| of an order-N high-pass with all poles at fc, in dB."""
    return order * 20.0 * np.log10(np.abs(1j * f / (1j * f + fc)))


def shelf_db(f, fz, fp):
    """First-order shelf. Fitted to capture-minus-plugin, fz is the PLUGIN's own LF corner (which
    moves with VOLUME) and fp is the CAPTURE's; the depth is 20*log10(fz/fp)."""
    return 20.0 * np.log10(np.abs((1j * f + fz) / (1j * f + fp)))


def fit_shapes(f, r):
    """Nested shape comparison at one level: is the extra loss one pole, two, or a shelf?"""
    out = {}
    for name, fn, x0, lo, hi in (
            ("1-pole", lambda f, fc: hp_db(f, fc, 1.0), [22.0], [2.0], [200.0]),
            ("2-pole", lambda f, fc: hp_db(f, fc, 2.0), [22.0], [2.0], [200.0]),
            ("shelf",  shelf_db, [5.0, 25.0], [0.5, 3.0], [300.0, 400.0])):
        s = least_squares(lambda x: (lambda m: r - m - np.mean(r - m))(fn(f, *x)),
                          x0, bounds=(lo, hi))
        out[name] = dict(params=[float(v) for v in s.x],
                         rms_db=float(np.sqrt(np.mean(s.fun ** 2))))
    fz, fp = out["shelf"]["params"]
    out["shelf"]["depth_db"] = float(20.0 * np.log10(fz / fp))
    out["shelf"]["capture_corner_hz"] = fp
    out["shelf"]["implied_plugin_corner_hz"] = fz
    return out


def fit_pole(f, r, free_order):
    """Fit r(f) ~ hp(f, fc, order) + c. Returns (fc, order, rms, worst)."""
    def resid(x):
        fc, order = 10.0 ** x[0], (x[1] if free_order else 1.0)
        m = hp_db(f, fc, order)
        return r - m - np.mean(r - m)
    x0 = [np.log10(22.0)] + ([1.0] if free_order else [])
    lo = [np.log10(2.0)] + ([0.3] if free_order else [])
    hi = [np.log10(200.0)] + ([4.0] if free_order else [])
    s = least_squares(resid, x0, bounds=(lo, hi))
    fc = 10.0 ** s.x[0]
    order = s.x[1] if free_order else 1.0
    return fc, order, float(np.sqrt(np.mean(s.fun ** 2))), float(np.max(np.abs(s.fun)))


def lf_curve(cap_seg, ren_seg, ref_seg):
    """capture-minus-plugin dB over the LF band, anchored to the midband so the comparison is
    SHAPE only (kOutputMakeup is unanchored -- build-plan limit L2)."""
    f, mc, nc = A.band_fr(cap_seg, ref_seg, frac=6)
    _, mr, nr = A.band_fr(ren_seg, ref_seg, frac=6)
    ok = (nc > 0) & (nr > 0)
    d = mc - mr
    anchor = ok & (f >= NORM_LO) & (f <= NORM_HI)
    if not np.any(anchor):
        return None, None
    d = d - np.mean(d[anchor])
    keep = ok & (f >= LF_LO) & (f <= LF_HI)
    return f[keep], d[keep]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    orig = A.load(A.ORIG)
    ref_seg = A.seg_of(orig, "sweep_clean", settled=False)
    segs = A.sweep_segments()
    tmp = tempfile.mkdtemp(prefix="lfmech_")

    print(f"LF mechanism probe | OS {args.os}x | sweep levels {sorted(segs)} dBFS")
    if args.self_test:
        print("  SELF-TEST: plugin render stands in for the capture. Corners must be level-")
        print("  invariant and order ~1 -- the plugin's LF is two linear RCs by construction.\n")

    report = {"generated": datetime.now(timezone.utc).isoformat(), "os_factor": args.os,
              "self_test": args.self_test, "lf_band_hz": [LF_LO, LF_HI],
              "midband_anchor_hz": [NORM_LO, NORM_HI], "captures": {}}

    for path, parsed in C.find_captures():
        name = os.path.splitext(os.path.basename(path))[0]
        out = os.path.join(tmp, name + ".wav")
        subprocess.run([args.bin, A.ORIG, out, "--os", str(args.os)] + C.render_args(parsed),
                       check=True, capture_output=True)
        ren, _ = A.align(A.load(out), orig)
        cap = ren if args.self_test else A.align(C.load_capture(path), orig)[0]

        rows, ent = {}, {}
        print(f"{name}")
        print(f"  {'level':>7s} {'fc (order=1)':>13s} {'resid':>7s} | "
              f"{'fc (free)':>10s} {'order':>6s} {'resid':>7s}")
        for db, seg in segs.items():
            f, d = lf_curve(A.seg_of(cap, seg, settled=False),
                            A.seg_of(ren, seg, settled=False), ref_seg)
            if f is None or len(f) < 5:
                continue
            fc1, _, rms1, _ = fit_pole(f, d, free_order=False)
            fcn, order, rmsn, _ = fit_pole(f, d, free_order=True)
            rows[db] = dict(fc_order1_hz=fc1, rms_order1_db=rms1,
                            fc_free_hz=fcn, order=order, rms_free_db=rmsn,
                            excess_at_25hz_db=float(np.interp(25.0, f, d)))
            flag = "  <- pedal distorting" if db == DIRTY_LEVEL_DB else ""
            print(f"  {db:>5d}dB {fc1:10.1f} Hz {rms1:6.3f} | "
                  f"{fcn:8.1f} Hz {order:6.2f} {rmsn:6.3f}{flag}")

        clean = [v for k, v in rows.items() if k != DIRTY_LEVEL_DB]
        if len(clean) >= 2:
            fcs = [v["fc_order1_hz"] for v in clean]
            ords = [v["order"] for v in clean]
            ent["fc_spread_x"] = max(fcs) / min(fcs)
            ent["order_mean"] = float(np.mean(ords))
            print(f"  => clean levels ({len(clean)}): corner spread {ent['fc_spread_x']:.2f}x, "
                  f"mean order {ent['order_mean']:.2f}")
        # shape comparison at the cleanest well-driven level
        f, d = lf_curve(A.seg_of(cap, "sweep_-26", settled=False),
                        A.seg_of(ren, "sweep_-26", settled=False), ref_seg)
        if f is not None and len(f) >= 5:
            sh = fit_shapes(f, d)
            ent["shapes_at_-26dBFS"] = sh
            print(f"  shape @-26 dBFS: 1-pole {sh['1-pole']['rms_db']:.3f} | "
                  f"2-pole {sh['2-pole']['rms_db']:.3f} | shelf {sh['shelf']['rms_db']:.3f} dB  "
                  f"(capture corner {sh['shelf']['capture_corner_hz']:.1f} Hz, "
                  f"plugin's implied {sh['shelf']['implied_plugin_corner_hz']:.1f} Hz, "
                  f"depth {sh['shelf']['depth_db']:.2f} dB)")
        ent["rows"] = rows
        report["captures"][name] = ent
        print()

    json.dump(report, open(OUTPUT_JSON, "w"), indent=1)
    print(f"wrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
