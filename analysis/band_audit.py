#!/usr/bin/env python3
"""Per-BAND THD and per-BAND compression, plugin vs capture, at matched drive.

Why per band rather than one number. On this pedal distortion is frequency dependent BY
CONSTRUCTION: the degeneration factor k(s) = 1 + gm*Zs(s) falls from K0 = 6.59 at DC to 1 above the
mode shelf's zero, and JfetStage's structure puts H2/H1 = A/(4*Vov*k^2) -- so the model predicts
distortion RISING with frequency by up to 20*log10(K0^2) = 32.7 dB in a bypassed mode, and dead flat
in DARK (where k is a constant at every frequency). A single 1 kHz THD figure cannot see any of
that, and the whole question "are we light at the bottom?" is a statement about the shape of that
curve, not its height.

WHAT IS COMPARED, AND THE THREE TRAPS IT AVOIDS.

  1. MATCHED DRIVE, not matched digital level. Harmonic and compression amplitude are set by volts
     at the gate. P1's rig ran at -12 dBu = 0.2752 V/FS against the plugin's kInputRef = 0.87, so a
     comparison at matched level compares the plugin 10.00 dB hotter than the reference ever saw
     (JfetStage.h, circuit.md note #10). --dbu-capture feeds the plugin the offset that fixes it.

  2. THE ORDER SET MOVES WITH FREQUENCY, so THD is NOT comparable across bands. At 48 kHz H3 of an
     8 kHz tone is already past Nyquist, so that band's THD is H2 alone while 125 Hz's is H2..H8.
     Reading a falling THD-vs-frequency curve as physics when it is really the order count dropping
     out is the band-audit version of note #7's threshold trap. Every row therefore reports the
     order set it used, and `thd_h2_only_dbc` gives a column that IS comparable across all bands.

  3. THE FLOOR IS MEASURED, NOT ASSUMED, and it is measured PER BAND. Below the mode shelf's zero
     every MODE position has Zs = R5, so all three modes must produce IDENTICAL distortion and
     identical compression there. The spread they actually show is that unit's own error floor in
     that band, obtained with no reference capture -- the fourth use of this project's free
     known-answer probe (magnitude: circuit.md note #7; harmonic: #10; compression: #11).

Run from the repo root:
    .venv/bin/python analysis/band_audit.py [--dbu-capture -12] [--os 8]
Writes analysis/reports/band_audit.json
"""
import argparse, json, os, subprocess, sys, tempfile
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G

OUTPUT_JSON = "analysis/reports/band_audit.json"
# kInputRef is read from src/PluginProcessor.h -- see captures.plugin_vfs().
PLUGIN_VFS = C.plugin_vfs()
DBU_REF_V = 0.7746
# Bands strictly below the LOWEST mode shelf zero (1.86 kHz), where the circuit forces all three
# MODE positions to Zs = R5 and therefore to IDENTICAL distortion and identical compression. Whatever
# spread they show there is that unit's own error floor in that band.
#
# ⚠ These deliberately run all the way down to 20 Hz. An earlier cut of this script started the
# probe at 125 Hz, which is exactly backwards: the bass is where the reference models are least
# trustworthy (a 132 ms receptive field is 2.6 periods at 20 Hz) and therefore where an independent
# floor is worth most. Starting the probe above the suspect region hides the thing it exists to find.
KNOWN_ZERO_TONE_BANDS = tuple(b for b in G.TONE_FREQ_LABELS if b < 1860.0)
# ⚠ COMPRESSION's version of the probe needs 3f below the zero, not f -- it is a third-order
# quantity, so it reads the loop at 3f too. Measured on the plugin (JfetStageTest section 8e) the
# probe's own spread is 0.0015 dB at 94 Hz but 0.059 dB at 797 Hz. See compression_audit.py.
KNOWN_ZERO_COMP_BANDS = tuple(b for b in G.COMP_FREQ_LABELS if 3.0 * b < 1860.0)


def vfs_from_dbu(dbu):
    return DBU_REF_V * (10.0 ** (dbu / 20.0)) * np.sqrt(2.0)


def render(binary, parsed, os_factor, out_path, extra=None):
    args = [binary, A.ORIG, out_path, "--os", str(os_factor)] + C.render_args(parsed)
    if extra:
        args += extra
    subprocess.run(args, check=True, capture_output=True)
    return A.load(out_path)


def tone_cells(aligned):
    """{(band, level_db): row} over the 16 x 4 tone grid.

    thd_dbc      -- RSS of every measurable order, in dBc. Comparable plugin-vs-capture, NOT across
                    bands (trap 2 above).
    thd_h2_only  -- H2 alone in dBc. Comparable everywhere, and on this model it is very nearly the
                    whole of THD since beta = 0 produces almost no H3.
    """
    out = {}
    for label, f0 in zip(G.TONE_FREQ_LABELS, G.TONE_FREQS):
        for db in G.TONE_LEVELS_DB:
            seg = A.seg_of(aligned, G.tone_name(label, db))
            pct, orders, h = A.thd_tone(seg, f0)
            if h.get(1) is None:
                continue
            out[(label, db)] = {
                "thd_dbc": float(20.0 * np.log10(pct / 100.0 + 1e-20)),
                "thd_pct": float(pct),
                "orders": [int(k) for k in orders],
                "h1_dbfs": float(h[1]),
                "H2": None if h.get(2) is None else float(h[2] - h[1]),
                "H3": None if h.get(3) is None else float(h[3] - h[1]),
                "H4": None if h.get(4) is None else float(h[4] - h[1]),
            }
    return out


def comp_cells(aligned):
    """{band: comp_db array over COMP_LEVELS_DB}, gain relative to that band's quietest cell."""
    return {lab: np.asarray(A.compression_curve(aligned, lab)["comp_db"], dtype=float)
            for lab in G.COMP_FREQ_LABELS}


def per_band_floor(by_mode, bands, pick):
    """{band: worst spread across the three modes} where the circuit forces them to agree.

    `pick(cells, band)` returns a 1-D array for that band. Needs all three modes of one unit.
    """
    floors = {}
    for band in bands:
        arrs = []
        for m in ("bright", "dark", "mid"):
            if m not in by_mode:
                return {}
            v = pick(by_mode[m], band)
            if v is None:
                break
            arrs.append(np.atleast_1d(np.asarray(v, dtype=float)))
        if len(arrs) != 3:
            continue
        stack = np.vstack(arrs)
        floors[band] = float(np.max(stack.max(axis=0) - stack.min(axis=0)))
    return floors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--dbu-capture", type=float, default=None,
                    help="NAM input_level_dbu of the capture rig; sets the matched-drive offset")
    ap.add_argument("--level", type=int, default=-6,
                    help="tone-grid level (dBFS) printed in the console table")
    args = ap.parse_args()

    offset = None
    if args.dbu_capture is not None:
        offset = 20.0 * np.log10(float(vfs_from_dbu(args.dbu_capture)) / PLUGIN_VFS)

    orig = A.load(A.ORIG)
    tmpdir = tempfile.mkdtemp(prefix="band_")
    print(f"Band audit: OS={args.os}x | tones {len(G.TONE_FREQ_LABELS)}x{len(G.TONE_LEVELS_DB)} | "
          f"comp {len(G.COMP_FREQ_LABELS)}x{len(G.COMP_LEVELS_DB)}")
    print("  matched " + (f"DRIVE (plugin fed {offset:+.2f} dB)" if offset is not None
                          else "LEVEL -- no calibration given, harmonics NOT comparable"))

    cap_tone, ren_tone, cap_comp, ren_comp, meta = {}, {}, {}, {}, {}
    for path, parsed in C.find_captures():
        name = os.path.splitext(os.path.basename(path))[0]
        cap = C.load_capture(path)
        if not A.is_full_length(cap, orig):
            print(f"  skip {name}: truncated")
            continue
        cap_al, _ = A.align(cap, orig)
        extra = ["--input-trim", f"{offset:.4f}"] if offset is not None else None
        ren_al, _ = A.align(render(args.bin, parsed, args.os,
                                   os.path.join(tmpdir, name + ".wav"), extra), orig)
        cap_tone[name], ren_tone[name] = tone_cells(cap_al), tone_cells(ren_al)
        cap_comp[name], ren_comp[name] = comp_cells(cap_al), comp_cells(ren_al)
        meta[name] = parsed
        print(f"  analysed {name}")

    # --- measured floors, per band, per unit ------------------------------------------------------
    by_unit = defaultdict(dict)
    for n, m in meta.items():
        by_unit[m["unit"]][m["mode"]] = n

    lvl = args.level
    thd_floor, comp_floor, h2_floor, inversion = {}, {}, {}, {}
    for unit, modes in by_unit.items():
        if len(modes) < 3:
            continue
        tone_by_mode = {m: cap_tone[n] for m, n in modes.items()}
        comp_by_mode = {m: cap_comp[n] for m, n in modes.items()}
        thd_floor[unit] = per_band_floor(
            tone_by_mode, KNOWN_ZERO_TONE_BANDS,
            lambda cells, b: [cells[(b, d)]["thd_dbc"] for d in G.TONE_LEVELS_DB
                              if (b, d) in cells] or None)
        # Same probe, resolved to the console level, and on H2 alone rather than on THD. H2 is the
        # order the model actually produces, so a floor measured on it is the one the plugin-vs-
        # capture H2 delta is answerable to.
        h2_floor[unit] = per_band_floor(
            tone_by_mode, KNOWN_ZERO_TONE_BANDS,
            lambda cells, b: (None if (b, lvl) not in cells or cells[(b, lvl)]["H2"] is None
                              else [cells[(b, lvl)]["H2"]]))
        inversion[unit] = {
            m: [b for b in KNOWN_ZERO_TONE_BANDS
                if (b, lvl) in tone_by_mode[m]
                and tone_by_mode[m][(b, lvl)]["H3"] is not None
                and tone_by_mode[m][(b, lvl)]["H2"] is not None
                and tone_by_mode[m][(b, lvl)]["H3"] >= tone_by_mode[m][(b, lvl)]["H2"]]
            for m in modes}
        comp_floor[unit] = per_band_floor(comp_by_mode, KNOWN_ZERO_COMP_BANDS,
                                          lambda cells, b: cells.get(b))

    # --- console table: per band, per unit, WITH the measured floor beside every delta ----------
    # The layout is deliberately one block per UNIT rather than one wide table across all seven
    # captures: the floor is a property of the unit (it needs that unit's three modes), so a delta
    # printed away from its own floor is not interpretable.
    for unit, modes in sorted(by_unit.items()):
        print(f"\n=== {unit.upper()} -- per-band H2 and THD at {lvl} dBFS in, matched drive ===")
        hf = h2_floor.get(unit, {})
        head = f"{'band':>7} {'floor':>6} "
        for m in ("bright", "dark", "mid"):
            if m in modes:
                head += f"|{('H2 ' + m):>26}"
        print(head)
        print(f"{'(Hz)':>7} {'(dB)':>6} " + "".join(
            f"|{'cap   plug  delta  vs':>26}" for m in ("bright", "dark", "mid") if m in modes))
        for band in G.TONE_FREQ_LABELS:
            fl = hf.get(band)
            line = f"{band:>7} " + (f"{fl:>6.1f} " if fl is not None else f"{'-':>6} ")
            for m in ("bright", "dark", "mid"):
                if m not in modes:
                    continue
                n = modes[m]
                c, r = cap_tone[n].get((band, lvl)), ren_tone[n].get((band, lvl))
                if c is None or r is None or c["H2"] is None or r["H2"] is None:
                    line += f"|{'-':>26}"
                    continue
                delta = r["H2"] - c["H2"]
                # "vs" = how far the delta clears this band's own measured floor. Under 1.0 means the
                # capture and the plugin are not distinguishable by this dataset in this band.
                mark = "-" if fl is None else f"{abs(delta) / max(fl, 1e-9):>4.1f}x"
                inv = "!" if (c["H3"] is not None and c["H2"] is not None and c["H3"] >= c["H2"]) else " "
                line += f"|{c['H2']:>7.1f}{r['H2']:>7.1f}{delta:>7.1f}{mark:>5}{inv}"
            print(line)
        for m, bands in sorted(inversion.get(unit, {}).items()):
            if bands:
                print(f"  ! {unit} {m}: H3 >= H2 (order inversion -- a square-law device cannot do "
                      f"this) in bands {', '.join(str(b) for b in bands)}")

    print(f"\n=== PER-BAND COMPRESSION at the top cell ({G.COMP_LEVELS_DB[-1]} dBFS), dB ===")
    print(f"{'band':>7} {'floor':>6} " + "".join(f"{n.replace('_V','_'):>26}" for n in sorted(cap_comp)))
    for band in G.COMP_FREQ_LABELS:
        fls = [comp_floor[u].get(band) for u in comp_floor if comp_floor[u].get(band) is not None]
        fl = f"{max(fls):>6.3f}" if fls else f"{'-':>6}"
        cells = []
        for n in sorted(cap_comp):
            c, r = cap_comp[n][band][-1], ren_comp[n][band][-1]
            cells.append(f"{c:>9.3f}{r:>8.3f}{r-c:>9.3f}")
        print(f"{band:>7} {fl} " + "".join(cells))
    print(f"{'':>7} {'':>6} " + "".join(f"{'cap  plug  delta':>26}" for _ in sorted(cap_comp)))

    payload = {
        "generated": datetime.now(timezone.utc).isoformat(),
        "os_factor": args.os, "matched": "drive" if offset is not None else "level",
        "drive_offset_db": offset, "plugin_vfs": PLUGIN_VFS,
        "capture_vfs": None if offset is None else float(vfs_from_dbu(args.dbu_capture)),
        "tone_levels_db": list(G.TONE_LEVELS_DB), "comp_levels_db": list(G.COMP_LEVELS_DB),
        "thd_floor_db": {u: {str(k): v for k, v in f.items()} for u, f in thd_floor.items()},
        "h2_floor_db": {u: {str(k): v for k, v in f.items()} for u, f in h2_floor.items()},
        "h2_floor_level_db": lvl,
        "order_inverted_bands": {u: {m: list(b) for m, b in d.items()}
                                 for u, d in inversion.items()},
        "comp_floor_db": {u: {str(k): v for k, v in f.items()} for u, f in comp_floor.items()},
        "captures": {},
    }
    for n in cap_tone:
        payload["captures"][n] = {
            "unit": meta[n]["unit"], "mode": meta[n]["mode"],
            "thd": [{"band": b, "level_db": d, "capture": cap_tone[n][(b, d)],
                     "plugin": ren_tone[n].get((b, d))}
                    for b in G.TONE_FREQ_LABELS for d in G.TONE_LEVELS_DB
                    if (b, d) in cap_tone[n]],
            "compression": {str(b): {"capture_db": cap_comp[n][b].tolist(),
                                     "plugin_db": ren_comp[n][b].tolist()}
                            for b in G.COMP_FREQ_LABELS},
        }
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json.dump(payload, open(OUTPUT_JSON, "w"), indent=2)
    print(f"\nwrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
