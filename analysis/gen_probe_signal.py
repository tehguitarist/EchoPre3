#!/usr/bin/env python3
"""A SHORT, SELF-CONTAINED supplementary capture signal covering the two gaps the main matrix has.

    .venv/bin/python analysis/gen_probe_signal.py     # -> analysis/probe_signal_48k.wav (~55 s)

⭐⭐ SELF-CONTAINED IS THE WHOLE DESIGN CONSTRAINT, not a nicety. The main signal is 290 s and the
full matrix took ~70 minutes; appending to it would invalidate every capture already made and force
that hour again. So this is a SEPARATE file carrying everything it needs to be analysed alone:

  * its own alignment marker, so it does not borrow the main signal's timing
  * its own 1 kHz cal tone AND a 220 Hz tone at -5 dBFS, which is exactly the point the owner's
    meter is calibrated at (1.7745 V RMS), so the play-side calibration can be re-verified from
    this file directly rather than assumed to have held since the session started
  * its own noise-floor tail
  * a repeat of each ladder's top cell AT THE END -- a within-file drift and repeatability check
    that costs 4 s and is the only thing that can catch the rig or the JFET moving mid-take
  * measurements that are RATIOS wherever possible (IMD products re their tones, harmonics in dBc,
    compression as an INCREMENT across level), so NO rig deconvolution and NO bypass reference is
    needed. That is what makes it standalone rather than merely short.

GAP 1 -- INTERMODULATION, which the main signal structurally cannot measure. Its twin-tone segment
is 220 + 660 Hz and 660 = 3 x 220 EXACTLY (circuit.md note #15), so every product lands on the
220 Hz harmonic grid and neither tone is a clean amplitude reference. Here the pair is 220 and 611
Hz -- inharmonic, 611 prime, first grid coincidence far above the band. IMD matters more than it
used to: the stage now solves `id = gm*g(vGate - Zs(z)*id)` against a frequency-dependent source
one-port and a volume-dependent drain load, and a single tone's harmonics can always be reproduced
by a memoryless map fitted to them. Two tones cannot.

GAP 2 -- THE LOAD LINE AT DRAIN LOADS THE MATRIX CANNOT REACH. VOLUME sits after the stage, so it
does not change the drive, but it does move the drain load (1.9 k at 7:30 to 17.8 k at 17:00) and
that moves triode onset by several dB. Load-line data exists only at 7:30/8:00/9:00, because from
10:30 up the pedal's own output overruns the converter and those takes are all pad 12, which never
clips. This ladder runs to -1 dBFS in fine steps so ONE short capture at a chosen pad reaches the
load line at whatever knob position is set.
⚠ It still cannot rescue BRIGHT above ~9:00: there the output clips the converter before the JFET
reaches triode. That is the rig's ceiling, not a signal-design problem, and no signal fixes it.

TONE CHOICE for the ladder: 220 Hz and 3150 Hz, deliberately either side of the 1.9 kHz mode-shelf
zero. Below the zero every MODE has Zs = R5, so the two modes MUST agree -- the free known-answer
probe this project uses everywhere. Above it they must differ as k^2. And 3 x 220 = 660 Hz is still
below the zero, which note #14 requires for the COMPRESSION probe specifically, since compression is
third order and reads the loop at 3f rather than at f.
"""
import json
import numpy as np
from scipy.io import wavfile

FS = 48000
F_IMD = (220.0, 611.0)
IMD_LEVELS_DB = (-26, -16, -6, -1)
TONE_FREQS = (220.0, 3150.0)
TONE_LEVELS_DB = (-26, -18, -12, -8, -4, -1)
CAL_DB = -5.0                     # the owner's meter point: 1.7745 V RMS at -5 dBFS / 220 Hz
IMD_SEC, TONE_SEC, CAL_SEC, GAP_SEC, MARKER_SEC, TAIL_SEC = 3.0, 2.0, 1.0, 0.4, 0.20, 2.0
OUT = "analysis/probe_signal_48k.wav"


def _fade(x, ms=15.0):
    n = max(8, int(ms * 1e-3 * FS))
    w = np.ones(len(x))
    w[:n] = np.sin(np.linspace(0, np.pi / 2, n)) ** 2
    w[-n:] = w[:n][::-1]
    return x * w


def tone(f, level_db, seconds):
    t = np.arange(int(seconds * FS)) / FS
    return _fade(10 ** (level_db / 20.0) * np.sin(2 * np.pi * f * t))


def twin(level_db, seconds):
    """Equal-amplitude pair, each 6 dB under the cell peak so the PAIR peaks at `level_db`."""
    t = np.arange(int(seconds * FS)) / FS
    a = 10 ** (level_db / 20.0) / 2.0
    return _fade(a * (np.sin(2 * np.pi * F_IMD[0] * t) + np.sin(2 * np.pi * F_IMD[1] * t)))


def marker():
    t = np.arange(int(MARKER_SEC * FS)) / FS
    k = (8000.0 / 200.0) ** (1.0 / MARKER_SEC)
    return _fade(10 ** (-12 / 20.0) * np.sin(2 * np.pi * 200.0 * (k ** t - 1) / np.log(k)), 5.0)


def build():
    gap = np.zeros(int(GAP_SEC * FS))
    parts, times, t = [], {}, 0.0

    def add(name, x):
        nonlocal t
        parts.extend([x, gap])
        times[name] = [round(t, 6), round(t + len(x) / FS, 6)]
        t += len(x) / FS + GAP_SEC

    add("marker_head", marker())
    add("cal_1k", tone(1000.0, CAL_DB, CAL_SEC))
    add("cal_220", tone(220.0, CAL_DB, CAL_SEC))       # the meter's own calibration point
    for db in IMD_LEVELS_DB:
        add(f"imd_{db}", twin(db, IMD_SEC))
    for f in TONE_FREQS:
        for db in TONE_LEVELS_DB:
            add(f"tone_{f:g}_{db}", tone(f, db, TONE_SEC))
    # repeats LAST -- within-file drift / repeatability, the only check that can catch the rig or
    # the JFET moving during the take itself
    add(f"imd_{IMD_LEVELS_DB[-1]}_repeat", twin(IMD_LEVELS_DB[-1], IMD_SEC))
    for f in TONE_FREQS:
        add(f"tone_{f:g}_{TONE_LEVELS_DB[-1]}_repeat", tone(f, TONE_LEVELS_DB[-1], TONE_SEC))
    add("noise_floor", np.zeros(int(TAIL_SEC * FS)))
    return np.concatenate(parts), times


def main():
    x, times = build()
    peak = float(np.max(np.abs(x)))
    assert peak < 1.0, "must not reach full scale"
    wavfile.write(OUT, FS, x.astype(np.float32))
    json.dump({"fs": FS, "imd_freqs": list(F_IMD), "imd_levels_db": list(IMD_LEVELS_DB),
               "tone_freqs": list(TONE_FREQS), "tone_levels_db": list(TONE_LEVELS_DB),
               "cal_db": CAL_DB, "segments": times},
              open(OUT.replace(".wav", "_times.json"), "w"), indent=2)
    print(f"wrote {OUT}   {len(x)/FS:.1f} s   peak {20*np.log10(peak):+.2f} dBFS   "
          f"{len(times)} segments")
    print(f"     + {OUT.replace('.wav', '_times.json')}")

    KIN, DIV, TRIODE = 4.4626, 0.90, 1.691
    print(f"\nLOAD-LINE REACH of the tone ladder's top cell, by pad "
          f"(triode onset {TRIODE:g} V at the gate):")
    for pad in (0, 4, 5, 8, 12):
        v = KIN * 10 ** ((TONE_LEVELS_DB[-1] - pad) / 20) * DIV
        print(f"   pad {pad:4.1f}   {v:5.2f} V   "
              + (f"{20*np.log10(v/TRIODE):+5.1f} dB past triode" if v > TRIODE else "linear only"))
    print("\nIMD products (none on either tone's harmonic grid):")
    f1, f2 = F_IMD
    for lbl, f in (("f2-f1", f2 - f1), ("f2+f1", f2 + f1), ("2f1-f2", abs(2 * f1 - f2)),
                   ("2f2-f1", 2 * f2 - f1)):
        print(f"   {lbl:7s} {f:7.1f} Hz   ({f/f1:.3f} x f1, {f/f2:.3f} x f2)")


if __name__ == "__main__":
    main()
