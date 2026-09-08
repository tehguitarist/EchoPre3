# Analysis Harness — Guitar Pedal Plugin Template

This directory contains a **pedal-agnostic** A/B validation harness for comparing
a circuit-emulation plugin against real-pedal captures. Every tool here was
extracted from a production pedal project and stripped of pedal-specific
dependencies (those live in `captures.py`, which you fill in).

## Quick Start

```bash
# 1. Generate the reference test signal
python3 analysis/gen_test_signal.py
# → writes analysis/test_signal_48k.wav

# 2. Implement captures.py (see "First-time Setup" below)
# 3. Build your OfflineRender binary (a CLI that mirrors processBlock)
cmake --build build --target OfflineRender

# 4. Run the comprehensive A/B (once captures exist in analysis/captures/)
python3 analysis/comprehensive_report.py --os 8
# → writes analysis/reports/comprehensive_data.json

# 5. Generate the dashboard
python3 analysis/dashboard_gen.py
# → writes analysis/reports/dashboard.html  (open in browser)
# Also go and modify the example_dashboard.html in /analysis/reports to fit the specific captures in this project. _

# 6. Audit against acceptance targets
python3 analysis/report_audit.py --write
# → writes analysis/reports/executive_summary.txt
```

## File Reference

### Core Library

| File | Lines | What It Does |
|------|-------|-------------|
| `analyze.py` | 297 | **Pedal-agnostic analysis library.** Load/align WAVs, frequency response (CSD/Welch), discrete-tone THD (harmonic binning), **Farina continuous THD(f)** with order-limiting (eliminates the spurious-edge-spike artefact), sub-sample fractional alignment, gain-matched null depth, linear-removed (coherence-based) null floor, generic capture-filename parser (clock HHMM or 0-10 scale). Everything else imports this. |
| `gen_test_signal.py` | 173 | **Reference signal generator.** Exponential sine sweeps (Farina ESS) at clean + 3 driven levels, discrete harmonic tones, 1 kHz level steps for compression knee, SMPTE IMD (60 Hz + 7 kHz, 4:1), guitar-band IMD (220 Hz + 660 Hz), plucked decay notes. **Append-only** — inserting segments in the middle invalidates all existing captures. |
| `captures.py` | — | **Pedal-specific interface (you implement).** Provides `find_captures()`, `load_capture()`, `render_args()`, and `RENDER_BIN`. The template scripts import from here. See "First-time Setup" below. |

### Dashboard & Visualisation

| File | Lines | What It Does |
|------|-------|-------------|
| `dashboard_gen.py` | 457 | Reads `comprehensive_data.json` and generates a **self-contained HTML dashboard** with: FR shape heatmap (capture × band, diverging colour scale), FR line charts per revision (pure SVG, no JS, no CDN), THD dumbbell charts vs. drive level, harmonic magnitude (H2–H7) heatmap, per-revision summary tiles. Supports dark mode via `prefers-color-scheme`. |

### Report & Audit Tools

| File | Lines | What It Does |
|------|-------|-------------|
| `report_audit.py` | 225 | Audits `comprehensive_data.json` against acceptance targets. **Generates `executive_summary.txt`**. Covers: (1) FR vs. target grading (per-band RMS, count exceeding tolerance), (2) THD data coverage — which bands have measurable THD vs. Nyquist-limited, (3) THD vs. drive level — is error clip-onset (level-dependent) or static (wrong fault type)?, (4) Harmonic magnitude deltas — correct per-order levels, not just RSS (THD). |
| `gap_audit.py` | ~260 | Grades every FR band + THD point against configurable thresholds (HUGE >3 dB / target >1.5 dB / good ≤1 dB). Reports **mean and spread** per band: large spread = setting-dependent error (taper/drive-tracking); consistent mean = fixed shape error (component value). **`--mode shape`** grades the CURVE, not just each point: fits a dB/octave trend line per revision (catches a bass-light/treble-heavy tilt that no single band is big enough to flag) and groups flagged bands into contiguous runs vs isolated spikes (a run is more likely a real curve feature, correct or not; an isolated spike is more likely noise or a narrow anomaly worth its own look) — see `docs/validation-and-capture.md` §1b. |
| `cascade_analysis.py` | 115 | Separates FR gaps by **which circuit stage** can cause them. Uses blend, drive, and frequency-region discriminators to prevent fitting a downstream stage to compensate for an upstream error. Runs on JSON only — no re-rendering. **Replace the `REGIONS` tuple with your pedal's frequency bands of interest.** |
| `capture_outlier_scan.py` | 263 | Flags captures that disagree with all siblings. Separates **capture-intrinsic** physics violations (corrupt file — quarantine) from **plugin-vs-capture** disagreement (real gap — investigate). Never says "wrong"; hands you the question. Runs on JSON only. |
| `knob_tolerant_null.py` | ~230 | **Knob-tolerant null test.** A captured knob setting is a best estimate someone read off a physical pot, not ground truth — nulling only at the exact label conflates a real model error with a mislabelled capture. Renders at the labelled ("nominal") settings, then coordinate-descends the continuous knob params within `--tolerance` (default ±5%) to find the deepest null, reporting both. A big nominal→best gap flags "label was probably off, judge the model by the best null"; a small gap confirms the nominal null is the honest accuracy number. **Re-renders the plugin** per trial (needs `captures.render_args()`). |
| `harmonic_audit.py` | ~195 | **Per-ORDER harmonic audit** (H2–H5), plugin vs capture, at MATCHED DRIVE (`--dbu-capture` converts a NAM `input_level_dbu` into the offset). THD is an RSS, so it can read correct while every term inside it is wrong; this reports the orders. Marks every cell sitting on the reference's own floor via two independent tests that need no reference capture: **order inversion** (H4 ≥ H3 is impossible for a mild polynomial) and **level slope** (a square law's absolute H2 must rise 2 dB/dB). ⚠ Read the units — that slope is on absolute dBFS, not dBc. |
| `compression_audit.py` | ~130 | **Per-band compression**, plugin vs capture, at matched drive. Compression reads the FUNDAMENTAL rather than a harmonic 40–60 dB down, so on this dataset it is the best-conditioned nonlinear measurement there is (floor 0.145–0.210 dB against 4–9 dB for the harmonics). It also carries the cubic's SIGN. Measures its own floor from the known-answer probe. ⚠ That probe needs **3f** below the mode shelf zero, not f — compression is third order. |
| `band_audit.py` | ~230 | **Per-band THD AND per-band compression in one view**, with the measured floor printed beside every delta so a number is never read without it. Reports the order set each THD cell used (it shrinks with frequency as harmonics pass Nyquist, so THD is not comparable across bands — `thd_h2_only` is), and flags order inversion per cell. The floor probe runs down to 20 Hz on purpose: the bass is where the reference models are least trustworthy, so starting it higher would hide what it exists to find. |
| `phase_sweep.py` | — | Whole-band **phase** comparison, plugin vs capture — the axis no other report covered, and the one that has caught three defects on this project that magnitude testing structurally cannot see. |

### Tooling Integrity Tests

These validate the estimators themselves before you trust any number they produce.

| File | Lines | What It Does |
|------|-------|-------------|
| `farina_validate.py` | 201 | Validates the **Farina continuous-THD curve** against the discrete-tone THD estimator via a **bracket test**: `THD(−18) ≤ THD_tone(−14) ≤ THD(−12)`. `--probe` mode dumps per-order magnitudes to identify which harmonic order spikes where. **Re-renders the plugin** for each capture (needs `captures.render_args()`). |
| `farina_regression_check.py` | 67 | Proves the order-limit fix is **bit-identical below 2714 Hz** on every capture. Run after any change to `harmonic_thd_curve()` in `analyze.py`. |
| `hf_thd_flatness_check.py` | 125 | Cross-validates swept THD against an **independent** discrete-tone estimator at 2 kHz and 4 kHz. Separates two questions usually conflated: (1) does the magnitude agree? (2) is it monotonic in level? **Re-renders the plugin** (needs `captures.render_args()`). |
| `tone_thd_nyquist_check.py` | 87 | Validates the discrete-tone THD estimator at HF. Unguarded `analyze.thd()` clamps out-of-band harmonics to the top FFT bin, inflating THD by up to √N of the near-Nyquist noise. Compares **guarded** (drop orders past Nyquist) vs. **unguarded**, quantifying the fabrication. |
| `base_rate_warp_measure.py` | ~105 | Measures the model's own **base-rate bilinear top-octave warp** by rendering the dry/linear path at 48 kHz vs. 96 kHz (near-analog reference) and differencing the FR. Droop = what a calibration high-shelf should invert. |

## Key Concepts

### The Test Signal

`test_signal_48k.wav` (48 kHz, 32-bit float, ~4.8 min, ~53 MB — gitignored, regenerate with
`python analysis/gen_test_signal.py`). Built for Echo Pre 3, which is a **clean, high-headroom
preamp**: sub-dB response differences and low-percent THD, so the signal favours long coherent
dwells over broad coverage of things a distortion pedal would need.

| Segment | Content | Purpose |
|---------|---------|---------|
| `marker_head` / `marker_tail` | 0.2 s chirp | Alignment; truncation detection |
| `cal_1k` | 1 kHz @ -18 dBFS, at 0.5–1.8 s | Level anchor + sample-rate mislabel detection |
| `noise_floor` | 2 s silence | The capture's own floor — the bound on everything else |
| `sweep_clean`, `sweep_-26/-16/-6` | 4 × 20 s ESS, 10 Hz → 22 kHz | FR at four input levels; continuous THD(f) by Farina |
| `comp_<f>_<dB>` | 16 freqs × 10 levels, ≥48 cycles/cell | **Per-band compression**, 20 Hz–20 kHz; doubles as THD(f, level) |
| `tone_<f>_<dB>` | 16 freqs × 4 levels, ≥96 cycles/cell | **THD**, 20 Hz–8 kHz, orders H2–H8 |
| `imd_guitar_<dB>` | 220 + 660 Hz at 3 levels | Audible chord intermod |
| `repeat_800_-11` | Exact duplicate of `comp_800_-11` | Repeatability floor |

Three design points that are easy to undo by accident:

- **Sweeps run 10 Hz → 22 kHz, not 20 → 20 k**, so the 20 Hz and 20 kHz *reporting* bands sit
  inside the sweep instead of on its edge where deconvolution artefacts live.
- **Every tone cell carries a 0.15 s settle head that `seg_of()` discards.** It exceeds the 132 ms
  receptive field of the reference NAM models, so no cell is contaminated by the one before it.
  `GAP` is 0.25 s for the same reason.
- **Cells hold at least 48 cycles**, which puts the nearest harmonic at least 48 DFT bins from the
  fundamental — far outside a Blackman-Harris main lobe. That is what makes reading harmonics off
  a compression cell valid rather than approximate.
- **All three blocks draw their input levels from one grid**, so -26, -16 and -6 dBFS are each
  measurable by the swept Farina THD, the discrete-tone THD and the comp-cell harmonic read.
  THD is level-dependent, so grids that do not share levels give three instruments and no way to
  arbitrate between them.

**THD above ~12 kHz is not measurable at 48 kHz by any method**, because H2 of a 12.5 kHz tone is
already past Nyquist. The tone grid therefore stops at 8 kHz, and `harmonics()` returns `None` for
an unmeasurable order rather than zero, so THD is never silently understated.

**Never insert segments in the middle** — it shifts every later segment's offset
and invalidates all existing captures. Append new segments at the end only.

### Reporting grids

| Quantity | Grid | Where |
|---|---|---|
| Frequency response | **60 bands**, 1/6 octave, 20 Hz–20 kHz | `band_fr()` |
| FR, densified | to 1/24 octave inside `INTEREST_BANDS` | `analysis_freqs()` |
| Compression | **16 bands**, 2/3 octave, 20 Hz–20 kHz × 10 levels | `compression_table()` |
| THD | **16 bands**, 20 Hz–8 kHz × 4 levels, H2–H8 | `thd_table()` |
| THD, continuous | Farina swept curve to ~10.4 kHz | `harmonic_thd_curve()` |
| Sign of the even term | waveform asymmetry, per cell | `asymmetry()` |
| Sign of the cubic | slope of `comp_db` with level | `compression_curve()` |

`INTEREST_BANDS` is set for this pedal: the C10 high-pass corner region (10–60 Hz, the
level-independent VOLUME probe), the two MODE source-bypass corners (1.5–6 kHz), and the input
low-pass plus top-octave droop region (5–13 kHz).

### Self-test

```bash
python analysis/analyze.py --selftest
```

Feeds the real test signal through systems whose response is known in closed form and asks each
instrument to recover it: a 1st-order 30 Hz–7.3 kHz bandpass for FR and for compression flatness,
and `y = x + 0.05x² + 0.02x³` for the harmonics. Twelve checks, including both of the SIGNS
`circuit.md` demands before a limiter is chosen: waveform asymmetry recovers the even term and
flips with it, and the compression slope reads expansion for a positive cubic and compression for
a negative one. Current margins are 0.037 dB on frequency response and 0.003 dB on harmonic
levels. **Run this after touching either file** — an instrument that has
not been checked against a known answer is not an instrument.

### The Farina THD Curve

`analyze.harmonic_thd_curve()` deconvolves a driven exponential sweep against
the clean reference sweep to extract time-separated harmonic impulse responses.
This yields a **continuous THD(f) curve** from a single capture.

**Order limiting** (on by default): the reference sweep has no energy above
`SWEEP_F1` (20 kHz), so order N is only measurable while `N·f ≤ SWEEP_F1`.
Without limiting, each order produces a large spurious spike at exactly
`SWEEP_F1/N` (e.g., H7 spikes at 2857 Hz). With limiting:
- Nothing below ~2714 Hz changes (all 7 orders in-band)
- Coverage extends to ~9.5 kHz (H2 only)
- Above 12 kHz, individual harmonic orders are unmeasurable at 48 kHz (H2 lands
  past Nyquist) — this is a sampling-theory limit, not a statement that the FR
  or THD in the top octave is irrelevant. The FR shape here still affects
  perceived brightness, especially when saturation is active.

### Calibration Workflow (the proven order)

1. **Validate STRUCTURE before AMOUNT** — get FR shape and per-harmonic structure
   (which orders, where placed) right first. THD magnitude is downstream.
2. **FR shape** — gain-match, compare linear FR. Fix EQ/tapers.
3. **Per-harmonic structure** — compare H2–H7 re fundamental. A correct THD can
   hide wrong individual magnitudes (same RSS, different timbre).
4. **Input reference calibration** (volts/FS from clip onset).
5. **Clip character** — asymmetry, knee softness, junction capacitance.
6. **Output level** — per-revision makeup gain.
7. **Re-run** full A/B; decompose residuals with `linear_removed_null()` before
   changing more constants.

## First-time Setup

### 1. Implement `captures.py`

This is the only file you need to write. It provides four things:

```python
RENDER_BIN = "build/OfflineRender_artefacts/Release/OfflineRender"  # your renderer path

def find_captures(directory="analysis/captures"):
    """Return [(path, parsed_dict), ...] for each .wav."""

def load_capture(path, expect_fs=48000):
    """Return float64 mono audio, auto-correcting rate-mislabeled headers."""

def render_args(parsed, extra_args=None):
    """Parsed settings -> flat CLI args list for your OfflineRender."""
```

A skeleton is already in `captures.py` with `load_capture` pre-implemented
(including the rate-mislabel fix). You only need to fill in `parse_capture()`
and `render_args()`.

### 2. Create your OfflineRender binary

Your plugin must provide a console-mode executable (e.g. a JUCE `ConsoleApplication`)
that mirrors `processBlock()` gain staging. Its CLI should accept:

- Input WAV path
- Output WAV path  
- Knob/switch positions (whatever your pedal has)
- `--os <factor>` for oversampling override
- Calibration overrides (for parameter sweeps)

### 3. Write your `comprehensive_report.py`

Model it after this pattern (it's what ties everything together):

```python
# comprehensive_report.py — you write this
import captures as C
import analyze as A
import json

for path, parsed in C.find_captures():
    cap = C.load_capture(path)
    ren = render_plugin(C.render_args(parsed))  # call your CLI
    # run A.transfer(), A.harmonic_thd_curve(), ...
    # collect into a JSON structure matching the dashboard schema
```

The NoAmp project's production version is available as a reference in the
`reports/example_dashboard.html` output format — study it to understand the
expected JSON schema.

## Naming Conventions for Capture Files

The `analyze.parse_filename()` helper auto-detects two notations:
- **Clock HHMM**: `V1200 B1330 T1200` — 0700=min, 1200=noon(0.5), 1700=max
- **0–10 scale**: `G3 V4 B6 T4` — plain dial position / 10

If your pedal uses different knob labels, write your own `parse_capture()` in
`captures.py` instead.

## Known Gotchas

- **Rate-mislabeled captures**: Some NAM modelers export 44.1 kHz audio inside
  a 48 kHz WAV header. Reading naively plays 8.8% fast and decorrelates the
  entire upper band. The `captures.load_capture()` skeleton includes detection
  via the 1 kHz cal tone — keep this logic.
- **Write 32-bit float renders**: Fixed-point output (16/24-bit int) hard-clips
  at ±1.0 FS. Driven sweeps routinely exceed 0 dBFS after makeup gain — this
  injects a spurious, input-level-independent THD floor that silently corrupts
  every measurement.
- **Judge wet path on full-wet captures**: At partial blend, the pedal's dry+wet
  paths can phase-cancel in the top octave (20 dB at 14 kHz on a BL=0.50
  capture). The plugin typically won't reproduce that cancellation, so a
  partial-blend FR read shows a false "plugin too bright" error.
- **Always write analysis scripts as files**, never as inline commands. Renders
  take seconds each; Farina harmonic analysis takes seconds per segment.
- **Grade the curve, not just each point** — point-by-point FR/THD grading
  (`gap_audit.py --mode summary`/`detail`) can't see a systematic tilt (every
  band individually "good," but bass-light/treble-heavy end-to-end is still a
  real, audible EQ error) and can misjudge a real, correct notch as an anomaly
  when it's read only in isolation, with no reference to the surrounding
  curve shape or sibling captures. Always also run `--mode shape` (trend-line
  fit + contiguous-run vs. isolated-spike check) before calling a revision
  done — see `docs/validation-and-capture.md` §1b.
- **A capture's knob setting is a best estimate, not ground truth** — it's a
  label someone read off a physical pot with no digital readout, so it can
  easily be a few percent off the true position. A shallow null against the
  exact labelled setting can mean the MODEL is wrong or the LABEL is wrong,
  and those need opposite fixes. Use `knob_tolerant_null.py` (coordinate-
  descends the knob params within a small tolerance to find the deepest null)
  before blaming the model for a nominal-setting null that doesn't deepen.

## Dependencies

- Python ≥ 3.9 — a project venv is set up at `.venv/` (gitignored); the system `python3` on this
  machine is 3.14 and has no numpy, so use `.venv/bin/python` or `python3.11`
- `numpy`
- `scipy` (for `scipy.io.wavfile`, `scipy.signal`, `scipy.signal.windows`)
- Your pedal's `OfflineRender` binary (C++ or otherwise)
