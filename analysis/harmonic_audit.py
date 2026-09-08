#!/usr/bin/env python3
"""Per-ORDER harmonic audit, plugin vs capture, with each capture's own error floor marked.

Why this is separate from the THD tables. THD is a root-sum-square over H2..H7, so it can read
correct while every term inside it is wrong, and on this pedal it is dominated by the NAM models'
error floor rather than by the pedal (circuit.md note #7, M5). This script reports the ORDERS, and
-- the part that makes them readable -- marks every cell that sits on that floor, using two
independent floor tests that need no reference capture:

  FLOOR TEST A -- ORDER INVERSION. A mild polynomial nonlinearity must produce a monotone decreasing
  harmonic series (H2 > H3 > H4 ...). A cell where H4 >= H3 is reporting the model's own error, not
  the pedal's. This is the test that disqualified everything above H2 in phase 1.

  FLOOR TEST B -- LEVEL SLOPE. ⚠ Read the units: the slope reported here is fitted on the ABSOLUTE
  harmonic level (dBFS), not on the dBc ratio, and the two differ by exactly one power of the drive.
  For a square law H2 goes as A^2, so the expected slope here is **2.0 dB/dB** (it is 1.0 dB/dB if
  you plot dBc, which is the number circuit.md's M5 quotes -- M5's "0.75 dB/dB" is a dBc figure and
  is NOT comparable to this column). H3 goes as A^3 -> 3.0 dB/dB. A cell whose order does not move
  with level is floor however plausible its magnitude looks, and a slope pulled BELOW the expected
  value is the signature of a floor the harmonic is only just climbing out of.

The two tests are independent: A is within-cell and needs one level, B is across-level and needs no
comparison between orders. A cell has to pass BOTH before its plugin-vs-capture delta means anything.

DRIVE, NOT LEVEL. `--vfs-capture` sets the volts-per-full-scale the CAPTURE's rig delivered, from
its NAM input calibration. Harmonic amplitude is set by the volts at the gate, so a comparison at
matched digital level carries the whole calibration offset; passing this makes the script report
each capture cell against the plugin cell at the same DRIVE instead, interpolating the plugin's own
level ladder. Without it the comparison is at matched level and says so.

Run from the repo root:
    .venv/bin/python analysis/harmonic_audit.py [--vfs-capture 4.3612] [--os 8]
Writes analysis/reports/harmonic_audit.json
"""
import argparse, json, os, subprocess, sys, tempfile
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G

OUTPUT_JSON = "analysis/reports/harmonic_audit.json"
ORDERS = (2, 3, 4, 5)
# kInputRef the renders were made with (src/PluginProcessor.h). Volts per full scale.
# kInputRef is read from src/PluginProcessor.h -- see captures.plugin_vfs().
PLUGIN_VFS = C.plugin_vfs()
# 0 dBu in volts RMS, for turning a NAM input_level_dbu into volts per full scale.
DBU_REF_V = 0.7746


def vfs_from_dbu(dbu):
    """volts per FULL SCALE (a sample of 1.0) from a NAM input_level_dbu.

    NAM calibrates by playing a 1 kHz sine at 0 dBFS and measuring RMS volts at the jack going into
    the gear, so `dbu` describes a full-scale SINE, whose RMS is 1/sqrt(2) of its peak. The peak
    volts a sample of 1.0 represents is therefore sqrt(2) times that measured RMS.
    """
    return DBU_REF_V * (10.0 ** (dbu / 20.0)) * np.sqrt(2.0)


def render(binary, parsed, os_factor, out_path, extra=None):
    args = [binary, A.ORIG, out_path, "--os", str(os_factor)] + C.render_args(parsed)
    if extra:
        args += extra
    subprocess.run(args, check=True, capture_output=True)
    return A.load(out_path)


def cell_orders(x, f0):
    """{order: dBFS} plus the fundamental, for one tone cell."""
    return A.harmonics(x, f0, max_order=max(ORDERS))


def collect(aligned):
    """{(freq_label, level_db): {order: dBc}} over the whole tone grid, referenced to H1."""
    out = {}
    for label, f0 in zip(G.TONE_FREQ_LABELS, G.TONE_FREQS):
        for db in G.TONE_LEVELS_DB:
            h = cell_orders(A.seg_of(aligned, G.tone_name(label, db)), f0)
            if h[1] is None:
                continue
            rec = {"h1_dbfs": h[1]}
            for k in ORDERS:
                rec[f"H{k}"] = None if h.get(k) is None else h[k] - h[1]
            out[(label, db)] = rec
    return out


def slope_per_db(cells, label, order):
    """dB of harmonic per dB of input, fitted across the level ladder at one frequency."""
    xs, ys = [], []
    for db in G.TONE_LEVELS_DB:
        r = cells.get((label, db))
        if r is None:
            continue
        v = r.get(f"H{order}")
        if v is None:
            continue
        # H2 in dBc rises 1 dB per dB for a square law; convert back to absolute dBFS for the fit.
        xs.append(db); ys.append(v + r["h1_dbfs"])
    if len(xs) < 3:
        return None
    return float(np.polyfit(xs, ys, 1)[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--vfs-capture", type=float, default=None,
                    help="volts per full scale the capture rig delivered (see vfs_from_dbu)")
    ap.add_argument("--dbu-capture", type=float, default=None,
                    help="NAM input_level_dbu for the capture rig; converted to --vfs-capture")
    ap.add_argument("--only", default=None, help="substring filter on capture name")
    args = ap.parse_args()

    vfs_cap = args.vfs_capture
    if args.dbu_capture is not None:
        vfs_cap = float(vfs_from_dbu(args.dbu_capture))

    drive_offset_db = None
    if vfs_cap is not None:
        drive_offset_db = 20.0 * np.log10(vfs_cap / PLUGIN_VFS)

    orig = A.load(A.ORIG)
    caps = [(p, d) for p, d in C.find_captures() if not args.only or args.only in p]
    tmpdir = tempfile.mkdtemp(prefix="harm_")

    print(f"Harmonic audit: {len(caps)} captures | OS={args.os}x | orders {ORDERS}")
    if vfs_cap is not None:
        print(f"  capture rig {vfs_cap:.4f} V/FS vs plugin {PLUGIN_VFS} V/FS "
              f"-> comparing at MATCHED DRIVE, plugin fed {drive_offset_db:+.2f} dB")
    else:
        print("  no calibration given -> comparing at MATCHED DIGITAL LEVEL (see module docstring)")

    results = {}
    for path, parsed in caps:
        name = os.path.splitext(os.path.basename(path))[0]
        cap = C.load_capture(path)
        if not A.is_full_length(cap, orig):
            continue
        cap_al, _ = A.align(cap, orig)
        cap_cells = collect(cap_al)

        # The plugin render. With a calibration, drive it drive_offset_db hotter so the volts at the
        # gate match the capture's; harmonic RATIOS (dBc) are what we compare, so the output level
        # this changes is irrelevant and no output compensation is needed.
        extra = None
        if drive_offset_db is not None:
            extra = ["--input-trim", f"{drive_offset_db:.4f}"]
        ren = render(args.bin, parsed, args.os, os.path.join(tmpdir, name + ".wav"), extra)
        ren_al, _ = A.align(ren, orig)
        ren_cells = collect(ren_al)

        rows = []
        for (label, db), c in sorted(cap_cells.items()):
            r = ren_cells.get((label, db))
            if r is None:
                continue
            h3, h4 = c.get("H3"), c.get("H4")
            inverted = (h3 is not None and h4 is not None and h4 >= h3)
            row = {"freq": label, "level_db": db, "order_inverted": bool(inverted)}
            for k in ORDERS:
                cv, rv = c.get(f"H{k}"), r.get(f"H{k}")
                row[f"H{k}"] = {
                    "capture_dbc": cv, "plugin_dbc": rv,
                    "delta_db": (None if (cv is None or rv is None) else rv - cv),
                    "slope_db_per_db": slope_per_db(cap_cells, label, k),
                }
            rows.append(row)

        results[name] = {"unit": parsed["unit"], "mode": parsed["mode"], "rows": rows}
        n_inv = sum(1 for r in rows if r["order_inverted"])
        print(f"  {name}: {len(rows)} cells, {n_inv} with H4>=H3 (floor test A failed)")

    payload = {
        "generated": datetime.now(timezone.utc).isoformat(),
        "os_factor": args.os,
        "plugin_vfs": PLUGIN_VFS,
        "capture_vfs": vfs_cap,
        "drive_offset_db": drive_offset_db,
        "matched": "drive" if drive_offset_db is not None else "level",
        "captures": results,
    }
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    with open(OUTPUT_JSON, "w") as fh:
        json.dump(payload, fh, indent=2)
    print(f"wrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
