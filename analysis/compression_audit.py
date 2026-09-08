#!/usr/bin/env python3
"""Compression audit -- plugin vs capture, at MATCHED DRIVE, with the floor measured.

Why compression is worth its own script, and why it is not just "another THD number".

  * It is a measurement on the FUNDAMENTAL, not on a harmonic 40-60 dB down. On this dataset that
    is the whole difference between measurable and not: `harmonic_audit.py` found P1's H3 sitting on
    its own error floor, so the cubic could not be read from it. Compression reads the same cubic
    off a full-amplitude signal.
  * It carries the cubic's SIGN. For y = x + a2*x^2 + a3*x^3 the fundamental's gain moves as a3*A^2,
    so `comp_db` FALLING with level means a3 < 0 (compressive) and rising means expansive. That is
    what `circuit.md` asks for before any limiter is chosen, and it is independent of a2.
  * It is a per-BAND measurement. On this pedal the MODE bypass sets how much degeneration each band
    sees, so the bands genuinely compress by different amounts -- a single 1 kHz ladder would miss
    the one feature that is diagnostic.

MATCHED DRIVE. Harmonic and compression amplitudes are set by volts at the gate, so a comparison at
matched digital level carries the trainer's whole calibration offset. `--dbu-capture` feeds the
plugin the offset that puts its gate volts on the capture's, cell for cell; since `comp_db` is
normalised to each render's own quietest cell, the output-level change that causes cancels exactly.

THE FLOOR, MEASURED NOT ASSUMED. Below the mode shelf's zero every MODE position has Zs = R5, so all
three modes must compress IDENTICALLY there. Whatever spread they show is that unit's compression
error floor, obtained with no reference capture -- the third instance of this project's free
known-answer probe (magnitude: circuit.md note #7; harmonic: note #10).

Run from the repo root:
    .venv/bin/python analysis/compression_audit.py [--dbu-capture -12] [--os 8]
Writes analysis/reports/compression_audit.json
"""
import argparse, json, os, subprocess, sys, tempfile
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G

OUTPUT_JSON = "analysis/reports/compression_audit.json"
# kInputRef is read from src/PluginProcessor.h -- see captures.plugin_vfs().
PLUGIN_VFS = C.plugin_vfs()
DBU_REF_V = 0.7746
# Bands where the modes must agree exactly.
#
# ⚠⚠ THE VALIDITY CONDITION IS 3f BELOW THE SHELF ZERO, NOT f -- corrected 2026-09-08, and it is not
# the same probe as the magnitude one. Compression is a THIRD-ORDER quantity, so it reads the
# feedback loop at 3f as well as at f; the magnitude flavour of this probe (circuit.md note #7) is
# exact as soon as f is under the 1.86 kHz zero, but this one is not exact until 3f is, which is a
# factor of three in probe frequency. Measured on the PLUGIN, where the answer is known by
# construction (JfetStageTest section 8e), the probe's own systematic spread is:
#
#     probe f      23 Hz     94 Hz     492 Hz    797 Hz
#     spread     0.0017 dB  0.0015    0.0201     0.0585   <- 3f = 2391 Hz, past the zero
#
# The 800 Hz row therefore carries ~0.06 dB of the probe's own error. On THIS dataset that turns out
# to reach nothing: dropping the row leaves both floors unchanged to three decimals (0.145 on P1,
# 0.210 on P2), so a lower band is the binding one. Both figures are reported anyway, because that
# is a fact about this capture set rather than about the probe, and a future set could bind at the
# top of the list. It matters more on the PLUGIN side, where 0.06 dB would be 30x the model's own
# 0.0017 dB spread.
KNOWN_ZERO_BANDS = (125, 200, 500, 800)
# The subset where 3f is also comfortably below the zero, i.e. where the probe is exact.
STRICT_ZERO_BANDS = (125, 200, 500)


def vfs_from_dbu(dbu):
    """volts per FULL SCALE from a NAM input_level_dbu (see harmonic_audit.vfs_from_dbu)."""
    return DBU_REF_V * (10.0 ** (dbu / 20.0)) * np.sqrt(2.0)


def render(binary, parsed, os_factor, out_path, extra=None):
    args = [binary, A.ORIG, out_path, "--os", str(os_factor)] + C.render_args(parsed)
    if extra:
        args += extra
    subprocess.run(args, check=True, capture_output=True)
    return A.load(out_path)


def curves(aligned):
    """{band_label: comp_db array over COMP_LEVELS_DB}, gain relative to the quietest cell."""
    return {lab: A.compression_curve(aligned, lab)["comp_db"] for lab in G.COMP_FREQ_LABELS}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--dbu-capture", type=float, default=None)
    args = ap.parse_args()

    offset = None
    if args.dbu_capture is not None:
        offset = 20.0 * np.log10(float(vfs_from_dbu(args.dbu_capture)) / PLUGIN_VFS)

    orig = A.load(A.ORIG)
    tmpdir = tempfile.mkdtemp(prefix="comp_")
    print(f"Compression audit: OS={args.os}x | {len(G.COMP_FREQ_LABELS)} bands x "
          f"{len(G.COMP_LEVELS_DB)} levels")
    print("  matched " + (f"DRIVE (plugin fed {offset:+.2f} dB)" if offset is not None
                          else "LEVEL -- no calibration given"))

    cap_c, ren_c, meta = {}, {}, {}
    for path, parsed in C.find_captures():
        name = os.path.splitext(os.path.basename(path))[0]
        cap = C.load_capture(path)
        if not A.is_full_length(cap, orig):
            continue
        cap_al, _ = A.align(cap, orig)
        extra = ["--input-trim", f"{offset:.4f}"] if offset is not None else None
        ren_al, _ = A.align(render(args.bin, parsed, args.os, os.path.join(tmpdir, name + ".wav"), extra), orig)
        cap_c[name], ren_c[name] = curves(cap_al), curves(ren_al)
        meta[name] = parsed

    # --- floor: modes must compress identically below the shelf zero -----------------------------
    floor = {}
    by_unit = defaultdict(dict)
    for n, m in meta.items():
        by_unit[m["unit"]][m["mode"]] = n
    floor_strict = {}
    for unit, modes in by_unit.items():
        if len(modes) < 3:
            continue

        def spread(bands):
            w = 0.0
            for lab in bands:
                arr = np.array([cap_c[modes[m]][lab] for m in ("bright", "dark", "mid")])
                w = max(w, float(np.max(arr.max(axis=0) - arr.min(axis=0))))
            return w

        floor[unit] = spread(KNOWN_ZERO_BANDS)
        floor_strict[unit] = spread(STRICT_ZERO_BANDS)
        print(f"  FLOOR {unit}: modes disagree by up to {floor[unit]:.3f} dB where they must agree "
              f"exactly ({floor_strict[unit]:.3f} dB over the 3f-valid bands only)")

    payload = {"generated": datetime.now(timezone.utc).isoformat(), "os_factor": args.os,
               "matched": "drive" if offset is not None else "level", "drive_offset_db": offset,
               "levels_db": list(G.COMP_LEVELS_DB), "floor_db": floor,
               "floor_db_3f_valid_bands": floor_strict,
               "known_zero_bands": list(KNOWN_ZERO_BANDS),
               "strict_zero_bands": list(STRICT_ZERO_BANDS), "captures": {}}
    for n in cap_c:
        payload["captures"][n] = {
            "unit": meta[n]["unit"], "mode": meta[n]["mode"],
            "bands": {str(lab): {"capture_db": [float(v) for v in cap_c[n][lab]],
                                 "plugin_db": [float(v) for v in ren_c[n][lab]]}
                      for lab in G.COMP_FREQ_LABELS},
        }
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json.dump(payload, open(OUTPUT_JSON, "w"), indent=2)
    print(f"wrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
