#!/usr/bin/env python3
"""Are we there yet? The plugin against the OWNER'S stated acceptance targets.

    frequency response   within +-1.0 dB overall, and +-0.5 dB from 80 Hz to 12 kHz
    phase                within 5 degrees across all bands
    absolute level       kOutputMakeup is anchored now, so this is finally a target and not a note
    THD                  within 5 % (= 0.42 dB), PER BAND, over 100 Hz .. 12 kHz
    compression          within 5 %
    harmonics            correct levels, per order

    .venv/bin/python analysis/goal_check.py [--unit p4|p1|p2|p3] [--os 8]

⭐⭐ THE DEFAULT ANCHOR IS NOW **P4**, THE OWNER'S OWN UNIT, AND THAT REVERSES THIS FILE'S PREVIOUS
HEADLINE. It used to say "the FR target is measurable against P1 only", because the whole reference
set was seven NAM models and note #9 disqualified P2 (cable pole, inverted polarity) and P3
(shelf-blind fit) for absolute response, leaving P1 -- which note #17 then showed is also the
NOISIEST model in the set. P4 is a raw capture of the test signal through a MEASURED, near-flat rig,
with both calibration figures written down. It supersedes all four NAM units as the anchor.

⚠⚠ A P4 CAPTURE NEEDS TWO CORRECTIONS BEFORE IT IS THE PEDAL, AND THIS SCRIPT APPLIES BOTH.
  * Deconvolve `p4_V1030_bypass.wav`, NOT the bare loop. They are not interchangeable: bypass minus
    loop is +0.37 dB at 18 kHz (the loop path carried a cable the bypass path did not), and the
    bypass path shares its cabling with every pedal capture. The chain also has ~0.5-0.75 dB of real
    HF droop over 8-20 kHz, which is the size of the entire 1 dB target, so this is not optional.
  * Undo the interface's 1 MOhm input load. NOT a scalar -- the pedal's output impedance is
    59-102 kOhm and moves with VOLUME, so the correction moves with the knob (0.341 dB across the
    rotation) and with frequency (0.120 dB within one position). Generated per capture from the
    network model, never typed.

⚠ P4 captures are compared at MATCHED DRIVE: the capture's `_pad` suffix is fed to OfflineRender's
--input-scale by captures.render_args(), so the plugin sees the same gate volts. That is binding for
anything harmonic (circuit.md note #8) and harmless for the linear bands.

⚠⚠ P4 IS A DIFFERENT UNIT FROM THE ONE THE MODEL IS VOICED TO, DELIBERATELY. The recorded decision
is to voice to the newer three-position units (P1/P2) and use P4 as the measurement baseline. P4's
JFET is ~25 % weaker (K0 5.06-5.19 against the shipped 6.59), and that gap lives entirely in the
mode shelf -- so expect BRIGHT to read ~1.9 dB bright at 10 kHz against P4 and DARK to be unaffected.
⛔ That is a recorded voicing decision, not a defect, and it must not be closed by moving `gm`.
➡ Read the DARK row as the model's own error; read the BRIGHT row's HF as the unit difference.

⚠ THD / COMPRESSION / PER-ORDER remain out of scope HERE, but no longer because the reference cannot
carry them -- P4's raw captures can. They need matched-drive per-order work, which is
`analysis/probe_compare.py` and `analysis/harmonic_audit.py`, not a banded sweep. This script covers
the linear targets.

⚠ HISTORIC, kept because it is still true OF P1: its 80-127 Hz and 4064-8127 Hz core-band misses are
the reference's, not the plugin's (circuit.md notes #7 M4, #9d, #17). P1 is brighter at 4-8 kHz than
the circuit as drawn can be, and an extra pole can only darken. Do not move an input-network
constant to close them.
"""
import argparse, os, subprocess, sys, tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import p4_corners as P
import lf_pole_attribution as LFA
from absolute_gain import VOLTS_CORR as _VOLTS_CORR

# The owner's targets.
FR_BAND_DB = 1.0          # everywhere
FR_CORE_DB = 0.5          # 80 Hz .. 12 kHz
FR_CORE = (80.0, 12000.0)
THD_PCT = 5.0             # -> 20*log10(1.05) = 0.42 dB
THD_BAND = (100.0, 12000.0)  # per band, not as an aggregate
COMP_PCT = 5.0
LEVEL_DB = 0.5            # absolute voltage-gain tolerance, now that kOutputMakeup is anchored
LEVEL_BAND = (200.0, 2000.0)  # above the LF poles, below the 1.9 kHz mode shelf zero
# The capture-side cable capacitance lives in p4_corners.CABLE_PF -- one definition.
PHASE_DEG = 5.0           # across all bands
# The owner's stated phase band (2026-09-11): 40 Hz .. 16 kHz at minimum. Reported as its own
# column beside the historic 200 Hz-12 kHz one, because circuit.md note #18's whole finding was a
# phase figure quoted against a band its arithmetic did not cover.
# ⚠ The best-fit delay is still fitted over PHASE_FIT_BAND (200 Hz-12 kHz) and NOT over this band:
# note #18 measured the fit window as one of the rungs that moves the number, so it is held fixed
# for continuity. That means the 40 Hz-16 kHz column includes some EXTRAPOLATION of the delay fit at
# both ends -- it is the honest statistic for the target, not a tighter one.
PHASE_BAND_OWNER = (40.0, 16000.0)
PHASE_FIT_BAND = (200.0, 12000.0)
# Shape normalisation window. Deliberately inside the core band and away from both roll-offs.
NORM = (200.0, 5000.0)
# The absolute anchor. P4 is the owner's own unit, raw-captured through a measured rig.
ANCHOR_UNIT = "p4"
# Play side and record side of the P4 rig -- see analysis/absolute_gain.py for the derivation and
# for the free known-answer check (a loop is a wire; this bypass is true bypass; both read 0.000).
VOLTS_CORR = _VOLTS_CORR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--os", type=int, default=8)
    ap.add_argument("--bin", default=C.RENDER_BIN)
    ap.add_argument("--unit", default=ANCHOR_UNIT,
                    help="which unit to anchor to. p4 is the owner's own pedal, raw-captured "
                         "through a measured rig, and is the default. The NAM units p1/p2/p3 are "
                         "kept selectable for the historic comparisons only -- note #9 disqualifies "
                         "p2 and p3 for absolute response and note #17 shows p1 is the noisiest "
                         "model in that set.")
    ap.add_argument("--sweep", default="sweep_-16",
                    help="which of the signal's four sweeps to analyse. ⚠⚠ NOT "
                         "`sweep_clean` -- that name means 'clean of DISTORTION', i.e. "
                         "it is the QUIETEST sweep (-41 dBFS) and therefore the worst "
                         "SNR in the set. p4_corners.FIT_SWEEP avoids it for the same "
                         "reason and CLAUDE.md says never to deconvolve against it. "
                         "-16 dBFS is the loudest sweep that is still LINEAR for every "
                         "capture in the matrix -- sweep_-6 puts 2.013 V on the gate of "
                         "a pad-0 capture, past the 1.94 V cutoff onset.")
    args = ap.parse_args()
    unit = args.unit
    SWEEP = args.sweep

    # ⚠ P4 needs the rig deconvolved and the interface load undone before it is the PEDAL. Both are
    # smooth curves on the continuous grid, interpolated onto the 1/3-octave centres below. A NAM
    # unit gets neither: it has no bypass reference and it was never loaded by this interface.
    bypass_fr = None
    if unit == "p4":
        if not os.path.exists(P.REF_BYPASS):
            sys.exit(f"missing the deconvolution reference {P.REF_BYPASS}")
        bypass_f, bypass_mag = P.load_fr(P.REF_BYPASS, seg=SWEEP)
        _bo = A.load(A.ORIG)
        _bc, _ = A.align(A.load(P.REF_BYPASS), _bo)
        bypass_cf, bypass_cH = A.transfer_complex(A.seg_of(_bc, SWEEP, settled=False),
                                                  A.seg_of(_bo, SWEEP, settled=False))

    # ⚠⚠ BOTH corrections live in p4_corners, in ONE definition each, and this script only calls
    # them. A second copy of a shipped correction is exactly harness fault 4 (circuit.md note #23).
    def pedal_correction_db(freqs, parsed):
        """dB to ADD to a banded capture so it reads as the pedal alone. Zero for a NAM unit."""
        if unit != "p4":
            return np.zeros_like(np.asarray(freqs, dtype=float))
        rig = np.interp(freqs, bypass_f, bypass_mag)
        return -rig + P.loading_correction_db(freqs, parsed["volume"], P.CABLE_PF)

    def pedal_correction_complex(freqs, parsed):
        """The same correction as a COMPLEX ratio, for the phase section.

        ⚠ The interface load is a complex divider, not a magnitude scaler, so its PHASE has to come
        out too -- it is 29.8 deg at 10 kHz and 40.7 at 15 kHz. Taking only the dB and leaving the
        angle would put a real correction in one section and not the other, which is the kind of
        asymmetry this project has been bitten by three times.
        """
        freqs = np.asarray(freqs, dtype=float)
        if unit != "p4":
            return np.ones_like(freqs, dtype=complex)
        rig = np.interp(freqs, bypass_cf, bypass_cH.real) + 1j * np.interp(freqs, bypass_cf,
                                                                          bypass_cH.imag)
        corr = P.loading_correction_complex(freqs, parsed["volume"])
        with np.errstate(invalid="ignore", divide="ignore"):
            return np.where(np.abs(rig) > 0, corr / rig, 1.0 + 0j)

    orig = A.load(A.ORIG)
    ref_seg = A.seg_of(orig, SWEEP, settled=False)
    tmp = tempfile.mkdtemp(prefix="goal_")

    # ⚠⚠ RENDER VIA p4_corners' CACHE when we can -- this script is the acceptance gate and gets
    # re-run with different --sweep / --os, and an uncached run costs ~10 minutes of renders.
    # ⛔ But ONLY when --bin is the default: P.render's cache key hashes captures.render_bin_key(),
    # i.e. C.RENDER_BIN, so calling it with a DIFFERENT binary would serve a render built from the
    # wrong one under a key that claims otherwise. That is exactly circuit.md note #23's fault 1,
    # one level further in. With --bin overridden we render uncached, as before.
    _cacheable = os.path.abspath(args.bin) == os.path.abspath(C.RENDER_BIN)

    def render_for(parsed, name):
        if _cacheable:
            return P.render(parsed, name, args.os)
        out = os.path.join(tmp, name + ".wav")
        if not os.path.exists(out):
            subprocess.run([args.bin, A.ORIG, out, "--os", str(args.os)] + C.render_args(parsed),
                           check=True, capture_output=True)
        return out

    _rendered = {}

    print(f"Goal check | OS {args.os}x | kInputRef {C.plugin_vfs()} V/FS (read from the header)")
    print(f"Anchor: {unit.upper()}"
          + ("  (owner's own unit; rig deconvolved, interface load undone, matched drive)"
             if unit == "p4" else "  (NAM model -- historic comparison only)") + "\n")
    print("=== 1. FREQUENCY RESPONSE (shape, 1/3 octave, normalised over "
          f"{NORM[0]:.0f}-{NORM[1]:.0f} Hz) ===")
    print(f"{'capture':>26} {'RMS 20-20k':>11} {'worst':>9} {'@Hz':>7} "
          f"{'>1.0dB':>7} {'core RMS':>9} {'core worst':>11} {'>0.5dB':>7}  verdict")

    worst_core, worst_all = 0.0, 0.0
    for path, parsed in C.find_captures():
        if parsed["unit"] != unit:
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        cap, _ = A.align(C.load_capture(path), orig)
        out = render_for(parsed, name)
        _rendered[name] = out
        ren, _ = A.align(A.load(out), orig)

        fc, mc, nc = A.band_fr(A.seg_of(cap, SWEEP, settled=False), ref_seg, frac=3)
        mc = mc + pedal_correction_db(fc, parsed)
        _, mr, nr = A.band_fr(A.seg_of(ren, SWEEP, settled=False), ref_seg, frac=3)
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
        print(f"{name[:26]:>26} {rms:>11.2f} {d[allb][wi]:>+9.2f} {fc[allb][wi]:>7.0f} "
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
    print(f"{'capture':>26} {'pol ren':>8} {'pol cap':>8} {'RMS 20-20k':>11} {'worst':>9} {'@Hz':>7} "
          f"{'200Hz-12k':>10} {'40Hz-16k':>9} {'>5deg':>7}  verdict")
    worst_ph_core = 0.0
    worst_ph_owner = 0.0
    worst_ph_owner_inc = 0.0
    worst_ph_owner_dark = 0.0
    for path, parsed in C.find_captures():
        if parsed["unit"] != unit:
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        cap, _ = A.align(C.load_capture(path), orig)
        ren, _ = A.align(A.load(_rendered[name]), orig)
        cs = A.seg_of(cap, SWEEP, settled=False)
        rs = A.seg_of(ren, SWEEP, settled=False)

        f, Hc = A.transfer_complex(cs, ref_seg)
        _, Hr = A.transfer_complex(rs, ref_seg)
        Hc = Hc * pedal_correction_complex(f, parsed)
        keep = (f >= 20.0) & (f <= 20000.0) & (np.abs(Hc) > 0) & (np.abs(Hr) > 0)
        f = f[keep]
        ratio = Hr[keep] / Hc[keep]
        raw = np.unwrap(np.angle(ratio))
        w = np.abs(Hc[keep])                      # weight by capture magnitude: ignore the noise floor
        band = (f >= PHASE_FIT_BAND[0]) & (f <= PHASE_FIT_BAND[1])
        oband = (f >= PHASE_BAND_OWNER[0]) & (f <= PHASE_BAND_OWNER[1])
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
        ow = float(np.max(np.abs(resid[oband]))) if np.any(oband) else float('nan')
        nOver = int(np.sum(np.abs(resid) > PHASE_DEG))
        worst_ph_core = max(worst_ph_core, cw)
        worst_ph_owner = max(worst_ph_owner, ow)
        # ⚠ 7:30 and 8:00 (x <= 0.1) are excluded from every FIT in this project for knob-slope
        # error -- the 1 kHz control law moves +-6.75 dB (7:30) and +-2.98 (8:00) per +-10 min of
        # knob error, against +-1.02 at 9:00 (circuit.md note #23). A headline that maxes over
        # captures the project already excludes reports a setting error as a model error.
        if parsed.get("volume", 1.0) > 0.1:
            worst_ph_owner_inc = max(worst_ph_owner_inc, ow)
            # ⭐ Split by mode for the same reason the LEVEL section does: DARK is the model's own
            # error, BRIGHT carries the recorded P1/P2-vs-P4 voicing gap (circuit.md #28) in exactly
            # this band. Pooling them reports a deliberate decision as a phase failure.
            if parsed.get("mode") == "dark":
                worst_ph_owner_dark = max(worst_ph_owner_dark, ow)
        print(f"{name[:26]:>26} {polR:>+8d} {polC:>+8d} {rms:>11.2f} {resid[wi]:>+9.2f} "
              f"{f[wi]:>7.0f} {cw:>10.2f} {ow:>9.2f} {nOver:>7}  "
              f"{'PASS' if (nOver == 0 and polR == polC == -1) else 'miss'}")
    print(f"\n  target: +-{PHASE_DEG:.0f} deg across all bands")
    print(f"  worst over {PHASE_BAND_OWNER[0]:.0f} Hz - {PHASE_BAND_OWNER[1]/1000:.0f} kHz "
          f"(the owner's stated band): {worst_ph_owner_inc:.2f} deg  [x > 0.1]"
          f"   |  {worst_ph_owner:.2f} deg incl. the excluded 7:30/8:00")
    print(f"    of which DARK (the model's own error): {worst_ph_owner_dark:.2f} deg"
          f"  -- BRIGHT carries the note #28 voicing gap in this band")
    print(f"  worst over 200 Hz - 12 kHz: {worst_ph_core:.2f} deg")
    print("  ⚠ BOTH polarity columns must read -1 -- a single common-source stage inverts. They are")
    print("    measured against the TEST SIGNAL, not against each other: a relative check reads +1")
    print("    when both are flipped and so cannot see a shared error. All three of P2's NAM")
    print("    captures read +1 here, which is how that rig's inversion was found (circuit.md #9).")

    # ---- 3. ABSOLUTE LEVEL ---------------------------------------------------------------------
    # ⭐⭐ NEW, AND ONLY POSSIBLE SINCE 2026-09-10. kOutputMakeup was exactly 1.0 and unanchored for
    # the whole project, so every comparison above this line is normalised and reads SHAPE. With the
    # makeup anchored from P4's own rig, the pedal's ABSOLUTE voltage gain is a target like any
    # other. It is reported here as a check on the anchored constant, not as a fit.
    #
    # ⚠⚠ Play side (+12.20 dBu) and record side (+14.29 dBu) are NOT equal, so a capture's digital
    # gain is 2.090 dB away from the pedal's voltage gain. Getting that backwards is a 4.2 dB error.
    # VOLTS_CORR comes from absolute_gain.py, which owns the arithmetic and the known-answer check.
    if unit == "p4":
        print(f"\n=== 3. ABSOLUTE LEVEL ({LEVEL_BAND[0]:.0f}-{LEVEL_BAND[1]:.0f} Hz, volts out per "
              f"volt in) ===")
        print(f"{'capture':>28} {'pedal dB':>9} {'plugin dB':>10} {'delta':>8}  verdict")
        deltas = {}
        for path, parsed in C.find_captures():
            if parsed["unit"] != unit:
                continue
            if parsed["volume"] <= 0.1 + 1e-9:   # 7:30 / 8:00 -- knob-slope dominated, see #23
                continue
            name = os.path.splitext(os.path.basename(path))[0]
            fc, mc, nc = A.band_fr(A.seg_of(A.align(C.load_capture(path), orig)[0],
                                            SWEEP, settled=False), ref_seg, frac=3)
            mc = mc + pedal_correction_db(fc, parsed)
            ren, _ = A.align(A.load(_rendered[name]), orig)
            _, mr, nr = A.band_fr(A.seg_of(ren, SWEEP, settled=False), ref_seg, frac=3)
            m = (nc > 0) & (nr > 0) & (fc >= LEVEL_BAND[0]) & (fc <= LEVEL_BAND[1])
            ped = float(np.mean(mc[m])) + parsed["pad_db"] + VOLTS_CORR
            plg = float(np.mean(mr[m])) + parsed["pad_db"]
            deltas.setdefault(parsed["mode"], []).append(ped - plg)
            print(f"{name:>28} {ped:>+9.3f} {plg:>+10.3f} {ped - plg:>+8.3f}  "
                  f"{'PASS' if abs(ped - plg) <= LEVEL_DB else 'miss'}")
        print(f"\n  target: +-{LEVEL_DB:.1f} dB")
        for mode, v in sorted(deltas.items()):
            v = np.array(v)
            print(f"  {mode:>8}: mean {np.mean(v):+.3f} dB, worst {v[np.argmax(np.abs(v))]:+.3f}, "
                  f"n={v.size}")
        print("  ⚠ DARK is the check on kOutputMakeup. BRIGHT carries the deliberate P1/P2-vs-P4 gm")
        print("    difference in the top of this band and is expected to read ~0.1 dB low -- that is")
        print("    a recorded voicing decision (circuit.md #21, #23), not a level error.")

    print("\n=== 4. THD, 5. COMPRESSION, 6. PER-ORDER HARMONICS ===")
    print("  ⚠ OUT OF SCOPE HERE, but no longer UNMEASURABLE -- and that distinction is new.")
    print("    It used to be a property of the reference: the NAM models' harmonic floor is 4-9 dB")
    print(f"    against a {THD_PCT:.0f} % target of {20 * np.log10(1 + THD_PCT / 100):.2f} dB, their")
    print("    compression floor 0.145-0.210 dB against a target of 0.01-0.03 dB, and note #15")
    print("    established both are SYSTEMATIC so they never average down. P4's RAW captures carry")
    print("    none of that floor. What they need is a matched-drive PER-ORDER comparison, which is")
    print("    a tone-cell measurement rather than a banded sweep.")
    print("    ➡ Use probe_compare.py (per-order H2/H3 and compression, matched drive, P4 only),")
    print("      harmonic_audit.py and band_audit.py. ⚠ A per-BAND THD target is HARDER than an")
    print("      aggregate, not easier: an RSS over orders averages a per-band error away.")


if __name__ == "__main__":
    main()
