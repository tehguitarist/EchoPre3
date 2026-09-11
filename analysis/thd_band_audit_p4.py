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
import argparse, json, os, re, subprocess, sys, tempfile
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G
import p4_corners as P

OUT = "analysis/reports/thd_band_audit_p4.json"

# ⚠ PARSED FROM THE HEADERS, NOT TYPED -- circuit.md note #23's fault 4. gm in particular moves if
# the voicing decision is ever revisited, and a stale copy here would silently mis-place the probe's
# validity boundary.
_HERE = os.path.dirname(os.path.abspath(__file__))
_SRC_JFET = open(os.path.join(_HERE, "..", "src", "dsp", "JfetStage.h")).read()
_SRC_CV = open(os.path.join(_HERE, "..", "src", "dsp", "CircuitValues.h")).read()


def _const(name, src):
    m = re.search(rf"{name}\s*=\s*([0-9.eE+-]+)", src)
    if m is None:
        raise RuntimeError(f"{name} not found -- a shipped header was restructured")
    return float(m.group(1))

# P4's mode shelf zero sits at ~1.86-1.95 kHz (circuit.md note #21).
SHELF_ZERO_HZ = 1800.0

# ⚠⚠ "BELOW THE SHELF ZERO" IS THE WRONG VALIDITY CONDITION FOR THE KNOWN-ANSWER PROBE, AND USING
# IT OVERSTATED THE FLOOR BY AN ORDER OF MAGNITUDE. The probe's premise is that BRIGHT and DARK both
# see Zs = R5, which needs the bypass cap to be effectively OUT of circuit -- i.e. |1/jwC| >> R5,
# not merely f < fz. At the zero itself |Zc| == R5, so the cap is fully in circuit there; at 800 Hz
# it is still only 2.5x R5. The contamination is computable with no free parameters,
# 40*log10(k_dark/k_bright) with k = 1 + gm*Zs, and it is what the "floor" was actually measuring:
#
#     Hz     20    50   125   200   315   500   800  1250  1600
#   pred   0.00  0.01  0.03  0.08  0.21  0.51  1.25  2.76  4.14   <- circuit, no free parameters
#   meas   0.02  0.01  0.09  0.21  0.52  1.21  2.70  5.44  7.32   <- the "floor" as measured
#
# A measurement floor does not track a circuit prediction across a 4-decade span. So every band
# from ~200 Hz up was reporting the mode shelf as though it were error.
# ⭐ Restricted to where the premise holds, the real floor is 0.01-0.09 dB median / <=0.32 dB worst,
# against circuit.md note #30a's recorded 0.23 dB median and 2.67 dB RMS.
# ⚠ Note #30a diagnosed that distribution as heavy-tailed and prescribed quoting its MEDIAN. The
# median was the right call for the wrong reason: this is not a tail, it is a monotone frequency
# TREND, so no choice of robust statistic fixes it -- the probe has to be restricted instead. A
# robust statistic over a mixture of valid and invalid cells still reports the invalid ones.
# ⛔ Do NOT widen this to "below the shelf zero" again.
PROBE_MAX_CONTAM_DB = 0.05   # keep only bands where the circuit's own mode difference is under this

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


def render(binary, parsed, os_factor, gm=None, tag="thd"):
    """⚠ `gm` is a MEASUREMENT override, never a ship setting. The model is voiced to P1/P2
    (circuit.md note #28), which deliberately makes ~1.26 dB less H2 than P4. Rendering at P4's own
    measured gm removes that known offset so the per-band residual can be compared against the
    measurement floor without the voicing decision sitting in the middle of it.

    ⚠⚠ CACHED VIA p4_corners.render ONLY WHEN `binary` IS THE DEFAULT. P.render's key hashes
    captures.render_bin_key(), i.e. C.RENDER_BIN, so handing it a DIFFERENT binary would file that
    render under a key claiming otherwise -- circuit.md note #23's fault 1, one level in. Same guard
    goal_check.py uses, same reason. With --bin overridden we render uncached, as before.
    ⭐ The cache key includes the --gm flag (it is in args_tail), so a shipped run and a --gm run
    never collide, and a --gm run reuses goal_check --gm's renders rather than repeating them.
    """
    extra = ["--gm", f"{gm:g}"] if gm else []
    if os.path.abspath(binary) == os.path.abspath(C.RENDER_BIN):
        return A.load(P.render(parsed, tag, os_factor, extra=extra))
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    subprocess.run([binary, A.ORIG, tmp.name, "--os", str(os_factor)] + C.render_args(parsed) + extra,
                   check=True, capture_output=True)
    out = A.load(tmp.name)
    os.unlink(tmp.name)
    return out


def mode_contamination_db(f_hz):
    """The mode difference the CIRCUIT itself puts into the BRIGHT-vs-DARK probe at `f_hz`, in dB.

    The probe assumes both modes see Zs = R5. BRIGHT actually sees R5 || (1/jwC), so
    k = 1 + gm*Zs differs between the modes, and H2/H1 goes as 1/k^2 at matched gate drive (note #8:
    the degeneration suppresses the drive by k AND the squared term by k again). Hence
    40*log10(k_dark/k_bright). No free parameters -- gm, R5 and the bright branch's measured R5*C all
    come from the shipped headers.

    ⚠ This UNDER-predicts the measured spread by about 2x (0.51 dB predicted against 1.21 measured at
    500 Hz, 1.25 against 2.70 at 800 Hz), because the cells it is evaluated on are hot rather than
    small-signal and the real suppression is steeper than the square-law expansion's. That is fine
    for its one job -- deciding where the probe stops being a floor -- as long as the threshold is
    read as a threshold on the PREDICTION and not on the truth. It makes the cut CONSERVATIVE by 2x,
    which is the safe direction: it keeps fewer bands, not more.
    ⛔ Do not repurpose it as a correction to subtract off. Predicting a contamination to 2x is not
    the same as being able to remove it.
    """
    f = np.atleast_1d(np.asarray(f_hz, dtype=float))
    gm = _const("double gm", _SRC_JFET)
    r5 = _const("kR5", _SRC_CV)
    tau = _const("tauBright", _SRC_JFET)
    cap = tau / r5
    zs = 1.0 / (1.0 / r5 + 1j * 2 * np.pi * f * cap)
    k_bright = np.abs(1.0 + gm * zs)
    k_dark = 1.0 + gm * r5
    out = 40.0 * np.log10(k_dark / k_bright)
    return out if np.size(out) > 1 else float(out[0])


def gate_capture_orders(h_dbfs, noise_floor_db, margin_db):
    """Drop any order that does not clear the capture's own noise floor by `margin_db`. If the
    fundamental itself fails, nothing in the cell is usable. Otherwise apply the order-inversion
    test (monotone decrease required from H3 up) on what survives the floor gate.

    ⚠⚠ AN INVERSION AT H3 DISQUALIFIES H2 AS WELL, AND THAT IS NOT CONSERVATISM -- IT IS THE WHOLE
    POINT OF THE TEST. Truncating the series above the inversion assumes the contaminant lives only
    in the orders above it. But H3 >= H2 means the contaminant is at least as large as H2 itself, so
    H2 is inside it, not above it. Keeping H2 from such a cell reports a corrupted reading as a model
    error: `p4_V0900_dark`'s tone_20_-1 cell (the deepest-clipping cell in the whole dataset, 5.3 dB
    past cutoff) has capture H3 8.2 dB ABOVE its H2, and its H2 was being reported as a +6.70 dB
    plugin error -- single-handedly producing the "dark LF<200" group's 6.3 dB worst case. ⭐ Two
    independent known-answer arguments say the cell is corrupted and the MODEL is right there:
      * The output high-pass (48.8 Hz at this knob setting, measured -- circuit.md note #21) is
        passive and sits AFTER the JFET, so it attenuates f more than 2f and must LIFT H2 in dBc at
        20 Hz by ~4.0 dB whatever the transistor does. The plugin tracks that prediction to 0.5 dB
        (+3.50 measured vs +4.01 predicted, re 125 Hz); the capture FALLS 2.9 dB. No transistor
        model can produce the capture's sign.
      * In DARK, Zs = R5 at every frequency, so k is frequency-independent and the harmonic
        ORDERING cannot change with frequency. The capture is H2-dominant at 125 Hz and
        H3-dominant at 20 Hz, at the same drive. The circuit forbids it.
    ⚠ An inversion at H4 or above is still only a truncation -- there H2 and H3 both sit clear of
    the contaminant, which is the case the original test was written for and it stays unchanged.
    """
    floor_ok = {k: v for k, v in h_dbfs.items()
                if v is not None and v >= noise_floor_db + margin_db}
    if 1 not in floor_ok:
        return {}
    # Order-inversion: walk upward from H2, stop at the first non-decreasing step (k >= 3).
    out = {1: floor_ok[1]}
    prev = None
    inverted_at = None
    for k in sorted(k for k in floor_ok if k >= 2):
        v = floor_ok[k]
        if k >= 3 and prev is not None and v >= prev:
            inverted_at = k
            break
        out[k] = v
        prev = v
    if inverted_at == 3:
        return {1: floor_ok[1]}       # H2 is inside the contaminant -- see the docstring
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
    ap.add_argument("--dump-cells", action="store_true",
                    help="write every per-cell reading into the report. OFF by default because it "
                         "is ~16k lines of JSON, which swamps a tracked report -- but it is what "
                         "localises a band's residual to a CAPTURE and a LEVEL rather than leaving "
                         "it as an aggregate, and an aggregate cannot tell 'this band is off by "
                         "2 dB' from 'fifteen cells are fine and one is 6 dB out'. That "
                         "distinction is what circuit.md note #32(b) turned on.")
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
    # ⚠⚠ NOT `sweep_clean`. This line USED to read seg="sweep_clean", which is circuit.md note #29's
    # fault surviving in a second script: "clean" means clean of DISTORTION, so it is the QUIETEST
    # sweep (-41 dBFS) and the worst SNR in the set by 25 dB. Measured cost of that choice on the rig
    # curve: 0.02 dB at 20-125 Hz (so it never explained anything at LF) but 0.25/0.37/0.48 dB at
    # 5/8/16 kHz -- i.e. comparable to the whole 0.42 dB THD target at the top band this audit
    # reports. p4_corners.FIT_SWEEP is the right default and is what load_fr() uses unasked.
    bypass_f, bypass_mag = P.load_fr(P.REF_BYPASS)

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
        ren = render(args.bin, parsed, args.os, args.gm,
                     tag=os.path.splitext(os.path.basename(path))[0])
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
                # ⭐ H1's own corrected level is kept so COMPRESSION is readable from this same
                # instrument. It is ungated on purpose: the fundamental is 40-60 dB above anything
                # the order gates exist to catch, and gating it would discard the one quantity that
                # is always measurable. corr() is applied at f0 so the rig and the interface load
                # come out of it exactly as they do for a harmonic.
                h1c = (hc[1] + float(corr(np.atleast_1d(f0))[0])) if hc.get(1) is not None else None
                h1r = hr[1] if hr.get(1) is not None else None
                cell[label][db] = {"thd_pct": tpct, "dbc": dbc, "orders": sorted(gc), "h1": h1c}
                rcell[label][db] = {"thd_pct": rpct, "dbc": rdbc, "h1": h1r}
        by_mode.setdefault(parsed["mode"], {})[name] = cell
        ren_by_mode.setdefault(parsed["mode"], {})[name] = rcell
        rows_out.append(name)

    print(f"  {dropped_total}/{cells_total} cells had >=1 order dropped by the floor/inversion gate "
          f"(noise floor ~{A.noise_floor_db(cap):.1f} dBFS on the last capture read)\n")

    # Per-cell rows, so a band's residual can be inspected instead of only aggregated. ⚠ An
    # aggregate cannot distinguish "this whole band is off by 2 dB" from "fifteen cells are fine and
    # one is 6 dB out", and those have completely different causes -- the first is the voicing
    # offset, the second is one capture cell. dark_20's RMS 2.37 / worst 6.33 is exactly that
    # ambiguity, which is what this dump exists to resolve.
    cell_rows = []
    for mode in sorted(by_mode):
        for name in sorted(by_mode[mode]):
            for label in G.TONE_FREQ_LABELS:
                # ⚠ ALL FOUR LEVELS here, not just HOT_LEVELS. The aggregates above are restricted
                # to the hot cells because the quiet ones cannot carry a trustworthy HARMONIC (the
                # script's docstring says why). COMPRESSION is different: it is read on the
                # fundamental as an INCREMENT across level (circuit.md note #16), so it NEEDS the
                # quiet cell as its reference and is not subject to that restriction.
                for db in G.TONE_LEVELS_DB:
                    c = by_mode[mode][name][label][db]
                    r = ren_by_mode[mode][name][label][db]
                    row = dict(mode=mode, capture=name, band_hz=float(label),
                               level_dbfs=float(db), orders=c["orders"])
                    if c["thd_pct"] is None or r["thd_pct"] is None:
                        # No usable harmonic, but H1 is still measurable -- keep the row so the
                        # compression read below does not silently lose exactly the hardest-driven
                        # cells, which are the ones whose harmonics get gated.
                        if c.get("h1") is not None and r.get("h1") is not None:
                            row["h1_cap_db"] = float(c["h1"])
                            row["h1_ren_db"] = float(r["h1"])
                            cell_rows.append(row)
                        continue
                    row["thd_cap_dbc"] = float(20 * np.log10(c["thd_pct"] / 100.0 + 1e-20))
                    row["thd_ren_dbc"] = float(20 * np.log10(r["thd_pct"] / 100.0 + 1e-20))
                    if c.get("h1") is not None and r.get("h1") is not None:
                        row["h1_cap_db"] = float(c["h1"])
                        row["h1_ren_db"] = float(r["h1"])
                    if 2 in c["dbc"] and 2 in r["dbc"]:
                        row["h2_cap_dbc"] = float(c["dbc"][2])
                        row["h2_ren_dbc"] = float(r["dbc"][2])
                    cell_rows.append(row)

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

    print(f"\n=== known-answer floor: BRIGHT vs DARK, corrected+gated dBc "
          f"(Zs = R5 for both -> must read identical) ===")
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
                    # ⚠ LABELLED, not a bare number. Recorded unlabelled, this floor could only ever
                    # be quoted as one whole-band statistic -- and a whole-band floor cannot answer
                    # "is the residual in THIS band real?", which is the only question it gets asked.
                    floor_rows.append(dict(band_hz=float(label), level_dbfs=float(db),
                                           spread_db=float(abs(tb - td))))
        for r in floor_rows:
            r["predicted_mode_diff_db"] = float(mode_contamination_db(r["band_hz"]))
            r["probe_valid"] = bool(r["predicted_mode_diff_db"] <= PROBE_MAX_CONTAM_DB)
        if floor_rows:
            arr = np.array([r["spread_db"] for r in floor_rows])
            # ⚠⚠ MEDIAN FIRST. This distribution is heavy-tailed (circuit.md note #30a): quoted as
            # its RMS it says the dataset cannot resolve the 0.42 dB target at all, quoted as its
            # median it resolves ~0.25 dB and the target sits just above it. An RMS floor is set by
            # its tail and will talk you out of a measurement you can actually make.
            print(f"  n={len(arr)}  median {np.median(arr):.2f} dB  mean {arr.mean():.2f}  "
                  f"RMS {np.sqrt((arr**2).mean()):.2f}  p90 {np.percentile(arr, 90):.2f}  "
                  f"worst {arr.max():.2f} dB")
            print(f"\n  per band, against the CIRCUIT's own predicted mode difference -- the probe "
                  f"is only\n  a floor where that prediction is ~0 (see PROBE_MAX_CONTAM_DB):")
            print(f"    {'Hz':>7} {'n':>4} {'median':>8} {'p90':>8} {'worst':>8} {'predicted':>10}  probe")
            for lab in sorted({r["band_hz"] for r in floor_rows}):
                sel = np.array([r["spread_db"] for r in floor_rows if r["band_hz"] == lab])
                pred = mode_contamination_db(lab)
                ok = pred <= PROBE_MAX_CONTAM_DB
                print(f"    {lab:>7.0f} {len(sel):>4} {np.median(sel):>8.2f} "
                      f"{np.percentile(sel, 90):>8.2f} {sel.max():>8.2f} {pred:>10.2f}  "
                      f"{'FLOOR' if ok else 'measures the shelf, not error'}")
            valid = np.array([r["spread_db"] for r in floor_rows if r["probe_valid"]])
            if valid.size:
                print(f"\n  ⭐ THE FLOOR, over the bands where the probe is valid only: "
                      f"n={valid.size}  median {np.median(valid):.3f} dB  "
                      f"p90 {np.percentile(valid, 90):.3f}  worst {valid.max():.3f} dB")
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
                   "floor_bright_vs_dark_below_shelf": floor_rows,
                   "cells": cell_rows if args.dump_cells else []}, fh, indent=2)
    print(f"\nwrote {out_path}")


if __name__ == "__main__":
    main()
