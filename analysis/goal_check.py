#!/usr/bin/env python3
"""Are we there yet? The plugin against the OWNER'S stated acceptance targets.

    frequency response   within +-1.0 dB overall, and +-0.5 dB from 80 Hz to 12 kHz
    phase                within 5 degrees across all bands
    THD                  within 5 % (= 0.42 dB), PER BAND, over 100 Hz .. 12 kHz
    compression          within 5 %
    harmonics            correct levels, per order

⚠⚠ TWO OF THESE FOUR CANNOT BE MEASURED AGAINST THE CURRENT REFERENCE, and the script says so
rather than printing a number that looks like an answer. 5 % of THD is 0.42 dB. The reference
models' own harmonic error floor is 4-9 dB (circuit.md note #10) and their compression floor is
0.145-0.210 dB against signals of 0.2-0.6 dB (note #11), and note #15 established those floors are
SYSTEMATIC rather than noise, so they do not average down. A 0.42 dB target sits an order of
magnitude under the instrument. Only the +12.2 dBu capture session can move that.

The frequency-response target IS measurable, and against P1 only: circuit.md note #9 disqualifies P2
(cable pole, inverted polarity) and P3 (shelf-blind fit) for absolute response.

⚠⚠ BUT READ THE FR MISSES WITH circuit.md NOTE #17 BESIDE THEM -- NEITHER CLUSTER IS A MODEL DEFECT.
The core-band cells this table reports as over target fall into two groups and both are the
reference's, not the plugin's:
  * 80-127 Hz, +0.6 to +1.06 dB -- the missing LF high-pass pole, confounded 27 / 18 / 13.5 Hz
    across three units (note #7's M4, note #9d). Blocked on the within-rig VOLUME sweep.
  * 4064-8127 Hz, -0.5 to -0.67 dB -- P1's own HF error. Note #17: the plugin's input pole is
    confirmed at 7.3 kHz by P2 (0.02 dB residual, all three modes) and accommodated by P3, while P1
    cannot be described by ANY pole cascade containing one, and its best fit still leaves a
    structured +0.3 dB hump at 3-5 kHz -- the same size as the miss. P1 is BRIGHTER there than the
    circuit as drawn can be, and an extra pole can only darken.
⛔ So do not move an input-network constant to close either cluster. P1 is the only capture that can
carry an absolute anchor and it is also the noisiest model in the set; those are both true at once.

⚠ Level is not shape. kOutputMakeup is still exactly 1.0 and unanchored, so every comparison here is
normalised over the midband and reads SHAPE. An absolute-level target needs `output_level_dbu`.

Run from the repo root:
    .venv/bin/python analysis/goal_check.py [--os 8]
"""
import argparse, os, subprocess, sys, tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C

# The owner's targets.
FR_BAND_DB = 1.0          # everywhere
FR_CORE_DB = 0.5          # 80 Hz .. 12 kHz
FR_CORE = (80.0, 12000.0)
THD_PCT = 5.0             # -> 20*log10(1.05) = 0.42 dB
THD_BAND = (100.0, 12000.0)  # per band, not as an aggregate
COMP_PCT = 5.0
PHASE_DEG = 5.0           # across all bands
# Shape normalisation window. Deliberately inside the core band and away from both roll-offs.
NORM = (200.0, 5000.0)
# The absolute anchor. Note #9: P1 is the only capture that can carry one.
ANCHOR_UNIT = "p1"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    args = ap.parse_args()

    orig = A.load(A.ORIG)
    ref_seg = A.seg_of(orig, "sweep_clean", settled=False)
    tmp = tempfile.mkdtemp(prefix="goal_")

    print(f"Goal check | OS {args.os}x | kInputRef {C.plugin_vfs()} V/FS (read from the header)")
    print(f"Anchor: {ANCHOR_UNIT.upper()} only -- circuit.md note #9 disqualifies the others for "
          f"absolute response.\n")
    print("=== 1. FREQUENCY RESPONSE (shape, 1/3 octave, normalised over "
          f"{NORM[0]:.0f}-{NORM[1]:.0f} Hz) ===")
    print(f"{'mode':>8} {'RMS 20-20k':>11} {'worst':>9} {'@Hz':>7} "
          f"{'>1.0dB':>7} {'core RMS':>9} {'core worst':>11} {'>0.5dB':>7}  verdict")

    worst_core, worst_all = 0.0, 0.0
    for path, parsed in C.find_captures():
        if parsed["unit"] != ANCHOR_UNIT:
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        cap, _ = A.align(C.load_capture(path), orig)
        out = os.path.join(tmp, name + ".wav")
        subprocess.run([args.bin, A.ORIG, out, "--os", str(args.os)] + C.render_args(parsed),
                       check=True, capture_output=True)
        ren, _ = A.align(A.load(out), orig)

        fc, mc, nc = A.band_fr(A.seg_of(cap, "sweep_clean", settled=False), ref_seg, frac=3)
        _, mr, nr = A.band_fr(A.seg_of(ren, "sweep_clean", settled=False), ref_seg, frac=3)
        # band_average's own docstring: a 0-bin band was INTERPOLATED, not measured. Never quote one.
        valid = (nc > 0) & (nr > 0)
        d = mr - mc
        d = d - np.mean(d[valid & (fc >= NORM[0]) & (fc <= NORM[1])])

        core = valid & (fc >= FR_CORE[0]) & (fc <= FR_CORE[1])
        allb = valid
        rms = float(np.sqrt(np.mean(d[allb] ** 2)))
        wi = int(np.argmax(np.abs(d[allb])))
        crms = float(np.sqrt(np.mean(d[core] ** 2)))
        cw = float(np.max(np.abs(d[core])))
        nOver = int(np.sum(np.abs(d[allb]) > FR_BAND_DB))
        nCoreOver = int(np.sum(np.abs(d[core]) > FR_CORE_DB))
        worst_core = max(worst_core, cw)
        worst_all = max(worst_all, float(np.max(np.abs(d[allb]))))
        print(f"{parsed['mode']:>8} {rms:>11.2f} {d[allb][wi]:>+9.2f} {fc[allb][wi]:>7.0f} "
              f"{nOver:>7} {crms:>9.2f} {cw:>11.2f} {nCoreOver:>7}  "
              f"{'PASS' if (nOver == 0 and nCoreOver == 0) else 'miss'}")
        if nCoreOver:
            miss = [(fc[i], d[i]) for i in range(len(fc)) if core[i] and abs(d[i]) > FR_CORE_DB]
            print("           core bands over target: "
                  + ", ".join(f"{f:.0f} Hz {v:+.2f}" for f, v in miss))

    print(f"\n  target: +-{FR_BAND_DB:.1f} dB everywhere, +-{FR_CORE_DB:.1f} dB over "
          f"{FR_CORE[0]:.0f}-{FR_CORE[1]:.0f} Hz")
    print(f"  worst:  {worst_all:.2f} dB overall, {worst_core:.2f} dB in the core band")

    # ---- 2. PHASE ------------------------------------------------------------------------------
    # ⚠ The best-fit PURE DELAY is removed, and that is not a convenience: the render's own latency
    # and the analysis gate's time origin are both pure delays, and a sub-sample null aligns them out
    # anyway, so leaving them in would report harness bookkeeping as model error. The fitted CONSTANT
    # offset is reported separately rather than folded in, because a polarity flip IS a constant and
    # would otherwise vanish into the residual -- which is exactly how P2's inversion hid for a whole
    # session. Read the polarity column FIRST.
    print("\n=== 2. PHASE (best-fit pure delay removed; polarity reported separately) ===")
    print(f"{'mode':>8} {'pol ren':>8} {'pol cap':>8} {'RMS 20-20k':>11} {'worst':>9} {'@Hz':>7} "
          f"{'200Hz-12k':>10} {'>5deg':>7}  verdict")
    worst_ph_core = 0.0
    for path, parsed in C.find_captures():
        if parsed["unit"] != ANCHOR_UNIT:
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        cap, _ = A.align(C.load_capture(path), orig)
        ren, _ = A.align(A.load(os.path.join(tmp, name + ".wav")), orig)
        cs = A.seg_of(cap, "sweep_clean", settled=False)
        rs = A.seg_of(ren, "sweep_clean", settled=False)

        f, Hc = A.transfer_complex(cs, ref_seg)
        _, Hr = A.transfer_complex(rs, ref_seg)
        keep = (f >= 20.0) & (f <= 20000.0) & (np.abs(Hc) > 0) & (np.abs(Hr) > 0)
        f = f[keep]
        ratio = Hr[keep] / Hc[keep]
        raw = np.unwrap(np.angle(ratio))
        w = np.abs(Hc[keep])                      # weight by capture magnitude: ignore the noise floor
        band = (f >= 200.0) & (f <= 12000.0)
        coef = np.polyfit(f[band], raw[band], 1, w=w[band])
        resid = np.degrees(raw - np.polyval(coef, f))
        # ⚠ AGAINST THE SOURCE, not against each other. polarity(render, capture) returns their
        # RELATIVE sign, which reads +1 when both invert -- i.e. it cannot see a flip they share, and
        # it cannot tell you the pedal inverts at all. Each is compared to the test signal instead,
        # and both must read -1 because a single common-source stage inverts.
        polR, _ = A.polarity(rs, ref_seg)
        polC, _ = A.polarity(cs, ref_seg)

        rms = float(np.sqrt(np.average(resid ** 2, weights=w)))
        wi = int(np.argmax(np.abs(resid)))
        cw = float(np.max(np.abs(resid[band])))
        nOver = int(np.sum(np.abs(resid) > PHASE_DEG))
        worst_ph_core = max(worst_ph_core, cw)
        print(f"{parsed['mode']:>8} {polR:>+8d} {polC:>+8d} {rms:>11.2f} {resid[wi]:>+9.2f} "
              f"{f[wi]:>7.0f} {cw:>10.2f} {nOver:>7}  "
              f"{'PASS' if (nOver == 0 and polR == polC == -1) else 'miss'}")
    print(f"\n  target: +-{PHASE_DEG:.0f} deg across all bands")
    print(f"  worst over 200 Hz - 12 kHz: {worst_ph_core:.2f} deg")
    print("  ⚠ BOTH polarity columns must read -1 -- a single common-source stage inverts. They are")
    print("    measured against the TEST SIGNAL, not against each other: a relative check reads +1")
    print("    when both are flipped and so cannot see a shared error. P2's captures read +1 here,")
    print("    which is why this anchor is P1 (circuit.md note #9).")

    print("\n=== 3. THD, 4. COMPRESSION, 5. PER-ORDER HARMONICS ===")
    print(f"  ⛔ NOT MEASURABLE against this reference set, and that is a property of the reference")
    print(f"     rather than of the model:")
    print(f"       {THD_PCT:.0f} % of THD PER BAND over {THD_BAND[0]:.0f}-{THD_BAND[1]:.0f} Hz")
    print(f"         = {20 * np.log10(1 + THD_PCT / 100):.2f} dB "
          f"| harmonic floor 4-9 dB      (circuit.md #10)")
    print(f"         ⚠ per band is HARDER than an aggregate, not easier: an RSS over orders can")
    print(f"           average a per-band error away, which is why band_audit.py exists.")
    print(f"       {COMP_PCT:.0f} % of compression  = 0.01-0.03 dB "
          f"| compression floor 0.145-0.210 dB (note #11)")
    print(f"     Those floors are SYSTEMATIC, not noise (note #15), so they do not average down.")
    print(f"     ➡ The +12.2 dBu capture is what makes these three answerable. Until then use")
    print(f"       band_audit.py / harmonic_audit.py / compression_audit.py, which report each")
    print(f"       delta beside the floor it has to clear.")


if __name__ == "__main__":
    main()
