#!/usr/bin/env python3
"""Echo Pre 3 capture I/O and render argument mapping.

The reference data is seven NAM-model renders plus one no-plugin null render (docs/build-plan.md
phase 0c), not raw pedal captures. Filenames encode the physical unit, the VOLUME clock position,
and the MODE label, e.g.:

    p1_V1430_bright.wav    unit P1, VOLUME at 2:30, MODE = Bright
    p3_V1000_mid.wav       unit P3, VOLUME at 10:00, MODE = Mid
    null_V0000_mid.wav     the no-plugin loop-check render (M0) -- volume/mode are don't-cares

The `V<HHMM>` clock token reuses analyze.py's clock-to-x convention (0700=min .. 1200=noon ..
1700=max), since the folder-name clock positions in the NAM data are exactly that scale.
"""
import os
import glob
import re

import numpy as np
from scipy.io import wavfile
from scipy import signal as sps

import analyze as A

RENDER_BIN = "build/OfflineRender_artefacts/Release/OfflineRender"
CAPTURE_DIR = "analysis/captures"

# MODE choice order matches the APVTS AudioParameterChoice layout (circuit.md note #2 / PluginProcessor):
# physical up=Bright, middle=Dark (centre-off), down=Mid -- ordered by lever position, not brightness.
MODE_LABELS = ("bright", "dark", "mid")
MODE_INDEX = {label: i for i, label in enumerate(MODE_LABELS)}

_CAPTURE_RE = re.compile(
    r"^(?P<unit>[a-z0-9]+)_V(?P<clock>\d{3,4})_(?P<mode>bright|dark|mid)$", re.IGNORECASE
)


def parse_capture(filename):
    """Parse an Echo Pre 3 capture filename into a dict of settings.

    Returns {"rev": unit, "unit": unit, "volume_clock": int, "volume": 0..1,
             "mode": label, "mode_index": 0..2, "sw": label}
    "rev"/"sw" are included for compatibility with the template's generic report scripts, which
    group/label captures by those keys.
    """
    stem = os.path.splitext(os.path.basename(filename))[0]
    m = _CAPTURE_RE.match(stem)
    if not m:
        raise ValueError(f"Capture filename does not match <unit>_V<HHMM>_<mode>.wav: {filename}")

    unit = m.group("unit").lower()
    clock = int(m.group("clock"))
    mode = m.group("mode").lower()

    return {
        "rev": unit,
        "unit": unit,
        "volume_clock": clock,
        "volume": A.clock_to_x(clock),
        "mode": mode,
        "mode_index": MODE_INDEX[mode],
        "sw": mode,
    }


def find_captures(directory=CAPTURE_DIR):
    """Return sorted [(path, parsed_dict), ...] for every .wav under directory."""
    if not os.path.isdir(directory):
        return []
    return [
        (p, parse_capture(p))
        for p in sorted(glob.glob(os.path.join(directory, "*.wav")))
    ]


def load_capture(path, expect_fs=48000):
    """Load a capture as float64 mono at ``expect_fs``.

    Some NAM modelers export 44.1 kHz audio inside a 48 kHz-labeled WAV. This function detects the
    speed error from the cal_1k tone (~1088 Hz on a mislabeled file) and resamples back to
    ``expect_fs``. A correctly-labeled file passes through untouched.
    """
    sr, x = wavfile.read(path)
    if x.dtype.kind in "iu":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)

    # Rate-mislabel detection via 1 kHz cal tone
    cal_win = (0.5, 1.45)
    seg = x[int(cal_win[0] * sr) : int(cal_win[1] * sr)]
    if len(seg) > 64:
        w = np.hanning(len(seg))
        mag = np.abs(np.fft.rfft(seg * w))
        peak_hz = np.fft.rfftfreq(len(seg), 1.0 / sr)[int(np.argmax(mag))]
        ratio = peak_hz / 1000.0
    else:
        ratio = 1.0

    _COMMON_RATES = (44100, 48000, 88200, 96000)
    if abs(ratio - 1.0) > 0.005:
        est = sr / ratio
        true_rate = min(_COMMON_RATES, key=lambda r: abs(r - est))
        x = sps.resample_poly(x, expect_fs, true_rate)
    elif sr != expect_fs:
        x = sps.resample_poly(x, expect_fs, sr)

    return np.asarray(x, dtype=np.float64)


def render_args(parsed, extra_args=None):
    """Parsed settings -> flat list of CLI flags for OfflineRender.

    Only VOLUME and MODE are pedal controls that vary per capture (there is no drive/blend/tone on
    this pedal -- see circuit.md). The "null" unit has no corresponding plugin render; callers doing
    the M0 loop check compare the null capture directly against the source signal instead of calling
    this.
    """
    args = ["--volume", f"{parsed['volume']:.6f}", "--mode", parsed["mode"]]
    if extra_args:
        args += list(extra_args)
    return args


if __name__ == "__main__":
    caps = find_captures()
    print(f"{len(caps)} captures in {CAPTURE_DIR}/")
    for path, d in caps:
        print(f"  {os.path.basename(path)}  ->  {d}")
