#!/usr/bin/env python3
"""Echo Pre 3 capture I/O and render argument mapping.

The reference data is seven NAM-model renders plus one no-plugin null render (docs/build-plan.md
phase 0c), not raw pedal captures. Filenames encode the physical unit, the VOLUME clock position,
and the MODE label, e.g.:

    p1_V1430_bright.wav    unit P1, VOLUME at 2:30, MODE = Bright
    p3_V1000_mid.wav       unit P3, VOLUME at 10:00, MODE = Mid

Two REFERENCE captures have no pedal mode to name, and forcing them to borrow one ("dark" on a
capture with no pedal in circuit) is actively misleading, so they get their own tokens:

    loop_V0000_none.wav    no pedal in circuit at all -- the M0 loop check
    p4_V1030_bypass.wav    pedal in circuit, footswitch bypassed (VOLUME noted, though inert)

⚠⚠ REFERENCE CAPTURES ARE EXCLUDED FROM find_captures() BY DEFAULT. They must not flow into the
comparison scripts, every one of which renders the plugin per capture and diffs it -- meaningless
for a file with no pedal in it. Pass include_reference=True to get them.

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

# Not pedal modes: "none" = nothing in circuit, "bypass" = pedal present, footswitch bypassed.
REFERENCE_MODES = ("none", "bypass")

_CAPTURE_RE = re.compile(
    r"^(?P<unit>[a-z0-9]+)_V(?P<clock>\d{3,4})_(?P<mode>bright|dark|mid|none|bypass)$",
    re.IGNORECASE,
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
        # None for a reference capture: there is no MODE position to index, and a caller that
        # reaches for it on one of those has made a category error rather than found a default.
        "mode_index": MODE_INDEX.get(mode),
        "sw": mode,
        "is_reference": mode in REFERENCE_MODES,
    }


def find_captures(directory=CAPTURE_DIR, include_reference=False):
    """Return sorted [(path, parsed_dict), ...] for the PEDAL captures under directory.

    ⚠ Reference captures (mode "none"/"bypass") are excluded unless include_reference=True. Every
    comparison script renders the plugin per capture and diffs the two, which is meaningless for a
    file recorded with no pedal in circuit -- and it would fail SILENTLY, as a mysterious outlier
    rather than an error. Ask for them explicitly when you want them.
    """
    if not os.path.isdir(directory):
        return []
    out = [(p, parse_capture(p)) for p in sorted(glob.glob(os.path.join(directory, "*.wav")))]
    if not include_reference:
        out = [(p, d) for p, d in out if not d["is_reference"]]
    return out


def find_reference_captures(directory=CAPTURE_DIR):
    """Just the no-pedal / bypassed captures -- the M0 loop check and the bypass anchor."""
    return [(p, d) for p, d in find_captures(directory, include_reference=True) if d["is_reference"]]


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
    this pedal -- see circuit.md).

    Reference captures: mode "bypass" renders the plugin bypassed, so the pair should NULL and the
    comparison is meaningful. Mode "none" has no plugin render at all -- compare that capture
    directly against the source signal (the M0 loop check), and this raises rather than inventing
    a setting for it.
    """
    if parsed["mode"] == "none":
        raise ValueError(
            f"mode 'none' has no plugin render: {parsed['unit']} was captured with no pedal in "
            "circuit. Compare it against analyze.ORIG directly (the M0 loop check)."
        )
    if parsed["mode"] == "bypass":
        args = ["--volume", f"{parsed['volume']:.6f}", "--mode", "dark", "--bypass"]
        return args + list(extra_args) if extra_args else args
    args = ["--volume", f"{parsed['volume']:.6f}", "--mode", parsed["mode"]]
    if extra_args:
        args += list(extra_args)
    return args


if __name__ == "__main__":
    caps = find_captures()
    print(f"{len(caps)} captures in {CAPTURE_DIR}/")
    for path, d in caps:
        print(f"  {os.path.basename(path)}  ->  {d}")

# --- The plugin's own input calibration, read from the SOURCE ------------------------------------
_PROCESSOR_HEADER = "src/PluginProcessor.h"
_KINPUTREF_RE = re.compile(r"kInputRef\s*=\s*([0-9.eE+-]+)")


def plugin_vfs():
    """kInputRef in volts per full-scale sample, PARSED FROM THE C++ HEADER.

    ⚠⚠ This is a function rather than a constant on purpose. Three analysis scripts each carried
    their own `PLUGIN_VFS = 0.87` copy, and when the plugin's calibration moved to 4.4626 all three
    kept computing matched-drive offsets that were silently 14.2 dB wrong -- every harmonic and
    compression comparison in the project with them. A duplicated calibration constant is not a
    style problem, it is a measurement that reports the wrong answer without failing.

    Reading the header means the two cannot drift: if the constant is renamed or removed this raises
    instead of returning a stale number, which is the behaviour that matters.
    """
    with open(_PROCESSOR_HEADER) as fh:
        m = _KINPUTREF_RE.search(fh.read())
    if not m:
        raise RuntimeError(f"kInputRef not found in {_PROCESSOR_HEADER} -- the analysis scripts "
                           "derive every matched-drive offset from it and must not guess")
    return float(m.group(1))


def drive_offset_db(capture_vfs):
    """dB to feed the plugin so its gate volts match a capture rig delivering `capture_vfs` V/FS."""
    return 20.0 * np.log10(capture_vfs / plugin_vfs())
