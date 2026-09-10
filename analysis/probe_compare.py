#!/usr/bin/env python3
"""Compare every probe capture against the plugin at the same settings -- the load line, at last.

    .venv/bin/python analysis/probe_compare.py [--vov V] [--self-test]

⭐⭐ THIS IS THE MEASUREMENT `Vov` HAS BEEN WAITING FOR. circuit.md note #16 fitted it twice from the
NAM set (0.126 and 0.165, against the shipped 0.4469) and deliberately did NOT apply it, because the
NAM harmonic floor pinned it only to ~1.4x AND because lowering it swaps the stage's clipping
MECHANISM -- triode first at the shipped value, cutoff first at 0.150. Note #16's stated condition
for applying the fit was a capture that REACHES the load line so the mechanism can be measured
rather than inferred. These are those captures.

⚠⚠ COMPARISONS ARE AT MATCHED DRIVE, NOT MATCHED DIGITAL LEVEL (circuit.md note #8, binding). The
capture's pad is fed to OfflineRender's --input-scale, so the plugin sees the same gate volts. Every
figure here is then a RATIO (dBc, or compression as an increment), so the rig's absolute gain and
the still-uncalibrated kOutputMakeup cancel and cannot contaminate the result.

⚠ COMPRESSION IS READ AS AN INCREMENT ACROSS LEVEL, never absolutely -- note #16 learned that the
hard way: a level-INDEPENDENT gain error reads as compression that was never there, and differencing
across level cancels it exactly.
"""
import argparse, json, os, subprocess, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import captures as C
import probe_analyse as PA

SIG = PA.SIG
OUT = "analysis/reports/probe_compare.json"
PROBE_DIR = "analysis/captures/probe"
CACHE = "/tmp/probe_renders"


def render(parsed, vov=None):
    os.makedirs(CACHE, exist_ok=True)
    args = ["--os", "8"] + C.render_args(parsed) + ([] if vov is None else ["--vov", f"{vov}"])
    import hashlib
    key = hashlib.sha1("|".join(args).encode()).hexdigest()[:10]
    out = f"{CACHE}/{key}.wav"
    if not os.path.exists(out):
        subprocess.run([C.RENDER_BIN, SIG, out] + args, check=True, capture_output=True)
    return out


def tone_table(x, segs, fs, freqs, levels, lag=0):
    """H2/H3 in dBc and compression as an increment re the quietest cell."""
    t = {}
    for f in freqs:
        base = None
        for d in levels:
            c = PA.cell(x, segs[f"tone_{f:g}_{d}"], fs, lag)
            h1 = PA.amp(c, f, fs)
            g = PA.db(h1)
            if base is None:
                base = g - d          # remove the cell's own nominal level
            t[(f, d)] = dict(h2=PA.db(PA.amp(c, 2 * f, fs)) - PA.db(h1),
                             h3=PA.db(PA.amp(c, 3 * f, fs)) - PA.db(h1),
                             comp=g - d - base)
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vov", type=float, default=None, help="sweep the plugin's Vov")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--unit", default="p4",
                    help="⚠⚠ MUST stay a single unit. The probe directory also holds NAM renders "
                         "of P1/P2/P3, whose rigs ran at unknown levels (P1's was -12 dBu, 24 dB "
                         "below the plugin's kInputRef), so their drive is NOT matched and pooling "
                         "them destroys the statistic -- it took the shipped-Vov mean from -3.4 dB "
                         "to -10.8 with an sd of 10.4. Use --unit p1 etc. deliberately, never all.")
    a = ap.parse_args()

    m = json.load(open(PA.META))
    fs, segs, freqs, levels = m["fs"], m["segments"], m["tone_freqs"], m["tone_levels_db"]
    ref = PA.load(SIG, fs)

    caps = sorted((c for c in C.find_captures(PROBE_DIR) if c[1]["unit"] == a.unit),
                  key=lambda kv: (kv[1]["volume_clock"], kv[1]["mode"]))
    if not caps:
        sys.exit(f"no probe captures in {PROBE_DIR}")

    report, agg = {}, []
    for path, d in caps:
        cap = ref if a.self_test else PA.load(path, fs)
        lag = 0 if a.self_test else PA.align(cap, ref)
        if a.self_test:
            cap = PA.load(render(d, a.vov), fs)
        ct = tone_table(cap, segs, fs, freqs, levels, lag)
        pt = tone_table(PA.load(render(d, a.vov), fs), segs, fs, freqs, levels)
        name = os.path.basename(path)[:-4]
        report[name] = {}
        print(f"\n== {name}   x={d['volume']:.3f}  pad {d['pad_db']:g}")
        for f in freqs:
            print(f"   {f:g} Hz   {'lvl':>4s} | {'H2 dBc cap':>10s} {'plug':>7s} {'delta':>7s}"
                  f" | {'comp cap':>9s} {'plug':>7s} {'delta':>7s}")
            for dd in levels:
                c, p = ct[(f, dd)], pt[(f, dd)]
                dh = c["h2"] - p["h2"]
                dc = c["comp"] - p["comp"]
                report[name][f"{f:g}_{dd}"] = dict(h2_cap=c["h2"], h2_plug=p["h2"], dh2=dh,
                                                   comp_cap=c["comp"], comp_plug=p["comp"], dcomp=dc)
                print(f"            {dd:4d} | {c['h2']:10.1f} {p['h2']:7.1f} {dh:+7.1f}"
                      f" | {c['comp']:9.3f} {p['comp']:7.3f} {dc:+7.3f}")
                if dd >= -12:      # only cells with real signal
                    agg.append(dh)
    agg = np.array(agg)
    print(f"\nH2 delta ({a.unit} minus plugin) over {len(agg)} cells at -12 dBFS and above:")
    print(f"   mean {np.mean(agg):+.2f} dB   median {np.median(agg):+.2f}   sd {np.std(agg):.2f}")
    print("   positive = the pedal makes MORE distortion than the model")
    json.dump(dict(vov=a.vov, rows=report, h2_mean=float(np.mean(agg)),
                   h2_median=float(np.median(agg))), open(OUT, "w"), indent=2, default=float)
    print(f"\nwrote {OUT}")


if __name__ == "__main__":
    main()
