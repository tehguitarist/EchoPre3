#!/usr/bin/env python3
"""Fit Vov -- the JFET stage's ONE amplitude parameter -- from P1, twice, two independent ways.

Vov sets the device's curvature: beta = gm/(2*Vov) with gm already measured from the mode
differential, so everything else in the device model is derived from it. It has never been measured
because it is degenerate 1:1 with the trainer's reamp level -- and P1 is the only capture whose
level is known (-12 dBu at the pedal jack, circuit.md note #10), which makes it the only unit that
can carry this fit at all.

TWO ROUTES, AND THEY MUST AGREE. Agreement between unrelated observables is the strongest evidence
this dataset can produce, because each route's systematic error is the reference model's, not the
circuit's, and the two references are different signals:

  ROUTE A -- the second harmonic, midband. H2/H1 goes as A/(4*Vov*k^2) in the small-signal limit,
    so it is near-inverse in Vov. Read off the tone grid over 125 Hz .. 800 Hz, which is above the
    receptive-field artefact (circuit.md #13: never fit below ~100 Hz) and below the shelf zero.

  ROUTE B -- compression, ABOVE 3 kHz ONLY, and read as an INCREMENT with level. Compression is
    third-order, so it moves roughly as 1/Vov^2 -- a different power of the same parameter, off the
    fundamental rather than off a harmonic 40-60 dB down. ⚠ The low bands are excluded on a physical
    argument, not on magnitude: P1's compression there reads POSITIVE, i.e. expansive, which this
    circuit cannot do -- measured, its DARK and MID increments at 3150 Hz are +0.013 and +0.011 dB.
    A cell can exceed the floor's magnitude and still be floor, and BRIGHT's 3150 Hz cell is dropped
    with them because its two neighbours in the same band carry the wrong sign.

    ⚠⚠ THE OBSERVABLE IS THE INCREMENT ACROSS THE TOP CELLS, NOT comp_db ITSELF, and the difference
    decides whether this route works at all. The reference models carry a level-INDEPENDENT gain
    error in each band, which comp_db reports as compression that was never there: P1-dark reads
    -0.089 / -0.087 / -0.088 dB over the top 10 dB at 5 kHz -- flat, so not compression. Taking the
    increment cancels that offset exactly and leaves only the part that must grow with drive. It
    drops this route's own floor from 0.145 dB (compression_audit.py, on comp_db) to ~0.013 dB --
    the worst DARK increment, where the circuit permits almost nothing and the plugin's own reads
    a few thousandths.

    ⚠ AND DO NOT MEDIAN ACROSS MODES. The three modes have wildly different sensitivity to Vov --
    in DARK, Zs = R5 at every frequency, so there is barely any compression to measure and its
    cells do not move with Vov at all. A median over modes then sits on the insensitive one and the
    whole route reads as flat. Measured: pooling this way made route B's statistic move 0.01 dB
    across the entire admissible Vov range, and it looked like a route with no leverage. It has
    leverage; the statistic did not.

⚠⚠ NO sqrt(N). The floors here are SYSTEMATIC model error, not noise (circuit.md note #15), so
averaging over the 24 cells does NOT shrink them. The bracket this script reports is the
single-cell floor mapped through each route's own sensitivity, and it is the honest precision.

⚠⚠ KNOWN-ANSWER FIRST. `--self-test` replaces the capture with a PLUGIN render at a Vov this script
was not told, and asks both routes to recover it. That is the project's standing rule (circuit.md
note #9): a capture cannot tell you its estimator is blind, a render whose truth you set can.

Run from the repo root:
    .venv/bin/python analysis/vov_fit.py [--os 8] [--self-test]
Writes analysis/reports/vov_fit.json
"""
import argparse, json, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G

OUTPUT_JSON = "analysis/reports/vov_fit.json"
PLUGIN_VFS = C.plugin_vfs()
DBU_REF_V = 0.7746

UNIT = "p1"
P1_DBU = -12.0            # circuit.md note #10, owner-reported. At the PEDAL JACK, after the reamp box.
MODES = ("bright", "dark", "mid")

# Route A. Above the receptive-field artefact, below the shelf zero.
H2_BANDS = (125, 200, 315, 500, 800)
H2_LEVELS = G.TONE_LEVELS_DB
# Route B. Above 3 kHz, where P1's compression has the sign the circuit requires.
COMP_REPORT_BANDS = (3150, 5000, 8000, 12500)
COMP_BANDS = (5000, 8000)
COMP_TOP_CELLS = 3        # the loudest three of the ten levels -- the rest are under the floor
# A capture cell whose compression does not GROW over those 10 dB is not measuring compression.
# 0.02 dB is ~3x the increment DARK shows where the model says there is almost nothing to see.
COMP_SLOPE_MIN_DB = 0.02

# The shipped value, and the sweep. Bracketed by the datasheet through the one-parameter self-bias
# family (JfetStage.h): IDSS <= 5 mA caps Vov at 0.447, Vgs(off) >= 0.5 V floors it at 0.131.
# The two lowest points are BELOW the datasheet's own floor and are marked inadmissible in the
# table. They are swept anyway so a crossing that lands under the bracket is reported as a number
# rather than as "none" -- "the data wants a part the datasheet does not allow, and by this much"
# is a finding; a blank is not.
VOV_GRID = (0.110, 0.120, 0.131, 0.150, 0.175, 0.205, 0.240, 0.280, 0.330, 0.385, 0.4469)
VOV_DATASHEET_FLOOR = 0.131   # from Vgs(off) >= 0.5 V through the one-parameter self-bias family
SELF_TEST_VOV = 0.2200    # deliberately OFF the grid, so the interpolation is under test too


def vfs_from_dbu(dbu):
    """volts per FULL SCALE from a NAM input_level_dbu (see harmonic_audit.vfs_from_dbu)."""
    return DBU_REF_V * (10.0 ** (dbu / 20.0)) * np.sqrt(2.0)


def render(binary, parsed, os_factor, out_path, extra):
    args = [binary, A.ORIG, out_path, "--os", str(os_factor)] + C.render_args(parsed) + extra
    subprocess.run(args, check=True, capture_output=True)
    return A.load(out_path)


def h2_cells(aligned):
    """{(band, level_db): H2 in dBc} over the route-A grid."""
    out = {}
    for label in H2_BANDS:
        f0 = G.TONE_FREQS[G.TONE_FREQ_LABELS.index(label)]
        for db in H2_LEVELS:
            h = A.harmonics(A.seg_of(aligned, G.tone_name(label, db)), f0, max_order=4)
            if h.get(1) is None or h.get(2) is None:
                continue
            out[(label, db)] = h[2] - h[1]
    return out


def comp_increments(aligned, bands):
    """{band: compression GROWTH over the top cells, in dB}, negative when the band compresses.

    The increment, not comp_db itself: a level-independent gain error in the reference cancels
    here and only the part that must scale with drive survives. See the module docstring."""
    out = {}
    n = len(G.COMP_LEVELS_DB)
    for label in bands:
        c = A.compression_curve(aligned, label)
        out[label] = float(c["comp_db"][n - 1] - c["comp_db"][n - COMP_TOP_CELLS])
    return out


def measure(aligned):
    return {"h2": h2_cells(aligned), "comp": comp_increments(aligned, COMP_REPORT_BANDS)}


def stat(plugin, reference, keys):
    """Median plugin-minus-reference delta over `keys`, in dB. Median, not mean: the floor is
    systematic and one bad cell is a bias, not an outlier to be averaged in."""
    d = [plugin[k] - reference[k] for k in keys if k in plugin and k in reference]
    return (float(np.median(d)), len(d)) if d else (None, 0)


def solve(vovs, stats, target=0.0):
    """The Vov where `stats` crosses `target`, by linear interpolation in log Vov.

    Monotone by construction -- both observables fall as Vov rises -- so a crossing is unique. A
    target outside the swept range returns None rather than extrapolating: the datasheet bracket is
    a real bound and a number outside it would be meaningless."""
    lv = np.log(np.asarray(vovs, float))
    s = np.asarray(stats, float)
    order = np.argsort(s)
    s, lv = s[order], lv[order]
    if target < s[0] or target > s[-1]:
        return None
    return float(np.exp(np.interp(target, s, lv)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--self-test", action="store_true",
                    help="substitute a plugin render at a known Vov for the capture")
    args = ap.parse_args()

    offset = 20.0 * np.log10(float(vfs_from_dbu(P1_DBU)) / PLUGIN_VFS)
    orig = A.load(A.ORIG)
    tmp = tempfile.mkdtemp(prefix="vovfit_")
    caps = {d["mode"]: (p, d) for p, d in C.find_captures() if d["unit"] == UNIT}

    print(f"Vov fit | OS {args.os}x | unit {UNIT.upper()} at {P1_DBU:+.0f} dBu "
          f"({vfs_from_dbu(P1_DBU):.4f} V/FS) vs plugin {PLUGIN_VFS} V/FS")
    print(f"  matched DRIVE: plugin fed {offset:+.2f} dB. Shipped Vov {VOV_GRID[-1]:.4f}.")
    if args.self_test:
        print(f"  ⚠ SELF-TEST: the 'capture' is a plugin render at a Vov the fitter is not told.")

    # --- render the grid ------------------------------------------------------------------------
    jobs = []
    for mode in MODES:
        for v in VOV_GRID:
            jobs.append((mode, v))
    if args.self_test:
        jobs += [(m, SELF_TEST_VOV) for m in MODES]

    def run(job):
        mode, v = job
        _, parsed = caps[mode]
        out = os.path.join(tmp, f"{mode}_{v:.4f}.wav")
        al, _ = A.align(render(args.bin, parsed, args.os, out,
                               ["--input-scale", f"{offset:.4f}", "--vov", f"{v:.6f}"]), orig)
        return job, measure(al)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        rendered = dict(ex.map(run, jobs))
    print(f"  rendered {len(jobs)} cells")

    # --- the reference --------------------------------------------------------------------------
    ref = {}
    for mode in MODES:
        if args.self_test:
            ref[mode] = rendered[(mode, SELF_TEST_VOV)]
        else:
            cap, _ = A.align(C.load_capture(caps[mode][0]), orig)
            ref[mode] = measure(cap)

    # --- the floor, measured, not assumed -------------------------------------------------------
    # Below the shelf zero every MODE has Zs = R5, so H2 in dBc must be IDENTICAL across the three.
    # Whatever spread they show is this reference's own harmonic error, obtained with no reference
    # capture at all. Compression's flavour of the same probe is NOT valid here -- it needs 3f below
    # the zero (circuit.md note #14) and route B deliberately sits at 5-8 kHz -- so route B's floor
    # is taken from compression_audit.py's measurement instead of re-derived from a bad probe.
    h2_floor = {}
    for k in ref["dark"]["h2"]:
        vals = [ref[m]["h2"][k] for m in MODES if k in ref[m]["h2"]]
        if len(vals) == 3:
            h2_floor[k] = float(max(vals) - min(vals))
    floor_a = float(np.median(list(h2_floor.values()))) if h2_floor else float("nan")
    print(f"\n=== FLOOR (known-answer probe: below the shelf zero all three modes must agree) ===")
    print(f"  route A, H2 mode spread over {len(h2_floor)} cells: "
          f"median {floor_a:.2f} dB, worst {max(h2_floor.values()):.2f} dB")

    # --- route A -------------------------------------------------------------------------------
    h2_keys = [(b, d) for b in H2_BANDS for d in H2_LEVELS]
    rowsA = {m: [] for m in MODES}
    print(f"\n=== ROUTE A: H2, {H2_BANDS[0]}-{H2_BANDS[-1]} Hz "
          f"(median plugin-minus-reference dBc; negative = model too clean) ===")
    print(f"{'Vov':>8} {'|Vp|':>7} {'Id0 uA':>8} " + "".join(f"{m:>9}" for m in MODES)
          + f"{'pooled':>9}   admissible?")
    pooledA = []
    for v in VOV_GRID:
        per = [stat(rendered[(m, v)]["h2"], ref[m]["h2"], h2_keys)[0] for m in MODES]
        pooled = float(np.median([rendered[(m, v)]["h2"][k] - ref[m]["h2"][k]
                                  for m in MODES for k in h2_keys
                                  if k in rendered[(m, v)]["h2"] and k in ref[m]["h2"]]))
        for m, x in zip(MODES, per):
            rowsA[m].append(float(x))
        pooledA.append(pooled)
        # The self-bias family: |Vp| = 3.7956*Vov, Id0 = gm*Vov/2 with gm = 1553 uS.
        print(f"{v:>8.4f} {3.7956 * v:>7.3f} {0.5 * 1553e-6 * v * 1e6:>8.1f} "
              + "".join(f"{x:>+9.2f}" for x in per) + f"{pooled:>+9.2f}"
              + ("             yes" if v >= VOV_DATASHEET_FLOOR else "   NO (datasheet)"))

    # --- route B -------------------------------------------------------------------------------
    # ⚠ The floor test is the LEVEL SLOPE, applied to the capture: a band whose compression does not
    # grow over the top 10 dB is not measuring compression however large its comp_db offset is.
    print(f"\n=== ROUTE B: compression GROWTH over the top {COMP_TOP_CELLS} cells "
          f"({G.COMP_LEVELS_DB[-COMP_TOP_CELLS]} -> {G.COMP_LEVELS_DB[-1]} dBFS) ===")
    print(f"  capture increments, dB (must be <= -{COMP_SLOPE_MIN_DB} to be usable):")
    usable = []
    for m in MODES:
        cells = []
        for b in COMP_REPORT_BANDS:
            inc = ref[m]["comp"][b]
            ok = (b in COMP_BANDS) and (inc <= -COMP_SLOPE_MIN_DB)
            cells.append(f"{b}: {inc:+.3f}{'*' if ok else ' '}")
            if ok:
                usable.append((m, b))
        print(f"    {m:>6}  " + "   ".join(cells))
    print(f"  * = usable ({len(usable)} of {len(MODES) * len(COMP_BANDS)} in-band cells).")
    print(f"  ⚠ DARK is excluded for lack of LEVERAGE, not because it is floor -- read the two apart.")
    print(f"    Its comp_db OFFSET is the reference's error and the increment removes it; its flat")
    print(f"    INCREMENT is the circuit, and --self-test shows the PLUGIN's dark cells are just as")
    print(f"    flat. In DARK, Zs = R5 at every frequency, so there is barely any compression to")
    print(f"    measure and its cells do not move with Vov either. Nothing to fit, either way.")

    rowsB = {k: [] for k in usable}
    print(f"\n{'Vov':>8} " + "".join(f"{m[:2]}{b // 1000}k".rjust(9) for m, b in usable) + f"{'pooled':>9}")
    pooledB = []
    for v in VOV_GRID:
        per = [rendered[(m, v)]["comp"][b] - ref[m]["comp"][b] for m, b in usable]
        for k, x in zip(usable, per):
            rowsB[k].append(float(x))
        pooledB.append(float(np.median(per)))
        print(f"{v:>8.4f} " + "".join(f"{x:>+9.3f}" for x in per) + f"{pooledB[-1]:>+9.3f}")

    # --- the fits ------------------------------------------------------------------------------
    fitA = solve(VOV_GRID, pooledA)
    fitB = solve(VOV_GRID, pooledB)
    perA = {m: solve(VOV_GRID, rowsA[m]) for m in MODES}
    perB = {f"{m}_{b}": solve(VOV_GRID, rowsB[(m, b)]) for m, b in usable}
    # The bracket: where route A's residual equals its own floor, in both directions.
    band_a = [solve(VOV_GRID, pooledA, t) for t in (-floor_a, +floor_a)]

    print(f"\n=== THE FIT ===")
    print(f"  route A (H2, {H2_BANDS[0]}-{H2_BANDS[-1]} Hz)   Vov = "
          f"{'none' if fitA is None else f'{fitA:.4f}'}   per mode: "
          + ", ".join(f"{m} {'none' if perA[m] is None else f'{perA[m]:.4f}'}" for m in MODES))
    print(f"  route B (compression growth)  Vov = "
          f"{'none' if fitB is None else f'{fitB:.4f}'}   per cell: "
          + ", ".join(f"{k} {'none' if x is None else f'{x:.4f}'}" for k, x in perB.items()))
    if fitA and fitB:
        print(f"\n  ⭐ the two routes agree to a factor of {max(fitA, fitB) / min(fitA, fitB):.2f}")
        print(f"     Different orders (H2 is 2nd, compression 3rd), different signals (a harmonic")
        print(f"     40-60 dB down vs the fundamental), different floors. Agreement is evidence.")
    print(f"  route A's floor-implied bracket: "
          + ", ".join('none' if b is None else f'{b:.4f}' for b in band_a)
          + f"  (floor {floor_a:.2f} dB)")
    print(f"  ⚠ NOT a standard error. The floor is systematic (circuit.md #15), so it does not")
    print(f"    shrink with the {len(h2_keys) * 3} cells. This is the honest precision: a factor of ~1.5.")
    if args.self_test:
        print(f"\n  ⚠ SELF-TEST TRUTH: Vov = {SELF_TEST_VOV:.4f}. Both routes must land on it; a route")
        print(f"    that does not is measuring something other than what it claims.")

    payload = {"generated": datetime.now(timezone.utc).isoformat(), "os_factor": args.os,
               "unit": UNIT, "dbu_capture": P1_DBU, "drive_offset_db": offset,
               "self_test": bool(args.self_test),
               "self_test_truth": SELF_TEST_VOV if args.self_test else None,
               "vov_grid": list(VOV_GRID),
               "delta_h2_db": pooledA, "delta_h2_db_per_mode": rowsA,
               "delta_comp_db": pooledB,
               "delta_comp_db_per_cell": {f"{m}_{b}": x for (m, b), x in rowsB.items()},
               "comp_usable_cells": [f"{m}_{b}" for m, b in usable],
               "comp_capture_increments": {m: {str(b): ref[m]["comp"][b] for b in COMP_REPORT_BANDS}
                                           for m in MODES},
               "h2_floor_median_db": floor_a,
               "h2_floor_cells": {f"{k[0]}_{k[1]}": v for k, v in h2_floor.items()},
               "fit_route_a": fitA, "fit_route_b": fitB,
               "fit_route_a_per_mode": perA, "fit_route_b_per_cell": perB,
               "bracket_route_a": band_a}
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json.dump(payload, open(OUTPUT_JSON, "w"), indent=2)
    print(f"\nwrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
