#!/usr/bin/env python3
"""Analyse a capture of `probe_signal_48k.wav`. Standalone: no rig reference, no deconvolution.

    .venv/bin/python analysis/probe_analyse.py <capture.wav> [--pad N]
    .venv/bin/python analysis/probe_analyse.py --self-test

⭐ EVERYTHING REPORTED HERE IS A RATIO, which is what lets the file stand alone. IMD products are
read against their own tones, harmonics in dBc, and compression as an INCREMENT across level. A
level-independent gain error in the chain -- which is all a missing rig reference amounts to --
cancels in every one of them. (circuit.md note #16 learned this the hard way: reading `comp_db`
absolutely reported the reference's constant gain error as compression that was never there.)

⚠⚠ THE ORDER SLOPES ARE ABSOLUTE, AND THE dBc ONES ARE ONE LOWER. An Nth-order product's amplitude
goes as A^N, so it rises N dB per dB of input in absolute terms and N-1 in dBc. Getting that
backwards makes a perfectly good instrument look broken -- it did, on the first run here.

⚠ Slopes are fitted on the SMALL-SIGNAL cells only. The top cells are into the load line by design
and rise faster than any power law; including them turns a clean 2.03 into 2.39 and reads as a
fault.

⚠⚠ AND THE QUIET CELLS ARE NOT AUTOMATICALLY SMALL-SIGNAL -- THEY CAN BE FLOOR. This is note #9's
standing rule (measure the instrument's own floor before reading anything through it) and the first
real capture broke the instrument for want of it. At VOLUME 7:30 the pedal records ~16 dB below any
other setting, so the chain's FIXED-AMPLITUDE distortion artefact sits high in dBc and swamps the
quiet cells: their products read a flat ~-41 dBc with H3 at or ABOVE H2, which a square-law device
cannot produce. Fitting a slope through them returned -0.09 where 1.00 was required and looked like
a broken capture. It was a broken measurement of a good capture.

➡ So this tool now reports the ABSOLUTE level of every product as well as its dBc, derives each
cell's LOCAL slope, and fits only over cells whose absolute level actually rises with drive. A cell
whose product does not grow is measuring the chain, not the pedal.
"""
import argparse, json, os, subprocess, sys

import numpy as np
from scipy.io import wavfile

SIG = "analysis/probe_signal_48k.wav"
META = SIG.replace(".wav", "_times.json")
EDGE = 0.25          # seconds trimmed from each cell edge, clear of the fades
SMALL_SIGNAL_CELLS = 2


def load(path, fs):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64) / (np.iinfo(x.dtype).max if x.dtype.kind in "iu" else 1.0)
    if x.ndim > 1:
        x = x.mean(axis=1)
    assert sr == fs, f"{path}: {sr} Hz, expected {fs}"
    return x


def align(cap, ref):
    """Lag from the head marker. Short file, so a plain full correlation is affordable and exact."""
    n = min(len(cap), len(ref), 8 * 48000)
    c = np.correlate(cap[:n] - cap[:n].mean(), ref[:n] - ref[:n].mean(), "full")
    return int(np.argmax(np.abs(c)) - (n - 1))


def amp(x, f, fs):
    """Coherent amplitude at f via a Blackman-windowed DFT bin -- no leakage assumptions."""
    t = np.arange(len(x)) / fs
    w = np.blackman(len(x))
    return 2.0 * np.abs(np.sum(x * w * np.exp(-2j * np.pi * f * t))) / np.sum(w)


def cell(x, seg, fs, lag=0):
    a, b = seg
    return x[int((a + EDGE) * fs) + lag: int((b - EDGE) * fs) + lag]


def db(v):
    return 20 * np.log10(max(float(v), 1e-20))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?")
    ap.add_argument("--pad", type=float, default=None, help="dB of digital attenuation on playback")
    ap.add_argument("--self-test", action="store_true",
                    help="render the signal through the plugin and check the known order slopes")
    a = ap.parse_args()

    m = json.load(open(META))
    fs, segs = m["fs"], m["segments"]
    f1, f2 = m["imd_freqs"]

    if a.self_test:
        path = "/tmp/probe_plug.wav"
        subprocess.run(["build/OfflineRender_artefacts/Release/OfflineRender", SIG, path,
                        "--os", "8", "--volume", "0.35", "--mode", "bright"],
                       check=True, capture_output=True)
        pad = 0.0
    else:
        if not a.path:
            sys.exit("need a capture path (or --self-test)")
        path = a.path
        pad = a.pad
        if pad is None:
            try:
                sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
                import captures as C
                pad = C.parse_capture(path)["pad_db"]
            except Exception:
                pad = 0.0

    ref = load(SIG, fs)
    cap = load(path, fs)
    lag = 0 if a.self_test else align(cap, ref)
    print(f"{os.path.basename(path)}   {len(cap)/fs:.1f} s   align lag {lag} samples   pad {pad:g} dB")

    # --- 1. calibration, from the file's own tones -------------------------------------------------
    print("\n1. CALIBRATION (this file carries its own -- it does not borrow the main signal's)")
    for name in ("cal_1k", "cal_220"):
        g = db(amp(cell(cap, segs[name], fs, lag), 1000.0 if name == "cal_1k" else 220.0, fs)) \
            - db(amp(cell(ref, segs[name], fs), 1000.0 if name == "cal_1k" else 220.0, fs))
        print(f"   {name:10s} chain gain {g + pad:+8.3f} dB (pad removed)")

    # --- 2. intermodulation ------------------------------------------------------------------------
    prods = {"f2-f1": (f2 - f1, 2), "f2+f1": (f2 + f1, 2),
             "2f1-f2": (abs(2 * f1 - f2), 3), "2f2-f1": (2 * f2 - f1, 3)}
    lv = m["imd_levels_db"]
    rows = {}
    for d in lv:
        x = cell(cap, segs[f"imd_{d}"], fs, lag)
        ref_amp = 0.5 * (amp(x, f1, fs) + amp(x, f2, fs))
        r = cell(ref, segs[f"imd_{d}"], fs)
        rows[d] = {k: db(amp(x, f, fs)) - db(ref_amp) for k, (f, _) in prods.items()}
        rows[d]["_abs"] = db(ref_amp)
        rows[d]["_gain"] = db(ref_amp) - db(0.5 * (amp(r, f1, fs) + amp(r, f2, fs)))
    print(f"\n2. INTERMODULATION, {f1:g} + {f2:g} Hz")
    print(f"   product level in dBFS (absolute) -- a product that does not RISE with drive is the")
    print(f"   chain's own artefact, not the pedal's (note #9: measure the floor first)\n")
    print(f"   {'product':8s} {'Hz':>7s} " + " ".join(f"{d:>7d}" for d in lv) +
          f" {'usable':>8s} {'slope':>7s} {'exp':>5s}")
    for k, (f, order) in prods.items():
        absv = [rows[d][k] + rows[d]["_abs"] for d in lv]
        # a cell is usable when the product still grows at a decent fraction of its order, and the
        # next cell up has not run into clipping
        # THE FIT WINDOW IS BOUNDED AT BOTH ENDS, and both bounds are MEASURED rather than assumed:
        #   below, by the chain's artefact -- a product that barely grows is not the pedal's
        #   above, by clipping -- once the FUNDAMENTAL compresses, the products leave the power law
        # Omitting the upper bound made the self-test read 2.39 against a required 2.00 and flag a
        # correct instrument as broken; omitting the lower one did the same at 7:30 with -0.09.
        clean = rows[lv[0]]["_gain"]
        good = [i for i in range(len(lv) - 1)
                if (absv[i + 1] - absv[i]) / (lv[i + 1] - lv[i]) > 0.5 * order
                and (clean - rows[lv[i + 1]]["_gain"]) < 0.3]
        # ONE adjacent pair is enough for a two-point slope, and with a 4-cell ladder whose top
        # two cells are clipping by design that is often all there is. Requiring two pairs reported
        # FLOOR on the self-test, where the answer is known and correct.
        if len(good) >= 1:
            idx = good[:3]
            sl = np.polyfit([lv[i] for i in idx] + [lv[idx[-1] + 1]],
                            [absv[i] for i in idx] + [absv[idx[-1] + 1]], 1)[0]
            tag = f"{lv[idx[0]]}..{lv[idx[-1] + 1]}"
            ok = "ok" if abs(sl - order) < 0.3 else "OFF"
        else:
            sl, tag, ok = float("nan"), "none", "FLOOR"
        print(f"   {k:8s} {f:7.1f} " + " ".join(f"{v:7.1f}" for v in absv) +
              f" {tag:>8s} {sl:7.2f} {order:5d}  {ok}")
    print("   ^ absolute dBFS. The fit window is bounded BELOW by the chain's artefact (a product")
    print("     that barely grows) and ABOVE by clipping (the fundamental compressing >0.3 dB).")
    print("     FLOOR = no cell pair survives both, so this setting cannot measure small-signal IMD.")
    print("   fundamental compression per cell: "
          + ", ".join(f"{d}: {rows[lv[0]]['_gain'] - rows[d]['_gain']:+.2f}" for d in lv))

    # --- 3. harmonics and compression --------------------------------------------------------------
    tl = m["tone_levels_db"]
    print("\n3. HARMONICS (dBc) and COMPRESSION (increment re the quietest cell)")
    for f in m["tone_freqs"]:
        print(f"\n   {f:g} Hz" + ("   <- below the 1.9 kHz shelf zero: MODES MUST AGREE here"
                                  if f < 1864 else "   <- above the zero: modes must differ as k^2"))
        print(f"      {'level':>6s} {'H2 dBc':>8s} {'H2 dBFS':>9s} {'d/dL':>6s} "
              f"{'H3 dBc':>8s} {'gain dB':>9s} {'compression':>12s}")
        base, prev = None, None
        for d in tl:
            x = cell(cap, segs[f"tone_{f:g}_{d}"], fs, lag)
            r = cell(ref, segs[f"tone_{f:g}_{d}"], fs)
            h1 = amp(x, f, fs)
            g = db(h1) - db(amp(r, f, fs))
            if base is None:
                base = g
            h2a = db(amp(x, 2 * f, fs))
            # local slope of the ABSOLUTE H2: a square law needs 2.0, and anything near 0 is floor
            sl = "" if prev is None else f"{(h2a - prev[1]) / (d - prev[0]):6.2f}"
            mark = "" if prev is None or (h2a - prev[1]) / (d - prev[0]) > 1.0 else "  <- floor"
            print(f"      {d:6d} {h2a - db(h1):8.1f} {h2a:9.1f} {sl:>6s} "
                  f"{db(amp(x, 3*f, fs)) - db(h1):8.1f} {g + pad:9.3f} {g - base:12.3f}{mark}")
            prev = (d, h2a)
        print("      ^ d/dL = local slope of ABSOLUTE H2 per dB of drive. A square law gives 2.0;")
        print("        near 0 means that cell is reading the chain's artefact, not the pedal.")

    # --- 4. within-file drift ----------------------------------------------------------------------
    print("\n4. WITHIN-FILE DRIFT -- top cells repeated at the end (rig or JFET moving mid-take)")
    for name in [k for k in segs if k.endswith("_repeat")]:
        orig = name[:-len("_repeat")]
        f = f1 if orig.startswith("imd") else float(orig.split("_")[1])
        d1 = db(amp(cell(cap, segs[orig], fs, lag), f, fs))
        d2 = db(amp(cell(cap, segs[name], fs, lag), f, fs))
        # ⚠ the repeat cells are the TOP cells, which are clipping by design -- gain there is a
        # steep function of drive, so they exaggerate any small level change. Judge drift on the
        # IMD cell (two tones, not clipped at the fundamental) before the clipped tone cells.
        flag = "ok" if abs(d2 - d1) < 0.05 else ("small" if abs(d2 - d1) < 0.25 else "DRIFT")
        note = "" if orig.startswith("imd") else "   (clipping cell -- gain-sensitive)"
        print(f"   {orig:22s} {d2 - d1:+7.3f} dB   {flag}{note}")

    nf = cell(cap, segs["noise_floor"], fs, lag)
    print(f"\n5. NOISE FLOOR  {db(np.sqrt(np.mean(nf ** 2))):.1f} dBFS RMS")


if __name__ == "__main__":
    main()
