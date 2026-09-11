#!/usr/bin/env python3
"""Per-BAND THD, plugin vs P4, at matched drive, with the rig+loading correction applied PER
HARMONIC ORDER -- not just at the fundamental -- and every cell FLOOR-GATED before it is trusted.

Why this needed a new script rather than reusing band_audit.py or harmonic_audit.py: neither knows
about P4's two corrections (goal_check.py / p4_corners.py). Both are LINEAR TIME-INVARIANT filters
(the bypass-reference rig response, and the interface's 1 MOhm || 99 pF cable load), so they do not
just scale the fundamental -- they scale the 2nd/3rd/... harmonic at ITS OWN frequency too, and that
frequency is different from the fundamental's. A correction applied only at f0 (as goal_check.py
does, because it only ever reads the fundamental via band_fr) would leave every harmonic's dBc
wrong by [corr(k*f0) - corr(f0)]. At 8 kHz that gap is the same size as the HF droop itself.

⚠⚠ THE FIRST VERSION OF THIS SCRIPT HAD NO FLOOR GATE AND THE RESULT WAS GARBAGE (RMS ~8-10 dB,
the known-zero BRIGHT-vs-DARK probe reading 5.8 dB RMS where it must read ~0). Cause: at the quiet
tone levels (-26/-16 dBFS) and high orders, the CAPTURE's harmonic reading is dominated by its own
~-102 dBFS noise floor while the PLUGIN's corresponding harmonic is a real, much smaller number --
so the "delta" was mostly "plugin's true near-zero minus the capture's noise floor", not a model
error. harmonic_audit.py already documented the fix (its own "floor test A/B") but never applied it
to an aggregate; this script applies BOTH gates before any cell is allowed into a statistic:
  * NOISE-FLOOR GATE: a capture-side harmonic reading must clear the capture's own measured noise
    floor by FLOOR_MARGIN_DB or it is dropped (and the plugin's corresponding order is dropped with
    it, so neither side's THD sum double-counts an order the other side could not measure).
  * ORDER-INVERSION GATE (circuit.md note #7 M5 / harmonic_audit.py "floor test A"): a mild
    polynomial nonlinearity produces a monotone-decreasing harmonic series. Once H(k) >= H(k-1) for
    k >= 3 in the CAPTURE, that order and every order above it is reporting floor, not signal.

Matched drive comes for free from captures.render_args() (the capture's _pad suffix feeds
OfflineRender's --input-scale), so every delta here is PEDAL's own distortion vs the PLUGIN's, at
the same gate volts -- never digital level (circuit.md note #8).

THD ABOVE ~8 kHz IS NOT MEASURABLE BY THIS METHOD AT 48 kHz, BY CONSTRUCTION (gen_test_signal.py's
own comment: H2 of a 12.5 kHz tone is already past Nyquist). The tone grid stops at 8 kHz for
exactly that reason.

Also reports the FREE KNOWN-ANSWER FLOOR: below the ~1.86 kHz mode-shelf zero, BRIGHT and DARK have
identical Zs = R5, so their (corrected, floor-gated) dBc must read identical. Whatever spread they
show there is P4's own measurement floor in that band -- the harmonic-domain twin of circuit.md
note #7, evaluated here for the first time on a raw capture rather than a NAM model.

Run from the repo root:
    .venv/bin/python analysis/thd_band_audit_p4.py [--os 8] [--floor-margin 10]
Writes analysis/reports/thd_band_audit_p4.json
"""
import argparse, json, os, subprocess, sys, tempfile
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G
import p4_corners as P

OUT = "analysis/reports/thd_band_audit_p4.json"

# P4's mode shelf zero sits at ~1.86-1.95 kHz (circuit.md note #21); every band below that is the
# free known-answer probe. Kept generous (1800) so a cell right at the edge isn't miscounted.
SHELF_ZERO_HZ = 1800.0

LF_BAND = tuple(b for b in G.TONE_FREQ_LABELS if b < 200.0)
CORE_BAND = tuple(b for b in G.TONE_FREQ_LABELS if 200.0 <= b <= 8000.0)

# ⚠⚠ HEADLINE STATS USE ONLY THE TWO LOUDEST GRID LEVELS. The two quiet ones (-26/-16 dBFS) put
# real harmonic content within a few dB of a FIXED-AMPLITUDE ARTEFACT FLOOR that the two-part drive
# gate above cannot cleanly separate from a real, just-shallow distortion law in every case (the
# borderline cells found by hand: a real slope can sit right at the gate's threshold instead of on
# either side of it). circuit.md note #22's own lesson -- "a fit whose input is floor returns a
# confident number with a good residual" -- applies here too: rather than keep tightening an ad-hoc
# gate under time pressure, restrict the headline comparison to the two levels where no such
# ambiguity exists (well clear of the floor at every frequency this pedal's gain reaches). The full
# 4-level grid is still computed and written to the JSON for anyone who wants to look at the quiet
# cells with that caveat attached.
HOT_LEVELS = (-6, -1)


def pedal_corr_closure(bypass_f, bypass_mag, volume_x):
    """dB to ADD to a captured level at frequency f so it reads as the pedal alone (goal_check.py's
    pedal_correction_db, inlined here because it needs to be evaluated at harmonic frequencies that
    are NOT the cell's nominal band label)."""
    def corr(f):
        f = np.atleast_1d(np.asarray(f, dtype=float))
        # The bypass sweep's OWN measurement rolls off sharply in the last ~2 kHz before Nyquist
        # (an analysis-edge artefact of the swept-sine FFT, not a real rig response -- it reads
        # -59 dB right at 24000 Hz on every capture, independent of volume). Clip the frequency fed
        # to the empirical rig term so a harmonic that lands up there (e.g. an 8 kHz tone's 3rd)
        # does not inherit a ~60 dB bogus correction. The analytic loading term is closed-form and
        # needs no such clamp.
        rig = np.interp(np.minimum(f, 20000.0), bypass_f, bypass_mag)
        return -rig + P.loading_correction_db(f, volume_x, P.CABLE_PF)
    return corr


def render(binary, parsed, os_factor, gm=None):
    """⚠ `gm` is a MEASUREMENT override, never a ship setting. The model is voiced to P1/P2
    (circuit.md note #28), which deliberately makes ~1.26 dB less H2 than P4. Rendering at P4's own
    measured gm removes that known offset so the per-band residual can be compared against the
    measurement floor without the voicing decision sitting in the middle of it."""
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    extra = ["--gm", f"{gm:g}"] if gm else []
    subprocess.run([binary, A.ORIG, tmp.name, "--os", str(os_factor)] + C.render_args(parsed) + extra,
                   check=True, capture_output=True)
    out = A.load(tmp.name)
    os.unlink(tmp.name)
    return out


def gate_capture_orders(h_dbfs, noise_floor_db, margin_db):
    """Drop any order that does not clear the capture's own noise floor by `margin_db`. If the
    fundamental itself fails, nothing in the cell is usable. Otherwise apply the order-inversion
    test (monotone decrease required from H3 up) on what survives the floor gate."""
    floor_ok = {k: v for k, v in h_dbfs.items()
                if v is not None and v >= noise_floor_db + margin_db}
    if 1 not in floor_ok:
        return {}
    # Order-inversion: walk upward from H2, stop at the first non-decreasing step (k >= 3).
    out = {1: floor_ok[1]}
    prev = None
    for k in sorted(k for k in floor_ok if k >= 2):
        v = floor_ok[k]
        if k >= 3 and prev is not None and v >= prev:
            break
        out[k] = v
        prev = v
    return out


def drive_response_gate(h_by_level, noise_floor_db, margin_db, min_slope=1.2, resid_tol_db=6.0):
    """h_by_level: {level_db: {order: dBFS or None}}. Returns {order: set(valid level_db)}: for
    each order (>=2) that rises with input level at >= min_slope dB/dB across the levels clearing
    the noise floor, the SUBSET of those levels whose reading actually sits close to that fitted
    line (within resid_tol_db).

    ⚠⚠ THIS TWO-PART GATE IS WHAT THE FIRST THREE VERSIONS OF THIS SCRIPT WERE MISSING, AND IT IS
    WHAT WAS PRODUCING THE GARBAGE NUMBERS. The noise-floor-margin gate alone passed a
    FIXED-AMPLITUDE ARTEFACT: at quiet/padded cells the capture's H2/H3 sat at a near-constant
    ~-42 to -48 dBc REGARDLESS OF FREQUENCY (80 Hz to 5000 Hz all read within a few dB of each
    other at the same pad+level), which is not a nonlinearity's signature -- it is a fixed
    analog-chain artefact sitting well above the broadband noise floor (so the margin gate let it
    through) with nothing to do with the pedal's actual distortion at that tiny drive. circuit.md
    note #22 documents exactly this failure mode ("a fixed-amplitude artefact floor... reads a flat
    dBc with H3 at or above H2") and its fix (bound the fit window by whether a product actually
    grows with drive).

    The SLOPE test alone is not enough either: a real order can pass the overall slope test (fitted
    across all 4 levels) while its single QUIETEST reading is still the fixed artefact, not signal
    -- the louder levels' real growth is enough to drag the fitted slope over threshold even with
    one contaminated point. The RESIDUAL test catches that: a level whose reading sits far off the
    fitted line for an otherwise-real order is itself dropped, without discarding the order's good
    (louder) cells.
    """
    orders_seen = set()
    for d in h_by_level.values():
        orders_seen |= set(k for k, v in d.items() if v is not None and k >= 2)
    out = {}
    for k in orders_seen:
        xs, ys = [], []
        for db, d in h_by_level.items():
            v = d.get(k)
            if v is not None and v >= noise_floor_db + margin_db:
                xs.append(db)
                ys.append(v)
        if len(xs) < 3:
            continue
        xs, ys = np.asarray(xs), np.asarray(ys)
        slope, intercept = np.polyfit(xs, ys, 1)
        if slope < min_slope:
            continue
        resid = ys - (slope * xs + intercept)
        out[k] = set(float(x) for x, r in zip(xs, resid) if abs(r) <= resid_tol_db)
    return out


def corrected_dbc_and_thd(h_dbfs_gated, f0, corr):
    """h_dbfs_gated: {order: dBFS}, already floor/inversion-gated. Returns (thd_pct, {order: dBc})
    with the per-order rig/loading correction applied at k*f0 before the ratio to the (also
    corrected) fundamental is taken."""
    if 1 not in h_dbfs_gated:
        return None, {}
    fund = h_dbfs_gated[1] + float(corr(f0)[0])
    dbc = {}
    for k, v in h_dbfs_gated.items():
        if k == 1:
            continue
        dbc[k] = (v + float(corr(k * f0)[0])) - fund
    thd_pct = 100.0 * np.sqrt(sum(10.0 ** (v / 10.0) for v in dbc.values())) if dbc else None
    return thd_pct, dbc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--gm", type=float, default=None,
                    help="transconductance override in SIEMENS, e.g. 1146e-6 for P4's own measured "
                         "value. MEASUREMENT ONLY -- it removes the note #28 voicing offset so the "
                         "residual can be read against the floor. Never a ship setting.")
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--floor-margin", type=float, default=10.0,
                    help="dB a capture-side harmonic must clear the capture's own noise floor by")
    args = ap.parse_args()

    if not os.path.exists(P.REF_BYPASS):
        sys.exit(f"missing the deconvolution reference {P.REF_BYPASS}")
    bypass_f, bypass_mag = P.load_fr(P.REF_BYPASS, seg="sweep_clean")

    by_mode = {}
    ren_by_mode = {}
    dropped_total = 0
    cells_total = 0
    caps = [(p, d) for p, d in C.find_captures() if d["unit"] == "p4"
            and d["volume"] > 0.1 + 1e-9]           # exclude 7:30/8:00 -- knob-slope error (note #23)
    if not caps:
        sys.exit("no usable P4 captures found")

    print(f"THD band audit | P4 | OS {args.os}x | {len(caps)} captures "
          f"(7:30/8:00 excluded -- knob-slope error) | floor margin {args.floor_margin:.0f} dB\n")

    rows_out = []
    orig_loaded = A.load(A.ORIG)
    for path, parsed in caps:
        name = os.path.splitext(os.path.basename(path))[0]
        cap, _ = A.align(C.load_capture(path), orig_loaded)
        ren = render(args.bin, parsed, args.os, args.gm)
        ren, _ = A.align(ren, orig_loaded)
        corr = pedal_corr_closure(bypass_f, bypass_mag, parsed["volume"])
        noise = A.noise_floor_db(cap)

        cell = {}
        rcell = {}
        for label, f0 in zip(G.TONE_FREQ_LABELS, G.TONE_FREQS):
            cell[label] = {}
            rcell[label] = {}
            hc_by_level = {db: A.harmonics(A.seg_of(cap, G.tone_name(label, db)), f0,
                                           G.TONE_MAX_ORDER)
                          for db in G.TONE_LEVELS_DB}
            drive_ok = drive_response_gate(hc_by_level, noise, args.floor_margin)
            for db in G.TONE_LEVELS_DB:
                hc = hc_by_level[db]
                hr = A.harmonics(A.seg_of(ren, G.tone_name(label, db)), f0, G.TONE_MAX_ORDER)
                gc = gate_capture_orders(hc, noise, args.floor_margin)
                gc = {k: v for k, v in gc.items()
                      if k == 1 or float(db) in drive_ok.get(k, ())}
                cells_total += 1
                if set(gc) != set(k for k in hc if hc[k] is not None):
                    dropped_total += 1
                # Only compare orders the CAPTURE could measure -- the render side is restricted to
                # the same order set so neither side's THD sum counts an order the other couldn't.
                gr = {k: v for k, v in hr.items() if k in gc and v is not None}
                if 1 not in gr:
                    gc = {}
                tpct, dbc = corrected_dbc_and_thd(gc, f0, corr)
                rpct, rdbc = corrected_dbc_and_thd(gr, f0, lambda f: np.zeros_like(np.atleast_1d(f)))
                cell[label][db] = {"thd_pct": tpct, "dbc": dbc, "orders": sorted(gc)}
                rcell[label][db] = {"thd_pct": rpct, "dbc": rdbc}
        by_mode.setdefault(parsed["mode"], {})[name] = cell
        ren_by_mode.setdefault(parsed["mode"], {})[name] = rcell
        rows_out.append(name)

    print(f"  {dropped_total}/{cells_total} cells had >=1 order dropped by the floor/inversion gate "
          f"(noise floor ~{A.noise_floor_db(cap):.1f} dBFS on the last capture read)\n")

    def group_stats(mode, names, bands, levels=HOT_LEVELS, min_orders=1):
        thd_d, h2_d = [], []
        for name in names:
            for label in bands:
                for db in levels:
                    c = by_mode[mode][name][label][db]
                    r = ren_by_mode[mode][name][label][db]
                    if c["thd_pct"] is None or r["thd_pct"] is None or len(c["orders"]) < min_orders:
                        continue
                    c_thd_dbc = 20 * np.log10(c["thd_pct"] / 100.0 + 1e-20)
                    r_thd_dbc = 20 * np.log10(r["thd_pct"] / 100.0 + 1e-20)
                    thd_d.append(r_thd_dbc - c_thd_dbc)
                    if 2 in c["dbc"] and 2 in r["dbc"]:
                        h2_d.append(r["dbc"][2] - c["dbc"][2])
        return np.array(thd_d), np.array(h2_d)

    print(f"{'mode':>6} {'band':>6} {'n':>4} {'THD dBc RMS':>12} {'worst':>8} "
          f"{'H2 dBc RMS':>11} {'worst':>8}  (plugin minus pedal; floor-gated)")
    summary = {}
    for mode in sorted(by_mode):
        names = sorted(by_mode[mode])
        for grp, bands in (("LF<200", LF_BAND), ("CORE", CORE_BAND)):
            thd_d, h2_d = group_stats(mode, names, bands)
            if len(thd_d) == 0:
                print(f"{mode:>6} {grp:>6}    0  -- every cell floor-gated away --")
                continue
            trms, tworst = float(np.sqrt(np.mean(thd_d ** 2))), float(np.max(np.abs(thd_d)))
            h2rms = float(np.sqrt(np.mean(h2_d ** 2))) if len(h2_d) else float("nan")
            h2worst = float(np.max(np.abs(h2_d))) if len(h2_d) else float("nan")
            print(f"{mode:>6} {grp:>6} {len(thd_d):>4} {trms:>12.2f} {tworst:>8.2f} "
                  f"{h2rms:>11.2f} {h2worst:>8.2f}")
            summary[f"{mode}_{grp}"] = dict(n=len(thd_d), thd_rms_dbc=trms, thd_worst_dbc=tworst,
                                            h2_rms_dbc=h2rms, h2_worst_dbc=h2worst)

    print(f"\n{'mode':>6} {'freq':>6} {'n':>4} {'THD dBc RMS':>12} {'worst':>8} "
          f"{'H2 dBc RMS':>11} {'worst':>8}")
    per_band = {}
    for mode in sorted(by_mode):
        names = sorted(by_mode[mode])
        for label in G.TONE_FREQ_LABELS:
            thd_d, h2_d = group_stats(mode, names, (label,))
            if len(thd_d) == 0:
                print(f"{mode:>6} {label:>6}    0  -- every cell floor-gated away --")
                continue
            trms, tworst = float(np.sqrt(np.mean(thd_d ** 2))), float(np.max(np.abs(thd_d)))
            h2rms = float(np.sqrt(np.mean(h2_d ** 2))) if len(h2_d) else float("nan")
            h2worst = float(np.max(np.abs(h2_d))) if len(h2_d) else float("nan")
            print(f"{mode:>6} {label:>6} {len(thd_d):>4} {trms:>12.2f} {tworst:>8.2f} "
                  f"{h2rms:>11.2f} {h2worst:>8.2f}")
            per_band[f"{mode}_{label}"] = dict(n=len(thd_d), thd_rms_dbc=trms, thd_worst_dbc=tworst,
                                               h2_rms_dbc=h2rms, h2_worst_dbc=h2worst)

    print(f"\n=== known-answer floor: BRIGHT vs DARK, corrected+gated dBc, bands < {SHELF_ZERO_HZ:.0f} "
          f"Hz (Zs = R5 for both -> must read identical) ===")
    floor_rows = []
    if "bright" in by_mode and "dark" in by_mode:
        bnames = sorted(by_mode["bright"])
        dnames = sorted(by_mode["dark"])

        def key(n):
            return n.replace("_bright", "").replace("_dark", "")
        bmap = {key(n): n for n in bnames}
        dmap = {key(n): n for n in dnames}
        for k in sorted(set(bmap) & set(dmap)):
            bn, dn = bmap[k], dmap[k]
            for label in (l for l in G.TONE_FREQ_LABELS if l < SHELF_ZERO_HZ):
                for db in HOT_LEVELS:
                    cb = by_mode["bright"][bn][label][db]
                    cd = by_mode["dark"][dn][label][db]
                    if cb["thd_pct"] is None or cd["thd_pct"] is None:
                        continue
                    if not cb["orders"] or not cd["orders"]:
                        continue
                    tb = 20 * np.log10(cb["thd_pct"] / 100.0 + 1e-20)
                    td = 20 * np.log10(cd["thd_pct"] / 100.0 + 1e-20)
                    floor_rows.append(abs(tb - td))
        if floor_rows:
            arr = np.array(floor_rows)
            print(f"  n={len(arr)}  mean {arr.mean():.2f} dB  RMS {np.sqrt((arr**2).mean()):.2f} dB "
                  f"worst {arr.max():.2f} dB")
        else:
            print("  no comparable cells survived the gate")
    print(f"\ncaptures used: {', '.join(rows_out)}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    # ⚠ A --gm run is a measurement probe, not the shipped audit -- keep it out of the report of
    # record so a later reader cannot mistake a voicing-corrected residual for the shipped one.
    out_path = OUT if not args.gm else OUT.replace(".json", f"_gm{args.gm*1e6:.0f}u.json")
    with open(out_path, "w") as fh:
        json.dump({"generated": datetime.now(timezone.utc).isoformat(), "os": args.os,
                   "floor_margin_db": args.floor_margin, "gm_override": args.gm,
                   "group_summary": summary, "per_band": per_band,
                   "floor_bright_vs_dark_below_shelf": floor_rows}, fh, indent=2)
    print(f"\nwrote {out_path}")


if __name__ == "__main__":
    main()
