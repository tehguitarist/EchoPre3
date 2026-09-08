#!/usr/bin/env python3
"""The three things in the capture set that nothing had ever read: the noise floor, the
repeatability floor, and the twin-tone segment.

WHY THIS EXISTS. Every nonlinear number this project quotes is bounded by measurements that were
generated into the test signal and then never analysed. `analyze.noise_floor_db()` and
`analyze.repeat_residual_db()` have been sitting in the library uncalled, and no script has ever
touched an `imd_guitar_*` segment. This reports all three.

⚠⚠ THE TWIN-TONE SEGMENT CANNOT MEASURE INTERMODULATION, AND THE REASON IS ARITHMETIC.
It is 220 Hz + 660 Hz, and 660 = 3 x 220 EXACTLY. Every intermodulation product of a memoryless
nonlinearity is m*f1 + n*f2 = (m + 3n) * 220, so all of them land on the 220 Hz harmonic grid and
none is separable from harmonic distortion of the 220 Hz tone. Specifically:

    f2 - f1 = 440 = 2*f1       the difference tone sits exactly on H2 of f1
    f2 + f1 = 880 = 4*f1       the sum tone sits on H4 of f1
    3*f1    = 660 = f2         f1's own third harmonic lands on the second INPUT TONE
    2*f1 - f2 = -220 -> 220    third-order IMD folds onto the f1 FUNDAMENTAL itself

The last two matter beyond this script: NEITHER input tone is a clean amplitude reference in this
segment, since each is contaminated by a third-order product of the other. Both contaminations are
third order and this stage is even-dominant, so the error is small -- but it is not zero, and it is
not something a "use the other tone" workaround escapes. Choosing two
harmonically related tones defeats the measurement the segment was added for. ➡ If the signal is
ever revised (it is append-only, so this means a new segment), use an inharmonic pair -- 220 Hz with
1234 Hz, say -- and the products separate completely.

⭐ WHAT IT CAN STILL DO, AND IT IS THE USEFUL PART: an OFF-GRID KNOWN-ANSWER PROBE. A memoryless
polynomial driven by these two tones can put energy ONLY on the 220 Hz grid. Anything at a
half-integer multiple is, by construction, not something the modelled circuit can produce -- so it is
that capture's own error floor for this segment, measured with no reference capture and no
assumptions. Same trick as the mode-differential probes, on a signal that had no probe at all.

Run from the repo root:
    .venv/bin/python analysis/imd_and_floors.py [--os 8]
Writes analysis/reports/imd_and_floors.json
"""
import argparse, json, os, subprocess, sys, tempfile
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G

OUTPUT_JSON = "analysis/reports/imd_and_floors.json"
F1, F2 = 220.0, 660.0
# On-grid bins: integer multiples of F1, which is where every product of a memoryless nonlinearity
# must land. 220 and 660 are the input tones themselves.
ON_GRID = (220.0, 440.0, 660.0, 880.0, 1100.0, 1320.0, 1540.0, 1760.0)
# Off-grid: half-integer multiples. Nothing the circuit can do reaches these.
OFF_GRID = (110.0, 330.0, 550.0, 770.0, 990.0, 1210.0, 1430.0, 1650.0)
LABEL = {220.0: "f1", 440.0: "2f1 = f2-f1", 660.0: "f2 = 3f1", 880.0: "f1+f2",
         1100.0: "2f2-f1", 1320.0: "2f2", 1540.0: "f1+2f2", 1760.0: "8f1"}


def render(binary, parsed, os_factor, out_path, extra=None):
    args = [binary, A.ORIG, out_path, "--os", str(os_factor)] + C.render_args(parsed)
    if extra:
        args += extra
    subprocess.run(args, check=True, capture_output=True)
    return A.load(out_path)


def bins(seg, freqs):
    """dBFS at each frequency, coherently (Blackman-Harris windowed DFT at that exact frequency)."""
    return {f: float(20.0 * np.log10(abs(A.dft_at(seg, f)) + 1e-20)) for f in freqs}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--dbu-capture", type=float, default=-12.0,
                    help="NAM input_level_dbu of the capture rig (P1's known value); "
                         "sets the matched-drive offset. Pass 0 to compare at matched level.")
    args = ap.parse_args()

    offset = None
    if args.dbu_capture:
        vfs = 0.7746 * (10.0 ** (args.dbu_capture / 20.0)) * np.sqrt(2.0)
        offset = 20.0 * np.log10(vfs / 0.87)

    orig = A.load(A.ORIG)
    tmp = tempfile.mkdtemp(prefix="imdf_")
    print(f"Twin tone {F1:.0f} + {F2:.0f} Hz, OS={args.os}x, matched "
          + (f"DRIVE (plugin fed {offset:+.2f} dB)" if offset else "LEVEL"))
    print("⚠ 660 = 3 x 220 exactly, so all IMD products land on the 220 Hz harmonic grid.\n")

    out = {}
    print("=== FLOORS (never reported before) ===")
    print(f"{'capture':>20} {'noise floor':>12} {'repeatability':>14}")
    for path, parsed in C.find_captures():
        name = os.path.splitext(os.path.basename(path))[0]
        cap = C.load_capture(path)
        if not A.is_full_length(cap, orig):
            continue
        cal, _ = A.align(cap, orig)
        nf = float(A.noise_floor_db(cal))
        rr = float(A.repeat_residual_db(cal))
        print(f"{name:>20} {nf:>9.1f} dBFS {rr:>11.1f} dB")
        out[name] = {"unit": parsed["unit"], "mode": parsed["mode"],
                     "noise_floor_dbfs": nf, "repeat_residual_db": rr, "levels": {}}

        extra = ["--input-scale", f"{offset:.4f}"] if offset else None
        ren, _ = A.align(render(args.bin, parsed, args.os,
                                os.path.join(tmp, name + ".wav"), extra), orig)
        for db in G.IMD_LEVELS_DB:
            seg = f"imd_guitar_{db}"
            c, r = A.seg_of(cal, seg), A.seg_of(ren, seg)
            cb, rb = bins(c, ON_GRID + OFF_GRID), bins(r, ON_GRID + OFF_GRID)
            # Reference bin. ⚠ NEITHER input tone is clean here: 2*f1 - f2 folds onto f1, and
            # 3*f1 lands on f2. Both contaminations are THIRD order, and this stage is
            # even-dominant with H3 40+ dB under H2, so either works as a reference to well under
            # 0.1 dB -- but do not describe f2 as "the clean one", because it is not.
            ref_c, ref_r = cb[F2], rb[F2]
            out[name]["levels"][str(db)] = {
                "capture_dbc": {str(f): cb[f] - ref_c for f in ON_GRID + OFF_GRID},
                "plugin_dbc": {str(f): rb[f] - ref_r for f in ON_GRID + OFF_GRID},
            }

    print("\n=== OFF-GRID PROBE: a memoryless circuit can put NOTHING here. ===")
    print("    Whatever a capture reads is its own error floor for this segment (dBc re f2).")
    print(f"{'capture':>20} {'level':>6} {'worst off-grid':>15} {'worst on-grid product':>22}")
    for name, rec in out.items():
        for db in G.IMD_LEVELS_DB:
            lv = rec["levels"][str(db)]
            off = max(lv["capture_dbc"][str(f)] for f in OFF_GRID)
            on = max(lv["capture_dbc"][str(f)] for f in ON_GRID if f not in (F1, F2))
            print(f"{name:>20} {db:>6} {off:>12.1f} dBc {on:>19.1f} dBc")

    print("\n=== ON-GRID PRODUCTS, plugin vs capture (dBc re f2), at the loudest cell ===")
    db = G.IMD_LEVELS_DB[-1]
    hdr = f"{'bin':>22}"
    names = list(out)
    for n in names:
        hdr += f"{n.replace('_V', '_'):>26}"
    print(hdr)
    print(f"{'':>22}" + "".join(f"{'cap    plug   delta':>26}" for _ in names))
    for f in ON_GRID:
        row = f"{LABEL.get(f, f'{f:.0f} Hz') + f' ({f:.0f})':>22}"
        for n in names:
            lv = out[n]["levels"][str(db)]
            c, p = lv["capture_dbc"][str(f)], lv["plugin_dbc"][str(f)]
            row += f"{c:>9.1f}{p:>8.1f}{p - c:>9.1f}"
        print(row)

    payload = {"generated": datetime.now(timezone.utc).isoformat(), "os_factor": args.os,
               "drive_offset_db": offset, "f1": F1, "f2": F2,
               "on_grid_hz": list(ON_GRID), "off_grid_hz": list(OFF_GRID),
               "reference_bin_hz": F2, "captures": out}
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json.dump(payload, open(OUTPUT_JSON, "w"), indent=2)
    print(f"\nwrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
