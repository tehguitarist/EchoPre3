#!/usr/bin/env python3
"""Phase 1 characterisation (M0-M6) -- docs/build-plan.md Sec.3.

Pure characterisation of the seven NAM reference captures, run before a line of DSP-fitting code
touches the JFET stage. No plugin render involved -- this only reads analysis/captures/*.wav.

M0 (as originally scoped -- a no-plugin null render) does not apply to this dataset: every capture
here is a NAM model's OUTPUT, not a bypass pass-through, so there is no bit-identical null to check.
What DOES transfer from M0's intent -- "is the render loop sample-aligned and un-truncated" -- is
checked here as an alignment/length pass, plus a KNOWN-ANSWER probe (P0 below) that measures how
much of the low end each NAM model actually gets right.

  !! METHOD NOTE (2026-09-07). The first version of this script read every corner as a naive
  "-3 dB below the plateau" threshold crossing. Every one of those scalars was wrong, because on
  this pedal no plateau is reached inside the audio band: the mode shelf's pole sits at K0 x its
  zero, i.e. 12-27 kHz. Reading a plateau in an 8-10 kHz window therefore normalised each branch to
  a different point on its own transition, and the resulting "corners" (~5.1-5.5 kHz for both caps)
  were an artifact of that normalisation, not a measurement. They also inverted the M1 verdict.
  Everything here now FITS A MODEL to the continuous curve and reports the fit residual, so a bad
  extraction shows up as a bad residual instead of a plausible-looking number.

Run: .venv/bin/python analysis/phase1_characterization.py
Writes: analysis/reports/phase1_characterization.json
"""
import json
import os

import numpy as np
from scipy.optimize import least_squares

import analyze as A
import gen_test_signal as G
import captures as C

R5_OHMS = 3600.0  # circuit.md: source degeneration resistor ("3k6", re-read off the crop 2026-09-07)
EXPECTED_ZERO_22N = 1.0 / (2 * np.pi * R5_OHMS * 22e-9)  # C1 branch, ~2.01 kHz
EXPECTED_ZERO_10N = 1.0 / (2 * np.pi * R5_OHMS * 10e-9)  # C2 branch, ~4.42 kHz

# C4 into (R4 + R3): the plugin drives the input from an ideal source, so R3 is in series with R4.
# This pole is a KNOWN nuisance term in the LF fit, not a free parameter -- pinning it is what makes
# the C10 pole identifiable from a single curve.
F_INPUT_HP = 1.0 / (2 * np.pi * (1.0e6 + 110.0e3) * 22e-9)  # 6.52 Hz

# circuit.md stage-3 solve at the power-law taper (p = 2.0), for the three captured knob positions.
PREDICTED_C10_HZ = {"p1": 24.4, "p2": 13.3, "p3": 28.5}

# Rotation of each captured unit's VOLUME knob, 7 o'clock = 0 to 5 o'clock = 1.
VOLUME_X = {"p1": 0.35, "p2": 0.75, "p3": 0.30}

# The input network's own low-pass, pinned when localising any EXTRA high-frequency pole a capture
# carries on top of it (circuit.md stage 1: R3 || R4 into C3).
F_INPUT_LP = 7300.0


def output_impedance(x, r_drain=20.0e3, p=2.0):
    """Source impedance looking back into the OUT jack at rotation x -- circuit.md stage 3.

    NOT the ~6 kOhm a conventional wiper-to-output divider gives: here the wiper is grounded and R9
    bridges node E to the jack, so R9 alone floors this near 110 kOhm and the pot barely moves it.
    That is why calibration-and-gain-staging.md section 4's "output load is negligible" does not
    apply to this pedal, and why an ordinary cable puts a pole in the audio band.
    """
    ra = max(500.0e3 * x ** p, 1.0)          # VOLUME lug 1 -> wiper, shunting node E
    rb = 500.0e3 - ra                        # wiper -> lug 3, in series with R8 shunting OUT
    inv_e = 1.0 / 240.0e3 + 1.0 / ra + (1.0 / r_drain if np.isfinite(r_drain) else 0.0)
    return 1.0 / (1.0 / (110.0e3 + rb) + 1.0 / (110.0e3 + 1.0 / inv_e))


def fit_extra_hf_pole(f, mag):
    """Fit (input pole PINNED at its computed value) x (one free extra pole) over 1-20 kHz.

    A capture that carries nothing but the pedal returns a pole far above the band. A capture whose
    chain loaded the pedal's ~100 kOhm output returns one inside it, and the capacitance that pole
    implies is the check on whether that reading is physically ordinary or absurd.
    """
    fs, d = grid(f, mag, 1000.0, 20000.0, 200)
    d = d - float(A.gain_at(f, mag, 1000.0))

    def model(p, v):
        return (p[1] - 10 * np.log10(1 + (v / F_INPUT_LP) ** 2)
                     - 10 * np.log10(1 + (v / np.exp(p[0])) ** 2))

    r = least_squares(lambda p: model(p, fs) - d, [np.log(20000.0), 0.0])
    return {"extra_pole_hz": float(np.exp(r.x[0])),
            "fit_rms_db": float(np.sqrt(np.mean((model(r.x, fs) - d) ** 2)))}


# --- shared loading ------------------------------------------------------------------------------
def load_all():
    orig = A.load(A.ORIG)
    out = {}
    for path, meta in C.find_captures():
        x = A.load(path)
        full = A.is_full_length(x, orig)
        aligned, lag = A.align(x, orig)
        out[path] = {"meta": meta, "audio": aligned, "lag": lag, "full_length": full}
    return orig, out


def sweep_curve(aligned, orig, seg="sweep_clean"):
    """Continuous Farina FR (f, mag_db) for one capture's clean sweep."""
    return A.sweep_fr(A.seg_of(aligned, seg, settled=False), A.seg_of(orig, seg, settled=False))


def grid(f, mag, lo, hi, n):
    """Resample a continuous curve onto a log frequency grid, so a fit weights octaves evenly."""
    fs = np.geomspace(lo, hi, n)
    return fs, np.array([A.gain_at(f, mag, p) for p in fs])


# --- M1 / M2: the mode differential is a first-order shelf ----------------------------------------
def fit_mode_shelf(f, mag_mode, mag_dark):
    """Fit (1 + jf/fz)/(1 + jf/fp) to a mode-minus-dark differential.

    Theory (circuit.md stage 2 + JfetStage.h): with Zs = R5 || 1/sC the ratio of a bypassed mode to
    DARK is exactly this shelf, with the zero at 1/(2*pi*R5*C) and the pole K0 x above it, where
    K0 = 1 + gm*R5. So BOTH numbers we need come out of one two-parameter fit, and the plateau is an
    extrapolation of the fit rather than a band average of a region that has not settled.
    """
    fs, d = grid(f, mag_mode - mag_dark, 150.0, 20000.0, 260)
    d = d - float(np.mean(d[fs < 400.0]))  # pin the differential's DC asymptote to 0 dB

    def model(p, x):
        fz, fp = np.exp(p)
        return 10 * np.log10((1 + (x / fz) ** 2) / (1 + (x / fp) ** 2))

    r = least_squares(lambda p: model(p, fs) - d, np.log([3000.0, 15000.0]))
    fz, fp = np.exp(r.x)
    rms = float(np.sqrt(np.mean((model(r.x, fs) - d) ** 2)))
    above = np.where(d >= 3.0)[0]
    return {
        "zero_hz": float(fz),
        "pole_hz": float(fp),
        "K0": float(fp / fz),          # plateau, linear -- equals 1 + gm*R5 for an ideal Norton drain
        "K0_db": float(20 * np.log10(fp / fz)),
        "fit_rms_db": rms,
        # A threshold read that needs no plateau: for a shelf this tall the +3 dB-over-DC point sits
        # within 3% of the zero, so it cross-checks the fit without inheriting its assumptions.
        "plus3db_over_dc_hz": float(fs[above[0]]) if len(above) else None,
        "implied_R5C_us": float(1e6 / (2 * np.pi * fz)),
    }


# --- M3 / M4: dark-curve corners, fitted rather than thresholded -----------------------------------
def fit_hf(f, mag):
    """First-order low-pass fit over 1-20 kHz, plus the octave slopes that say whether it IS one."""
    fs, d = grid(f, mag, 1000.0, 20000.0, 200)
    d = d - float(A.gain_at(f, mag, 1000.0))

    def model(p, x):
        return p[1] - 10 * np.log10(1 + (x / np.exp(p[0])) ** 2)

    r = least_squares(lambda p: model(p, fs) - d, [np.log(7000.0), 0.0])
    slope = lambda a, b: float((np.interp(b, fs, d) - np.interp(a, fs, d)) / np.log2(b / a))
    return {
        "lp_corner_hz": float(np.exp(r.x[0])),
        "fit_rms_db": float(np.sqrt(np.mean((model(r.x, fs) - d) ** 2))),
        "slope_6k_12k_db_per_oct": slope(6000.0, 12000.0),
        "slope_10k_19k_db_per_oct": slope(10000.0, 19000.0),
    }


def fit_lf(f, mag):
    """C10's high-pass corner, with the input network's C4/R4 pole PINNED at its computed value.

    Fitting two free poles to one LF curve is ill-conditioned (it splits 20% differently between
    modes of the same unit, which cannot be real -- MODE does not touch C10). Pinning the known
    nuisance pole makes the C10 corner a single well-determined parameter.
    """
    fs, d = grid(f, mag, 18.0, 400.0, 140)
    d = d - d[-1]

    def model(p, x):
        return (p[1]
                + 10 * np.log10(x ** 2 / (x ** 2 + np.exp(p[0]) ** 2))
                + 10 * np.log10(x ** 2 / (x ** 2 + F_INPUT_HP ** 2)))

    r = least_squares(lambda p: model(p, fs) - d, [np.log(30.0), 0.0])
    return {
        "c10_corner_hz": float(np.exp(r.x[0])),
        "fit_rms_db": float(np.sqrt(np.mean((model(r.x, fs) - d) ** 2))),
        "input_pole_pinned_hz": float(F_INPUT_HP),
    }


# --- M5: harmonics, WITH a floor audit -------------------------------------------------------------
def harmonic_vs_level(aligned, label=800):
    """H2 re fundamental across the whole level ladder. A square law must give ~1 dB of H2 per dB of
    input; an H2 that does not move with level is the NAM model's own error floor, not the pedal."""
    f0 = A._comp_freq(label)
    rows = []
    for db in G.COMP_LEVELS_DB:
        name = G.comp_name(label, db)
        if name not in A.T:
            continue
        h = A.harmonics(A.seg_of(aligned, name), f0)
        rows.append({"in_db": db, "h2_dbc": float(h[2] - h[1]), "h3_dbc": float(h[3] - h[1]),
                     "h4_dbc": float(h[4] - h[1])})
    if len(rows) >= 2:
        top = [r for r in rows if r["in_db"] >= -16]
        slope = (float(np.polyfit([r["in_db"] for r in top], [r["h2_dbc"] for r in top], 1)[0])
                 if len(top) >= 2 else None)
    else:
        slope = None
    return {"rows": rows, "h2_slope_db_per_db_top": slope}


def harmonic_floor_audit(aligned, labels=(200, 800), db=-6):
    """H4 > H3 is impossible for a mild polynomial nonlinearity. Wherever it happens, everything
    above H2 is the model's error floor and no cubic can be read off it."""
    out = {}
    for label in labels:
        name = G.comp_name(label, db)
        if name not in A.T:
            continue
        h = A.harmonics(A.seg_of(aligned, name), A._comp_freq(label))
        dbc = {k: float(h[k] - h[1]) for k in range(2, 9)}
        out[label] = {"dbc": dbc, "h4_exceeds_h3": dbc[4] > dbc[3],
                      "floor_dbc_est": float(np.median([dbc[k] for k in range(3, 9)]))}
    return out


def compression_knee(aligned, labels=(80, 800, 3150)):
    """Gain change vs level. Reported as the top cell's value, not as a slope over the whole ladder:
    the curve is flat to within 0.01 dB and then bends only in the last cell, so a straight-line fit
    dilutes a real knee into a meaningless slope (the first version of this script did exactly that
    and reported -0.001 dB/dB)."""
    out = {}
    for label in labels:
        c = A.compression_curve(aligned, label)
        out[label] = {"in_db": [float(v) for v in c["in_db"]],
                      "comp_db": [float(v) for v in c["comp_db"]],
                      "top_cell_db": float(c["comp_db"][-1]),
                      "max_below_top_db": float(np.max(np.abs(c["comp_db"][:-1])))}
    return out


# --- M6: unit spread, measured on a quantity that cancels the rig -----------------------------------
def band_spread(f, a_db, b_db):
    centers, ba, na = A.band_average(f, a_db)
    _, bb, nb = A.band_average(f, b_db)
    v = (na > 0) & (nb > 0)
    diff = ba[v] - bb[v]
    return {
        "centers_hz": centers[v].tolist(),
        "diff_db": diff.tolist(),
        "mean_abs_db": float(np.mean(np.abs(diff))),
        "rms_db": float(np.sqrt(np.mean(diff ** 2))),
        "max_abs_db": float(np.max(np.abs(diff))),
        "max_at_hz": float(centers[v][int(np.argmax(np.abs(diff)))]),
    }


def main():
    orig, caps = load_all()
    report = {"sanity": [], "P0": {}, "M1": {}, "M2": {}, "M3": {}, "M3b": {}, "M4": {},
              "M5": {}, "M6": {}}

    print("=== Sanity (M0 substitute: alignment + length, not a null -- see module docstring) ===")
    for path, d in sorted(caps.items()):
        m = d["meta"]
        report["sanity"].append({"path": path, "unit": m["unit"], "mode": m["mode"],
                                 "volume_clock": m["volume_clock"], "full_length": d["full_length"],
                                 "align_lag_samples": d["lag"]})
        print(f"  {path:42s} full_length={d['full_length']!s:5} lag={d['lag']:+d} samples")
    lags = [r["align_lag_samples"] for r in report["sanity"]]
    print(f"  Alignment lag {min(lags):+d}..{max(lags):+d} samples "
          f"({min(lags)/A.FS*1000:+.3f}..{max(lags)/A.FS*1000:+.3f} ms) -- per-render latency, not truncation.")

    curves = {}
    for path, d in caps.items():
        m = d["meta"]
        curves[(m["unit"], m["mode"])] = sweep_curve(d["audio"], orig)

    print("\n=== P0: KNOWN-ANSWER PROBE -- how much of the low end does each NAM model get right? ===")
    print("    Below the shelf zero, Zs = R5 in every mode (the unselected 1 M branch shifts it by")
    print("    <0.5%), so every mode differential MUST read 0.00 dB there. Whatever it reads instead")
    print("    is that model's own LF error, measured with no reference capture and no assumptions.")
    for unit in ("p1", "p2"):
        for mode in ("bright", "mid"):
            f, mm = curves[(unit, mode)]
            d = mm - curves[(unit, "dark")][1]
            vals = {p: float(A.gain_at(f, d, p)) for p in (20, 30, 50, 80, 120, 200)}
            worst = max(abs(v) for v in vals.values())
            report["P0"].setdefault(unit, {})[mode] = {"err_db": vals, "worst_abs_db": worst}
            print(f"  {unit} {mode:6s}-dark: " + " ".join(f"{k}Hz {v:+.2f}" for k, v in vals.items())
                  + f"   worst {worst:.2f} dB")

    print("\n=== M1/M2: mode differential fitted as a first-order shelf ===")
    print(f"    schematic zeros: 22 nF -> {EXPECTED_ZERO_22N:.0f} Hz,  10 nF -> {EXPECTED_ZERO_10N:.0f} Hz")
    for unit in ("p1", "p2"):
        fD, mD = curves[(unit, "dark")]
        report["M1"][unit] = {}
        for mode in ("bright", "mid"):
            f, mm = curves[(unit, mode)]
            s = fit_mode_shelf(f, mm, mD)
            report["M1"][unit][mode] = s
            near = "22nF" if abs(np.log(s["zero_hz"] / EXPECTED_ZERO_22N)) < abs(
                np.log(s["zero_hz"] / EXPECTED_ZERO_10N)) else "10nF"
            print(f"  {unit} {mode:6s}: zero={s['zero_hz']:7.0f} Hz  pole={s['pole_hz']:8.0f} Hz  "
                  f"K0={s['K0']:5.2f} ({s['K0_db']:5.2f} dB)  RMS={s['fit_rms_db']:.2f} dB  "
                  f"-> nearer {near}  (+3dB-over-DC at {s['plus3db_over_dc_hz']:.0f} Hz)")
        b, m = report["M1"][unit]["bright"], report["M1"][unit]["mid"]
        lower = "bright" if b["zero_hz"] < m["zero_hz"] else "mid"
        report["M1"][unit]["lower_zero_label"] = lower
        report["M1"][unit]["cap_ratio_measured"] = float(b["zero_hz"] and m["zero_hz"] / b["zero_hz"])
        print(f"    -> the '{lower}' label carries the LOWER zero, i.e. the 22 nF cap. "
              f"Cap ratio measured {m['zero_hz']/b['zero_hz']:.2f} vs 2.20 drawn.")

    for unit in ("p1", "p2"):
        k0 = float(np.mean([report["M1"][unit][m]["K0"] for m in ("bright", "mid")]))
        report["M2"][unit] = {"K0": k0, "K0_db": float(20 * np.log10(k0)),
                              "gm_S_alpha0": float((k0 - 1) / R5_OHMS)}
        print(f"  {unit}: K0 = 1 + gm*R5 = {k0:.2f} ({20*np.log10(k0):.2f} dB)  "
              f"=> gm = {(k0-1)/R5_OHMS*1e6:.0f} uS at R5 = 3.6 k (a LOWER bound: any finite ro raises it)")
    k0s = [report["M2"][u]["K0"] for u in ("p1", "p2")]
    report["M2"]["summary"] = {"K0_mean": float(np.mean(k0s)), "K0_spread": float(max(k0s) - min(k0s)),
                               "gm_S_mean_alpha0": float((np.mean(k0s) - 1) / R5_OHMS)}
    print(f"  mean K0 = {np.mean(k0s):.2f}, unit spread {max(k0s)-min(k0s):.2f} "
          f"({20*np.log10(max(k0s)/min(k0s)):.2f} dB)")

    print("\n=== M3: input LP corner, fitted first-order + an order audit ===")
    for unit in sorted({u for u, _ in curves}):
        key = (unit, "dark") if (unit, "dark") in curves else (unit, "mid")
        h = fit_hf(*curves[key])
        report["M3"][unit] = dict(h, mode_used=key[1])
        print(f"  {unit}/{key[1]:6s}: LP = {h['lp_corner_hz']:6.0f} Hz  RMS={h['fit_rms_db']:.2f} dB  "
              f"slope 6-12k={h['slope_6k_12k_db_per_oct']:+5.1f}  10-19k={h['slope_10k_19k_db_per_oct']:+5.1f} dB/oct "
              f"(first order is -6; schematic predicts ~7300 Hz)")

    print("\n=== M3b: is the extra HF rolloff the VOLUME knob, or the load on a ~100 k output? ===")
    print("    circuit.md stage 3: the OUT source impedance barely moves with VOLUME, because R9")
    print("    (110 k) bridges to the jack and floors it. Tabulated across every drain-drive")
    print("    assumption, so the conclusion does not rest on which one is right:")
    for x_label, x in (("10:00", 0.30), ("10:30", 0.35), ("2:30", 0.75)):
        zs = [output_impedance(x, r) for r in (20.0e3, 100.0e3, float("inf"))]
        print(f"      VOL {x_label}: Zout = " + " / ".join(f"{z/1e3:5.0f}k" for z in zs)
              + "  (drain drive 20k / 100k / ideal)")
    allz = [output_impedance(x, r) for x in VOLUME_X.values()
            for r in (20.0e3, 100.0e3, float("inf"))]
    report["M3b"] = {"zout_span_ohm": [float(min(allz)), float(max(allz))],
                     "zout_span_ratio": float(max(allz) / min(allz)), "per_capture": {}}
    print(f"    -> across ALL of them Zout spans {min(allz)/1e3:.0f}k..{max(allz)/1e3:.0f}k, "
          f"a factor of {max(allz)/min(allz):.2f}. VOLUME cannot move the top octave by more than that.")
    for unit in ("p1", "p2", "p3"):
        key = (unit, "dark") if (unit, "dark") in curves else (unit, "mid")
        ex = fit_extra_hf_pole(*curves[key])
        z = output_impedance(VOLUME_X[unit])
        cload = 1.0 / (2 * np.pi * z * ex["extra_pole_hz"])
        report["M3b"]["per_capture"][unit] = dict(ex, mode_used=key[1], zout_ohm=z,
                                                  implied_cload_f=float(cload))
        print(f"    {unit}/{key[1]:6s} (VOL x={VOLUME_X[unit]:.2f}): extra pole "
              f"{ex['extra_pole_hz']:8.0f} Hz  RMS={ex['fit_rms_db']:.2f} dB  "
              f"-> implies {cload*1e12:5.0f} pF at Zout {z/1e3:.0f}k")
    print("    (a few tens of pF is 'no load'; a few hundred is an ordinary cable run)")

    print("\n=== M4: C10 high-pass corner, input pole pinned at %.2f Hz ===" % F_INPUT_HP)
    for (unit, mode) in sorted(curves):
        lf = fit_lf(*curves[(unit, mode)])
        clock = next(d["meta"]["volume_clock"] for d in caps.values()
                     if d["meta"]["unit"] == unit and d["meta"]["mode"] == mode)
        pred = PREDICTED_C10_HZ[unit]
        report["M4"].setdefault(unit, {})[mode] = dict(lf, volume_clock=clock, predicted_hz=pred,
                                                       ratio_to_prediction=lf["c10_corner_hz"] / pred)
        print(f"  {unit}/{mode:6s} (VOL {clock}): {lf['c10_corner_hz']:5.1f} Hz  vs predicted {pred:4.1f}  "
              f"ratio {lf['c10_corner_hz']/pred:.2f}x   RMS={lf['fit_rms_db']:.3f} dB")
    print("  (MODE does not touch C10, so the within-unit spread across modes bounds this method's error.)")

    print("\n=== M5: harmonics, level dependence, and a floor audit ===")
    for unit in ("p1", "p2"):
        aud = next(d["audio"] for d in caps.values()
                   if d["meta"]["unit"] == unit and d["meta"]["mode"] == "dark")
        floor = harmonic_floor_audit(aud)
        lvl = harmonic_vs_level(aud)
        comp = compression_knee(aud)
        report["M5"][unit] = {"floor_audit": floor, "h2_vs_level": lvl, "compression": comp}
        for label, fa in floor.items():
            print(f"  {unit} @{label} Hz: " + " ".join(f"H{k}={v:+6.1f}" for k, v in fa["dbc"].items())
                  + ("   <- H4 > H3: everything above H2 is the model's floor" if fa["h4_exceeds_h3"] else ""))
        s = lvl["h2_slope_db_per_db_top"]
        print(f"    H2 vs level (top 4 cells): {s:+.2f} dB/dB "
              f"(a square law gives +1.0; ~0 means H2 is floor, not signal)")
        print(f"    compression, top cell: " + " ".join(
            f"{k}Hz {v['top_cell_db']:+.3f} (below top max {v['max_below_top_db']:.3f})"
            for k, v in comp.items()))

    print("\n=== M6: unit spread ===")
    print("  (a) on the MODE DIFFERENTIAL, which cancels rig gain and converter response:")
    for mode in ("bright", "mid"):
        f = curves[("p1", mode)][0]
        d1 = curves[("p1", mode)][1] - curves[("p1", "dark")][1]
        d2 = curves[("p2", mode)][1] - curves[("p2", "dark")][1]
        sp = band_spread(f, d1, d2)
        report["M6"].setdefault("differential", {})[mode] = sp
        print(f"    {mode}-dark, p1 vs p2: mean|d|={sp['mean_abs_db']:.2f}  RMS={sp['rms_db']:.2f}  "
              f"max|d|={sp['max_abs_db']:.2f} dB at {sp['max_at_hz']:.0f} Hz")
    print("  (b) on the ABSOLUTE response, which does NOT -- three units means three rigs:")
    f1, m1 = curves[("p1", "dark")]
    f2, m2 = curves[("p2", "dark")]
    sp = band_spread(f1, m1 - A.gain_at(f1, m1, 1000.0), m2 - A.gain_at(f2, m2, 1000.0))
    report["M6"]["absolute_dark"] = sp
    lo = np.array(sp["centers_hz"]) < 2000.0
    dd = np.array(sp["diff_db"])
    report["M6"]["absolute_dark_below_2k"] = {"mean_abs_db": float(np.mean(np.abs(dd[lo]))),
                                              "max_abs_db": float(np.max(np.abs(dd[lo])))}
    print(f"    dark, p1 vs p2: mean|d|={sp['mean_abs_db']:.2f}  RMS={sp['rms_db']:.2f}  "
          f"max|d|={sp['max_abs_db']:.2f} dB at {sp['max_at_hz']:.0f} Hz")
    print(f"    below 2 kHz only: mean|d|={report['M6']['absolute_dark_below_2k']['mean_abs_db']:.2f}  "
          f"max|d|={report['M6']['absolute_dark_below_2k']['max_abs_db']:.2f} dB")
    print("    -> the tolerance band comes from (a). (b) is dominated by the trainers' rigs, not by")
    print("       the two pedals, and using it would set an artificially loose band.")

    os.makedirs("analysis/reports", exist_ok=True)
    out_path = "analysis/reports/phase1_characterization.json"
    with open(out_path, "w") as fh:
        json.dump(report, fh, indent=2, default=str)
    print(f"\nWrote {out_path}")


if __name__ == "__main__":
    main()
