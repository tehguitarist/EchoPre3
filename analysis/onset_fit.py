#!/usr/bin/env python3
"""Why the model's H2-vs-DRIVE curve has the wrong SHAPE, and which mechanism fixes it.

    .venv/bin/python analysis/onset_fit.py --self-test        # validate the oracle first
    .venv/bin/python analysis/onset_fit.py                    # the residual, per mechanism

circuit.md note #24 left one substantive modelling gap: no single `Vov` closes both the MEAN H2
delta and its DRIVE SLOPE, so the model's clipping ONSET is misplaced as well as its curvature.
This is the instrument for that. It exists because a `Vov`/`gm` sweep through OfflineRender costs
~8 renders of a 56 s signal per parameter point, which is far too slow to explore a two-parameter
family, let alone a third mechanism.

⭐ THE ORACLE IS MEMORYLESS, WHICH IS WHY IT IS EXACT AND FAST -- and that is a property of the
data, not a simplification. In DARK the source one-port is the bare resistor R5 with no state
(JfetStage's own algebra collapses to it), and at 220 Hz the input network is flat and the mode
shelf does not exist. So one period of gate sine -> the closed-form quadratic per sample -> an FFT
is the EXACT steady-state answer, with no discretisation, no oversampling and no filter state.

⚠⚠ EVERY CANDIDATE IS CHECKED AGAINST THE PLUGIN FIRST (note #9's standing rule). `--self-test`
compares the oracle's H2 dBc against a real OfflineRender of the same cells, where the answer is
known by construction. A mechanism sweep run on an unvalidated oracle measures the oracle.
"""
import argparse, hashlib, json, os, subprocess, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import captures as C
import probe_analyse as PA
import lf_pole_attribution as LF

SIG = PA.SIG
PROBE_DIR = "analysis/captures/probe"
OUT = "analysis/reports/onset_fit.json"
CACHE = "/tmp/probe_renders"

# Shipped constants, parsed from the source so this file never becomes a second definition
# (circuit.md note #23 fault 4).
R5, R6, VA = 3.6e3, 22.0e3, 22.0
R3, R4, C3, C4 = 110.0e3, 220.0e-12, 22.0e-9, 1.0e6   # placeholders, overwritten below


def _parse_consts():
    src = open("src/dsp/CircuitValues.h").read() + open("src/dsp/JfetStage.h").read()
    import re
    def g(name):
        m = re.search(rf"\b{name}\s*=\s*([-\d.eE+]+)", src)
        return float(m.group(1))
    return dict(R3=g("kR3"), C3=g("kC3"), C4=g("kC4"), R4=g("kR4"), R6=g("kR6"), R5=g("kR5"),
                R9=g("kR9"), R8=g("kR8"), R10=g("kR10"), POT=g("kVolumePot"),
                TAPER=g("kVolumeTaperP"), C10=g("kC10"), VA=g("kVA"),
                gm=g("double gm"), ro=g("double ro"),
                mExp=g("double mExp"), vp=g("double vp"))


K = _parse_consts()
KINPUT_REF = float(__import__("re").search(r"kInputRef\s*=\s*([\d.]+)",
                                           open("src/PluginProcessor.h").read()).group(1))


def in_network(f):
    """Volts at the jack -> volts at the gate. Mirrors InputNetwork::analyticResponseS."""
    s = 2j * np.pi * np.asarray(f, dtype=complex)
    zC3, zC4 = 1.0 / (s * K["C3"]), 1.0 / (s * K["C4"])
    zGate = zC4 + K["R4"]
    zA = zC3 * zGate / (zC3 + zGate)
    return (zA / (K["R3"] + zA)) * (K["R4"] / zGate)


def drain_load(x):
    """Mirrors OutputNetwork::drainNodeImpedance() -- the per-block constant the stage is given."""
    ra = max(K["POT"] * x ** K["TAPER"], 0.1)
    rb = K["POT"] - ra
    zE = 1.0 / (1 / K["R10"] + 1 / ra + 1 / (K["R9"] + K["R8"] + rb))
    rd = K["R6"] * K["ro"] / (K["R6"] + K["ro"])
    return rd * zE / (rd + zE)


class Device:
    """Shichman-Hodges plus the mechanisms under test. `lam` and `nsub` default to OFF, i.e. to
    exactly what the plugin ships, so a sweep's zero point is the shipped model by construction."""

    def __init__(self, gm=None, vov=None, lam=0.0, nsub=0.0, m=None, zload=20.6e3, vp=None):
        self.gm = K["gm"] if gm is None else gm
        self.m = K["mExp"] if m is None else m
        # ⭐⭐ |Vp| IS THE BETTER PARAMETER, and `vov` is kept only so the older sweeps still run.
        # Self-bias with a transfer law of exponent m gives Vov = |Vp| / (1 + gm*R5/m), so
        # Id0 = gm*|Vp|/(m + gm*R5), which is BOUNDED ABOVE by |Vp|/R5 however large gm gets. Under
        # the (gm, Vov) parameterisation Id0 = gm*Vov/m instead, which grows without limit -- and
        # that is exactly how a Vov sweep at a fixed gm walked the quiescent drain to a NEGATIVE
        # voltage and got read as a curvature measurement (circuit.md note #22a). Parameterised on
        # |Vp| the bias point cannot collapse at all; the guard becomes unnecessary rather than
        # merely present.
        if vp is None and vov is None:
            vp = K["vp"]
        if vp is not None:
            self.vov = vp / (1.0 + self.gm * K["R5"] / self.m)
        else:
            self.vov = vov
        self.lam = lam            # channel-length modulation, 1/V
        self.nsub = nsub          # subthreshold slope volts (0 = hard cutoff)
        self.zload = zload

    @property
    def vp(self):
        """Pinch-off magnitude implied by the self-bias solve."""
        return self.vov + self.id0 * K["R5"]

    @property
    def idss(self):
        return self.id0 * (self.vp / self.vov) ** self.m

    @property
    def id0(self):
        # gm = dI/dVgs = m*beta*Vov^(m-1) = m*Id0/Vov, so the self-bias family generalises with m.
        return self.gm * self.vov / self.m

    @property
    def beta(self):
        return self.id0 / self.vov ** self.m

    @property
    def vds_q(self):
        return K["VA"] - self.id0 * (K["R6"] + K["R5"])

    def current(self, vov_i, vds_i):
        """Total drain current. Vectorised.

        ⭐ The triode branch is `beta*(Vov_i^m - (Vov_i - Vds_i)^m)`, which is the ONLY generalisation
        of Shichman-Hodges that keeps every property the solve depends on: at m = 2 it expands to
        exactly `beta*(2*Vov_i*Vds_i - Vds_i^2)`, at the boundary Vds_i = Vov_i it meets the
        saturation value with dI/dVds = 0 (so the map stays C1), and both partials stay non-negative
        (so F is still strictly increasing and the root is still unique). It is not an interpolation
        chosen to look smooth."""
        b, m = self.beta, self.m
        vds = np.maximum(vds_i, 0.0)
        vpos = np.maximum(vov_i, 0.0)
        sat = vds >= vov_i
        i = np.where(sat, b * vpos ** m, b * (vpos ** m - np.maximum(vpos - vds, 0.0) ** m))
        if self.nsub > 0.0:
            # Smooth the cutoff corner: (Vov_i)+ -> n*log(1+exp(Vov_i/n)), the standard soft-plus
            # the EKV/ACM models use. Reduces to Vov_i well above cutoff and to an exponential tail
            # below it, so nothing in the saturation region moves.
            v = np.clip(vov_i / self.nsub, -60.0, 60.0)
            veff = self.nsub * np.logaddexp(0.0, v)
            sat2 = vds >= veff
            i = np.where(sat2, b * veff ** m,
                         b * (veff ** m - np.maximum(veff - vds, 0.0) ** m))
        else:
            i = np.where(vov_i <= 0.0, 0.0, i)
        if self.lam > 0.0:
            i = i * (1.0 + self.lam * vds)
        return i

    def solve(self, vgate, rd=None, iters=60):
        """i such that i = I(Vov + vgate - rd*i, Vds_q - i*(zload+rd)) - Id0, by bisection.

        ⚠ BISECTION, not the plugin's closed form. The closed form is exact only for the pure
        square law; the moment a mechanism is added it stops being a quadratic. Bisection is slow
        and completely mechanism-agnostic, which is the right trade for an oracle. The bracket is
        the same free one the C++ solve uses (cutoff below, drain bottomed above)."""
        rd = K["R5"] if rd is None else rd
        vgate = np.asarray(vgate, dtype=float)
        lo = np.full_like(vgate, -self.id0)
        hi = np.full_like(vgate, self.vds_q / (self.zload + rd))
        for _ in range(iters):
            mid = 0.5 * (lo + hi)
            vs = rd * mid
            f = mid - (self.current(self.vov + vgate - vs, self.vds_q - mid * self.zload - vs) - self.id0)
            pos = f > 0.0
            hi = np.where(pos, mid, hi)
            lo = np.where(pos, lo, mid)
        return 0.5 * (lo + hi)


_HOUT = {}
_HIN = {}


def _hout(f, x):
    """|output network| at f, 2f, 3f. Memoised: it is a 3x3 complex solve per frequency and the fit
    calls it thousands of times with the same few (f, x) pairs."""
    k = (f, x)
    if k not in _HOUT:
        _HOUT[k] = np.abs(LF.out_network([f, 2 * f, 3 * f], x,
                                         zd=K["R6"] * K["ro"] / (K["R6"] + K["ro"])))
    return _HOUT[k]


def cell_harmonics(dev, amp_jack, f, x, n=1024):
    """H1 and H2 of the OUTPUT, for a jack-referred sine of amplitude `amp_jack` volts.

    Exact: the map is memoryless, so one period sampled finely and Fourier-analysed IS the
    steady-state spectrum. The output network is applied per harmonic afterwards, because it is
    linear and sits after the nonlinearity."""
    t = np.arange(n) / n
    if f not in _HIN:
        _HIN[f] = in_network([f])[0]
    hin = _HIN[f]
    vg = amp_jack * abs(hin) * np.sin(2 * np.pi * t)
    i = dev.solve(vg, rd=K["R5"])
    spec = np.fft.rfft(i) / n
    hout = _hout(f, x)
    return tuple(abs(spec[k]) * 2 * hout[k - 1] for k in (1, 2, 3))


def h2_dbc(dev, amp_jack, f, x):
    h1, h2, _ = cell_harmonics(dev, amp_jack, f, x)
    return 20 * np.log10(max(h2, 1e-30) / max(h1, 1e-30))


def h3_dbc(dev, amp_jack, f, x):
    h1, _, h3 = cell_harmonics(dev, amp_jack, f, x)
    return 20 * np.log10(max(h3, 1e-30) / max(h1, 1e-30))


def h1_db(dev, amp_jack, f, x):
    return 20 * np.log10(max(cell_harmonics(dev, amp_jack, f, x)[0], 1e-30))


def render(parsed):
    os.makedirs(CACHE, exist_ok=True)
    args = ["--os", "8"] + C.render_args(parsed)
    key = hashlib.sha1(("|".join(args) + "|" + C.render_bin_key()).encode()).hexdigest()[:10]
    out = f"{CACHE}/{key}.wav"
    if not os.path.exists(out):
        # Temp-and-rename, so a concurrent run cannot read a half-written wav as a short render
        # (p4_corners.render carries the full reasoning).
        tmp_out = f"{out}.{os.getpid()}.tmp"
        subprocess.run([C.RENDER_BIN, SIG, tmp_out] + args, check=True, capture_output=True)
        os.replace(tmp_out, out)
    return out


def measured(path, segs, fs, freq, levels, lag):
    """{level: (H2 dBc, H1 dB, H3 dBc)} for one capture's tone ladder at `freq`."""
    x = PA.load(path, fs)
    rows = {}
    for d in levels:
        c = PA.cell(x, segs[f"tone_{freq:g}_{d}"], fs, lag)
        h1 = PA.amp(c, freq, fs)
        rows[d] = (PA.db(PA.amp(c, 2 * freq, fs)) - PA.db(h1), PA.db(h1),
                   PA.db(PA.amp(c, 3 * freq, fs)) - PA.db(h1))
    return rows


def dark_cells(freq=220.0):
    """Every DARK P4 probe capture, as (name, x, pad, {level: (h2_dbc, h1_db)}).

    ⚠ DARK ONLY, and 220 Hz only, for the oracle's sake: DARK is where the source one-port is the
    bare resistor with no state, and 220 Hz is below the mode shelf zero AND flat in the input
    network. Both are what make the memoryless oracle exact rather than approximate."""
    m = json.load(open(PA.META))
    fs, segs, levels = m["fs"], m["segments"], m["tone_levels_db"]
    ref = PA.load(SIG, fs)
    out = []
    for path, d in sorted(C.find_captures(PROBE_DIR), key=lambda kv: kv[1]["volume_clock"]):
        if d["unit"] != "p4" or d["mode"] != "dark":
            continue
        lag = PA.align(PA.load(path, fs), ref)
        out.append((os.path.basename(path)[:-4], d, measured(path, segs, fs, freq, levels, lag)))
    return out, levels, segs, fs, ref


def cells(caps, freq, floor_slope=1.6):
    """Every usable (capture, level) cell as a flat list of dicts.

    ⚠ FLOOR-SCREENED ON THE ABSOLUTE H2's OWN GROWTH, not on a level threshold. A square-law
    device's absolute H2 rises 2 dB per dB of drive, so a cell whose H2 grew by much less than that
    is reading the chain's fixed-amplitude artefact rather than the pedal (probe_analyse.py's rule,
    and note #22's: at 7:30 the pedal records ~16 dB down and its quiet cells read a flat -41 dBc
    with H3 above H2). Every -26 and -18 cell fails this; nothing at -12 and above does.
    """
    m = json.load(open(PA.META))
    levels = m["tone_levels_db"]
    out = []
    for name, d, meas in caps:
        prev = None
        for lv in levels:
            aj = KINPUT_REF * 10 ** ((lv - d["pad_db"]) / 20)
            h2abs = meas[lv][0] + meas[lv][1]
            slope = None if prev is None else (h2abs - prev[0]) / (lv - prev[1])
            if slope is not None and slope >= floor_slope:
                out.append(dict(name=name, x=d["volume"], lv=lv, amp_jack=aj,
                                a_gate=aj * abs(in_network([freq])[0]),
                                cap=meas[lv][0], cap_h1=meas[lv][1], cap_h3=meas[lv][2],
                                slope=slope, zload=drain_load(d["volume"])))
            prev = (h2abs, lv)
    return out


def residuals(rows, freq, **kw):
    """pedal minus model, in dB, per cell. Negative = the model makes MORE H2 than the pedal."""
    out = []
    for r in rows:
        dev = Device(zload=r["zload"], **kw)
        out.append(r["cap"] - h2_dbc(dev, r["amp_jack"], freq, r["x"]))
    return np.array(out)


def regime(dev, a_gate):
    """Fraction of a period spent in cutoff and in triode, for the peak drive `a_gate`."""
    t = np.arange(4096) / 4096
    vg = a_gate * np.sin(2 * np.pi * t)
    i = dev.solve(vg)
    vs = K["R5"] * i
    vov_i = dev.vov + vg - vs
    vds_i = dev.vds_q - i * dev.zload - vs
    return float(np.mean(vov_i <= 1e-12)), float(np.mean((vov_i > 1e-12) & (vds_i < vov_i)))


VALIDATION_CANDIDATES = [
    ("shipped SH, Vov 0.4469", dict(vov=0.4469, m=2.0)),
    ("SH, Vov 0.63 (best H2 shape)", dict(vov=0.63, m=2.0)),
    ("SH, Vov 0.73 (zero H2 offset)", dict(vov=0.73, m=2.0)),
    ("m 1.6, Vov 0.55  <- FITTED on H2@220 only", dict(vov=0.55, m=1.6)),
]


def _screen(series, need):
    """Indices whose absolute product grew at least `need` dB per dB from the cell below.

    An Nth-order product must rise N dB/dB, so a cell that grew much less is reading the chain's
    fixed-amplitude artefact rather than the pedal -- the screen probe_analyse.py uses and the one
    note #22 was built after. H3 needs a stricter screen than H2 because it sits 20-30 dB lower and
    reaches the floor much sooner: on this dataset the two captures that share a gate drive of
    0.849 V disagree on H3 by 3.8 dB and agree on H2 to 0.07."""
    keep, prev = [], None
    for k, (lv, absdb) in enumerate(series):
        if prev is not None and (absdb - prev[1]) / (lv - prev[0]) >= need:
            keep.append(k)
        prev = (lv, absdb)
    return keep


def validate(caps, freqs=(220.0, 3150.0)):
    """Score every candidate against ONE fitted observable and THREE that were not fitted.

    ⚠⚠ THE POINT IS THE THREE. A one-parameter generalisation will always improve the observable it
    was fitted to; the question this project keeps having to answer (note #22's `Vov` fit, note
    #21's C10) is whether the improvement is structure or just a spent degree of freedom. H3 is a
    different ORDER, 3150 Hz is a 14x different FREQUENCY, and compression is read on the
    FUNDAMENTAL. None of them entered the fit.

    ⭐ DARK at 3150 Hz is still exactly memoryless, which is why it is usable here: with no bypass
    cap engaged the source one-port is the bare resistor at every frequency. Only the input and
    output networks differ, and both are linear and applied per harmonic."""
    m = json.load(open(PA.META))
    fs, segs, levels = m["fs"], m["segments"], m["tone_levels_db"]
    ref = PA.load(SIG, fs)
    obs = {}
    for f in freqs:
        for name, d, _ in caps:
            meas = measured(f"{PROBE_DIR}/{name}.wav", segs, fs, f,
                            levels, PA.align(PA.load(f"{PROBE_DIR}/{name}.wav", fs), ref))
            h2keep = _screen([(lv, meas[lv][0] + meas[lv][1]) for lv in levels], 1.6)
            h3keep = _screen([(lv, meas[lv][2] + meas[lv][1]) for lv in levels], 2.2)
            base = None
            for k, lv in enumerate(levels):
                aj = KINPUT_REF * 10 ** ((lv - d["pad_db"]) / 20)
                if base is None:
                    base = (meas[lv][1] - lv, aj, lv)
                rec = dict(x=d["volume"], amp_jack=aj, zload=drain_load(d["volume"]),
                           a_gate=aj * abs(in_network([f])[0]), f=f, name=name, lv=lv,
                           comp=meas[lv][1] - lv - base[0], base_amp=base[1])
                if k in h2keep:
                    obs.setdefault((f, "h2"), []).append(dict(rec, cap=meas[lv][0]))
                if k in h3keep:
                    obs.setdefault((f, "h3"), []).append(dict(rec, cap=meas[lv][2]))
                if k in h2keep:                     # compression, on the same well-driven cells
                    obs.setdefault((f, "comp"), []).append(dict(rec, cap=rec["comp"]))
    return obs


def score(obs, kw, gm):
    out = {}
    for (f, what), rs in sorted(obs.items()):
        d = []
        for r in rs:
            dev = Device(zload=r["zload"], gm=gm, **kw)
            if what == "h2":
                mdl = h2_dbc(dev, r["amp_jack"], f, r["x"])
            elif what == "h3":
                mdl = h3_dbc(dev, r["amp_jack"], f, r["x"])
            else:
                mdl = (h1_db(dev, r["amp_jack"], f, r["x"])
                       - h1_db(dev, r["base_amp"], f, r["x"])
                       - 20 * np.log10(r["amp_jack"] / r["base_amp"]))
            d.append(r["cap"] - mdl)
        d = np.array(d)
        out[(f, what)] = (float(np.mean(d)), float(np.sqrt(np.mean((d - np.mean(d)) ** 2))),
                          float(np.max(np.abs(d))), len(d))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true",
                    help="check the oracle against a real plugin render before any capture is read")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--gm", type=float, default=None,
                    help="⚠ pin to the UNIT's own gm (P4 = 1146e-6) when fitting curvature against "
                         "it. In DARK, H2/H1 = A/(4*Vov*K0^2) and K0 = 1 + gm*R5, so a 1553-vs-1146 "
                         "gm difference is 4.4 dB of H2 on its own -- read as curvature it is the "
                         "whole residual. Convert the answer back through the invariant Vov*K0^2.")
    ap.add_argument("--vov", type=float, default=None)
    ap.add_argument("--lam", type=float, default=0.0)
    ap.add_argument("--nsub", type=float, default=0.0)
    ap.add_argument("-m", type=float, default=None,
                    help="transfer-law exponent (2.0 = Shichman-Hodges; shipped is 1.60)")
    ap.add_argument("--vp", type=float, default=None, help="pinch-off magnitude = the cutoff onset")
    ap.add_argument("--validate", action="store_true",
                    help="score the candidates against H2 (fitted) and three observables that "
                         "were not: H3, a 14x different frequency, and compression")
    ap.add_argument("--sweep", default=None,
                    help="MECH:lo:hi:n -- sweep one mechanism and report the residual's mean, sd, "
                         "and its SPLIT either side of cutoff onset (the shape statistic)")
    a = ap.parse_args()

    caps, levels, segs, fs, ref = dark_cells(a.freq)
    if not caps:
        sys.exit("no dark P4 probe captures")

    if a.self_test:
        print("SELF-TEST -- oracle vs the shipped plugin, DARK, same cells. Both are the model, so\n"
              "any disagreement is the oracle's, and it must be small before a capture is read.\n")
        worst = 0.0
        for name, d, _ in caps:
            plug = measured(render(d), segs, fs, a.freq, levels, 0)
            dev = Device(zload=drain_load(d["volume"]))
            print(f"== {name}  x={d['volume']:.3f} pad {d['pad_db']:g}  zLoad {dev.zload/1e3:.2f} k")
            print(f"   {'lvl':>4s} {'A_gate V':>9s} {'oracle':>8s} {'plugin':>8s} {'diff':>7s}")
            for lv in levels:
                aj = KINPUT_REF * 10 ** ((lv - d["pad_db"]) / 20)
                o = h2_dbc(dev, aj, a.freq, d["volume"])
                p = plug[lv][0]
                print(f"   {lv:4d} {aj * abs(in_network([a.freq])[0]):9.3f} {o:8.2f} {p:8.2f} {o - p:+7.2f}")
                if lv >= -18:
                    worst = max(worst, abs(o - p))
        print(f"\nworst |oracle - plugin| over cells at -18 dBFS and above: {worst:.2f} dB")
        return

    rows = cells(caps, a.freq)
    kw = dict(gm=a.gm, vov=a.vov, vp=a.vp, lam=a.lam, nsub=a.nsub, m=a.m)

    if a.validate:
        gm = 1146e-6 if a.gm is None else a.gm
        print(f"VALIDATION -- P4 DARK, gm pinned at {gm*1e6:.0f} uS (the unit's own, from its mode\n"
              f"differential). Each cell is `pedal minus model`; offset / shape-RMS / worst, in dB.\n"
              f"⭐ ONLY the first column was fitted.\n")
        obs = validate(caps)
        keys = [(220.0, "h2"), (220.0, "h3"), (3150.0, "h2"), (3150.0, "h3"),
                (220.0, "comp"), (3150.0, "comp")]
        hdr = {"h2": "H2", "h3": "H3", "comp": "comp"}
        print(f"   {'candidate':42s}" + "".join(
            f"{hdr[w] + ' @' + str(int(f)):>19s}" for f, w in keys))
        for label, kwc in VALIDATION_CANDIDATES:
            sc = score(obs, kwc, gm)
            line = f"   {label:42s}"
            for k in keys:
                mn, sh, wo, n = sc[k]
                line += f"  {mn:+6.2f}/{sh:5.2f}/{wo:4.1f}"
            print(line)
        print(f"\n   cells: " + "  ".join(
            f"{hdr[w]}@{int(f)} n={score(obs, VALIDATION_CANDIDATES[0][1], gm)[(f, w)][3]}"
            for f, w in keys))
        print("\n   ⚠ compression is read as an INCREMENT re each capture's quietest kept cell, so a\n"
              "     level-independent gain error in the chain cancels (circuit.md note #16).")
        return

    if a.sweep:
        mech, lo, hi, n = a.sweep.split(":")
        print(f"SWEEP {mech} over [{lo}, {hi}], {len(rows)} cells, DARK {a.freq:g} Hz, "
              f"gm={'shipped' if a.gm is None else f'{a.gm*1e6:.0f} uS'}\n")
        print(f"   {mech:>10s} {'mean':>7s} {'sd':>6s} | {'below cutoff':>13s} {'above':>7s} "
              f"{'SHAPE':>7s}")
        print(f"   {'':>10s} {'':>7s} {'':>6s} | {'mean':>13s} {'mean':>7s} {'above-below':>7s}")
        best = None
        for v in np.linspace(float(lo), float(hi), int(n)):
            k = dict(kw); k[mech] = v
            r = residuals(rows, a.freq, **k)
            dev0 = Device(zload=rows[0]["zload"], **k)
            onset = cutoff_onset(dev0)
            below = np.array([x for x, rr in zip(r, rows) if rr["a_gate"] < onset])
            above = np.array([x for x, rr in zip(r, rows) if rr["a_gate"] >= onset])
            bm = float(np.mean(below)) if len(below) else float("nan")
            am = float(np.mean(above)) if len(above) else float("nan")
            print(f"   {v:10.4f} {np.mean(r):+7.2f} {np.std(r):6.2f} | {bm:+13.2f} {am:+7.2f} "
                  f"{am - bm:+7.2f}")
            if best is None or np.std(r) < best[1]:
                best = (v, float(np.std(r)))
        print(f"\n   lowest scatter at {mech} = {best[0]:.4f}  (sd {best[1]:.2f} dB)")
        return

    dev_hdr = Device(**kw)
    print(f"P4 DARK, {a.freq:g} Hz.  gm={dev_hdr.gm*1e6:.0f} uS  Vov={dev_hdr.vov:.4f}  "
          f"m={dev_hdr.m:g}  lam={dev_hdr.lam:g}  nsub={dev_hdr.nsub:g}")
    print(f"cutoff onset {cutoff_onset(dev_hdr):.3f} V gate.  "
          f"delta = pedal minus model (negative = the model makes MORE H2)\n")
    r = residuals(rows, a.freq, **kw)
    print(f"   {'capture':28s} {'lvl':>4s} {'A_gate':>7s} {'zLoad k':>8s} {'cut%':>5s} {'tri%':>5s} "
          f"{'cap':>7s} {'model':>7s} {'delta':>7s}")
    for rr, dl in zip(rows, r):
        dev = Device(zload=rr["zload"], **kw)
        cu, tr = regime(dev, rr["a_gate"])
        print(f"   {rr['name']:28s} {rr['lv']:4d} {rr['a_gate']:7.3f} {rr['zload']/1e3:8.2f} "
              f"{100*cu:5.1f} {100*tr:5.1f} {rr['cap']:7.2f} {rr['cap'] - dl:7.2f} {dl:+7.2f}")
    print(f"\n   {len(r)} cells   mean {np.mean(r):+.2f} dB   sd {np.std(r):.2f}   "
          f"worst {np.max(np.abs(r)):.2f}")
    json.dump(dict(freq=a.freq, gm=dev_hdr.gm, vov=dev_hdr.vov, lam=dev_hdr.lam, nsub=dev_hdr.nsub,
                   m=dev_hdr.m,
                   cells=[dict(rr, delta=float(d)) for rr, d in zip(rows, r)],
                   mean=float(np.mean(r)), sd=float(np.std(r))), open(OUT, "w"), indent=2, default=float)
    print(f"\nwrote {OUT}")


def triode_onset(dev):
    """Gate volts at which the positive peak first enters triode."""
    w = np.linspace(0, 6, 20001)
    i = dev.solve(w)
    vs = K["R5"] * i
    vov_i = dev.vov + w - vs
    vds_i = dev.vds_q - i * dev.zload - vs
    k = np.argmax(vds_i < vov_i)
    return float(w[k]) if k else float("nan")


def cutoff_onset(dev):
    """Gate volts at which the negative peak first reaches cutoff."""
    w = np.linspace(0, 8, 20001)
    i = dev.solve(-w)
    vs = K["R5"] * i
    k = np.argmax(dev.vov - w - vs <= 0.0)
    return float(w[k]) if k else float("nan")


if __name__ == "__main__":
    main()
