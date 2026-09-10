#!/usr/bin/env python3
"""The pedal's ABSOLUTE voltage gain, measured -- and therefore `kOutputMakeup` at last.

    .venv/bin/python analysis/absolute_gain.py

⭐⭐ WHY THIS IS POSSIBLE NOW AND NEVER WAS BEFORE. `kOutputMakeup` has been exactly 1.0 and
unanchored since the project began, because every earlier reference was a NAM model carrying an
unknown rig gain (build-plan limit L2). These captures have BOTH calibration figures written down:

    play   input_level_dbu  = +12.20 dBu  ->  kInputRef = 4.4626 V peak per full scale
    record output_level_dbu = +14.29 dBu  ->  5.6767 V peak per full scale

The two are NOT equal, so a capture's digital gain is NOT the pedal's voltage gain -- they differ by
a fixed 20*log10(kIn/kOut) = -2.090 dB. Undo that and the capture reports VOLTS OUT PER VOLT IN,
which is the quantity the plugin computes and the only one the model can be compared against.

    20*log10(G_pedal) = (capture's digital gain) + pad - 20*log10(kIn/kOut)

⭐ AND IT HAS A FREE KNOWN ANSWER, which is the only reason to believe it. The bypass capture is
genuine true bypass (nulls at -70.8 dB against the loop, 0.008 dB insertion loss), so its voltage
gain must be exactly 1.000. It reads -0.004 dB. The bare loop, being a wire, must read 0.000 too.
⚠ The record calibration was itself derived from the loop, so the LOOP is not independent evidence
-- but a loopback's voltage gain is 1 by construction, which is what makes that derivation sound
rather than circular, and the bypass path (different cabling, different take) reproducing it to
4 millidB is genuine confirmation.

⚠⚠ THE PLUGIN IS COMPARED UNLOADED AND THE CAPTURE IS NOT. The interface presents 1 MOhm, and this
pedal's output impedance is 92-139 kOhm (circuit.md stage 3), so that load is worth 0.5-0.96 dB and
it MOVES WITH THE KNOB by 0.341 dB. A constant would be absorbed harmlessly by kOutputMakeup; a
knob-dependent one lands straight in the VOLUME taper fit. It is undone per capture from the
network model here, never from a typed table.
"""
import json, os, subprocess, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as A
import captures as C
import p4_corners as P

OUT = "analysis/reports/absolute_gain.json"
KIN = 4.4626                                  # V peak/FS, play side  (+12.20 dBu)
KOUT = 0.7746 * 10 ** (14.29 / 20) * np.sqrt(2)  # V peak/FS, record side (+14.29 dBu)
VOLTS_CORR = -20 * np.log10(KIN / KOUT)       # dB to add to a digital gain to get a VOLTS gain
BAND = (200.0, 2000.0)                        # midband: above the LF poles, below the mode shelf


def midband_db(f, mag, band=BAND):
    m = (f >= band[0]) & (f <= band[1]) & np.isfinite(mag)
    return float(np.mean(mag[m]))


def main():
    orig = A.load(A.ORIG)
    report = {"kInputRef_v_per_fs": KIN, "record_v_per_fs": float(KOUT),
              "volts_correction_db": float(VOLTS_CORR), "band_hz": list(BAND), "rows": {}}

    # --- the known answer, first ------------------------------------------------------------------
    checks = {}
    for path, d in C.find_reference_captures():
        f, mag = P.load_fr(path)
        g = midband_db(f, mag) + d["pad_db"] + VOLTS_CORR
        checks[os.path.basename(path)] = g
    report["known_answer_checks_db"] = checks
    print("KNOWN ANSWER -- a loop is a wire and this bypass is true bypass, so both must read 0.000\n")
    for k, v in sorted(checks.items()):
        print(f"   {k:26s} {v:+7.4f} dB  {'ok' if abs(v) < 0.05 else 'OFF'}")

    bypass_fr = P.load_fr(P.REF_BYPASS)[1]

    print(f"\nPEDAL VOLTAGE GAIN, {BAND[0]:.0f}-{BAND[1]:.0f} Hz, rig deconvolved and interface load undone\n")
    print(f"   {'capture':26s} {'pedal dB':>9s} {'plugin dB':>10s} {'makeup dB':>10s} {'x':>6s}")
    rows = []
    for path, d in C.find_captures():
        if d["unit"] != "p4":
            continue
        f, mag = P.load_fr(path)
        pedal = midband_db(f, mag - bypass_fr
                           + P.loading_correction_db(f, d["volume"])) + d["pad_db"] + VOLTS_CORR
        pf, pm = P.load_fr(P.render(d, f"plug_V{d['volume_clock']:04d}_pad{d['pad_db']:g}_{d['mode']}"))
        plug = midband_db(pf, pm) + d["pad_db"]
        name = os.path.basename(path)[:-4]
        report["rows"][name] = dict(pedal_gain_db=pedal, plugin_gain_db=plug,
                                    makeup_db=pedal - plug, x=d["volume"], **{
                                        k: d[k] for k in ("volume_clock", "mode", "pad_db")})
        rows.append((name, pedal, plug, pedal - plug, d["volume"]))
        print(f"   {name:26s} {pedal:+9.3f} {plug:+10.3f} {pedal - plug:+10.3f} {d['volume']:6.3f}")

    # ⛔ 7:30 AND 8:00 (x <= 0.1) are excluded: the 1 kHz control law moves +-6.75 dB and +-2.98 dB
    # per +-10 min of knob error there, against +-1.02 at 9:00, so a few minutes of setting error is
    # worth many dB. 7:30's two takes disagree by 6.5 dB (dark) and 10.8 dB (bright) where 9:00's
    # agree to 0.03 dB -- measured, not assumed.
    #
    # ⭐⭐ AND THE ANCHOR IS FITTED FROM **DARK ONLY**, which is not fussiness either. P4's own JFET is
    # ~25 % weaker than the model's (K0 = 5.06-5.19 against the shipped 6.59, circuit.md note #21),
    # and the recorded decision is to VOICE to P1/P2 rather than to P4 -- so that difference is
    # deliberate and permanent. It lives entirely in the mode shelf, whose zero is at 1.9 kHz, i.e.
    # inside the top of this 200-2000 Hz band. In DARK the shelf does not exist at all (Zs = R5, flat
    # at every frequency), so dark is the only mode in which this band measures a pure level.
    # It shows up exactly as predicted: at every knob position from 9:00 up, bright reads 0.09-0.18 dB
    # LOWER makeup than dark, because the plugin's larger K0 makes it too loud in bright's own band.
    # Averaging the two modes would fold a known voicing decision into a level constant.
    #
    # Duplicate takes at one knob position are averaged first, so a position captured twice does not
    # get double weight in the mean.
    def summarise(sel, label, key):
        by_knob = {}
        for name, _pedal, _plug, mk, x in sel:
            by_knob.setdefault(round(x, 4), []).append(mk)
        per = np.array([float(np.mean(v)) for v in by_knob.values()])
        if per.size == 0:
            return
        report[f"{key}_db_mean"] = float(np.mean(per))
        report[f"{key}_linear"] = float(10 ** (np.mean(per) / 20))
        report[f"{key}_sd_db"] = float(np.std(per, ddof=1)) if per.size > 1 else 0.0
        report[f"{key}_spread_db"] = float(np.max(per) - np.min(per))
        report[f"{key}_n_knobs"] = int(per.size)
        print(f"   {label:44s} {np.mean(per):+7.3f} dB = x{10 ** (np.mean(per) / 20):.4f}"
              f"   sd {np.std(per, ddof=1) if per.size > 1 else 0.0:.3f}"
              f"   spread {np.max(per) - np.min(per):.3f}   n={per.size}")

    usable = [r for r in rows if r[4] > 0.1]
    print()
    dark = [r for r in usable if report["rows"][r[0]]["mode"] == "dark"]
    bright = [r for r in usable if report["rows"][r[0]]["mode"] == "bright"]
    summarise(dark, "kOutputMakeup  <- DARK only, x > 0.1", "makeup")
    summarise(bright, "(bright, for comparison -- do NOT use)", "makeup_bright")
    summarise(usable, "(both modes pooled -- do NOT use)", "makeup_pooled")

    json.dump(report, open(OUT, "w"), indent=2, default=float)
    print(f"\nwrote {OUT}")


if __name__ == "__main__":
    main()
