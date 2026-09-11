#!/usr/bin/env python3
"""Integrity-check a freshly recorded capture BEFORE spending a session on the rest.

Written 2026-09-10 against the owner's first loop capture. Every check here is one that, if it
fails, invalidates the capture silently rather than loudly -- which is the whole reason to run it
at the desk while the gear is still set up.

    .venv/bin/python analysis/check_capture.py analysis/captures/<file>.wav [--loop]

--loop marks a no-pedal loopback, which changes what "pass" means: the response should be FLAT and
the distortion negligible, whereas a pedal capture should not be either.

⚠⚠ IT ALSO CHANGES THE EXPECTED POLARITY, AND THE FIRST VERSION OF THIS SCRIPT GOT THAT WRONG.
Stage 2 is a single common-source JFET stage, which MUST invert (circuit.md stage 2), so a genuine
pedal capture reads -1 and a loopback or bypassed capture reads +1. Flagging every inverted file as
BAD would have condemned every real capture in the session. The expectation is taken from the mode
token, so it is right without anyone having to remember.

⚠⚠ THE DISTORTION SECTION MEASURES ITS OWN FLOOR FIRST (circuit.md note #9's standing rule). The
reference deconvolved against itself must read 0.0000 %, and the same reference plus this capture's
measured noise gives the level below which a THD number means nothing. Without both, a rising
noise floor reads as distortion -- and on the first real capture the raw THD came back at 1.72 %
on the quietest sweep purely because THD is a RATIO and the artefact is fixed in amplitude.

📌 THD is an RSS over orders and hides the per-order picture completely (circuit.md note #10). The
per-order table is the one to read; H2 is what the `Vov` fit consumes.
"""
import argparse, os, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A

# Model expectations at the shipped calibration, for the clearance column (circuit.md note re §16).
PEDAL_H2_DBC = {-6: -22.0, -12: -36.6}
OK, WARN, BAD = "  ok  ", " WARN ", " BAD  "


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--loop", action="store_true", help="no-pedal loopback: expect flat and clean")
    args = ap.parse_args()

    # Expected polarity: +1 through a loop or a bypassed pedal, -1 through the active circuit.
    try:
        import captures as C
        parsed = C.parse_capture(args.path)
        is_ref = parsed["is_reference"]
        pad = parsed["pad_db"]
    except Exception:
        parsed, is_ref, pad = None, args.loop, 0.0
    is_ref = is_ref or args.loop
    want_pol = +1 if is_ref else -1

    orig = A.load(A.ORIG)
    raw = A.load(args.path)
    cap, lag = A.align(raw, orig)
    ref = A.seg_of(orig, "sweep_clean", settled=False)
    tag = "reference (no pedal / bypassed)" if is_ref else "active pedal capture"
    print(f"{os.path.basename(args.path)}   {len(raw)} samples, {len(raw)/A.FS:.2f} s"
          + f"   [{tag}" + (f", source padded {pad:g} dB]" if pad else "]") + "\n")

    # --- 1. integrity -----------------------------------------------------------------------
    print("1. INTEGRITY")
    last_end = max(b for _, b in A.T.values())
    trunc = len(raw) < last_end * A.FS
    pol, ang = A.polarity(cap, orig)
    peak = float(np.max(np.abs(cap)))
    nclip = int(np.sum(np.abs(cap) >= 0.999))
    print(f"   {BAD if trunc else OK} content ends at {last_end:.1f} s, file holds "
          f"{len(raw)/A.FS:.1f} s" + ("  *** TRUNCATED" if trunc else ""))
    print(f"   {OK} align lag {lag} samples")
    print(f"   {OK if pol == want_pol else BAD} polarity {pol:+d} at {ang:.1f} deg "
          f"(expected {want_pol:+d}: " + ("loop/bypass passes straight through)" if is_ref
          else "one common-source stage must invert)")
          + ("" if pol == want_pol else "   *** WRONG WAY ROUND -- see circuit.md note #9b"))
    print(f"   {BAD if nclip else OK} peak {20*np.log10(peak):.2f} dBFS, {nclip} samples at/over 0.999")
    print(f"   {BAD if not np.all(np.isfinite(cap)) else OK} finite, DC offset {np.mean(cap):+.2e}")

    # --- 2. frequency response --------------------------------------------------------------
    print("\n2. FREQUENCY RESPONSE")
    f, m, n = A.band_fr(A.seg_of(cap, "sweep_clean", settled=False), ref, frac=6)
    ok = n > 0
    mid = float(np.mean(m[ok & (f >= 300) & (f <= 3000)]))
    for lo, hi, label in ((20, 40, "20-40 Hz  "), (40, 400, "40-400 Hz "),
                          (400, 8000, "0.4-8 kHz "), (8000, 20000, "8-20 kHz  ")):
        s = ok & (f >= lo) & (f <= hi)
        if not np.any(s):
            continue
        d = m[s] - mid
        flag = OK if (not args.loop or np.max(np.abs(d)) < 0.5) else WARN
        print(f"   {flag} {label} {np.min(d):+6.3f} .. {np.max(d):+6.3f} dB  re midband")
    if args.loop:
        print("   ^ a loopback should be flat. Any HF droop here is REAL and must be deconvolved")
        print("     from every pedal capture -- it is the same size as the project's 1 dB target.")

    # --- 3. noise ---------------------------------------------------------------------------
    print("\n3. NOISE")
    nf = A.seg_of(cap, "noise_floor", settled=False)
    nf_rms = A.rms_db(nf)
    print(f"   {OK if nf_rms < -85 else WARN} noise floor {nf_rms:.1f} dBFS RMS, "
          f"peak {20*np.log10(np.max(np.abs(nf))):.1f} dBFS")

    # --- 4. distortion, floor first ----------------------------------------------------------
    print("\n4. DISTORTION  (per order -- THD alone hides this, note #10)")
    segs = A.sweep_segments()
    self_thd = {}
    rng = np.random.default_rng(0)
    for db, seg in segs.items():
        r = A.seg_of(orig, seg, settled=False)
        _, t0, _ = A.harmonic_thd_curve(r, r)
        noisy = r + rng.normal(0, np.sqrt(np.mean(nf ** 2)), len(r))
        fr, t1, _ = A.harmonic_thd_curve(noisy, r)
        s = (fr >= 100) & (fr <= 5000) & np.isfinite(t1)
        self_thd[seg] = (float(np.nanmedian(t0[np.isfinite(t0)])), float(np.nanmedian(t1[s])))
    print(f"   estimator's own floor (reference vs itself): "
          f"{max(v[0] for v in self_thd.values()):.4f} %  <- must be 0.0000")
    # ⚠⚠ THE CLEARANCE COLUMN IS ONLY MEANINGFUL ON A REFERENCE CAPTURE, and printing it on an
    # active one reads BAD exactly when the pedal AGREES with the model. It is a HEADROOM check:
    # "is this chain's own distortion far enough below the pedal's, for the pedal's to be
    # measurable?" That question needs the chain measured with no pedal in it. On an active capture
    # the measured H2 IS the pedal's, so `expected - measured` is ~0 by construction for a correct
    # model, and the column would flag a good capture of a well-modelled pedal as unusable.
    print(f"   {'segment':13s} {'fund':>8s} {'THD':>8s} {'noise-only':>11s} | "
          + " ".join(f"{'H%d dBc' % k:>8s}" for k in (2, 3))
          + (" | H2 clearance" if is_ref else " | (clearance: N/A on an active capture)"))
    for db, seg in segs.items():
        x = A.seg_of(cap, seg, settled=False)
        fr, thd, Hn = A.harmonic_thd_curve(x, A.seg_of(orig, seg, settled=False))
        s = (fr >= 100) & (fr <= 5000)
        H1 = np.nanmedian(Hn[1][s])
        dbc = {k: 20 * np.log10(np.nanmedian(Hn[k][s]) / H1) for k in (2, 3) if k in Hn}
        f0 = 20 * np.log10(np.max(np.abs(x)))
        row = (f"   {seg:13s} {f0:7.1f}  {np.nanmedian(thd[s]):7.4f}% "
               f"{self_thd[seg][1]:10.4f}% | "
               + " ".join(f"{dbc.get(k, np.nan):8.1f}" for k in (2, 3)))
        if is_ref:
            exp = PEDAL_H2_DBC[-6] if db >= -10 else PEDAL_H2_DBC[-12]
            clear = exp - dbc.get(2, np.nan)
            flag = OK if clear > 12 else (WARN if clear > 6 else BAD)
            row += f" |{flag}{clear:+.0f} dB"
        print(row)
    if is_ref:
        print("   ^ clearance = the pedal's expected H2 minus this chain's. Under ~6 dB the cell")
        print("     cannot measure harmonics; it is still fine for frequency response.")
    else:
        print("   ^ the H2/H3 dBc figures above are THE PEDAL'S OWN distortion and are the useful")
        print("     numbers here. No clearance column: see the comment above -- on an active")
        print("     capture it would compare the pedal against itself and flag agreement as BAD.")
        print("     For a per-order plugin-vs-pedal comparison use probe_compare.py or")
        print("     thd_band_audit_p4.py, which apply the rig correction per harmonic order.")

    # --- 5. calibration ----------------------------------------------------------------------
    print("\n5. CALIBRATION")
    c = A.rms_db(A.seg_of(cap, "cal_1k", settled=False))
    co = A.rms_db(A.seg_of(orig, "cal_1k", settled=False))
    g = c - co
    play_fs_rms = 4.4626 / np.sqrt(2)
    rec_fs = play_fs_rms * 10 ** (-g / 20)
    print(f"   loop/chain gain {g:+.3f} dB  (cal_1k: {c:.3f} vs {co:.3f} dBFS)")
    if args.loop:
        print(f"   => output_level_dbu = {20*np.log10(rec_fs/0.7746):+.2f} dBu "
              f"(record full scale = {rec_fs:.3f} V RMS)")
        print("   ⚠ assumes the play side is set to 1.7745 V RMS at -5 dBFS. Confirm before trusting.")
        print("   ⚠ the loop's source is the interface output, NOT the pedal's ~130 kOhm, so this")
        print("     does NOT include the input-loading correction of checklist section 2c.")


if __name__ == "__main__":
    main()
