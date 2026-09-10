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
        rows[d] = {k: db(amp(x, f, fs)) - db(ref_amp) for k, (f, _) in prods.items()}
        rows[d]["_abs"] = db(ref_amp)
    small = lv[:SMALL_SIGNAL_CELLS]
    print(f"\n2. INTERMODULATION, {f1:g} + {f2:g} Hz -- products in dBc re the mean of the two tones")
    print(f"   {'product':8s} {'Hz':>7s} " + " ".join(f"{d:>7d}" for d in lv) +
          f" {'dBc slope':>10s} {'expected':>9s}")
    for k, (f, order) in prods.items():
        vals = [rows[d][k] for d in lv]
        sl = np.polyfit(small, [rows[d][k] for d in small], 1)[0]
        ok = "ok" if abs(sl - (order - 1)) < 0.25 else "OFF"
        print(f"   {k:8s} {f:7.1f} " + " ".join(f"{v:7.1f}" for v in vals) +
              f" {sl:10.2f} {order - 1:9.1f}  {ok}")
    print(f"   (slope fitted on the {SMALL_SIGNAL_CELLS} quietest cells; the loud ones are into "
          "the load line by design)")

    # --- 3. harmonics and compression --------------------------------------------------------------
    tl = m["tone_levels_db"]
    print("\n3. HARMONICS (dBc) and COMPRESSION (increment re the quietest cell)")
    for f in m["tone_freqs"]:
        print(f"\n   {f:g} Hz" + ("   <- below the 1.9 kHz shelf zero: MODES MUST AGREE here"
                                  if f < 1864 else "   <- above the zero: modes must differ as k^2"))
        print(f"      {'level':>6s} {'H2 dBc':>8s} {'H3 dBc':>8s} {'gain dB':>9s} {'compression':>12s}")
        base = None
        for d in tl:
            x = cell(cap, segs[f"tone_{f:g}_{d}"], fs, lag)
            r = cell(ref, segs[f"tone_{f:g}_{d}"], fs)
            h1 = amp(x, f, fs)
            g = db(h1) - db(amp(r, f, fs))
            if base is None:
                base = g
            print(f"      {d:6d} {db(amp(x, 2*f, fs)) - db(h1):8.1f} "
                  f"{db(amp(x, 3*f, fs)) - db(h1):8.1f} {g + pad:9.3f} {g - base:12.3f}")

    # --- 4. within-file drift ----------------------------------------------------------------------
    print("\n4. WITHIN-FILE DRIFT -- top cells repeated at the end (rig or JFET moving mid-take)")
    for name in [k for k in segs if k.endswith("_repeat")]:
        orig = name[:-len("_repeat")]
        f = f1 if orig.startswith("imd") else float(orig.split("_")[1])
        d1 = db(amp(cell(cap, segs[orig], fs, lag), f, fs))
        d2 = db(amp(cell(cap, segs[name], fs, lag), f, fs))
        flag = "ok" if abs(d2 - d1) < 0.05 else "DRIFT"
        print(f"   {orig:22s} {d2 - d1:+7.3f} dB   {flag}")

    nf = cell(cap, segs["noise_floor"], fs, lag)
    print(f"\n5. NOISE FLOOR  {db(np.sqrt(np.mean(nf ** 2))):.1f} dBFS RMS")


if __name__ == "__main__":
    main()
