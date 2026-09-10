#!/usr/bin/env python3
"""Cross-pedal voicing comparison: P1, P2, P3, P4 -- shelf shape, absolute DARK FR shape, and
THD-vs-level, all measured DIRECTLY FROM EACH PEDAL'S OWN CAPTURES (no plugin render at all, so
nothing here can go stale against the binary).

Why this is a separate script from everything else: every other THD/FR instrument in this project
compares the PLUGIN against one pedal. This one compares the PEDALS against EACH OTHER, which is a
different question (circuit.md's "which unit do we voice to") and needs none of the plugin-side
machinery.

Three things, per unit:
  1. MODE-DIFFERENTIAL SHELF (bright-minus-dark, mid-minus-dark, in dB vs frequency). This cancels
     rig gain, trainer level and unit-to-unit calibration differences (circuit.md note #7), so it is
     the one thing here that is GENUINELY comparable across P1/P2/P4 with no caveats. P3 has no dark
     capture, so it gets none of this; P4 has no mid capture, so its MID shelf is an EXTRAPOLATION
     (same K0 as its measured bright/dark, zero scaled by the measured cap ratio).
  2. ABSOLUTE DARK-MODE FR SHAPE, normalised to 0 dB at 1 kHz. NOT rig-cancelling -- P1/P2 are NAM
     renders through unknown trainer rigs, P4 is a raw capture through a measured one. Shown anyway
     because it is the only way to see LF/HF shape differences beyond the mode shelf, with the
     normalisation point called out so nobody reads the 1 kHz agreement as calibration.
  3. THD-vs-LEVEL at 800 Hz (the same band phase1_characterization.py's harmonic_vs_level uses),
     DARK and BRIGHT where available. ⚠⚠ NOT a matched-drive comparison -- P1's capture rig is the
     only one with a known calibration (-12 dBu); P2/P3 ran hotter by an unknown amount (circuit.md
     note #10), and P4's pad is accounted for only within P4 itself. Read the SHAPE (does it rise,
     how steeply) more than the absolute digital-level x-axis position across units.

Run from the repo root:
    .venv/bin/python analysis/voicing_compare.py
Writes analysis/reports/voicing_compare.json
"""
import json, os, sys
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G
import phase1_characterization as P1C

OUT = "analysis/reports/voicing_compare.json"

# Same shelf model phase1_characterization.fit_mode_shelf uses, exposed standalone for the
# extrapolated P4 MID curve and the shipped-plugin reference overlay (pure analytic, no render).
def shelf_db(f, fz, k0):
    fp = fz * k0
    return 10 * np.log10((1 + (f / fz) ** 2) / (1 + (f / fp) ** 2))


def main():
    orig = A.load(A.ORIG)
    caps = {}
    for path, meta in C.find_captures(include_reference=False):
        x = A.load(path)
        aligned, _ = A.align(x, orig)
        caps[os.path.splitext(os.path.basename(path))[0]] = {"meta": meta, "audio": aligned}

    # Pick one representative capture per (unit, mode). P4: the cleanest matched pad-12 pair at
    # 9:00, which is well inside the rotation (not 7:30/8:00's knob-slope error, circuit.md #23).
    UNIT_CAPS = {
        "p1": {"bright": "p1_V1030_bright", "dark": "p1_V1030_dark", "mid": "p1_V1030_mid"},
        "p2": {"bright": "p2_V1430_bright", "dark": "p2_V1430_dark", "mid": "p2_V1430_mid"},
        "p3": {"mid": "p3_V1000_mid"},
        "p4": {"bright": "p4_V0900_bright_pad12", "dark": "p4_V0900_dark_pad12"},
    }

    out = {"generated": datetime.now(timezone.utc).isoformat(), "shelves": {}, "dark_fr": {},
           "thd_vs_level": {}}

    # --- 1. mode-differential shelf --------------------------------------------------------------
    sweeps = {}  # name -> (f, mag)
    for unit, modes in UNIT_CAPS.items():
        for mode, name in modes.items():
            f, mag = P1C.sweep_curve(caps[name]["audio"], orig)
            sweeps[name] = (f, mag)

    for unit, modes in UNIT_CAPS.items():
        if "dark" not in modes:
            continue
        fd, md = sweeps[modes["dark"]]
        for mode in ("bright", "mid"):
            if mode not in modes:
                continue
            fm, mm = sweeps[modes[mode]]
            fit = P1C.fit_mode_shelf(fm, mm, md)
            fs = np.geomspace(150.0, 20000.0, 120)
            curve = [float(A.gain_at(fm, mm, p) - A.gain_at(fd, md, p)) for p in fs]
            curve = [c - curve[0] for c in curve]  # pin DC asymptote like fit_mode_shelf does
            out["shelves"][f"{unit}_{mode}"] = {"fit": fit, "freqs": fs.tolist(), "diff_db": curve}

    # P4 MID: no capture exists. Extrapolate -- same K0 (gm is mode-independent, circuit.md stage 2b:
    # both branches reach the SAME plateau), zero scaled by the measured cap ratio from P1/P2 (the
    # two units that have both branches; mean ~2.23, close to drawn 2.20 and note #2's measured 2.24).
    p4_bright_fit = out["shelves"]["p4_bright"]["fit"]
    cap_ratios = [out["shelves"][f"{u}_bright"]["fit"]["zero_hz"]
                  / out["shelves"][f"{u}_mid"]["fit"]["zero_hz"] for u in ("p1", "p2")]
    # NB zero_BRIGHT/zero_MID < 1 normally (bright's 22 nF -> lower zero than mid's 10 nF), so the
    # ratio we want (mid_zero / bright_zero) is the reciprocal of zero_bright/zero_mid above.
    cap_ratio_mean = float(np.mean([1.0 / r for r in cap_ratios]))
    p4_mid_zero_est = p4_bright_fit["zero_hz"] * cap_ratio_mean
    fs = np.geomspace(150.0, 20000.0, 120)
    out["shelves"]["p4_mid_ESTIMATED"] = {
        "fit": {"zero_hz": p4_mid_zero_est, "K0": p4_bright_fit["K0"],
                "note": f"extrapolated: P4 bright zero x mean(P1,P2 mid/bright cap ratio)="
                        f"{cap_ratio_mean:.3f}, SAME K0 as P4's own measured bright/dark"},
        "freqs": fs.tolist(),
        "diff_db": [float(shelf_db(f, p4_mid_zero_est, p4_bright_fit["K0"])) for f in fs],
    }

    # Shipped plugin reference shelves (pure analytic -- m/|Vp| don't affect the SMALL-SIGNAL shelf,
    # which is set only by gm/R5/tau, so this needs no render and cannot be stale against the binary).
    SHIPPED_GM_R5 = 1.5531069e-3 * 3600.0   # circuit.md note #8/#23
    SHIPPED_K0 = 1.0 + SHIPPED_GM_R5
    SHIPPED_ZERO_BRIGHT = 1864.0            # circuit.md stage 2b table (measured mean, note #2)
    SHIPPED_ZERO_MID = 4166.0
    for label, fz in (("bright", SHIPPED_ZERO_BRIGHT), ("mid", SHIPPED_ZERO_MID)):
        out["shelves"][f"shipped_{label}"] = {
            "fit": {"zero_hz": fz, "K0": SHIPPED_K0, "note": "shipped plugin, analytic, gm=1.5531mS"},
            "freqs": fs.tolist(),
            "diff_db": [float(shelf_db(f, fz, SHIPPED_K0)) for f in fs],
        }

    # --- 2. absolute DARK FR shape, normalised at 1 kHz ------------------------------------------
    for unit, modes in UNIT_CAPS.items():
        if "dark" not in modes:
            continue
        f, mag = sweeps[modes["dark"]]
        fs = np.geomspace(20.0, 20000.0, 200)
        curve = np.array([A.gain_at(f, mag, p) for p in fs])
        curve = curve - A.gain_at(f, mag, 1000.0)
        out["dark_fr"][unit] = {"freqs": fs.tolist(), "db": curve.tolist()}

    # --- 3. THD vs level at 800 Hz, dark + bright where available --------------------------------
    for unit, modes in UNIT_CAPS.items():
        for mode, name in modes.items():
            hv = P1C.harmonic_vs_level(caps[name]["audio"], label=800)
            levels = [r["in_db"] for r in hv["rows"]]
            h2 = [r["h2_dbc"] for r in hv["rows"]]
            h3 = [r["h3_dbc"] for r in hv["rows"]]
            out["thd_vs_level"][f"{unit}_{mode}"] = {"levels_dbfs": levels, "h2_dbc": h2,
                                                      "h3_dbc": h3, "slope": hv["h2_slope_db_per_db_top"]}

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(out, fh, indent=2)
    print(f"wrote {OUT}")
    print(f"P4 MID extrapolation: cap ratio {cap_ratio_mean:.3f}, zero_est {p4_mid_zero_est:.1f} Hz, "
          f"K0 {p4_bright_fit['K0']:.3f}")


if __name__ == "__main__":
    main()
