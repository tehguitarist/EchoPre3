#!/usr/bin/env python3
"""Plugin-vs-capture PHASE evaluation — the axis comprehensive_report.py does not cover.

Why this exists as its own script. `comprehensive_report.py` compares magnitude (FR) and harmonic
content (THD). Both are structurally BLIND to a polarity flip and to dispersion: a stage wired 180
degrees out has a bit-identical magnitude response. This project has already shipped exactly that
bug once (the input WDF network's inverter, caught only by a phase test -- see circuit.md), so the
phase axis is measured here rather than assumed.

Three things are reported, in increasing order of how much they can be trusted:

  1. POLARITY of every capture and every render. Absolute, unambiguous, and the first thing to read
     -- an inverted render nulls at about +6 dB and reads as a catastrophic modelling error.
  2. ABSOLUTE phase error, plugin vs capture, with the best-fit PURE DELAY removed. Raw phase error
     is dominated by bulk latency (the NAM plugin's own, the oversampler's, the DAW's), which is a
     linear-phase term a sub-sample null aligns out anyway. What remains after removing it is
     dispersion: real disagreement about the circuit's phase.
  3. The MODE DIFFERENTIAL phase, within one unit. This is the rig-cancelling measurement -- the
     trainer's converters, the reamp chain and the unit's own variance all divide out -- and it is
     the only phase number here not limited by the capture rig. circuit.md note #7 uses the
     magnitude version of exactly this ratio.

Two floors are MEASURED rather than assumed, because this project has twice had a correct
implementation fail an asymmetric comparison (closed form vs measurement):

  F1. The two phase estimators (Farina-gated linear IR, and Welch/CSD cross-spectrum) are run on the
      same pair. Where they disagree, neither is evidence.
  F2. Below the mode shelf's zero every MODE position has Zs = R5, so the mode differential must be
      0 degrees below ~200 Hz by construction. Whatever it reads instead is that NAM model's own
      phase error, obtained with no reference capture. (circuit.md note #7's magnitude twin.)

Run from the repo root:
    .venv/bin/python analysis/phase_sweep.py [--os 8]
Writes analysis/reports/phase_sweep.json
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import gen_test_signal as G

OUTPUT_JSON = "analysis/reports/phase_sweep.json"

# Phase is read over this band only. Below 30 Hz the NAM models see under two cycles of a 13 Hz
# corner inside their 132 ms receptive field (build-plan limit L3); above 15 kHz the capture carries
# the trainer's converters, not the pedal.
BAND_LO, BAND_HI = 30.0, 15000.0
# The delay fit runs over the midband only, where coherence is highest, then is removed everywhere.
FIT_LO, FIT_HI = 100.0, 8000.0
REPORT_FREQS = (50, 100, 200, 500, 1000, 2000, 4000, 8000, 12000, 15000)
# F2's known-zero window: below the lowest mode shelf zero (1.86 kHz), with margin.
LF_KNOWN_ZERO_HI = 200.0


def render(binary, parsed, os_factor, out_path):
    # in/out are POSITIONAL on this CLI (see comprehensive_report.render_plugin), not flags.
    args = [binary, A.ORIG, out_path, "--os", str(os_factor)]
    args += C.render_args(parsed)
    subprocess.run(args, check=True, capture_output=True)
    return A.load(out_path)


def farina_spectrum(out_seg, ref_seg):
    """Complex spectrum of the LINEAR path, harmonic impulses gated out.

    Preferred over a raw cross-spectrum on a driven system: the Farina gate discards the harmonic
    impulse responses, which sit before the linear one, so harmonic energy cannot contaminate the
    phase estimate. The gate's own time origin is a fixed offset -- a pure delay, removed by the fit.
    """
    ir, nfft = A.farina_linear_ir(out_seg, ref_seg)
    spec = np.fft.rfft(ir, nfft)
    return np.fft.rfftfreq(nfft, 1.0 / A.FS), spec


def csd_spectrum(out_seg, ref_seg):
    f, H = A.transfer_complex(out_seg, ref_seg)
    return f, H


def _interp_complex(f_src, H_src, f_dst):
    re = np.interp(f_dst, f_src, H_src.real)
    im = np.interp(f_dst, f_src, H_src.imag)
    return re + 1j * im


def phase_error_curve(H_a, H_b, f, fit_lo=FIT_LO, fit_hi=FIT_HI):
    """Phase of (H_a / H_b) in degrees, with the best-fit pure delay AND constant offset removed.

    Returns (residual_deg, fitted_delay_ms, raw_deg, fitted_offset_deg). The fit is weighted by
    |H_b| so a band where the reference has no energy cannot steer the delay estimate.

    ⚠ THE RESIDUAL IS DELIBERATELY BLIND TO A POLARITY FLIP. Removing the constant term is what
    makes the number mean "dispersion" rather than "latency plus dispersion", but a 180 degree flip
    IS a constant, so it lands in `fitted_offset_deg` and leaves the residual almost unchanged.
    That is the same blindness a magnitude-only FR has, one level up -- so polarity is reported
    separately and unconditionally by `A.polarity()`, and the fitted offset is returned here rather
    than discarded so the two can never be confused. Read polarity FIRST.
    """
    ratio = H_a / (H_b + 1e-30)
    raw = np.unwrap(np.angle(ratio))
    m = (f >= fit_lo) & (f <= fit_hi)
    if m.sum() < 8:
        return np.degrees(raw), float("nan"), np.degrees(raw), float("nan")
    w = np.abs(H_b[m])
    # Weighted least squares on phase vs frequency: slope -> pure delay, intercept -> constant offset.
    Xf = f[m]
    Y = raw[m]
    W = w / (w.sum() + 1e-30)
    xm = np.sum(W * Xf)
    ym = np.sum(W * Y)
    slope = np.sum(W * (Xf - xm) * (Y - ym)) / (np.sum(W * (Xf - xm) ** 2) + 1e-30)
    intercept = ym - slope * xm
    resid = raw - (slope * f + intercept)
    delay_ms = -slope / (2 * np.pi) * 1000.0
    # Wrap the constant into (-180, 180] so a flip reads as ~+-180, not as an unwrap multiple.
    offset_deg = float((np.degrees(intercept) + 180.0) % 360.0 - 180.0)
    return np.degrees(resid), float(delay_ms), np.degrees(raw), offset_deg


def band_stats(f, deg, lo=BAND_LO, hi=BAND_HI):
    m = (f >= lo) & (f <= hi)
    d = deg[m]
    if d.size == 0:
        return {}
    return {
        "rms_deg": float(np.sqrt(np.mean(d ** 2))),
        "peak_deg": float(np.max(np.abs(d))),
        "peak_hz": float(f[m][int(np.argmax(np.abs(d)))]),
    }


def at_freqs(f, deg, freqs=REPORT_FREQS):
    return {str(int(t)): float(np.interp(t, f, deg)) for t in freqs}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    args = ap.parse_args()

    orig = A.load(A.ORIG)
    ref = A.seg_of(orig, "sweep_clean")
    caps = C.find_captures()
    if not caps:
        sys.exit("no captures found")

    results = {}
    tmpdir = tempfile.mkdtemp(prefix="phase_sweep_")

    print(f"Phase sweep: {len(caps)} captures | OS={args.os}x | band {BAND_LO:.0f}-{BAND_HI:.0f} Hz")

    for path, parsed in caps:
        name = os.path.splitext(os.path.basename(path))[0]
        cap = C.load_capture(path)
        if not A.is_full_length(cap, orig):
            print(f"  {name}: TRUNCATED, skipped")
            continue
        cap_al, cap_lag = A.align(cap, orig)

        ren_path = os.path.join(tmpdir, name + "_render.wav")
        ren = render(args.bin, parsed, args.os, ren_path)
        ren_al, ren_lag = A.align(ren, orig)

        cap_seg = A.seg_of(cap_al, "sweep_clean")
        ren_seg = A.seg_of(ren_al, "sweep_clean")

        # 1. Polarity, absolute, before anything else is read.
        cap_pol, cap_ang = A.polarity(cap_seg, ref)
        ren_pol, ren_ang = A.polarity(ren_seg, ref)

        # 2. Absolute phase error, two independent estimators (floor F1).
        f_far, Hc_far = farina_spectrum(cap_seg, ref)
        _, Hr_far = farina_spectrum(ren_seg, ref)
        f_csd, Hc_csd = csd_spectrum(cap_seg, ref)
        _, Hr_csd = csd_spectrum(ren_seg, ref)

        band = (f_far >= BAND_LO) & (f_far <= BAND_HI)
        f_b = f_far[band]
        res_far, delay_far, raw_far, off_far = phase_error_curve(Hr_far[band], Hc_far[band], f_b)

        Hc_c = _interp_complex(f_csd, Hc_csd, f_b)
        Hr_c = _interp_complex(f_csd, Hr_csd, f_b)
        res_csd, delay_csd, _, off_csd = phase_error_curve(Hr_c, Hc_c, f_b)

        estimator_gap = band_stats(f_b, res_far - res_csd)

        results[name] = {
            "unit": parsed["unit"],
            "mode": parsed["mode"],
            "volume_clock": parsed["volume_clock"],
            "align_lag_capture": int(cap_lag),
            "align_lag_render": int(ren_lag),
            "polarity": {
                "capture": {"sign": cap_pol, "midband_deg": cap_ang},
                "render": {"sign": ren_pol, "midband_deg": ren_ang},
                "match": cap_pol == ren_pol,
            },
            "absolute_phase": {
                "farina": {
                    "residual": band_stats(f_b, res_far),
                    "at_freqs_deg": at_freqs(f_b, res_far),
                    "fitted_delay_ms": delay_far,
                    "fitted_offset_deg": off_far,
                    "raw_peak_deg": float(np.max(np.abs(raw_far))),
                },
                "csd": {
                    "residual": band_stats(f_b, res_csd),
                    "at_freqs_deg": at_freqs(f_b, res_csd),
                    "fitted_delay_ms": delay_csd,
                    "fitted_offset_deg": off_csd,
                },
                "estimator_gap_F1": estimator_gap,
            },
            "_spec": {"f": f_b, "cap": Hc_far[band], "ren": Hr_far[band]},
        }
        print(f"  {name}: cap pol {cap_pol:+d} ({cap_ang:+.1f} deg), ren pol {ren_pol:+d} "
              f"({ren_ang:+.1f} deg) "
              f"| resid {band_stats(f_b, res_far)['rms_deg']:.2f} deg RMS "
              f"| const {off_far:+.1f} deg | F1 gap {estimator_gap['rms_deg']:.2f} deg RMS")

    # 3. Mode differential phase, within unit (rig cancels), plus floor F2.
    by_unit = defaultdict(dict)
    for name, r in results.items():
        by_unit[r["unit"]][r["mode"]] = r

    differentials = {}
    for unit, modes in sorted(by_unit.items()):
        if "dark" not in modes:
            continue
        dark = modes["dark"]
        for mode in ("bright", "mid"):
            if mode not in modes:
                continue
            m = modes[mode]
            f_b = m["_spec"]["f"]
            # ratio-of-ratios: (mode/dark) for the capture vs the same for the plugin.
            d_cap = m["_spec"]["cap"] / (dark["_spec"]["cap"] + 1e-30)
            d_ren = m["_spec"]["ren"] / (dark["_spec"]["ren"] + 1e-30)
            res, delay, raw, off = phase_error_curve(d_ren, d_cap, f_b)

            # F2: below the shelf zero the differential is Zs = R5 in both positions -> 0 degrees.
            lf = (f_b >= BAND_LO) & (f_b <= LF_KNOWN_ZERO_HI)
            cap_lf = np.degrees(np.unwrap(np.angle(d_cap)))[lf]
            ren_lf = np.degrees(np.unwrap(np.angle(d_ren)))[lf]

            differentials[f"{unit}_{mode}_vs_dark"] = {
                "unit": unit,
                "mode": mode,
                "residual": band_stats(f_b, res),
                "at_freqs_deg": at_freqs(f_b, res),
                "fitted_delay_ms": delay,
                "fitted_offset_deg": off,
                "floor_F2_known_zero": {
                    "capture_peak_deg": float(np.max(np.abs(cap_lf))) if cap_lf.size else None,
                    "plugin_peak_deg": float(np.max(np.abs(ren_lf))) if ren_lf.size else None,
                    "window_hz": [BAND_LO, LF_KNOWN_ZERO_HI],
                },
            }
            print(f"  DIFF {unit} {mode}/dark: residual {res[(f_b>=BAND_LO)&(f_b<=BAND_HI)].std():.2f} "
                  f"deg SD, peak {band_stats(f_b,res)['peak_deg']:.2f} deg @ "
                  f"{band_stats(f_b,res)['peak_hz']:.0f} Hz | F2 cap floor "
                  f"{np.max(np.abs(cap_lf)):.2f} deg")

    for r in results.values():
        r.pop("_spec", None)

    payload = {
        "generated": datetime.now(timezone.utc).isoformat(),
        "os_factor": args.os,
        "band_hz": [BAND_LO, BAND_HI],
        "fit_band_hz": [FIT_LO, FIT_HI],
        "captures": results,
        "mode_differentials": differentials,
    }
    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    with open(OUTPUT_JSON, "w") as fh:
        json.dump(payload, fh, indent=2)
    print(f"wrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
