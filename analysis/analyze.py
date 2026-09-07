#!/usr/bin/env python3
"""Reusable analysis primitives for A/B-ing a pedal plugin against real-pedal captures.

This is the template's validation LIBRARY — the hard-won, pedal-agnostic parts: load/align,
frequency response, THD (discrete + continuous Farina swept-sine), sub-sample-aligned null depth,
the capture-filename parser, and the segment map (imported from gen_test_signal.py as the single
source of truth). Per-pedal ORCHESTRATORS (compare-vs-batch, null-vs-batch, knob-tracking pass/fail)
sit on top of this and call your pedal's OfflineRender CLI — see docs/validation-and-capture.md.

Run from the repo root (paths are repo-root-relative). FS + segment layout come from the generator.
"""
import os, re, numpy as np
from scipy.io import wavfile
from scipy import signal as sps
import gen_test_signal as G

FS = G.FS
ORIG = "analysis/test_signal_48k.wav"
T = G.segment_times()   # {segment_name: (t0, t1)} — single source of truth (no hand-typed offsets)


# --- I/O + alignment --------------------------------------------------------------------------
def load(path):
    sr, x = wavfile.read(path)
    if x.dtype.kind in "iu":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    assert sr == FS, f"{path}: expected {FS} Hz, got {sr}"
    return x


def is_full_length(x, orig, frac=0.95):
    """Guard against truncated captures: a short file's missing segments read as zeros and produce
    garbage (huge fake deltas / -200 dB nulls) rather than an honest skip. Check BEFORE align()
    (align pads to full length, which would defeat this check)."""
    return len(x) >= frac * len(orig)


def align(render, orig):
    """Integer-sample align `render` to `orig` via FFT cross-correlation on the clean sweep."""
    a, b = T["sweep_clean"]
    ref = orig[int(a * FS):int(b * FS)]
    seg = render[int(a * FS):int(min(len(render), (b + 0.5) * FS))]
    n = min(len(ref), len(seg))
    corr = sps.correlate(seg[:n] - seg[:n].mean(), ref[:n] - ref[:n].mean(), mode="full", method="fft")
    lag = int(np.argmax(np.abs(corr))) - (n - 1)
    if lag > 0:
        render = render[lag:]
    elif lag < 0:
        render = np.concatenate([np.zeros(-lag), render])
    if len(render) < len(orig):
        render = np.concatenate([render, np.zeros(len(orig) - len(render))])
    return render[:len(orig)], lag


def seg_of(x, name, settled=True):
    """Slice a named segment. `settled` (default) drops the leading SETTLE of a tone cell, which
    exceeds the 132 ms receptive field of the reference NAM models, so no cell carries the previous
    one's tail. Pass settled=False only to inspect the transition itself."""
    a, b = G.analysis_window(name, T) if settled else T[name]
    return x[int(a * FS):int(b * FS)]


# --- Level / frequency response ---------------------------------------------------------------
def rms_db(x):
    return 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def transfer(out, inp):
    f, Pxy = sps.csd(inp, out, FS, nperseg=8192)
    f, Pxx = sps.welch(inp, FS, nperseg=8192)
    H = np.abs(Pxy) / (Pxx + 1e-20)
    return f, 20 * np.log10(H + 1e-12)


def gain_at(f, mag, target):
    return mag[int(np.argmin(np.abs(f - target)))]


def fractional_octave_freqs(f_lo=20.0, f_hi=20000.0, frac=3):
    import math
    n = int(math.floor(frac * math.log2(f_hi / f_lo)))
    return [f_lo * 2.0 ** (i / frac) for i in range(n + 1)]


# Named regions where this pedal has narrow, must-resolve FR features; densified past the 1/6-oct
# base so the notch depth / peak Q read accurately (values from circuit.md / reference-fr-targets).
# (f_lo, f_hi, frac): local fractional-octave resolution inside that band.
INTEREST_BANDS = (
    (10.0, 60.0, 24),       # C10 high-pass corner — moves ~13 Hz (VOL up) to ~54 Hz (VOL down),
                            # and is the LEVEL-INDEPENDENT probe of where a capture's VOLUME sits
    (1500.0, 6000.0, 24),   # the two MODE source-bypass corners (22 nF ~2.01 kHz, 10 nF ~4.42 kHz)
                            # and the shelf between them — the pedal's defining feature
    (5000.0, 13000.0, 12),  # input R3/C3 low-pass (~7.3 kHz) + low-OS top-octave droop region
)


def analysis_freqs(f_lo=20.0, f_hi=20000.0, base_frac=6, interest=INTEREST_BANDS):
    """The reporting frequency grid for FR analyses: a 1/6-octave base (~60 pts, finer than a 24-band
    EQ) with extra points injected inside INTEREST_BANDS so the ~800 Hz notch, ~430 Hz mid features,
    treble peak, and HF rolloff are resolved. Returns a sorted, de-duplicated list. The sweep itself
    is continuous (transfer() has ~6 Hz bins) — this only sets where the tables/plots sample it."""
    freqs = set(fractional_octave_freqs(f_lo, f_hi, base_frac))
    for lo, hi, frac in interest:
        freqs.update(x for x in fractional_octave_freqs(lo, hi, frac) if lo - 1e-6 <= x <= hi + 1e-6)
    return sorted(freqs)


def sweep_segments():
    """Ordered {input_dBFS: segment_name} for every full-range sweep. Iterate this to read FR
    (band_fr) and THD (harmonic_thd_curve) as a function of INPUT LEVEL. The lowest level
    ('sweep_clean') is the linear reference and the alignment anchor."""
    segs = {G.SWEEP_LEVELS_DB[0]: "sweep_clean"}
    for db in G.SWEEP_LEVELS_DB[1:]:
        segs[db] = f"sweep_{db}"
    return dict(sorted(segs.items()))


# --- THD: discrete tone + continuous Farina swept-sine ----------------------------------------
def thd(x, f0):
    w = np.hanning(len(x)); X = np.abs(np.fft.rfft(x * w)); fr = np.fft.rfftfreq(len(x), 1 / FS)
    def amp(fc):
        i = int(np.argmin(np.abs(fr - fc))); return np.max(X[max(0, i - 3):i + 4])
    fund = amp(f0)
    harm = np.sqrt(sum(amp(f0 * k) ** 2 for k in range(2, 9)))
    return 100 * harm / (fund + 1e-20), fund


ORDER_LIMIT_MARGIN = 0.95   # keep order N only while N*f <= SWEEP_F1*this (edge spike sits AT f1/N)


def harmonic_thd_curve(capture_sweep, ref_sweep, max_order=7, order_limit=True):
    """Continuous THD(f) via Farina exponential-sweep harmonic separation. Deconvolve the captured
    driven sweep against the clean reference sweep; the N-th harmonic IR is time-advanced by
    dt_N = T*ln(N)/ln(f1/f0), so gate each, FFT, and map to the fundamental axis. Returns
    (freqs, thd_pct, {order: |H|}).

    ORDER LIMITING (`order_limit=True`, the default — added 2026-07-17 after this curve was finally
    validated against thd(), as this docstring had demanded all along; it FAILED).

    The deconvolution divides by the reference sweep's spectrum, which carries NO energy above
    SWEEP_F1 (20 kHz). So order N is only measurable while N*f <= SWEEP_F1: past that, the
    regularised division (|X|^2 + eps) blows up and order N produces a large SPURIOUS EDGE SPIKE
    at exactly f = SWEEP_F1/N, then collapses to ~0. Measured on V1E D0.50 (analysis/
    farina_validate.py --probe), H7 re fundamental:
        2800 Hz -53.0 dB | 2857 Hz -35.0 | 2874 Hz -16.8 | 2900 Hz -10.7 | 3000 Hz -76.9
    i.e. a 36 dB spike centred on 20000/7 = 2857 Hz, which drove THD 4.7% -> 29.7% and was
    reported as a real "plugin THD 14.0% vs pedal 2.4% @2874 Hz" finding on nearly every V1E
    capture. The same artefact sits at 20000/6=3333, 20000/5=4000 (this one broke the 4 kHz
    discrete-tone bracket test), 20000/4=5000, 20000/3=6667, 20000/2=10000.

    The fix masks order N above SWEEP_F1*ORDER_LIMIT_MARGIN/N. Consequences worth knowing:
      * Nothing below 19000/7 = 2714 Hz changes AT ALL (every order is still in band there), so
        every THD fit ever made on this project — all at the 100/200 Hz anchors — is untouched.
      * Coverage EXTENDS above the old 3 kHz ceiling instead of stopping: at 6 kHz H2+H3 remain
        valid; at 9.5 kHz H2 alone. Above ~9.5 kHz this sweep can measure NO harmonic, and above
        12 kHz THD does not exist at 48 kHz at all (H2 would land past Nyquist). "THD at 18 kHz"
        is not a measurable quantity here — it is not a tooling gap.
      * `Hn` is returned masked too, so per-order magnitudes agree with the THD built from them.
    Pass order_limit=False only to reproduce a pre-2026-07-17 number."""
    n = min(len(capture_sweep), len(ref_sweep))
    y = capture_sweep[:n].astype(np.float64); x = ref_sweep[:n].astype(np.float64)
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(x, nfft); Y = np.fft.rfft(y, nfft)
    eps = 1e-6 * np.mean(np.abs(X) ** 2)
    ir = np.fft.irfft(Y * np.conj(X) / (np.abs(X) ** 2 + eps), nfft)
    T_sweep = n / FS
    R = np.log(G.SWEEP_F1 / G.SWEEP_F0)

    def gated_spectrum(order):
        dt = T_sweep * np.log(order) / R
        center = int(round((-dt) * FS)) % nfft
        if order == 1:
            half = int(0.04 * FS)
        else:
            gap = (T_sweep / R) * np.log((order + 1) / order)   # secs to the next-higher order
            half = int(0.35 * gap * FS)                          # 35% of the gap -> no overlap
        half = max(half, int(0.01 * FS))
        idx = (np.arange(center - half, center + half) % nfft)
        spec = np.fft.rfft(ir[idx] * np.hanning(len(idx)), nfft)
        return np.fft.rfftfreq(nfft, 1 / FS), np.abs(spec)

    fr, H1 = gated_spectrum(1)
    Hn = {1: H1}
    for N in range(2, max_order + 1):
        frN, mag = gated_spectrum(N)
        Hn[N] = np.interp(fr, frN / N, mag, left=0.0, right=0.0)   # remap harmonic->fundamental axis
        if order_limit:
            # Order N is unmeasurable once its harmonic leaves the reference sweep's band.
            Hn[N] = np.where(N * fr <= G.SWEEP_F1 * ORDER_LIMIT_MARGIN, Hn[N], 0.0)
    with np.errstate(divide="ignore", invalid="ignore"):
        harm = np.sqrt(sum(Hn[N] ** 2 for N in range(2, max_order + 1)))
        thd_pct = 100.0 * harm / (H1 + 1e-20)
    return fr, thd_pct, Hn


def thd_max_measurable_hz(max_order=2):
    """Highest fundamental at which THD is measurable from this sweep, using orders up to
    `max_order`. THD needs at least H2, so the ceiling is SWEEP_F1*margin/2 ~= 9.5 kHz — and no
    test signal can beat FS/4 = 12 kHz at 48 kHz, because H2 lands past Nyquist above that."""
    return min(G.SWEEP_F1 * ORDER_LIMIT_MARGIN / max_order, FS / (2.0 * max_order))


# --- Sub-sample-aligned null test -------------------------------------------------------------
def frac_align(test, ref):
    """Shift `test` by a FRACTIONAL number of samples to best line up with `ref` (FFT phase ramp;
    parabolic refinement of the xcorr peak). Integer alignment isn't enough for a deep null —
    1 sample at 20 kHz is ~150 deg of phase error."""
    n = min(len(test), len(ref))
    a = test[:n] - test[:n].mean(); b = ref[:n] - ref[:n].mean()
    corr = sps.correlate(a, b, mode="full", method="fft")
    k = int(np.argmax(np.abs(corr)))
    if 0 < k < len(corr) - 1:
        y0, y1, y2 = np.abs(corr[k - 1]), np.abs(corr[k]), np.abs(corr[k + 1])
        denom = (y0 - 2 * y1 + y2); delta = 0.5 * (y0 - y2) / denom if denom else 0.0
    else:
        delta = 0.0
    lag = (k - (n - 1)) + delta
    X = np.fft.rfft(test); freqs = np.fft.rfftfreq(len(test))
    return np.fft.irfft(X * np.exp(-1j * 2 * np.pi * freqs * (-lag)), len(test))


def null_depth(ref, test):
    """Optimal-gain-match `test` to `ref`, subtract, return (null_dB, applied_gain_dB). The gain
    match means the null measures TIMBRE/shape/phase agreement, NOT absolute level (report level
    separately). frac_align first. This is the RAW null — its residual still contains every LINEAR
    mismatch (EQ-shape, phase) plus the nonlinear part; use linear_removed_null() to split them."""
    g = float(np.dot(ref, test) / (np.dot(test, test) + 1e-30))
    resid = ref - g * test
    null_db = 20 * np.log10((np.sqrt(np.mean(resid ** 2)) + 1e-20) / (np.sqrt(np.mean(ref ** 2)) + 1e-20))
    return null_db, 20 * np.log10(abs(g) + 1e-20)


def linear_removed_null(test, ref):
    """The null floor if EVERY linear (EQ + phase) difference were perfectly matched — i.e. the
    residual that is genuinely NONLINEAR (clipping-harmonic phase) plus the capture's own fidelity.
    Computed from the magnitude-squared coherence gamma^2(f) (Welch-averaged, so limited DOF — it
    does NOT overfit the way a per-bin Y/X division would): residual power fraction = 1 - gamma^2.

    Interpretation vs the raw null_depth():
      - linear_removed MUCH deeper than raw  -> residual is mostly LINEAR -> a better taper / less
        discretization warp could deepen the real (shipped-plugin) null toward it.
      - linear_removed ~= raw                -> residual is mostly nonlinear / capture floor -> you
        are near the limit; tweaking the plugin won't help.
    This is a DIAGNOSTIC (it applies a correction not in the plugin) — the shipped plugin's honest
    null stays the null_depth() number; report this separately as the nonlinear/clipping-match floor."""
    n = min(len(test), len(ref))
    f, cxy = sps.coherence(test[:n], ref[:n], FS, nperseg=8192)
    f, pyy = sps.welch(ref[:n], FS, nperseg=8192)
    resid_frac = np.sum(pyy * (1.0 - cxy)) / (np.sum(pyy) + 1e-30)
    return 10.0 * np.log10(resid_frac + 1e-20)


# --- Capture-filename parsing -----------------------------------------------------------------
# Auto-detects both notations seen in practice:
#   clock HHMM ('V1200 B1330 ... switch mid'): 0700=min .. 1200=noon .. 1700=max, 3-4 digits
#   0-10 scale ('G3 V4 B6 T4 SYM'):            plain dial 0..10, /10
# A token value >= 100 is clock; < 100 is the 0-10 scale. Switch token OR a Sym/Asym/Open keyword.
def clock_to_x(hhmm):
    s = str(int(hhmm))
    if len(s) == 5:
        s = s[:4]                       # 'G10300' typo -> 1030
    if len(s) == 3 and s[0] == "1":
        s = s + "0"                     # '120' missing-trailing-zero -> 1200
    v = int(s); h, m = v // 100, v % 100
    return max(0.0, min(1.0, (h + m / 60.0 - 7.0) / 10.0))


def knob_to_x(raw):
    v = int(raw)
    return clock_to_x(v) if v >= 100 else max(0.0, min(1.0, v / 10.0))


def switch_to_mode(name):
    m = re.search(r"switch (\w+)", name, re.IGNORECASE)
    if m:
        return {"up": 0, "mid": 1, "down": 2}[m.group(1).lower()]
    low = name.lower()
    if "asym" in low:
        return 0
    if "open" in low:
        return 1
    return 2


def parse_filename(name, knobs=("B", "T", "V", "G")):
    """Filename -> dict of knob positions (0..1) + mode (0/1/2) + sw label. `knobs` lists the
    single-letter tags to extract (override per pedal if your labels differ)."""
    def g(k):
        mm = re.search(rf"{k}0*(\d+)", name)   # tolerate a leading-zero typo e.g. 'B01200'
        return int(mm.group(1)) if mm else 0
    mode = switch_to_mode(name)
    out = {k: knob_to_x(g(k)) for k in knobs}
    out["mode"] = mode
    out["sw"] = ["up", "mid", "down"][mode]
    return out


# ==============================================================================================
# Echo Pre 3 instruments: fractional-octave FR, per-band compression, per-order harmonics.
#
# This pedal is subtle — sub-dB response differences and low-percent THD — so every instrument
# below is coherent (evaluated at exactly the known tone frequency) rather than peak-picking an
# FFT bin, and every one has a known-answer test in `--selftest`. Read
# docs/measurement-discipline.md before trusting any number these produce on real data.
# ==============================================================================================

FR_BAND_FRAC = 6        # 1/6 octave -> 60 bands over 20 Hz .. 20 kHz


def fr_bands(f_lo=20.0, f_hi=20000.0, frac=FR_BAND_FRAC):
    """(centres, lower_edges, upper_edges) for the fractional-octave FR report."""
    centers = np.asarray(fractional_octave_freqs(f_lo, f_hi, frac), dtype=float)
    half = 2.0 ** (1.0 / (2.0 * frac))
    return centers, centers / half, centers * half


def band_average(f, mag_db, frac=FR_BAND_FRAC, f_lo=20.0, f_hi=20000.0):
    """Power-average a continuous magnitude response into fractional-octave bands.

    Returns (centres, band_db, n_bins). **Check n_bins.** A band with 0 bins was interpolated at
    its centre because the underlying spectrum is coarser than the band — that is a resolution
    warning, not a measurement, and it is exactly how a too-short analysis gate silently fakes a
    low-frequency response."""
    centers, lo, hi = fr_bands(f_lo, f_hi, frac)
    f = np.asarray(f, dtype=float)
    lin = 10.0 ** (np.asarray(mag_db, dtype=float) / 20.0)
    out = np.empty(len(centers))
    nbin = np.zeros(len(centers), dtype=int)
    for i, (a, b) in enumerate(zip(lo, hi)):
        m = (f >= a) & (f < b)
        nbin[i] = int(m.sum())
        if nbin[i]:
            out[i] = 20 * np.log10(np.sqrt(np.mean(lin[m] ** 2)) + 1e-20)
        else:
            out[i] = float(np.interp(centers[i], f, mag_db))
    return centers, out, nbin


# The linear IR gate. `pre` is bounded inside farina_linear_ir() so it can never reach the H2
# impulse; `post` sets the low-frequency resolution (1.0 s -> ~1 Hz, enough for a 1/6-octave band
# at 20 Hz, which is only 2.3 Hz wide).
LINEAR_GATE_PRE = 0.60
LINEAR_GATE_POST = 1.00


def _deconvolve(capture_sweep, ref_sweep):
    n = min(len(capture_sweep), len(ref_sweep))
    y = np.asarray(capture_sweep[:n], dtype=np.float64)
    x = np.asarray(ref_sweep[:n], dtype=np.float64)
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(x, nfft)
    Y = np.fft.rfft(y, nfft)
    eps = 1e-6 * np.mean(np.abs(X) ** 2)
    return np.fft.irfft(Y * np.conj(X) / (np.abs(X) ** 2 + eps), nfft), nfft, n


def farina_linear_ir(capture_sweep, ref_sweep, pre=LINEAR_GATE_PRE, post=LINEAR_GATE_POST):
    """The LINEAR impulse response only, gated out of a Farina deconvolution.

    Deliberately asymmetric. harmonic_thd_curve() uses a symmetric +-40 ms gate, which is right for
    THD but far too short at the bottom: 40 ms is under one cycle at 20 Hz, so a symmetric gate
    cannot resolve the low bands at all. The harmonic impulses all sit BEFORE the linear one, so
    only the pre-gate has to stay clear of them; the post-gate is free to run long."""
    ir, nfft, n = _deconvolve(capture_sweep, ref_sweep)
    dt2 = (n / FS) * np.log(2.0) / np.log(G.SWEEP_F1 / G.SWEEP_F0)   # H2 sits dt2 earlier
    pre = min(pre, 0.45 * dt2)
    ipre, ipost = int(pre * FS), int(post * FS)
    idx = np.arange(-ipre, ipost) % nfft
    return ir[idx] * sps.windows.tukey(ipre + ipost, 0.10), nfft


def sweep_fr(capture_sweep, ref_sweep, **kw):
    """Continuous magnitude response (dB) of the system that turned ref_sweep into capture_sweep."""
    seg, nfft = farina_linear_ir(capture_sweep, ref_sweep, **kw)
    spec = np.fft.rfft(seg, nfft)
    return np.fft.rfftfreq(nfft, 1.0 / FS), 20 * np.log10(np.abs(spec) + 1e-20)


def band_fr(capture_sweep, ref_sweep, frac=FR_BAND_FRAC, **kw):
    """Fractional-octave frequency response. Default 1/6 octave = 60 bands, 20 Hz .. 20 kHz."""
    f, mag = sweep_fr(capture_sweep, ref_sweep, **kw)
    return band_average(f, mag, frac=frac)


# --- Coherent tone measurement ------------------------------------------------------------------
_BH_CACHE = {}


def _bh_window(n):
    """Blackman-Harris window, its sum, and the time axis — memoised, since a compression table
    calls dft_at() thousands of times at a handful of distinct lengths."""
    hit = _BH_CACHE.get(n)
    if hit is None:
        w = sps.windows.blackmanharris(n)
        hit = _BH_CACHE[n] = (w, float(np.sum(w)), np.arange(n) / FS)
    return hit


def dft_at(x, f):
    """Complex PEAK amplitude at exactly `f`, Blackman-Harris windowed.

    Evaluated at the known frequency rather than at the nearest FFT bin, so there is no scalloping
    loss and no need to peak-pick. The window's sidelobes are below -90 dB and its main lobe is
    ~8 bins wide; tone cells hold at least 48 cycles, so the nearest harmonic is at least 48 bins
    away and contributes nothing. That is what makes reading H2..H8 off a compression cell valid."""
    n = len(x)
    w, wsum, t = _bh_window(n)
    return 2.0 * np.sum(np.asarray(x, dtype=np.float64) * w * np.exp(-2j * np.pi * f * t)) / wsum


def harmonic_phasors(x, f0, max_order=None):
    """{order: complex peak amplitude, or None past Nyquist}. Keeps phase, which `harmonics()`
    throws away — needed if you ever want to reconstruct the shaper rather than just size it."""
    max_order = max_order or G.TONE_MAX_ORDER
    nyq = FS / 2.0
    return {k: (dft_at(x, k * f0) if k * f0 < 0.98 * nyq else None)
            for k in range(1, max_order + 1)}


def harmonics(x, f0, max_order=None):
    """{order: dBFS peak} for 1..max_order. An order whose frequency is at or past Nyquist returns
    None — it is not measurable at this sample rate, and calling it zero would understate THD."""
    return {k: (None if v is None else 20 * np.log10(abs(v) + 1e-20))
            for k, v in harmonic_phasors(x, f0, max_order).items()}


def asymmetry(x, pct=99.5):
    """Peak asymmetry of a tone cell, mean-removed: 2*(pos - neg)/(pos + neg).

    **This is how the SIGN of the even nonlinearity gets measured.** For a stage behaving as
    y = x + a2*x^2 + a3*x^3 driven at amplitude A, this reads a2*A/(1 + a3*A^2), so a positive
    result means the positive half of the waveform is the larger one and a2 > 0.

    Deliberately not a DC measurement. DC would be the obvious read on a2, but C10 removes it in
    the pedal and the converters remove it again in any real capture, so DC is structurally
    unavailable. Waveform asymmetry survives both. Peaks are taken at the `pct` percentile rather
    than as a raw max, so a noisy reamp capture does not read its worst sample as the peak; the
    small bias that introduces is identical on both halves and cancels in the ratio.

    The companion sign — the sign of the CUBIC — is already in `compression_curve()`: `comp_db`
    rising with level is expansion (a3 > 0), falling is compression (a3 < 0). Both signs are what
    circuit.md asks for before a limiter is chosen."""
    y = np.asarray(x, dtype=np.float64)
    y = y - np.mean(y)
    pos = float(np.percentile(y, pct))
    neg = float(-np.percentile(y, 100.0 - pct))
    return 2.0 * (pos - neg) / (pos + neg + 1e-20)


def thd_tone(x, f0, max_order=None):
    """(THD %, measurable orders, {order: dBFS}). THD is built only from orders that fit under
    Nyquist, and the order list is returned so a table can say which ones those were."""
    h = harmonics(x, f0, max_order)
    fund = 10.0 ** (h[1] / 20.0)
    orders = [k for k in sorted(h) if k >= 2 and h[k] is not None]
    harm = np.sqrt(sum((10.0 ** (h[k] / 20.0)) ** 2 for k in orders)) if orders else 0.0
    return 100.0 * harm / (fund + 1e-20), orders, h


# --- Per-band compression -----------------------------------------------------------------------
def _comp_freq(label):
    return G.COMP_FREQS[list(G.COMP_FREQ_LABELS).index(label)]


def compression_curve(capture, label):
    """Output level and gain vs input level at one of the 16 COMP frequencies.

    `comp_db` is gain relative to the QUIETEST cell, so it reads 0 at the bottom and goes negative
    as the stage compresses. Comparing comp_db across bands is the whole point: on this circuit the
    MODE bypass sets how much degeneration each band sees, so the bands genuinely compress by
    different amounts and a 1 kHz-only ladder would miss it."""
    f0 = _comp_freq(label)
    ins = np.array([float(db) for db in G.COMP_LEVELS_DB])
    outs = np.array([20 * np.log10(abs(dft_at(seg_of(capture, G.comp_name(label, db)), f0)) + 1e-20)
                     for db in G.COMP_LEVELS_DB])
    gain = outs - ins
    return {"label": label, "freq": f0, "in_db": ins, "out_db": outs,
            "gain_db": gain, "comp_db": gain - gain[0]}


def compression_table(capture):
    """[(label, small_signal_gain_dB, loud_gain_dB, compression_dB)] for all 16 bands."""
    rows = []
    for label in G.COMP_FREQ_LABELS:
        c = compression_curve(capture, label)
        rows.append((label, float(c["gain_db"][0]), float(c["gain_db"][-1]), float(c["comp_db"][-1])))
    return rows


def thd_table(capture, max_order=None):
    """THD and per-order harmonic levels over the 16 x 4 tone grid."""
    rows = []
    for label, f0 in zip(G.TONE_FREQ_LABELS, G.TONE_FREQS):
        for db in G.TONE_LEVELS_DB:
            pct, orders, h = thd_tone(seg_of(capture, G.tone_name(label, db)), f0, max_order)
            rows.append({"label": label, "freq": f0, "in_db": db, "thd_pct": pct,
                         "orders": orders, "h_db": h})
    return rows


# --- Measurement floors -------------------------------------------------------------------------
def noise_floor_db(capture):
    """RMS of the silent segment. Every level below this is noise wearing a number."""
    return rms_db(seg_of(capture, "noise_floor"))


def repeat_residual_db(capture):
    """Residual between the repeat cell and its byte-identical twin earlier in the signal, relative
    to the cell's own level. A memoryless system returns -inf. Anything above this floor is the
    capture's repeatability limit, and no result smaller than it is real."""
    a = seg_of(capture, G.REPEAT_TWIN)
    b = seg_of(capture, G.REPEAT_TWIN.replace("comp_", "repeat_"))
    n = min(len(a), len(b))
    return rms_db(a[:n] - b[:n]) - rms_db(a[:n])


# ==============================================================================================
# Known-answer self-test.  `python analyze.py --selftest`
#
# measurement-discipline.md's first rule: an instrument you have not tested against a known answer
# is not an instrument. Each check below feeds the real test signal through a system whose response
# is known in closed form, then asks the instrument to recover it.
# ==============================================================================================
def _selftest(tol_fr_db=0.25, tol_harm_db=0.15, tol_comp_db=0.02):
    fails = []

    def check(name, ok, detail):
        print(f"  {'PASS' if ok else 'FAIL'}  {name:38} {detail}")
        if not ok:
            fails.append(name)

    sig, times = G.assemble()
    sig = sig.astype(np.float64)

    # --- Known LINEAR system: 1st-order 30 Hz .. 7.3 kHz bandpass -------------------------------
    b, a = sps.butter(1, [30.0, 7300.0], btype="band", fs=FS)
    lin_out = sps.lfilter(b, a, sig)

    ref = lin_out[int(times["sweep_clean"][0] * FS):int(times["sweep_clean"][1] * FS)]
    src = sig[int(times["sweep_clean"][0] * FS):int(times["sweep_clean"][1] * FS)]
    centers, meas, nbin = band_fr(ref, src)

    w, h = sps.freqz(b, a, worN=1 << 16, fs=FS)
    _, ideal, _ = band_average(w, 20 * np.log10(np.abs(h) + 1e-20))
    err = np.abs(meas - ideal)

    check("FR band count >= 45", len(centers) >= 45, f"{len(centers)} bands @ 1/{FR_BAND_FRAC} oct")
    check("FR every band resolved", int(np.sum(nbin == 0)) == 0,
          f"{int(np.sum(nbin == 0))} interpolated bands (want 0)")
    check("FR recovers known filter", float(np.max(err)) < tol_fr_db,
          f"max |err| {np.max(err):.3f} dB @ {centers[int(np.argmax(err))]:.0f} Hz "
          f"(tol {tol_fr_db})")

    # Compression of a LINEAR system must be flat at every band, at every level.
    worst, worst_lbl = 0.0, None
    for label in G.COMP_FREQ_LABELS:
        c = compression_curve(lin_out, label)
        m = float(np.max(np.abs(c["comp_db"])))
        if m > worst:
            worst, worst_lbl = m, label
    check("compression flat on linear system", worst < tol_comp_db,
          f"max |comp| {worst:.4f} dB @ {worst_lbl} Hz (tol {tol_comp_db})")

    # --- Known STATIC nonlinearity: y = x + a2 x^2 + a3 x^3 -------------------------------------
    # For x = A sin: H1 = A + 0.75 a3 A^3, H2 = 0.5 a2 A^2, H3 = 0.25 a3 A^3, H4+ = 0 exactly.
    a2, a3 = 0.05, 0.02
    nl_out = sig + a2 * sig ** 2 + a3 * sig ** 3

    worst, worst_cell = 0.0, None
    for label in (50, 200, 800, 2500):
        f0 = G.TONE_FREQS[list(G.TONE_FREQ_LABELS).index(label)]
        for db in (-16, -1):
            A = 10.0 ** (db / 20.0)
            want = {1: A + 0.75 * a3 * A ** 3, 2: 0.5 * a2 * A ** 2, 3: 0.25 * a3 * A ** 3}
            got = harmonics(seg_of(nl_out, G.tone_name(label, db)), f0, max_order=3)
            for k in (1, 2, 3):
                e = abs(got[k] - 20 * np.log10(want[k]))
                if e > worst:
                    worst, worst_cell = e, f"H{k} @ {label} Hz {db} dBFS"
    check("harmonics recover known shaper", worst < tol_harm_db,
          f"max |err| {worst:.3f} dB @ {worst_cell} (tol {tol_harm_db})")

    # H2/H3 sign and dominance: a positive a2 must make H2 dominate H3 at these levels.
    pct, orders, h = thd_tone(seg_of(nl_out, G.tone_name(200, -1)),
                              G.TONE_FREQS[list(G.TONE_FREQ_LABELS).index(200)])
    check("even-dominant shaper reads even-dominant", h[2] > h[3],
          f"H2 {h[2]:.1f} dB > H3 {h[3]:.1f} dB, THD {pct:.3f}%")

    # --- The two SIGNS circuit.md demands before a limiter is chosen ---------------------------
    # Even term: asymmetry reads a2*A/(1 + a3*A^2), so its sign is the sign of a2.
    f800 = G.TONE_FREQS[list(G.TONE_FREQ_LABELS).index(800)]
    A = 10.0 ** (-1 / 20.0)
    want_asym = a2 * A / (1.0 + a3 * A ** 2)
    got_asym = asymmetry(seg_of(nl_out, G.tone_name(800, -1)))
    check("asymmetry recovers the even term", abs(got_asym - want_asym) < 0.002,
          f"{got_asym:+.4f} vs {want_asym:+.4f} expected")

    flipped = asymmetry(seg_of(sig - a2 * sig ** 2 + a3 * sig ** 3, G.tone_name(800, -1)))
    check("asymmetry sign follows the even term", flipped < 0 < got_asym,
          f"a2>0 -> {got_asym:+.4f}, a2<0 -> {flipped:+.4f}")

    # Cubic term: comp_db rising with level is expansion, falling is compression.
    exp_db = compression_curve(sig + 0.02 * sig ** 3, 800)["comp_db"][-1]
    cmp_db = compression_curve(sig - 0.02 * sig ** 3, 800)["comp_db"][-1]
    check("compression sign follows the cubic", cmp_db < 0 < exp_db,
          f"a3>0 -> {exp_db:+.3f} dB (expands), a3<0 -> {cmp_db:+.3f} dB (compresses)")

    # --- Cross-method agreement ------------------------------------------------------------------
    common = sorted(set(G.COMP_LEVELS_DB) & set(G.SWEEP_LEVELS_DB) & set(G.TONE_LEVELS_DB))
    check("levels shared by all three THD methods", len(common) >= 3,
          f"{common} dBFS")

    # A memoryless system must repeat a duplicated cell exactly.
    check("repeat twin nulls on memoryless system", repeat_residual_db(nl_out) < -120,
          f"{repeat_residual_db(nl_out):.1f} dB")

    # Nyquist masking: the top fundamental must drop unmeasurable orders rather than call them zero.
    _, orders_hi, _ = thd_tone(seg_of(nl_out, G.tone_name(8000, -1)), G.TONE_FREQS[-1])
    check("orders past Nyquist are dropped", orders_hi == [2],
          f"measurable orders at 8 kHz: {orders_hi} (H3 = 24 kHz is at Nyquist)")

    print(f"\n{'ALL PASS' if not fails else 'FAILED: ' + ', '.join(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        sys.exit(_selftest())
    print(__doc__)
    print(f"{len(T)} segments, {max(b for _, b in T.values()):.1f} s")
