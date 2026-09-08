#!/usr/bin/env python3
"""Comprehensive A/B capture signal — Echo Pre 3 (Echoplex EP-3 / Chase Tone Secret Preamp).

Render this through each reference (a NAM model in the NAM plugin, or the real pedal via a reamp
loop) and through the plugin's OfflineRender, then compare with analyze.py.

** This pedal is a CLEAN, high-headroom preamp, not a distortion. ** The signal is built for
subtlety: sub-dB frequency-response differences, low-percent THD, and a compression characteristic
that is a gentle square-law bend rather than a knee. That drives every choice below.

Coverage
--------
  marker_head / marker_tail   short chirps          exact alignment + truncation detection
  cal_1k                      1 kHz @ -18 dBFS      level anchor; occupies 0.5-1.8 s so
                                                    captures.load_capture()'s rate-mislabel
                                                    detector (window 0.5-1.45 s) still works
  noise_floor                 2 s silence           the capture's own noise floor — the honest
                                                    lower bound on every measurement below
  sweep_clean / sweep_<dB>    4 x 20 s ESS 10 Hz -> 22 kHz
                                continuous FR at four input levels. The sweep is continuous, so
                                the FR band count is a REPORTING choice, not a signal limit —
                                analyze.band_fr() reports 1/6 octave (60 bands, 20 Hz - 20 kHz)
                                densified to 1/24 octave inside INTEREST_BANDS. Each sweep also
                                yields a continuous THD(f) curve by Farina harmonic separation.
  comp_<f>_<dB>               16 freqs x 10 levels  THE compression instrument: output level vs
                                input level at 16 frequencies spanning 20 Hz - 20 kHz, so
                                compression is characterised PER BAND. This is what shows treble
                                compressing while bass does not — which is exactly what this
                                circuit does, since the MODE bypass sets how much degeneration
                                each band sees. Cells are long enough (>=48 cycles) to double as
                                a harmonic read, giving THD(f, level) on the same 16 x 10 grid.
  tone_<f>_<dB>               16 freqs x 4 levels   THE THD instrument: >=96 cycles per cell for
                                harmonic SNR, 20 Hz - 8 kHz, densified through 1.25-3.15 kHz where
                                the MODE corners sit. 8 kHz is the top because H2 of anything
                                higher lands above Nyquist at 48 kHz. Orders H2..H8 are extracted
                                per cell, each masked where N*f exceeds Nyquist, so "all the
                                harmonics" means all the ones the sample rate can carry.
  imd_guitar_<dB>             220 + 660 Hz @ 3 levels   audible chord intermod
  repeat_1k                   duplicate of a comp cell  context-sensitivity / repeatability floor

Removed from the template default, and why
------------------------------------------
  - imd_smpte (60 Hz + 7 kHz, 4:1). A distortion-pedal measure. On a stage this clean the products
    sit near the capture noise floor and near a NAM model's own error, so it reports noise.
  - decay_220 / decay_1k plucked notes. They probed touch response qualitatively; the per-band
    compression ladder measures the same physics quantitatively across 16 bands and 13 levels.
  - The single 1 kHz level ladder. Superseded by the 16-frequency ladder — compression on this
    circuit is frequency-dependent by construction (the MODE bypass sets how much degeneration
    each band sees), so a 1 kHz-only ladder measures one band of a control that acts on all of them.
  - The -14 dBFS single-level discrete tone row. Superseded by the tone_ block, which sweeps level.
  - The 8-frequency discrete tone row. Superseded by 16 frequencies at 4 levels each.

Design notes
------------
  - The sweeps are TRUE exponential sine sweeps (ESS): f(t)=f0*exp(t/T*k), k=ln(f1/f0). The
    analyzer builds the matching Farina inverse filter to deconvolve them into the linear impulse
    response PLUS time-separated harmonic-order responses -> a continuous THD(f) curve from one
    capture.
  - Sweeps run 10 Hz -> 22 kHz, not 20 -> 20 k, so the 20 Hz and 20 kHz REPORTING bands sit inside
    the sweep rather than on its edge, where Farina deconvolution artefacts live.
  - 20 s sweeps (up from the template's 10 s). Longer buys two things this pedal needs: LF energy
    for the C10 corner, and harmonic-order separation of 1.8 s at H2 instead of 0.9 s.
  - All sweeps share ONE length, so there is one inverse filter and one gating layout.
  - SETTLE (0.15 s) is prepended to every tone cell and discarded by the analyzer. It exceeds the
    132 ms receptive field of the reference NAM models, so no cell is contaminated by the previous
    one. GAP is 0.25 s for the same reason.
  - Tone cell length scales as ~24 cycles at low frequencies so a 20 Hz cell is not measured over
    five periods.
  - Peak level is -1 dBFS, not 0, to keep an offline bounce clear of inter-sample clipping.

  ** Changing this layout invalidates existing captures.** Only ever APPEND new segments at the end
  (and re-capture); inserting in the middle shifts every later segment's offset.
"""
import numpy as np

FS = 48000

# --- ISO third-octave reporting grid -----------------------------------------------------------
# 31 bands, 20 Hz .. 20 kHz. Synthesis and binning both use the EXACT base-2 centres; the nominal
# labels are only for naming segments and printing tables.
ISO31_EXACT = [1000.0 * 2.0 ** (k / 3.0) for k in range(-17, 14)]
ISO31_NOMINAL = (20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000,
                 20000)
assert len(ISO31_EXACT) == len(ISO31_NOMINAL) == 31

# --- Sweeps ------------------------------------------------------------------------------------
SWEEP_F0 = 10.0                 # below the 20 Hz band edge (17.8 Hz) so that band is not on the edge
SWEEP_F1 = 22000.0              # above the 20 kHz band edge (22.4 Hz -> clipped by Nyquist anyway)
SWEEP_SEC = 20.0
# Sweep, comp and tone levels are all drawn from ONE grid (COMP_LEVELS_DB below), so the swept
# Farina THD, the discrete-tone THD and the comp-cell harmonic read can be compared to each other
# at the SAME input level. THD is level-dependent, so grids that do not share levels cannot be
# cross-checked at all — three instruments and no way to arbitrate between them.
CLEAN_FR_LEVELS_DB = (-41, -26)     # linear-reference FR, and the clean end of the range
DRIVEN_LEVELS_DB = (-16, -6)        # driven FR + continuous THD(f)
SWEEP_LEVELS_DB = CLEAN_FR_LEVELS_DB + DRIVEN_LEVELS_DB

# --- Per-band compression ladder ---------------------------------------------------------------
# Every other ISO band (2/3-octave spacing) = 16 frequencies across 20 Hz .. 20 kHz. Compression is
# read per band because on this circuit it genuinely differs per band.
COMP_FREQS = tuple(ISO31_EXACT[::2])
COMP_FREQ_LABELS = tuple(ISO31_NOMINAL[::2])
# 10 levels, 5 dB apart. -46 dBFS is a rolled-off volume knob; -1 dBFS is a hot humbucker dug into.
COMP_LEVELS_DB = tuple(range(-46, 0, 5))
COMP_CYCLES = 48          # cells hold >=48 periods, so they also serve as a harmonic read

# --- THD / harmonic block ----------------------------------------------------------------------
# 16 fundamentals, 20 Hz .. 8 kHz, densified through 1.25-3.15 kHz where the MODE bypass corners
# sit. The 8 kHz ceiling is physics, not choice: at 48 kHz, H2 of a 12.5 kHz tone is already past
# Nyquist, so THD is unmeasurable above ~12 kHz by ANY discrete-tone or swept method.
_ISO = dict(zip(ISO31_NOMINAL, ISO31_EXACT))
TONE_FREQ_LABELS = (20, 31.5, 50, 80, 125, 200, 315, 500, 800, 1250, 1600, 2000, 2500, 3150,
                    5000, 8000)
TONE_FREQS = tuple(_ISO[f] for f in TONE_FREQ_LABELS)
TONE_LEVELS_DB = (-26, -16, -6, -1)
TONE_CYCLES = 96          # long dwell -> harmonic SNR; this is the block the JFET shaper is fit to
TONE_MAX_ORDER = 8        # H2..H8 extracted per cell, masked where N*f > Nyquist

# ⚠ THESE TWO DEFECTS ARE RECORDED, NOT FIXED: the signal is APPEND-ONLY, so changing them would
# invalidate all seven existing captures. Fix both by APPENDING a replacement segment whenever the
# signal is next revised (docs/build-plan.md §15.4).
#
#   (a) ⚠⚠ 220 and 660 Hz are HARMONICALLY RELATED -- 660 = 3 x 220 exactly -- so every
#       intermodulation product m*f1 + n*f2 = (m + 3n)*220 lands on the 220 Hz harmonic grid and
#       none is separable from harmonic distortion. Neither input tone is a clean amplitude
#       reference either, each being contaminated by a third-order product of the other. This
#       segment therefore cannot measure IMD at all. Use an INHARMONIC pair -- 220 Hz with 1234 Hz,
#       say -- and the products separate completely.
#   (b) ⚠ -19 and -3 are NOT on the shared level grid that COMP_LEVELS_DB defines, which breaks the
#       rule stated above it: the whole point of one grid is that the swept, discrete-tone and
#       twin-tone instruments can be compared at the SAME input level. The one segment measuring a
#       different physical quantity is the one that cannot be cross-checked. Put the replacement on
#       the grid (-21, -11, -1).
IMD_LEVELS_DB = (-19, -11, -3)
REPEAT_TWIN = "comp_800_-11"   # repeat_800_-11 is a byte-for-byte duplicate of this cell

SETTLE = 0.15      # discarded head of every tone cell (> the 132 ms NAM receptive field)
GAP = 0.08         # silence between segments. Deliberately SHORT: clearing the reference models'
                   # 132 ms memory is SETTLE's job and SETTLE is inside every cell, so a long gap
                   # would buy the same thing twice. At 236 segments, 0.25 s of gap was 59 s (18%)
                   # of the signal. The gap now only has to keep segment edges clear of alignment
                   # error, which is sample-exact via cross-correlation.
MARKER_SEC = 0.20


def dbfs(db):
    return 10.0 ** (db / 20.0)


def silence(sec):
    return np.zeros(int(sec * FS), dtype=np.float64)


def fade(x, ms=5.0):
    n = int(ms * 1e-3 * FS)
    if n * 2 < len(x):
        r = np.linspace(0, 1, n)
        x[:n] *= r
        x[-n:] *= r[::-1]
    return x


def tone(freq, sec, db, settle=0.0):
    """A sine of total length `settle + sec`. The analyzer discards the first `settle` seconds."""
    t = np.arange(int((sec + settle) * FS)) / FS
    return fade(dbfs(db) * np.sin(2 * np.pi * freq * t))


def cell_sec(freq, cycles=COMP_CYCLES, lo=0.35, hi=1.50):
    """Analysis-window length for a tone cell: ~`cycles` periods, clamped. Keeps a 20 Hz cell
    honest (many periods) without making an 8 kHz cell pointlessly long. The cycle count sets the
    DFT resolution relative to the fundamental, so `cycles` periods puts the nearest harmonic
    `cycles` bins away — far outside a Blackman-Harris main lobe, which is why harmonics can be
    read off these cells without leakage."""
    return float(np.clip(cycles / freq, lo, hi))


def log_sweep(sec, db, f0=SWEEP_F0, f1=SWEEP_F1):
    """True exponential sine sweep (Farina ESS): instantaneous freq f(t)=f0*exp(t/T*k), k=ln(f1/f0)."""
    t = np.arange(int(sec * FS)) / FS
    T = sec
    k = np.log(f1 / f0)
    phase = 2 * np.pi * f0 * T / k * (np.exp(t / T * k) - 1.0)
    return fade(dbfs(db) * np.sin(phase), 10.0)


def twin_tone(f_lo, f_hi, sec, db_peak, ratio_lo=1.0, ratio_hi=1.0):
    """Two summed tones, scaled so the peak sits at db_peak (amplitudes in ratio_lo:ratio_hi)."""
    t = np.arange(int(sec * FS)) / FS
    a_lo = ratio_lo / (ratio_lo + ratio_hi)
    a_hi = ratio_hi / (ratio_lo + ratio_hi)
    x = a_lo * np.sin(2 * np.pi * f_lo * t) + a_hi * np.sin(2 * np.pi * f_hi * t)
    x *= dbfs(db_peak) / (np.max(np.abs(x)) + 1e-20)
    return fade(x)


def marker():
    """Short chirp used only to pin alignment and prove the file is not truncated."""
    return log_sweep(MARKER_SEC, -12, 200.0, 8000.0)


def comp_name(label, db):
    return f"comp_{label:g}_{db}"


def tone_name(label, db):
    return f"tone_{label:g}_{db}"


def build_segments():
    """Ordered list of (name, audio_array). Pure data — no I/O."""
    segs = []
    segs.append(("marker_head", marker()))
    segs.append(("cal_1k", tone(1000, 1.3, -18)))
    segs.append(("noise_floor", silence(2.0)))

    # Full-range FR + continuous THD(f) at four input levels. 'sweep_clean' is both the primary
    # clean FR read and the alignment anchor (analyze.align()).
    segs.append(("sweep_clean", log_sweep(SWEEP_SEC, CLEAN_FR_LEVELS_DB[0])))
    for db in SWEEP_LEVELS_DB[1:]:
        segs.append((f"sweep_{db}", log_sweep(SWEEP_SEC, db)))

    # Per-band compression: frequency outer, level inner, so each frequency's ladder is contiguous.
    for freq, label in zip(COMP_FREQS, COMP_FREQ_LABELS):
        sec = cell_sec(freq)
        for db in COMP_LEVELS_DB:
            segs.append((comp_name(label, db), tone(freq, sec, db, settle=SETTLE)))

    # THD / harmonic block: long dwell for harmonic SNR, level swept at every frequency.
    for freq, label in zip(TONE_FREQS, TONE_FREQ_LABELS):
        sec = cell_sec(freq, cycles=TONE_CYCLES, lo=0.60, hi=2.00)
        for db in TONE_LEVELS_DB:
            segs.append((tone_name(label, db), tone(freq, sec, db, settle=SETTLE)))

    for db in IMD_LEVELS_DB:
        segs.append((f"imd_guitar_{db}", twin_tone(220, 660, 2.0, db)))

    # Repeatability / context floor: an EXACT duplicate of one comp cell, rendered in a different
    # context. A memoryless system repeats it identically, so the residual against comp_800_-11 is
    # the measurement's own noise floor — the number every other result has to beat to mean anything.
    segs.append((REPEAT_TWIN.replace("comp_", "repeat_"),
                 tone(_ISO[800], cell_sec(_ISO[800]), -11, settle=SETTLE)))
    segs.append(("marker_tail", marker()))
    return segs


def assemble(lead=None, gap=GAP, tail=0.5):
    """Concatenate segments with lead/gap/tail silence; return (signal, timing_map).
    timing_map[name] = (t0, t1) bounds the AUDIO of that segment in seconds.

    `lead` defaults to whatever puts cal_1k at exactly 0.5 s (lead + MARKER_SEC + gap), which is
    what captures.load_capture()'s sample-rate-mislabel detector expects. Derived, not hardcoded —
    it silently broke once when GAP changed."""
    lead = (0.5 - MARKER_SEC - gap) if lead is None else lead
    assert lead > 0, f"GAP {gap} + marker {MARKER_SEC} leaves no room before cal_1k at 0.5 s"
    parts = [silence(lead)]
    pos = lead
    times = {}
    for name, audio in build_segments():
        t0 = pos
        parts.append(audio)
        pos += len(audio) / FS
        times[name] = (t0, pos)
        parts.append(silence(gap))
        pos += gap
    parts.append(silence(tail - gap if tail > gap else 0.0))
    sig = np.concatenate(parts).astype(np.float32)
    return sig, times


def segment_times():
    """Timing map only — the analyzer's single source of truth for segment bounds."""
    return assemble()[1]


def analysis_window(name, times=None):
    """(t0, t1) of the part of a segment that is safe to measure: tone cells have SETTLE removed."""
    times = times or segment_times()
    t0, t1 = times[name]
    if name.startswith(("comp_", "tone_", "repeat_")):
        t0 += SETTLE
    return t0, t1


if __name__ == "__main__":
    from scipy.io import wavfile

    sig, times = assemble()
    out = "analysis/test_signal_48k.wav"
    wavfile.write(out, FS, sig)
    peak = float(np.max(np.abs(sig)))
    print(f"wrote {out}  ({len(sig)/FS:.1f} s, {len(sig)} samples, {FS} Hz)")
    print(f"peak = {peak:.3f}  ({20*np.log10(peak+1e-20):.2f} dBFS)")
    print(f"segments: {len(times)}")
    cal = times["cal_1k"]
    print(f"cal_1k at {cal[0]:.3f}..{cal[1]:.3f} s "
          f"(rate detector needs 0.5..1.45 inside: "
          f"{'OK' if cal[0] <= 0.5 and cal[1] >= 1.45 else 'FAIL'})")
    print(f"compression grid: {len(COMP_FREQS)} freqs x {len(COMP_LEVELS_DB)} levels")
    print(f"  freqs (nominal): {', '.join(f'{f:g}' for f in COMP_FREQ_LABELS)}")
    print(f"  levels (dBFS):   {', '.join(str(d) for d in COMP_LEVELS_DB)}")
    print(f"THD grid: {len(TONE_FREQS)} freqs x {len(TONE_LEVELS_DB)} levels, "
          f"orders H2..H{TONE_MAX_ORDER}")
    print(f"  freqs (nominal): {', '.join(f'{f:g}' for f in TONE_FREQ_LABELS)}")
    nyq = FS / 2.0
    worst = [f for f in TONE_FREQ_LABELS if 2 * _ISO[f] > nyq]
    print(f"  fundamentals with NO measurable H2 at {FS} Hz: {worst or 'none'}")
    print("\nblock timing (s):")
    for pfx in ("marker_head", "cal_1k", "noise_floor", "sweep_", "comp_", "tone_",
                "imd_", "repeat_1k", "marker_tail"):
        sel = [(n, v) for n, v in times.items() if n.startswith(pfx)]
        if sel:
            print(f"  {pfx:12} {sel[0][1][0]:8.3f} .. {sel[-1][1][1]:8.3f}  ({len(sel)} segs)")
    assert REPEAT_TWIN in times, REPEAT_TWIN
    a, b = times[REPEAT_TWIN]; c, d = times[REPEAT_TWIN.replace("comp_", "repeat_")]
    print(f"\nrepeat twin: {REPEAT_TWIN} ({b-a:.3f}s) vs its duplicate ({d-c:.3f}s) "
          f"-> {'OK' if abs((b-a)-(d-c)) < 1e-9 else 'LENGTH MISMATCH'}")
