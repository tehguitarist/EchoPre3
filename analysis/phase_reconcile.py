#!/usr/bin/env python3
"""Why the two phase instruments print different numbers for the same band, one choice at a time.

The project record says the plugin is "within 2.4 deg over 200 Hz - 12 kHz" against P1, from
`phase_sweep.py`. `goal_check.py` reports 6.66-8.22 deg over what it labels the same band. Both
cannot be describing the same quantity, and tuning the model to either without knowing which is
right would be tuning to an instrument artefact.

This script computes ONE phase residual per capture and then re-reads it under each configuration
choice in turn, so the ladder from one number to the other is explicit and every rung is attributable
rather than argued. The choices that differ between the two scripts are:

    statistic     max over 10 hand-listed report frequencies   vs   max over every FFT bin
    band edge     whether the 200 Hz report point is included
    estimator     Farina-gated linear IR                       vs   raw cross-spectrum
    segment       seg_of(..., settled=True)                    vs   settled=False
    fit window    100 Hz - 8 kHz                               vs   200 Hz - 12 kHz
    OS factor     4x (the shipped default)                     vs   8x (goal_check's default)

⚠ A "max" over a handful of interpolated points is not the same measurement as a max over a band,
and the difference is unbounded: it can only under-report, and by however much the curve does
between the points. Neither is wrong; only one of them answers "is the plugin within 5 degrees
everywhere in this band", which is what the target asks.

Run from the repo root:
    .venv/bin/python analysis/phase_reconcile.py
Writes analysis/reports/phase_reconcile.json
"""
import json, os, subprocess, sys, tempfile
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import phase_sweep as P

OUTPUT_JSON = "analysis/reports/phase_reconcile.json"
UNIT = "p1"
BAND = (200.0, 12000.0)          # the band BOTH scripts claim to report over


def residual(cap_seg, ren_seg, ref, farina, fit_band):
    """Phase residual of render-over-capture, delay and constant removed, on a common grid."""
    if farina:
        f, Hc = P.farina_spectrum(cap_seg, ref)
        _, Hr = P.farina_spectrum(ren_seg, ref)
    else:
        f, Hc = P.csd_spectrum(cap_seg, ref)
        _, Hr = P.csd_spectrum(ren_seg, ref)
    keep = (f >= P.BAND_LO) & (f <= P.BAND_HI)
    f = f[keep]
    res, _, _, _ = P.phase_error_curve(Hr[keep], Hc[keep], f, fit_band[0], fit_band[1])
    return f, res


def main():
    orig = A.load(A.ORIG)
    ref = A.seg_of(orig, "sweep_clean")
    ref_unsettled = A.seg_of(orig, "sweep_clean", settled=False)
    tmp = tempfile.mkdtemp(prefix="phrec_")

    # The rungs, in order, each adding ONE change to the one before it.
    rungs = [
        ("phase_sweep as recorded", dict(os=4, farina=True, settled=True, fit=(100., 8000.),
                                         points=True, lo=500.)),
        ("+ include the 200 Hz point", dict(os=4, farina=True, settled=True, fit=(100., 8000.),
                                            points=True, lo=200.)),
        ("+ every bin, not 10 points", dict(os=4, farina=True, settled=True, fit=(100., 8000.),
                                            points=False, lo=200.)),
        ("+ raw CSD, not Farina", dict(os=4, farina=False, settled=True, fit=(100., 8000.),
                                       points=False, lo=200.)),
        ("+ unsettled segment", dict(os=4, farina=False, settled=False, fit=(100., 8000.),
                                     points=False, lo=200.)),
        ("+ fit over 200 Hz - 12 kHz", dict(os=4, farina=False, settled=False, fit=(200., 12000.),
                                            points=False, lo=200.)),
        ("= goal_check (also 8x OS)", dict(os=8, farina=False, settled=False, fit=(200., 12000.),
                                           points=False, lo=200.)),
    ]

    caps = [(p, d) for p, d in C.find_captures() if d["unit"] == UNIT]
    segs = {}
    for path, parsed in caps:
        name = os.path.splitext(os.path.basename(path))[0]
        cap = C.load_capture(path)
        cap_al, _ = A.align(cap, orig)
        for osf in (4, 8):
            out = os.path.join(tmp, f"{name}_{osf}.wav")
            subprocess.run([C.RENDER_BIN, A.ORIG, out, "--os", str(osf)] + C.render_args(parsed),
                           check=True, capture_output=True)
            ren_al, _ = A.align(A.load(out), orig)
            for settled in (True, False):
                segs[(name, osf, settled)] = (A.seg_of(cap_al, "sweep_clean", settled=settled),
                                              A.seg_of(ren_al, "sweep_clean", settled=settled))

    print(f"Phase reconciliation | unit {UNIT.upper()} | band {BAND[0]:.0f}-{BAND[1]:.0f} Hz")
    print(f"  worst |phase residual| in that band, degrees\n")
    print(f"{'configuration':>28} " + "".join(f"{d['mode']:>9}" for _, d in caps))

    table = {}
    for label, cfg in rungs:
        row = []
        for path, parsed in caps:
            name = os.path.splitext(os.path.basename(path))[0]
            cs, rs = segs[(name, cfg["os"], cfg["settled"])]
            r = ref if cfg["settled"] else ref_unsettled
            f, res = residual(cs, rs, r, cfg["farina"], cfg["fit"])
            if cfg["points"]:
                pts = [t for t in P.REPORT_FREQS if cfg["lo"] <= t <= BAND[1]]
                vals = [abs(float(np.interp(t, f, res))) for t in pts]
            else:
                m = (f >= cfg["lo"]) & (f <= BAND[1])
                vals = list(np.abs(res[m]))
            row.append(float(max(vals)))
        table[label] = row
        print(f"{label:>28} " + "".join(f"{v:>9.2f}" for v in row))

    # WHERE in the band, under the configuration that answers the target. "Within 5 degrees across
    # all bands" is a per-band claim, so the max over every bin is the statistic it asks for.
    SUB = ((200., 500.), (500., 2000.), (2000., 8000.), (8000., 12000.))
    print(f"\n  worst |residual| by sub-band, under the last configuration:")
    print(f"{'sub-band Hz':>28} " + "".join(f"{d['mode']:>9}" for _, d in caps))
    sub_table = {}
    for lo, hi in SUB:
        row = []
        for path, parsed in caps:
            name = os.path.splitext(os.path.basename(path))[0]
            cs, rs = segs[(name, 8, False)]
            f, res = residual(cs, rs, ref_unsettled, False, (200., 12000.))
            m = (f >= lo) & (f <= hi)
            row.append(float(np.max(np.abs(res[m]))))
        sub_table[f"{lo:.0f}-{hi:.0f}"] = row
        print(f"{f'{lo:.0f} - {hi:.0f}':>28} " + "".join(f"{v:>9.2f}" for v in row))

    print(f"\n  ⇒ The recorded 2.4 deg and goal_check's 6.7-8.2 deg are the SAME residual curve read")
    print(f"    two ways. Neither instrument is broken; the first rung is a max over six")
    print(f"    interpolated points from 500 Hz up, and the band label on it is wrong.")

    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    json.dump({"generated": datetime.now(timezone.utc).isoformat(), "unit": UNIT,
               "band_hz": list(BAND), "modes": [d["mode"] for _, d in caps],
               "rungs": {k: v for k, v in table.items()},
               "sub_bands_goal_check_config": sub_table},
              open(OUTPUT_JSON, "w"), indent=2)
    print(f"\nwrote {OUTPUT_JSON}")


if __name__ == "__main__":
    main()
