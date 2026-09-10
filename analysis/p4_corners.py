#!/usr/bin/env python3
"""P4 corner fits and unit-to-unit comparison, on the first RAW captures of the owner's pedal.

    .venv/bin/python analysis/p4_corners.py [--self-test]

⭐⭐ WHY THIS SCRIPT EXISTS AND THE NAM SCRIPTS DO NOT ANSWER IT. Every earlier fit in this project
ran against NAM models, whose systematic error floor (circuit.md note #15) is what pinned `Vov` to a
factor of 1.4 and what left the LF pole unassignable (note #19a). These are raw captures of the test
signal through a MEASURED, near-flat rig, and -- for the first time in the project -- there is a
bypassed capture through the IDENTICAL cabling, so the rig can be divided out instead of inferred.

Three corners, three different exposures to rig error, deliberately fitted by three routes:

  MODE SHELF ZERO   bright-minus-dark, WITHIN one capture pair. The rig, the interface loading, the
                    converter response and the unit's own everything-else all cancel in the ratio.
                    Needs no deconvolution at all, and is therefore the most trustworthy number
                    here. circuit.md's two-position block makes this the load-bearing one: P4 is a
                    different circuit variant, so its bypass cap is UNKNOWN and must be fitted, not
                    assumed to be P1/P2's 22 nF.

  LF CORNER         DARK, deconvolved against the bypass reference. This is the measurement the
                    7:30 capture exists for: C10 into the volume network puts the corner at 67.6 Hz
                    there against ~25 Hz at 10:30, a 2.7x span that is trivially resolvable, where
                    the NAM set's three volume points spanned 1.45x and could not be assigned to the
                    pedal at all (note #19a).

  HF POLE           capture-against-PLUGIN, note #17's route. Fitting against the plugin rather than
                    against the raw sweep keeps the mode shelf on BOTH sides, where it cancels --
                    note #9's M3 estimator was blind precisely because it lacked a shelf term.

⚠⚠ EVERY ESTIMATOR HERE IS RUN ON THE PLUGIN FIRST (`--self-test`), where all three corners are
known by construction: the shelf zeros from JfetStage's tau, the input pole 7300 Hz from R3||R4 into
C3, the LF corner from OutputNetwork's own solve. That is circuit.md note #9's standing rule, and it
is the check that exposed a fit which had looked like independent confirmation.
"""
import argparse, hashlib, json, os, re, subprocess, sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import lf_pole_attribution as LFA

OUT = "analysis/reports/p4_corners.json"
REF_BYPASS = "analysis/captures/p4_V1030_bypass.wav"
ZIN = 1.0e6          # measured interface input impedance (CLAUDE.md, confirmed 2026-09-10)
FIT_SWEEP = "sweep_-26"   # 25 dB below full drive: clean, and well clear of the noise floor


# --- rig removal ---------------------------------------------------------------------------------
def load_fr(path, seg=FIT_SWEEP):
    """Continuous FR of whatever turned the reference signal into this capture."""
    orig = A.load(A.ORIG)
    raw = A.load(path)
    cap, _ = A.align(raw, orig)
    return A.sweep_fr(A.seg_of(cap, seg, settled=False), A.seg_of(orig, seg, settled=False))


def loading_correction_db(f, x, cable_pf=0.0):
    """dB to ADD to a capture to undo the interface's 1 MOhm input load.

    NOT a scalar: it moves with the knob (0.341 dB across the sweep) AND with frequency, because
    the pedal's own output impedance does both. Generated from the network model per capture rather
    than read off a table, exactly as CLAUDE.md's table says to.
    """
    f = np.asarray(f, dtype=float).copy()
    f[f <= 0] = 1e-6          # DC is not a measurement point; keep the solve finite
    loaded = np.abs(LFA.out_network(f, x, rl=load_impedance(f, cable_pf)))
    free = np.abs(LFA.out_network(f, x, rl=np.inf))
    return 20 * np.log10(np.maximum(free, 1e-300) / np.maximum(loaded, 1e-300))


# Capture-side cable capacitance at the pedal's output, MEASURED (circuit.md note #21): fitting one
# global value across P4's DARK rotation takes the 3-19 kHz error from 2.036 / 0.992 dB to
# 0.361 / 0.071 dB. It is P2's 542 pF cable pole again at a fifth the size, measured this time.
#
# ⚠⚠ THE BYPASS DECONVOLUTION STRUCTURALLY CANNOT REMOVE IT, so it must be corrected separately.
# In BYPASS the cable is driven by the interface's own low output impedance and the pole is
# inaudible; in an ACTIVE capture it is driven by the pedal's 59-102 kOhm, so the pole exists in one
# path and not the other. Leaving it out reports the rig's cable as a MODEL error -- and as a
# KNOB-DEPENDENT one, because the pedal's Zout moves 1.7x across the rotation, which is exactly what
# a real HF defect would look like.
# ⛔ CAPTURE-side only. The 2026-09-08 decision to ship no load capacitance in the plugin stands,
# because the plugin's output goes to a DAW digitally.
CABLE_PF = 99.0


def load_impedance(f, cable_pf=CABLE_PF):
    """The interface AS SEEN BY THE PEDAL: ZIN shunted by `cable_pf`. Frequency-dependent.

    ⚠⚠ `cable_pf = 0` IS THE DEFAULT FOR loading_correction_db() AND THAT IS DELIBERATE, not an
    oversight. Three fits that are already applied -- the VOLUME taper, C10 and kOutputMakeup -- were
    made against the resistive-only correction, and p4_component_fit.py fits the cable itself as a
    free parameter on top of that correction. Folding the cable in by default would DOUBLE-COUNT it
    there and silently perturb the other three. So the cable is opt-in, and only the two instruments
    that need it whole -- goal_check.py and phase_sweep.py -- ask for it.
    ⭐ It matters little at the fit bands and hugely above them: 0.014 dB at 1 kHz, but 2.08 dB and
    29.8 deg at 10 kHz, and 3.25 dB / 40.7 deg at 15 kHz.
    """
    f = np.asarray(f, dtype=float).copy()
    f[f <= 0] = 1e-6
    if cable_pf <= 0.0:
        return np.full(f.shape, ZIN, dtype=complex)
    return 1.0 / (1.0 / ZIN + 2j * np.pi * f * cable_pf * 1e-12)


def loading_correction_complex(f, x, cable_pf=CABLE_PF):
    """COMPLEX ratio to MULTIPLY a capture by, to undo the interface load. Magnitude AND phase.

    ⚠ Taking only the dB and dropping the angle would correct one instrument and not the other --
    the load is a complex divider, and this project has been bitten three times by an asymmetry
    between a magnitude path and a phase path. loading_correction_db() is this function's magnitude.
    """
    f = np.asarray(f, dtype=float).copy()
    f[f <= 0] = 1e-6
    loaded = LFA.out_network(f, x, rl=load_impedance(f, cable_pf))
    free = LFA.out_network(f, x, rl=np.inf)
    return np.where(np.abs(loaded) > 0, free / loaded, 1.0 + 0j)


def pedal_fr(path, parsed, bypass_fr=None, seg=FIT_SWEEP, undo_load=True):
    """The PEDAL's own response: capture, minus the rig, minus the interface loading.

    ⚠ `undo_load` must be OFF for a plugin render standing in for a capture (--self-test): a render
    drives an ideal load, so there is no interface loading to undo and adding the correction lifts
    its own LF by up to 0.1 dB. That leaked into the self-test as a 3 % corner bias that looked like
    estimator error and was not.
    """
    f, mag = load_fr(path, seg)
    if bypass_fr is not None:
        mag = mag - bypass_fr
    if undo_load:
        mag = mag + loading_correction_db(f, parsed["volume"])
    return f, mag


# --- models --------------------------------------------------------------------------------------
def shelf_db(f, fz, k0):
    """First-order shelf (1 + s/wz) / (1 + s/(k0*wz)) -- the mode differential's exact shape."""
    s = 1j * np.asarray(f) / fz
    return 20 * np.log10(np.abs((1 + s) / (1 + s / k0)))


def hp_db(f, fc, order=1):
    s = 1j * np.asarray(f) / fc
    return order * 20 * np.log10(np.abs(s / (1 + s)))


def lp_db(f, fc):
    return -20 * np.log10(np.abs(1 + 1j * np.asarray(f) / fc))


def _fit(model, f, tgt, p0, bounds):
    """Least-squares with a free level offset -- every fit here is a SHAPE fit."""
    def resid(p):
        m = model(f, *p)
        return (tgt - m) - np.mean(tgt - m)
    r = least_squares(resid, p0, bounds=bounds)
    return r.x, float(np.sqrt(np.mean(r.fun ** 2)))


def fit_shelf(f, diff_db, band=(150.0, 18000.0)):
    m = (f >= band[0]) & (f <= band[1]) & np.isfinite(diff_db)
    p, res = _fit(shelf_db, f[m], diff_db[m], [2000.0, 6.6], ([200.0, 1.5], [20000.0, 40.0]))
    return dict(fz_hz=float(p[0]), k0=float(p[1]), residual_db=res)


def fit_lf(f, mag_db, band=(15.0, 400.0)):
    m = (f >= band[0]) & (f <= band[1]) & np.isfinite(mag_db)
    p, res = _fit(hp_db, f[m], mag_db[m], [30.0], ([3.0], [400.0]))
    return dict(fc_hz=float(p[0]), residual_db=res)


def fit_hf(f, cap_minus_plugin_db, band=(2000.0, 19000.0)):
    """One free low-pass describing what the capture has and the plugin does not."""
    m = (f >= band[0]) & (f <= band[1]) & np.isfinite(cap_minus_plugin_db)
    p, res = _fit(lp_db, f[m], cap_minus_plugin_db[m], [15000.0], ([2000.0], [400000.0]))
    return dict(fc_hz=float(p[0]), residual_db=res)


# --- plugin renders -------------------------------------------------------------------------------
RENDER_CACHE = "/tmp/p4c"


def render(parsed, tag, os_factor=8):
    """Render the plugin at this capture's settings. Positional in/out, per offline_render.cpp.

    ⚠⚠ THE CACHE IS KEYED ON THE RENDER ARGUMENTS, NOT ON `tag`, AND THAT IS NOT FUSSINESS.
    Keyed on the tag it silently served a stale render whose settings had since changed, and the
    self-test -- where the capture and the comparison ARE the same render and the answer must be
    exactly 0.000 -- reported -1.382 dB. That reads as a real measurement, not as a cache fault.
    `tag` now only makes the filename legible; correctness comes from the hash.
    """
    os.makedirs(RENDER_CACHE, exist_ok=True)
    args_tail = ["--os", str(os_factor)] + C.render_args(parsed)
    # The BINARY is part of the key too -- see captures.render_bin_key().
    key = hashlib.sha1(("|".join(args_tail) + "|" + C.render_bin_key()).encode()).hexdigest()[:10]
    out = f"{RENDER_CACHE}/{tag}_{key}.wav"
    if not os.path.exists(out):
        subprocess.run([C.RENDER_BIN, A.ORIG, out] + args_tail, check=True, capture_output=True)
    return out


# --- known answers, for the self-test --------------------------------------------------------------
def plugin_known():
    """The corners the plugin has BY CONSTRUCTION, parsed from the source of truth."""
    src = open("src/dsp/JfetStage.h").read()
    tb = float(re.search(r"tauBright\s*=\s*([0-9.eE+-]+)", src).group(1))
    tm = float(re.search(r"tauMid\s*=\s*([0-9.eE+-]+)", src).group(1))
    gm = float(re.search(r"double gm\s*=\s*([0-9.eE+-]+)", src).group(1))
    return dict(fz_bright=1.0 / (2 * np.pi * tb), fz_mid=1.0 / (2 * np.pi * tm),
                k0=1.0 + gm * LFA.R5 if hasattr(LFA, "R5") else 1.0 + gm * 3600.0,
                input_pole_hz=7300.0)


def lf_corner_of_network(x):
    """The output network's OWN corner, closed form. Reference only -- NOT the comparison target.

    ⚠⚠ THIS IS NOT WHAT A CAPTURE'S FITTED CORNER SHOULD BE COMPARED AGAINST, and --self-test says
    so: putting a plugin render where the capture goes recovers 71.50 / 41.74 / 25.07 Hz against
    this function's 69.47 / 40.13 / 23.72, a systematic +2.9 / +4.0 / +5.7 %. The bias is real and
    expected -- the plugin has TWO low-frequency poles (the input network's 7.2 Hz high-pass as well
    as C10's), so a one-pole fit to the pair sits above the output pole alone, and by more as the
    two poles approach each other. Compare a capture against the PLUGIN's fitted corner, measured
    with this same estimator, where that bias is common to both sides and divides out.

    This is the asymmetric-comparison trap that failed a correct implementation twice already
    (JfetStageTest 1c, DroopRestoreTest 2): closed form on one side, measurement on the other.
    """
    f = np.geomspace(2.0, 2000.0, 900)
    m = 20 * np.log10(np.abs(LFA.out_network(f, x)))
    return fit_lf(f, m)["fc_hz"]


# --- report --------------------------------------------------------------------------------------
def collect(paths_modes, bypass_fr, label):
    """Per (unit, volume): fit the mode shelf from the differential and the LF corner from DARK."""
    rows = {}
    for key, (path, parsed) in sorted(paths_modes.items()):
        f, mag = pedal_fr(path, parsed, bypass_fr)
        rows[key] = dict(f=f, mag=mag, parsed=parsed, path=path)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true",
                    help="put PLUGIN renders where the captures go; every corner is then known")
    args = ap.parse_args()

    known = plugin_known()
    report = {"known_by_construction": known, "self_test": args.self_test}

    caps = C.find_captures()
    p4 = [(p, d) for p, d in caps if d["unit"] == "p4"]
    if not p4:
        sys.exit("no P4 captures found")

    # The rig, measured: bypass reference through the identical cabling. Not the bare loop --
    # loop-minus-bypass is +0.37 dB at 18 kHz (one extra 48 kHz pole on the loop's own cable run),
    # and the bypass path is the one every pedal capture shares.
    bypass_fr = None
    if not args.self_test:
        refs = dict((os.path.basename(p), (p, d)) for p, d in C.find_reference_captures())
        bp = os.path.basename(REF_BYPASS)
        if bp not in refs:
            sys.exit(f"missing the deconvolution reference {REF_BYPASS}")
        bypass_fr = load_fr(REF_BYPASS)[1]

    # group by (volume clock, pad) so a shelf is only ever fitted within ONE take pair
    groups = {}
    for path, d in p4:
        groups.setdefault((d["volume_clock"], d["pad_db"]), {})[d["mode"]] = (path, d)

    report["p4"] = {}
    for (clock, pad), bymode in sorted(groups.items()):
        tag = f"V{clock:04d}_pad{pad:g}"
        entry = {"volume_clock": clock, "pad_db": pad, "modes": sorted(bymode)}
        frs = {}
        for mode, (path, d) in bymode.items():
            if args.self_test:
                path = render(d, f"{d['unit']}_{tag}_{mode}")
            frs[mode] = pedal_fr(path, d, bypass_fr, undo_load=not args.self_test)
            entry.setdefault("x", d["volume"])

        # 1. MODE SHELF -- the rig-cancelling one. bright minus dark, no deconvolution needed.
        if "bright" in frs and "dark" in frs:
            f = frs["bright"][0]
            entry["shelf"] = fit_shelf(f, frs["bright"][1] - frs["dark"][1])

        # 2. LF CORNER -- from DARK, rig divided out.
        if "dark" in frs:
            f, m = frs["dark"]
            entry["lf"] = fit_lf(f, m)
            d = bymode["dark"][1]
            pf, pm = load_fr(render(d, f"plug_{tag}_dark"))
            entry["lf"]["plugin_fc_hz"] = fit_lf(pf, pm)["fc_hz"]        # same estimator, both sides
            entry["lf"]["network_only_fc_hz"] = lf_corner_of_network(d["volume"])

        # 3. HF POLE -- capture against the PLUGIN, so the mode shelf cancels (note #17).
        for mode in ("dark", "bright"):
            if mode not in frs:
                continue
            path, d = bymode[mode]
            pf = load_fr(render(d, f"plug_{tag}_{mode}"))
            f, m = frs[mode]
            entry.setdefault("hf", {})[mode] = fit_hf(f, m - np.interp(f, pf[0], pf[1]))
        report["p4"][tag] = entry

    json.dump(report, open(OUT, "w"), indent=2, default=float)
    print(json.dumps({k: v for k, v in report.items() if k != "p4"}, indent=2, default=float))
    for tag, e in sorted(report["p4"].items()):
        print(f"\n== {tag}   x={e.get('x', float('nan')):.3f}   modes={e['modes']}")
        if "shelf" in e:
            s = e["shelf"]
            print(f"   shelf zero {s['fz_hz']:8.1f} Hz   K0 {s['k0']:5.2f}   residual {s['residual_db']:.3f} dB")
        if "lf" in e:
            l = e["lf"]
            print(f"   LF corner  {l['fc_hz']:8.2f} Hz   vs plugin RENDER {l['plugin_fc_hz']:7.2f} Hz "
                  f"= ratio {l['fc_hz'] / l['plugin_fc_hz']:.3f}   (network alone "
                  f"{l['network_only_fc_hz']:.2f})   residual {l['residual_db']:.3f} dB")
        for mode, h in sorted(e.get("hf", {}).items()):
            print(f"   HF extra pole ({mode:6s}) {h['fc_hz']:10.1f} Hz   residual {h['residual_db']:.3f} dB")
    print(f"\nwrote {OUT}")


if __name__ == "__main__":
    main()
