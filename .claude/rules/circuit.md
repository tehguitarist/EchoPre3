# Circuit Reference — Echo Pre 3 (Echoplex EP-3 preamp)

> This file is the **source of truth** for component values and topology. Fill every `<...>`
> placeholder by reading the schematic directly. Do NOT approximate or copy values from a build
> kit / forum trace without confirming against the primary schematic.

Target circuit: the Echoplex EP-3 tube preamp, reimagined as a JFET preamp — traced via the
**Chase Tone Secret Preamp** schematic. Single gain stage, no clipping diodes; a high-voltage
(≈22 V) rail feeds a self-biased 2N5457 common-source stage whose *source-bypass network* is the
pedal's entire voicing control.

## Schematics

| File | Role |
|------|------|
| `schematic.png` | **Primary and only source of truth** for values + topology (Chase Tone Secret Preamp) |

### Crop index

| Crop file | Source image | Contains |
|-----------|--------------|----------|
| `crop_power.png` | `schematic.png` | +9 V in → D1 → IC1 LT1054 → D2–D5 / C6–C8 multiplier ladder → D6 zener → VA |
| `crop_input_jfet.png` | `schematic.png` | IN → R3/C3 → C4 → R4 gate bias → Q1 gate; R6 drain load; top of R5 |
| `crop_mode_switch.png` | `schematic.png` | Q1 source → R5, C1/R1 and C2/R2 branches, MODE switch lugs |
| `crop_volume_out.png` | `schematic.png` | Drain → C10 → R10 → volume network (R9/R8/pot) → OUT |
| `crop_volume_pot_zoom.png` | `schematic.png` | Tight zoom confirming the VOLUME pot lug 1/2/3 connections |

---

## ⚠ Schematic-reading gotchas (these caused real bugs)

- **Resistor value notation:** `2m2` / `2M2` means **2.2 MΩ**, `2k2` = 2.2 kΩ, `2R2` = 2.2 Ω. The
  letter is the decimal point AND the multiplier. Misreading `2m2` as 2.2 Ω (or 2.2 mΩ) is an easy
  ~6-orders-of-magnitude error. Double-check every R against its neighbours' magnitudes.
  *(This board uses `3k6` = 3.6 kΩ for R5 — the only such notation here.)*
- **Series vs pulldown:** a large resistor (e.g. 2.2 MΩ) at the input is usually a **pulldown to
  ground** (bias/pop suppression), not a series element. A 2.2 MΩ *series* resistor would attenuate
  ~14 dB and roll off treble hard — if your model does that, you've mis-traced the topology.
  ⚠ **On THIS board the exception applies: R3 (110 k) genuinely IS in series with the input** —
  verified inline in `crop_input_jfet.png`. R4 (1 M) is the pulldown. Don't "correct" R3.
- **Pot wiring:** confirm pin1/pin3/wiper → node mapping for every pot. "Rheostat" (wiper jumpered
  to an end, no ground leg) behaves very differently from a "divider". Trace it; don't assume.
- **Taper:** note the designation (A = audio/log, B = linear). Kits sometimes substitute — follow
  the schematic, not the kit.
- **Op-amp inverting vs non-inverting:** verify which input the signal enters. Confirm output
  polarity later with a DC-step test per stage. *(N/A here — no op-amps in the signal path.)*
- **Power section parts in the signal columns:** supply-filter R/C (and any series Schottky) can be
  mislabelled as signal-path parts. Exclude VREF-divider and supply-filter components from the DSP
  model. *(Here: D1–D6, C5–C9, IC1 are ALL supply-only — see "Power supply" below.)*
- **Same component VALUES ≠ same TOPOLOGY across schematic sources.** Two traces of "the same"
  pedal can share every R/C value designator-for-designator while wiring one network completely
  differently. Identical values are NOT evidence the topology matches — redraw the actual node
  connections from each source before concluding they agree.
- **If the pedal has more than one full gain stage in series, verify the ACTUAL signal order from
  the real unit** — never assume it from the physical/UI layout. *(N/A here — one gain stage.)*
- **Once you have a component's value, re-reading it again rarely finds a new bug — re-tracing its
  connections often does.** Spend follow-up passes on the **node graph** below, not on re-scanning
  values already captured here.
- **A bridging (feedback-spanning) or shunt component changes the topology, not just a value.**
  ⚠ **On THIS board that is R9**, which bridges the drain-coupled node E straight to OUT while the
  VOLUME pot shunts *both* ends — see "Volume network" below. It is not a series element in a
  simple divider, and modelling it as one gets the control law qualitatively wrong.

---

## Signal path summary

```
IN → R3 110k (series) → [A] → C4 22n → [G] → Q1 2N5457 common source → [D] → C10 100n → [E]
                         │                    │  (R6 22k drain load to VA)                 │
                    C3 220pF↓GND         R4 1M↓GND     source [S] → R5 3k6 ↓GND       R10 240k↓GND
                                                        + MODE bypass network              │
                                                                                    VOLUME network
                                                                                     (R9 / R8 / pot)
                                                                                            → OUT
```

Single-supply, **no VREF divider**: the gate is referenced to 0 V through R4, the JFET self-biases
via R5, so **signal ground IS ground** and the model is naturally bipolar around 0 V at the input.
The only DC offset in the chain sits at the drain (≈ mid-rail), and C10 removes it.

Supply: **VA ≈ 22 V** (charge-pumped from 9 V, zener-clamped — see below). This is a high-headroom
clean preamp; the JFET's square-law curvature, not a clipper, is the entire nonlinearity.

## Component values (from `schematic.png` — do not approximate)

### Input network
| Ref | Value | Function |
|-----|-------|----------|
| `R3` | 110 kΩ | **Series** input resistor (forms input LPF with C3; sets input Z with R4) |
| `C3` | 220 pF | Shunt to GND at node A — RF/brightness filter |
| `C4` | 22 nF | Input DC-blocking coupling cap into the gate |
| `R4` | 1 MΩ | Gate bias pulldown to GND (sets gate at 0 V DC) |

### Gain stage
| Ref | Value | Function |
|-----|-------|----------|
| `Q1` | 2N5457 (N-JFET) | Common-source gain stage, self-biased |
| `R6` | 22 kΩ | Drain load resistor to VA |
| `R5` | 3.6 kΩ | Source degeneration resistor to GND (sets bias + unbypassed gain) |

### MODE source-bypass network
| Ref | Value | Function |
|-----|-------|----------|
| `C1` | 22 nF | Source-bypass cap, **"BRIGHT"** branch (→ MODE lug 3) — label MEASURED, note #2 |
| `R1` | 1 MΩ | Pulldown holding C1's bottom node at GND when not switched (anti-pop) |
| `C2` | 10 nF | Source-bypass cap, **"MID"** branch (→ MODE lug 1) — label MEASURED, note #2 |
| `R2` | 1 MΩ | Pulldown holding C2's bottom node at GND when not switched (anti-pop) |
| `MODE` | SPDT **ON-OFF-ON**, lug 2 = common → GND | Grounds one branch's bottom node, engaging that bypass cap; centre = neither = "DARK" |

### Output / volume network
| Ref | Value | Function |
|-----|-------|----------|
| `C10` | 100 nF | Drain output coupling cap (removes drain DC) |
| `R10` | 240 kΩ | Pulldown to GND at node E |
| `R9` | 110 kΩ | **Bridging** resistor, node E → OUT (series arm of the volume network) |
| `R8` | 110 kΩ | OUT → VOLUME lug 3 (upper shunt arm) |
| `VOLUME` | 500 kΩ **A** (audio) | Wiper (lug 2) to GND; lug 1 → node E, lug 3 → R8 |

### Power supply (NOT in the signal path — do not model as audio)
| Ref | Value | Function |
|-----|-------|----------|
| `D1` | 1N4002 | Reverse-polarity protection, shunt across +9 V (cathode to +9 V) |
| `C9` | 220 µF | Input supply reservoir |
| `IC1` | LT1054IP | Used as a **square-wave oscillator/driver**, not as its usual inverter (see below) |
| `D2`–`D5` | 1N5818 ×4 | Schottky ladder of a Dickson voltage multiplier |
| `C8`, `C6` | 10 µF | Pumped ("flying") caps — bottoms driven by IC1 pin 2 |
| `C7` | 10 µF | Inter-stage reservoir to GND |
| `D6` | 1N4748A | **22 V** zener, shunt clamp defining VA |
| `C5` | 220 µF | VA reservoir |

### Nonlinear devices
- **`Q1` = 2N5457 N-channel JFET — the ONLY nonlinear element in the signal path.**
  There are **no clipping diodes, no op-amps, and no CMOS** in the audio chain.
- ⚠ **Not WDF-native.** `chowdsp_wdf` has no JFET element — see the triage table below and
  `docs/nonlinear-component-modeling.md` §2.
- ⚠ **Datasheet spread is huge — confirmed from `docs/refs/onsemi_2N5457-2N5458_datasheet.pdf`:**
  IDSS 1.0–5.0 mA (min/max), Vgs(off) −0.5 to −6.0 V, Yfs (≈ gm at Vgs=0) 1000–5000 µmhos — a 5×
  spread on every amplitude parameter. The datasheet's own Typical Characteristics graphs show
  three sample units at Vgs(off) ≈ −1.2 / −3.5 / −5.8 V, i.e. spanning nearly the whole rated range
  — visual confirmation that "typical" is not a safe default. Every amplitude parameter (gm, bias
  point, shaper curvature) **must be fitted to a capture** of the actual unit; only the R/C corners
  and the polarity are trustworthy in advance. This matters more than usual here, since the maker
  states Q1 is a "cherry picked" vintage part (see "Validation notes" #4), not a random production
  sample — don't assume it lands near the datasheet's typ column.
- ✅ **`gm` is now MEASURED (note #7): 1.5–1.7 mS, i.e. gm·R5 ≈ 5.6–6.2 and K0 = 1 + gm·R5 ≈ 6.6–7.2.**
  Two units agree to 0.34 dB. That sits comfortably inside the datasheet's 1000–5000 µS band, near
  its low end but **above** the typical self-bias solve's 813 µS — so "cherry picked" did not mean
  "unusually weak". Only `gm` is pinned; the bias point and the shaper's curvature are still open.
- ✅ Datasheet fetched to `docs/refs/onsemi_2N5457-2N5458_datasheet.pdf` (onsemi, Rev. 6, Feb 2010).

### Parts triage vs `docs/nonlinear-component-modeling.md` §0

| Component class | WDF-native? | What we need |
|---|---|---|
| R3, R4, R5, R6, R8, R9, R10, C1–C4, C10, VOLUME pot | ✅ | `ResistorT` / `CapacitorT`, nothing external |
| MODE switch (3 discrete source-bypass topologies) | ✅ | precomputed topology per position |
| **Q1 2N5457 common-source gain stage** | ❌ | fitted transconductance + shaper — §2 "Path B" |
| Drain clipping against the VA rail / cutoff | ❌ (asymmetric clamp) | rail limits from VA ≈ 22 V and the bias point |
| D1–D6, C5–C9, IC1 (supply only) | n/a | not in the signal path — **do not model** |

⭐ **The triage is the deliverable: exactly one part (Q1) needs an external model.** Everything else
is `CapacitorT`/`ResistorT`. Don't invent a model for anything above the Q1 row.

### Designator census (re-verified against `schematic.png`, 2026-09-07)

Every designator drawn on the schematic is accounted for in the tables above — **29 parts, none
missing, none invented**:

| Block | Designators | Count |
|---|---|---|
| Input network | R3, C3, C4, R4 | 4 |
| Gain stage | Q1, R6, R5 | 3 |
| MODE bypass | C1, R1, C2, R2, MODE | 5 |
| Output / VOLUME | C10, R10, R9, R8, VOLUME | 5 |
| Power (excluded from DSP) | D1, C9, IC1, D2–D5, C8, C7, C6, D6, C5 | 12 |

`C1`–`C10` are gapless. `D1`–`D6` are gapless. **`R7` does not appear anywhere on the drawing** —
the resistor series runs R1…R6, R8, R9, R10. The power section contains no resistors at all, so it
is not hiding there.

⚠ **Treat the R7 gap as an open question, not as noise.** A single gap in an otherwise gapless
series usually means either (a) a part deleted in a schematic revision without renumbering, or
(b) a part present on the real board but omitted from this drawing. Only (b) would affect the
model. Its neighbours R6 (drain load) and R8 (volume upper shunt) bracket the drain-to-output
region, so if a part is missing it most plausibly sits there — but that is a guess from numbering
alone, with no circuit evidence behind it. **Do not add a speculative R7 to the model.** Model what
is drawn, and resolve it if a photo of the real board or another trace of the same circuit ever
becomes available.

## Topology — node graphs

Signal-path stages are marked **Linear** / **Nonlinear** with the adaptor shape they need.

### Stage 1 — Input network (**Linear**, plain series/parallel tree)

```
Node IN:   guitar/DAW source, R3 leg 1 — no other connection
Node A:    R3 leg 2, C3 leg 1, C4 leg 1 — no other connection
Node GND:  C3 leg 2
Node G:    C4 leg 2, R4 leg 1, Q1 GATE — no other connection
Node GND:  R4 leg 2
```

Corners (unloaded / as-drawn):
- **HP** `C4` into `R4`: 1/(2π·1 M·22 n) ≈ **7.2 Hz**.
- **LP** `R3` with `C3`, loaded by R4 through C4 in the audio band:
  1/(2π·(110 k ∥ 1 M)·220 p) ≈ **7.3 kHz** (≈ 6.6 kHz if you ignore the 1 M load).
- Passband divider into the gate: R4/(R3+R4) ≈ 0.90 (**−0.9 dB**).
- Input impedance seen by the source ≈ **1.11 MΩ** in the passband.

✅ **Plugin-vs-pedal note — DECIDED 2026-09-08: model an IDEAL source, and do NOT add a guitar
source impedance.** In hardware this LP corner sits *above* a real guitar's source impedance and
pickup resonance, so the pedal is darker in situ than the maths suggests, and the modelled corner
lands at the full ~7.3 kHz. That is the right answer in both contexts that matter, for two separate
reasons:

- **Against the reference.** NAM's standard training protocol plays a fixed digital input file out
  of an interface through a reamp box into the gear, and a reamp box drives well under 1 kΩ — so the
  reference captures contain no guitar loading either. (The seven `.nam` files record no chain
  metadata, so that is the protocol rather than something verified per file. The M3 fits corroborate
  it: P1 and P3 land at 6.7 and 7.2 kHz against 7.3 kHz drawn, which is where an ideal-source drive
  puts the corner; a guitar's source impedance would have dragged them well below.)
- **In use.** A DI track already contains the guitar's own cable capacitance, pickup resonance and
  the loading of whatever it was recorded into. Adding a source impedance in the plugin would
  **double-count** it.

**A/B against captures made through the same interface**, not against theory.

### Stage 2 — JFET gain stage (**Nonlinear** — external model, NOT a WDF element)

```
Node G:  Q1 gate      (see stage 1)
Node D:  Q1 drain, R6 leg 1, C10 leg 1 — no other connection
Node VA: R6 leg 2     (22 V supply rail, AC ground)
Node S:  Q1 source, R5 leg 1, C1 leg 1, C2 leg 1 — no other connection
Node GND: R5 leg 2
```

DC bias — ⛔ **THE PARAGRAPH THIS REPLACED DESCRIBED A SQUARE LAW (m = 2) AND IS SUPERSEDED BY
NOTE #26.** The transfer law is measured at **m = 1.60**, not 2, and the amplitude parameter is
**|Vp| (cutoff onset in gate volts), not `Vov`** — `Vov` is now a DERIVED quantity,
`Vov = |Vp| / (1 + gm·R5/m)`. The shipped pair is **`m` = 1.60, `|Vp|` = 1.942 V**, fitted from P4's
probe captures (note #26), at the voicing decision's shipped `gm` = 1.5531 mS this gives
**`Vov` = 0.4328 V, `Id0` ≈ 420 µA, `IDSS` ≈ 4.65 mA, `Vd` ≈ 11.25 V** (note #26c) — i.e. the drain
sits close to mid-rail after all, not the square-law fit's 14.4 V. `ro` = 1.1921 MΩ (re-derived at
this `Id0`, not the square law's 1.4407 MΩ). These four numbers (`m`, `|Vp|`, `gm`, `ro`) are the
complete device model; do not reconstruct a square-law bias point from first principles here —
read `JfetStage.h` and note #26/#26a/#26c for the derivation and the solve.

⚠ The drain voltage above moves with whichever `gm` ships (see the voicing decision, note #26c):
P4's own measured `gm` ≈ 1146 µS gives `Vds_q` ≈ 8.36 V with the SAME `m`/`|Vp|`; nothing else in
the device model changes either way.

⭐⭐⭐ **THE STRUCTURAL TRAP — READ `docs/nonlinear-component-modeling.md` §2 BEFORE WRITING THIS
STAGE. It is worth ~20 dB, and on THIS pedal it is not a corner case — it is the whole circuit.**
A degenerated common-source stage is a **current source, not a voltage source**:

```
k(s)    = 1 + gm·Zs(s)     where Zs = R5 ∥ (MODE bypass branch)
Gm(s)   = gm / k(s)        transconductance RISES with frequency
Rout(s) = ro · k(s)        drain output resistance FALLS with frequency
⇒ open-circuit gain Gm·Rout = gm·ro — FLAT, independent of the degeneration
```

The MODE switch's "HF lift" is therefore **not an unconditional gain shelf**. It appears only to
the extent the stage is *loaded* — and here the load (R6 ∥ the C10 → R10 → volume network) is what
converts the drain current into the voltage you hear. Applying a shelf to a voltage output **and**
driving the volume network from an ideal source double-counts the lift. Output the drain **Norton
current** and stamp `Zout(s) = ro·k(s) ∥ R6` into the output network's nodal solve.

Because MODE's entire audible job *is* this bypass lift, getting this wrong will look like "the
mode switch does far too much" — the failure mode is loud and obvious, so validate it early with a
mode-to-mode difference measurement, not just a single-mode FR.

⛔ **THE THREE PARAGRAPHS THIS REPLACED DESCRIBE THREE GENERATIONS OF SUPERSEDED IMPLEMENTATION —
kept only for the STRUCTURAL lessons, which are still correct; none of the formulas or the "square
law" framing below them describe the shipped stage any more.** In order: (1) driving the shaper
with a plain `vg` double-counted nothing but also produced nothing real; (2) note #8 found that the
degeneration suppresses distortion TWICE, fixed with a second-order expansion
`id2 = c·u1²/k(s) ⇒ H2/H1 = A/(4·Vov·k²)` — still a square law; (3) note #12 ("Path A") replaced the
expansion with a per-sample implicit solve `id = gm·g(vg − Zs(z)·id)`, which produces compression
and H3 for the first time but still assumed `g` was a square law. **Note #26 replaces (3)'s `g`
itself**: the measured transfer law is `m = 1.60`, not 2, so the stage solves
`id = gm·sign(u)·beta·|u|^m` in saturation (Halley, not closed-form) with a matching
`beta·(Vov^m − (Vov−Vds)^m)` triode branch — see `JfetStage.h`. The two things that DO carry forward
unchanged: the stage is still a Norton current source (`Zout(s) = ro·k(s) ∥ R6`, unchanged by the
exponent), and the degeneration still suppresses the nonlinearity it creates (that suppression is
now inside the per-sample solve rather than a separate filter pass, so there is no longer a second
factor to forget).

⚠ **"Square-law, even-dominant" is now only approximately right.** `m = 1.60 < 2` means the stage is
LESS purely quadratic than a true square law — H2 still dominates H3 (note #26's table), but don't
assume the even/odd ratio a square law would predict; read it off `JfetStage.h`'s fitted pair
instead. `beta` (the old cubic-sign question from `nonlinear-component-modeling.md` §2 finding (b))
no longer applies — the triode branch's cubic-like behaviour is forced by the solve's own
continuity requirement, not a free sign to choose (note #26, note #11).

Rails: the drain can only swing between roughly the JFET's saturation floor and **VA = 22 V** — an
**asymmetric** clamp. At normal guitar levels it should never reach either; it matters only for
extreme input-trim settings. Model it as a separate output clamp (per §3's pattern), not as part of
the shaper.

### Stage 2b — MODE source-bypass network (**Linear**, 3 discrete topologies)

```
Node S:   Q1 source, R5 leg 1, C1 leg 1, C2 leg 1
Node N1:  C1 leg 2, R1 leg 1, MODE lug 3 — no other connection
Node N2:  C2 leg 2, R2 leg 1, MODE lug 1 — no other connection
Node GND: R5 leg 2, R1 leg 2, R2 leg 2, MODE lug 2 (common)
```

The switch grounds **one branch's bottom node**, shorting out that branch's 1 M pulldown so its cap
actually bypasses R5. The unselected branch keeps its 1 M in series, which against R5 = 3.6 k is
~280× larger — i.e. **negligible bypass**, correctly modelled as "off" (but include the 1 M if you
want the sub-Hz behaviour exactly right).

Effective source impedance per position, `Zs = R5 ∥ (Rsw + 1/jωC)`, corner where `|Zc| = R5`:

| Lever | Label | Branch grounded | Bypass corner (drawn) | Bypass corner (MEASURED) | Character |
|---|---|---|---|---|---|
| **UP** | **BRIGHT** | **C1 = 22 nF (lug 3)** | 1/(2π·3.6 k·22 n) ≈ 2.01 kHz | **1.86 kHz** | lift reaches down into the upper mids → louder and brighter at every frequency |
| **MIDDLE** | **DARK** | none (centre-off) | — | — | no bypass: R5 fully degenerating → lowest gain, flat |
| **DOWN** | **MID** | **C2 = 10 nF (lug 1)** | 1/(2π·3.6 k·10 n) ≈ 4.42 kHz | **4.17 kHz** | lift confined to the top octaves |

⚠ **The lever↔cap mapping in this table is the REVERSE of what this file assumed until 2026-09-07.**
It is now measured, not inferred — see note #2. Do not "correct" it back on the reasoning that a
22 nF cap ought to be the one called "mid".

**Confirmed 3-position ON-OFF-ON.** The maker calls it the "Exclusive 3-Way EQ 1970s Era EP3
Mini-Toggle", naming the positions by era: **Early 1970s = BRIGHT · Late 1970s = DARK · Hybrid
Early & Late = MID** — matching the owner's up/middle/down labelling by treble content. The centre
"DARK" position is decisive evidence for the centre-off:
*no-bypass is the only state this circuit can produce that has less treble than both cap positions*,
and it only exists on a switch with a genuine centre-off. R1/R2 are therefore doing real work —
holding both cap bottom nodes at ground so the centre position neither floats nor pops.

✅ **Both cap positions reach the SAME HF plateau — now MEASURED, not assumed.** Fitting each
mode-versus-DARK differential as a first-order shelf gives a plateau of 16.39 / 16.02 dB (P1) and
16.75 / 16.35 dB (P2) for the 22 nF / 10 nF branches — the two branches agree to 0.37 dB despite
their poles sitting an octave apart (12.6 kHz vs 26.8 kHz). BRIGHT vs MID is *where the lift starts*,
not how far it goes. Don't model one as a higher-gain version of the other — that's a different (and
wrong) shape.

⚠ **Neither shelf has settled anywhere inside the audio band.** The pole sits at K0 × the zero, and
K0 ≈ 6.6, so the plateau is reached at ~12.6 kHz (BRIGHT) and ~26.8 kHz (MID — above Nyquist at
48 kHz). Any measurement that reads a "plateau" from an 8–10 kHz band average is reading a point on
the transition, and will report both corners in the wrong place. Fit the shelf; don't threshold it.

### Stage 3 — Output / VOLUME network (**Linear**, but a genuine bridged network — NOT a divider)

```
Node D:   Q1 drain, R6 leg 1, C10 leg 1
Node E:   C10 leg 2, R10 leg 1, R9 leg 1, VOLUME lug 1 — no other connection
Node OUT: R9 leg 2, R8 leg 1, output jack — no other connection
Node GND: R10 leg 2, VOLUME lug 2 (WIPER)
Node P3:  R8 leg 2, VOLUME lug 3 — no other connection
```

**Verified twice at high zoom (`crop_volume_pot_zoom.png`): the wiper (lug 2) goes to GROUND, and
the two END lugs go to node E and to R8 respectively.** This is *not* the usual "wiper → output"
divider. The pot splits into two shunt arms that move in **opposite** directions:

- `Ra` = lug 1 → wiper: shunts **node E** to ground (→ 0 Ω at one stop, shorting the signal)
- `Rb` = wiper → lug 3: in series with R8, shunts **OUT** to ground
- `Ra + Rb = 500 kΩ` always; `R9` bridges E → OUT as the series arm

⚠ **The output source impedance is ~92–139 kΩ, and it barely moves with VOLUME.** Looking back into
the jack you see `(R8 + Rb)` in parallel with `(R9 + Z_E)`, and `R9` alone is 110 kΩ, so the pot
cannot bring it down the way a conventional wiper-to-output divider would. Computed at the three
captured knob positions, across drain drive impedances from the physical ~20 kΩ to an ideal current
source, the whole range is 92–139 kΩ — a spread of 1.5× against a rotation that changes `Ra` by
6×. Two consequences:

- ⚠ **`docs/calibration-and-gain-staging.md` §4 ("output load: almost never worth modelling") does
  NOT apply to this pedal.** Its arithmetic assumes a ~6 kΩ source impedance; this is 15–23× that,
  which drags its "treble corner ~50 kHz" bullet down to **~3 kHz with 500 pF of ordinary cable**.
  That section now carries an explicit exception pointing here.
- ✅ **DECIDED 2026-09-08: ship NO load capacitance — same answer as the input side, same reason.**
  The plugin drives an ideal load, so it will always be brighter in the top octave than the real
  pedal into a real cable, and note #7 measures one reference capture carrying a 3.2 kHz output-load
  pole from ~542 pF. Do not model it anyway: **the plugin's output goes to a DAW, digitally — the
  user's signal path after the plugin has no cable in it.** Baking a load in would import one
  trainer's cable as a permanent voicing. The evidence that it *is* one trainer's cable rather than a
  property of the capture protocol is that P1 and P3 need only 39–52 pF, i.e. nothing.
  ⚠ **What this DOES bind is step-9 validation:** A/B the top octave against P1 or P3 only. A null
  against P2 above ~2 kHz is measuring that cable, and no plugin constant should be moved to close
  it. If the "pedal into a long cable into an amp" darkening is ever wanted as a voicing, it belongs
  behind a user-facing control, not inside a fitted constant.

⭐ **This network is deliberately NON-MONOTONIC, and that is CORRECT — it reproduces the original
EP-3's volume wiring.** Output rises from silence to a peak around 1–2 o'clock, then *falls back*
a dB or two by full rotation. Confirmed against the pedal maker's own published description — see
"Validation notes" #1, which also gives four calibration points for the taper fit. Do not
"fix" it into a conventional divider.

`C10` HP corner: into R10 ∥ (the volume network's input resistance), so it **moves with the volume
setting** — and it moves the opposite way round from what an earlier draft said. Recomputed with a
20 kΩ drain impedance in series:

| `Ra` | 10 k | 50 k | 100 k | 200 k | 300 k | 400 k | 500 k |
|---|---|---|---|---|---|---|---|
| C10 HP corner | 54 Hz | 27 Hz | 19 Hz | 14.5 Hz | 13.1 Hz | 13.0 Hz | 14.0 Hz |

So the corner is **lowest (~13 Hz) near the top of the range** and climbs to ~54 Hz as the volume
is wound down — the earlier "≈21 Hz near the top" was both the wrong value and the wrong end of
the sweep (21 Hz corresponds to `Ra` ≈ 130 k, i.e. mid-rotation). It is not a fixed corner; solve
it inside the coupled network rather than pre-computing one number.

## Op-amp model

**N/A — there are no op-amps in this circuit.** The gain device is Q1 (JFET). The "rail" behaviour
that the op-amp section of the template docs would normally cover is instead the JFET drain's swing
limit against VA (see stage 2).

## Power supply / rail (informational — excluded from the DSP model)

+9 V → `D1` reverse-polarity shunt → `C9` reservoir → `IC1` LT1054.

`IC1` is wired unusually: **pins 1 and 8 are jumpered together to +9 V, pins 3 and 5 go to GND, and
pin 2 is the only output used** (driving the bottom plates of C8 and C6). Pins 4, 6 and 7 are
unconnected. Tying the output-stage pin to ground makes the flying-cap pin swing as a **0 ↔ 9 V
square wave**, i.e. the LT1054 is being used as a **plain oscillator/driver**, not as its usual
voltage inverter.

That square wave drives a **Dickson multiplier**: `D2`–`D5` (1N5818 Schottky) with `C8`/`C6` pumped
and `C7` as an intermediate reservoir → roughly `3 × 9 V − 4·Vf(Schottky) ≈ 25–26 V` unloaded,
which the **`D6` 1N4748A 22 V zener** then clamps.

➡ **The only number the DSP model needs from this whole section is `VA = 22 V`** (the zener rating).
It is a hard clamp, not a soft one: the multiplier's unloaded output comfortably exceeds 22 V and
the audio stage draws well under a mA, so the rail sits at the zener voltage. Model VA as a
constant. **Do not model D1–D6, C5–C9 or IC1 as audio components.**

✅ **Independently confirmed by the pedal maker**, who describes the supply as ramping 9 VDC to
**26 VDC** and then regulating/filtering to **22 VDC** ("Vintage EP3 Power"). That matches the
computed unloaded ladder output (`3 × 9 V − 4·Vf_Schottky ≈ 25–26 V`) and the D6 zener rating
exactly, so both the multiplication factor and the rail are settled. The maker also specifies a
**150–200 mA @ 9 V** supply requirement and warns never to feed it more than 9 V — irrelevant to
the model, but it confirms the charge pump is the current-hungry part, not the audio stage.

⚠ The LT1054 pin-function mapping above is an *inference* from the drawn connections (the pin
numbers and wires are read directly from the schematic and are reliable; the interpretation of what
each pin does is not independently verified against a datasheet). It does not matter for the audio
model — VA = 22 V is fixed by the zener regardless — so this is recorded for completeness only.
Don't spend time refining it unless the supply itself is ever in question.

## Interactive / coupled controls

- **VOLUME is a coupled network, not a divider.** The pot loads node E *and* node OUT
  simultaneously, and it also shifts the C10 high-pass corner. Model E, OUT and the pot as **one
  network solved together** — computing a "volume gain" and applying it as a scalar after a
  fixed-corner high-pass will be wrong at both ends of the range. Use
  `ScopedDeferImpedancePropagation` when updating Ra and Rb together (they are one physical
  control; they must never be updated one at a time).
- **MODE is discrete, not continuous.** Three (or two — see #2) fixed topologies; precompute each
  rather than rebuilding the tree at runtime.
- **MODE ↔ VOLUME are otherwise independent** (different nodes, no shared network), so they can be
  validated separately.

## Multi-stage / multi-channel series pedals

**N/A** — this is a single gain stage with a single bypass. No stage-ordering ambiguity to resolve.

## Validation notes

### 1. ✅ RESOLVED — the VOLUME network IS non-monotonic, and that is correct (do not "fix" it)

Modelling the network exactly as drawn (wiper to ground, ends to node E and R8), driving node E
from a ≈20 kΩ drain impedance, gives this control law for the **network alone**:

| Ra (lug 1 → wiper) | →0 | 10 k | 25 k | 50 k | 100 k | 150 k | **176 k** | 200 k | 300 k | 400 k | 500 k |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Network gain | −∞ | −11.3 | −7.1 | −5.2 | −4.1 | −3.8 | **−3.79 (peak)** | −3.80 | −4.2 | −5.2 | −7.7 |

The true peak is at **`Ra` = 176 k = 35.3 % of the pot** (an earlier draft bolded 200 k; both round
to −3.8 dB, so the table shape was right and only the marked cell was off). All the real
attenuation is in the bottom ~10 % of `Ra`; the `Ra → 0` end is a genuine short, so its gain is
−∞, not the finite figure an earlier draft quoted — that number was just whichever epsilon the
sweep happened to use. Don't read it as a level.

⚠ **CORRECTION (verified 2026-09-07): the peak's POSITION is NOT independent of the drain source
impedance.** An earlier draft claimed the curve was unchanged under an ideal-current-source drive
and therefore robust to that assumption. Re-solving the two-node network refutes this:

| Drive impedance at node E | Peak `Ra` | % of pot | Peak-to-full-CW fall-back |
|---|---|---|---|
| 1 kΩ | 57 k | 11 % | 4.34 dB |
| 10 kΩ | 143 k | 29 % | 4.01 dB |
| **20 kΩ (≈ R6 ∥ ro — the physical case)** | **176 k** | **35 %** | **3.92 dB** |
| 100 kΩ | 242 k | 48 % | 3.88 dB |
| ideal current source | 280 k | 56 % | 4.11 dB |

The *shape* (silence → peak → fall-back) survives everywhere, and the fall-back DEPTH is genuinely
invariant (3.86–4.34 dB across the whole range) — but the peak **location** moves by a factor of
1.6 in `Ra`, and the peak location is exactly what the taper fit below is anchored to. It is
pinned in practice only because `R6` = 22 k sits permanently across the drain, so `Zout` cannot
exceed ~22 k however large `ro` is. **Model the drain as a Norton current source with
`Zout = ro·k(s) ∥ R6` stamped into this solve (stage 2's ⭐⭐⭐ note) — do not drive node E from an
ideal voltage source, and do not assume the taper fit is insensitive to how you drive it.**

**This was initially flagged as a probable schematic drawing error. It is not.** The pedal maker's
published description of the control matches the computed curve in shape (see the caveat below —
an earlier draft said "point-for-point", which overstates it):

> FULL COUNTER-CLOCKWISE = NO SIGNAL · 10 to 11 o'clock = UNITY GAIN (depends upon EQ setting) ·
> 1 to 2 o'clock = 3 dB+ MAXIMUM OUTPUT BOOST · 3 to 5 o'clock = 1 or 2 dB with FLAT EQ
> — "*Wired just like an original EP3*"

Silence at full CCW, a peak short of full rotation, and a deliberate fall-back past it are all
reproduced by the as-drawn topology and by nothing else. **Model it exactly as drawn.** A
conventional wiper-to-output divider would be monotonic and therefore wrong.

⚠ **One quantity did NOT match at the time this was written, and it is worth knowing before the
capture session.** The maker puts the peak at +3 dB and 3–5 o'clock at +1 to +2 dB — a fall-back of
**1–2 dB**. The as-drawn network falls back **3.9 dB** from peak to full CW, and as the table above
shows that depth is insensitive to the drive impedance, so it is not an artefact of any assumption
we've made. Roughly 2 dB is unaccounted for. Candidate explanations, none confirmed: the taper
compresses the top of the rotation so 5 o'clock never reaches `Ra` = 500 k electrically; "3 to 5
o'clock" is a rounded range quoted at 3 o'clock rather than at the stop; or the copy is simply
approximate. **Treat this as an open discrepancy to settle with the VOLUME sweep capture
(measurement #1 below), not as a confirmation.** Do not tune other constants to close a 2 dB gap
that may not exist.

✅ **RESOLVED 2026-09-10 (note #21): it was the maker's copy, not the circuit.** P4's VOLUME sweep
measured a **3.86 dB** fall-back against the as-drawn prediction of 3.91 dB — agreement to 0.05 dB.
The maker's published "1–2 dB" is simply wrong (marketing rounding, not a circuit fact); the peak
POSITION claim they also published is the only one of their four control-law figures that held up.

⭐ **This gives four free calibration points for the taper fit — use them.** `dsp.md` asks for at
least two knob positions to constrain a taper's shape; the maker's notes supply four across the
full range, before any capture is made:

| Knob position | Rotation (7→5 o'clock sweep) | Target output |
|---|---|---|
| Full CCW | 0 % | −∞ (no signal) |
| 10–11 o'clock | ≈ 25–33 % | 0 dB (unity) |
| 1–2 o'clock | ≈ 58–67 % | **+3 dB (maximum)** |
| 3–5 o'clock | ≈ 75–100 % | +1 to +2 dB |

The peak of the *network* sits at `Ra` = 35.3 % of 500 k **for the physical ~20 kΩ drive
impedance**; for that to land at 1–2 o'clock (≈60–65 % rotation) a power-law taper
`R = Rmax·x^p` needs **p ≈ 2.0** (p = 1.4 puts the peak at ~12 o'clock; the aggressive
`10^(2x−2)` law puts it at ~2:40 — see `dsp.md`, which warns that approximation is too steep).
Recompute `p` if the drive impedance assumption changes: at an ideal current source the peak needs
only p ≈ 1.4, so the two questions are coupled and must be fitted together.
**Fit the taper so the peak lands at 1–2 o'clock**, then check the other three points.
Treat these as a sanity oracle, not gospel: they are marketing copy, rounded, and explicitly
qualified with "depends upon EQ setting". A real VOLUME sweep capture still supersedes them.

📌 ⛔ **This paragraph used to derive a level anchor from the maker's copy. MEASUREMENT HAS NOW
REFUTED IT — the whole inference is struck out, and the reasoning is kept only so it is not
re-derived.** The argument was: if the pedal is +3 dB at the volume peak while the network is
−3.8 dB and the input network −0.9 dB, the JFET stage must be ≈ +7 to +8 dB, which is ~4 dB *below*
a nominal-2N5457 estimate — so expect the fitted `gm` well **below** nominal.

Note #7 measures the degeneration factor directly, and it comes out **K0 ≈ 6.6**, i.e. gm ≈ 1.5–1.7 mS
— roughly **twice** the nominal-typical 813 µS, and about **nine times** the ~180 µS this marketing
inference implied. The DARK stage gain that K0 implies is **+10.9 dB at VOLUME 10:30 and +12.5 dB at
2:30** (it moves because the load `ZL = R6 ∥ Z_E` moves with the pot), which brackets the "naive
nominal-2N5457 estimate" this paragraph dismissed. Note that gain is *derived*, not measured — L2
still applies, there is no absolute anchor — but it is derived from a measured ratio via
`Av_dark = ZL·(K0−1)/(R5·K0)`, which is independent of `ro`. **Expect fitted `gm` ABOVE nominal
typical, not below.**

That makes this the *second* independent axis on which the maker's published control points fail
(the first is the 3.9 dB versus 1–2 dB fall-back in the ⚠ block above). **Demote them from
"free calibration targets" to rough shape hints.** The one claim of theirs still in load-bearing use
is the *position* of the volume peak at 1–2 o'clock, which is what fixes `p ≈ 2.0`; that is a
position claim, structurally independent of the level claims that just failed, so it stands for now
— but it is now the last unverified thing holding up the taper, and the VOLUME sweep capture
(§5 measurement #1) is the only thing that can confirm it.

### 2. ✅ RESOLVED — MODE is a 3-position ON-OFF-ON: BRIGHT / DARK / MID

The schematic symbol alone (a plain SPDT) could not distinguish ON-OFF-ON from ON-ON. Resolved by
the owner's labelling: **up = BRIGHT, middle = DARK, down = MID**, ordered by treble content. The
centre "DARK" label settles it — an unbypassed R5 is the only state with less treble than either
cap position, and it requires a genuine centre-off. Modelled as three topologies (see stage 2b).

### ✅ RESOLVED 2026-09-07 by measurement — and the lever↔cap mapping is the OPPOSITE of the guess

The residual unknown recorded here used to read: *"up = 10 nF (lug 1)" is inferred from the
electrical meaning of the labels, not traced.* Build-plan M1 has now measured it, and the inference
was wrong.

Method: within one unit the three MODE captures differ in exactly one thing, so the mode-minus-DARK
magnitude ratio cancels rig gain, converter response and unit variance. Theory says that ratio is
exactly a first-order shelf, `(1 + s·R5·C) / (1 + s·R5·C/K0)`, with its zero at the drawn bypass
corner. Fitting that two-parameter shelf over 150 Hz–20 kHz:

| Capture | Fitted zero | Fitted pole | Plateau | Fit residual |
|---|---|---|---|---|
| P1 "bright" | 1876 Hz | 12381 Hz | 16.39 dB | 0.25 dB RMS |
| P1 "mid" | 4326 Hz | 27350 Hz | 16.02 dB | 0.24 dB RMS |
| P2 "bright" | 1853 Hz | 12739 Hz | 16.75 dB | 0.08 dB RMS |
| P2 "mid" | 4005 Hz | 26302 Hz | 16.35 dB | 0.03 dB RMS |

**The BRIGHT position carries the ~1.86 kHz zero, so BRIGHT engages C1 = 22 nF; MID carries the
~4.17 kHz zero, so MID engages C2 = 10 nF.** Both units agree, from two different trainers, with
sub-0.3 dB fit residuals. `JfetStage::bypassCap()` was swapped to match; the `Mode` enum ORDER was
deliberately left alone, because it is the APVTS choice index and `architecture.md` forbids
reordering that.

⭐ **The label reasoning that produced the wrong guess is worth remembering.** This file argued the
22 nF "lifts the 2–4.4 kHz upper-mid band as well, which reads as *mid*". That is a real perceptual
description of the band it adds — but the 22 nF branch sits **above** the 10 nF branch at *every*
frequency until the two converge, so it is also unambiguously the brighter and louder of the two
positions. An aesthetic argument about which band a filter emphasises lost to a two-parameter fit.
The stored NAM loudness metadata pointed the same way all along (Bright > Mid > Dark in both units),
which `docs/build-plan.md` flagged as a hypothesis worth ten minutes; it was right.

⚠ **A separate, smaller discrepancy fell out of the same fit: the measured zeros are ~7% BELOW the
drawn ones**, consistently — 1864 Hz mean vs 2010 Hz drawn, 4166 Hz mean vs 4421 Hz drawn. The zero
depends only on the product R5·C, and the two branches imply the same R5 to within 1.6%, so this is
one common offset rather than two independent cap tolerances. Either R5 measures ≈ 3.85 kΩ, or both
caps run ≈ 7% high (one parts bin across a maker's build run would do it). **`schematic.png` was
re-read at high zoom: R5 is unambiguously "3k6", so this is not a transcription error.** The
measured cap RATIO is 2.24 against 2.20 drawn, i.e. C1/C2 is confirmed. Model the shelf from the
measured (τ, K0) pair rather than arguing which component carries the 7% — the DSP consumes only
those two numbers per mode, and this dataset cannot separate R5 from C.

**For the UI/APVTS:** order the choice list to match the physical lever top-to-bottom —
`["Bright", "Dark", "Mid"]` — so the on-screen `ThreePositionSwitch` reads the same way as the
hardware. Note that this is deliberately *not* ordered by brightness; it is ordered by position.

### 3. ✅ RESOLVED — VOLUME rotation direction: CW = louder

Was: confirm which physical rotation direction moves the wiper toward lug 3. Settled 2026-09-09
(note #20a) by fitting the LF-corner data against both senses: the shipped CW-is-louder sense fits
at 9.3 Hz RMS against 17.5 Hz for the reversed wiring, and P4's full VOLUME sweep (note #21) is
consistent with it throughout. **CW = louder stands; do not revisit.**

### 4. Values resolved by judgement, and the parts the maker specifies

Every R and C value on this board is legible directly from `schematic.png` and has been transcribed
above as drawn — none needed judgement. The only *inferences* recorded anywhere in this file are the
LT1054 pin-function mapping (immaterial — see "Power supply") and the DC bias estimate in stage 2
(explicitly flagged as fit-to-capture).

Two build details the maker states that affect how much the nominal part numbers are worth:
- **"Vintage 1970s JFET cherry picked to cream-of-the-crop specs."** Q1 is a hand-selected vintage
  device, not a nominal 2N5457. This *strengthens* the §2 warning: nominal SPICE was already
  untrustworthy across a 5:1 production spread, and deliberate selection moves the part somewhere
  specific within (or beyond) it. **Fit every amplitude parameter to captures; treat the datasheet
  as a sanity range only.** The ~4 dB gain discrepancy noted in #1 is consistent with this.
- **"Accurate Vintage Spec, Aged Carbon Film Resistors."** Not modellable and not worth modelling —
  a resistor's value is its value. Noted only so a later session doesn't mistake the marketing for
  a circuit difference.

### 5. ✅ DONE — the 2026-09-10 P4 capture session answered all four

Kept for the record of what was originally asked for; all four landed in the same session.

1. ~~VOLUME sweep, everything else fixed~~ — done, note #21 (taper `p` = 2.30).
2. ~~One capture per MODE position at fixed volume~~ — P4 has only BRIGHT/DARK, no MID (it is a
   two-position variant), but DARK alone gave the undegenerated gain and `gm` was re-checked against
   it (note #23 item 1). The lever→lug mapping question was already settled from the NAM set
   (note #2) and P4 corroborates which branch its own BRIGHT engages (note #21).
3. ~~Harmonic spectrum at several input levels~~ — done and then some: the probe captures (note #22)
   reach the load line, which is what finally replaced the square-law assumption entirely (note #26).
4. ~~Bypass/unity anchor capture~~ — done, `p4_V1030_bypass.wav`; both `kInputRef` and
   `kOutputMakeup` are now anchored (notes #21, #23).

### 7. ✅ Phase-1 characterisation against the NAM captures (2026-09-07) — what measurement settled

Run by `analysis/phase1_characterization.py`; raw numbers in `analysis/reports/phase1_characterization.json`.
Seven NAM renders, three physical units, three trainers. Read `docs/build-plan.md` §1 limits L1–L3
first — several results below are limited by the *dataset*, not by the pedal.

**The single most important method finding.** The first pass read every corner as a naive "−3 dB
below the plateau" threshold and produced four plausible-looking scalars, **every one of them wrong,
including one that inverted the MODE verdict.** The cause is structural, not sloppiness: on this
pedal nothing reaches a plateau inside the audio band (the mode shelf's pole is at K0 × its zero,
so 12.6–26.8 kHz), so an 8–10 kHz "plateau" window normalises each branch to a different point on
its own transition. **Fit a model to the continuous curve and report the residual.** A fit that is
wrong shows a bad residual; a threshold that is wrong just returns a number.

| Measurement | Predicted here | Measured | Verdict |
|---|---|---|---|
| MODE shelf zeros | 2010 / 4421 Hz | 1864 / 4166 Hz | ✅ topology right, labels swapped, R5·C 7% high (note #2) |
| `K0 = 1 + gm·R5` | 3.93 at nominal gm | **6.59** (P1 6.46, P2 6.72) | ⭐ gm ≈ 1.5–1.7 mS, ~2× nominal |
| Input LP corner | ~7.3 kHz | 6.7 kHz (P1); P3's 7.2 kHz is an ARTEFACT | ⚠ confirmed ONCE only — see note #9 |
| C10 HP corner | 24.4 / 13.3 / 28.5 Hz | 43.8 / 26.2 / 37.9 Hz | ⚠ 1.3–2.0× high, confounded — do NOT act on it |
| Harmonics | even-dominant, square law | H2 −47 dBc rising 0.75 dB/dB | ✅ confirmed; H3 unmeasurable |

**⭐ `gm` came out of a ratio, exactly as planned, and it is the session's real deliverable.** The
mode-versus-DARK shelf's plateau is `K0 = 1 + gm·R5` for an ideal Norton drain, and `R5` is known,
so `gm` needs no level calibration and dodges limits L1 and L2 entirely. Measured K0 = 6.59 ± 0.13
across two units. With a finite `ro` the true `gm·R5 = (K0−1)(1 + ZL/ro)` is slightly *larger*, so
the α = 0 reading of **1553 µS is a lower bound**; at `ro` = 200 kΩ–1 MΩ it lands at 1.58–1.71 mS.
Two branches whose poles sit an octave apart agree on K0 to 0.37 dB, which is a genuine
cross-check rather than the same number read twice.

**⚠ P2's capture cannot be used for absolute frequency response, and the mechanism is now
identified: an output-load pole, NOT the VOLUME knob and NOT a converter.** Its HF rolls off at
−8.6 to −10.7 dB/octave through the top two octaves. P1 fits a first-order low-pass at 6.7 kHz
against the drawn 7.3 kHz.

⛔ **This paragraph originally read "P1 and P3 ... two units confirm the input network". THAT IS
REFUTED — see note #9. P3's 7.2 kHz is an artefact of running a shelf-blind estimator on a MID-mode
capture, and P1 is the only unit that confirms anything here.** The rest of this note stands: the
cascade fit below still localises P2's rolloff correctly, because it is a *relative* comparison
between candidate extra poles, and P1's own row is fitted in DARK where the estimator is sound.

Fitting each capture as the pedal's own 7.3 kHz input pole cascaded with **one** free extra pole
localises it:

| Capture | VOLUME | Extra pole | Fit residual | Load capacitance it implies |
|---|---|---|---|---|
| P1 dark | 10:30 | 30.2 kHz | 0.35 dB | 52 pF |
| P3 mid | 10:00 | 40.2 kHz | 0.55 dB | 39 pF |
| **P2 dark** | **2:30** | **3.2 kHz** | **0.05 dB** | **542 pF** |

P2's is the *best* fit in the set, and 542 pF at ~92 kΩ is an entirely ordinary cable run — roughly
five metres, or a couple of patch cables plus a reamp box. P1 and P3 need essentially nothing. Two
cascaded first-order poles also explain the −8.6 to −10.7 dB/octave slope without invoking any
higher-order converter filter, which is simpler and better supported than the "trainer's converter"
reading this note first carried.

⛔ **The VOLUME knob is ruled out as the cause, and it is worth recording why, because P2 is the
only capture at a high volume setting so the question is a fair one.** The pedal's output impedance
is nearly volume-independent (see stage 3): across the three captured positions AND every drain
drive assumption from ~20 kΩ to an ideal current source, it spans 92–139 kΩ, a factor of **1.5**.
The required load capacitance differs by a factor of **10**. Volume can therefore account for at
most 1.5 of a 10× discrepancy. Worse for the hypothesis, under the physical ~20 kΩ drive the sign is
backwards: 2:30 has the *lowest* output impedance of the three, so a capacitive load predicts P2
should be the **brightest** capture, and it is by far the darkest. The input side cannot do it
either — moving the input pole from 7.3 kHz to 3.2 kHz needs about 150 kΩ of extra source
impedance, and reamp boxes drive well under 1 kΩ.

➡ Use **P1 only** for anything above ~2 kHz (note #9 removes P3 from this list). **P2 remains the
best capture in the set for the mode differential** (0.03–0.08 dB shelf residuals), because a post-JFET pole multiplies all three modes
equally and cancels in the ratio.

**⚠ The C10 / VOLUME corner is a real measurement of something, but it cannot arbitrate the taper.**
Fitting it with the input network's own 6.5 Hz pole pinned (fitting two free poles is
ill-conditioned) is clean — 0.03–0.15 dB residuals, and within a unit the three MODE captures agree
to 7%, which they must, since MODE does not touch C10. Yet every unit reads high: ×1.75–1.88 (P1),
×1.95–2.00 (P2), ×1.33 (P3). Before revising anything, note what the confound is:

- **Volume is 1:1 confounded with unit AND trainer.** Every volume position in this dataset is a
  different pedal recorded through a different rig, and unlike the MODE differential there is no
  ratio that cancels the rig. Every rig has its own LF response, and it can only push a corner *up*
  — which is the direction all three err.
- **The confound is demonstrably the same size as the signal.** P1 (10:30) and P3 (10:00) sit at
  nearly the same predicted corner, 24.4 vs 28.5 Hz — a 17% circuit difference. They measure 43.8
  vs 37.9 Hz: 16% apart **in the opposite order**. Two rigs at one knob position disagree by as much
  as the knob itself moves across that range.
- **It is not a taper error.** Back-solving `Ra` from each measured corner and fitting `Ra = 500k·xᵖ`
  needs p = 3.2, 7.8 and 2.5 for the three points. A wrong exponent would give one consistent p; a
  rig-dominated residual gives three that disagree, which is what we have.
- **It is not the NAM models failing at LF.** The known-answer probe below bounds their LF error at
  0.6 dB, and a 24 Hz-versus-44 Hz corner is a 4.5 dB difference at 20 Hz.

➡ **Keep `p ≈ 2.0`. Do not retune the taper, the drive impedance, or C10 from this.** Record the
measured corners as observed and settle it with the two-way pedal's VOLUME sweep (`build-plan.md` §7),
which is a *within-rig* sweep and therefore the only measurement that can separate these.

⭐ **A known-answer probe for the NAM models themselves, which cost nothing and should be reused.**
Below its shelf zero, every MODE position has `Zs = R5` (the unselected branch's 1 MΩ shifts it by
under 0.5%), so **every mode differential must read exactly 0.00 dB below ~200 Hz.** Whatever it
reads instead is that model's own error, measured with no reference capture and no assumptions.
P2 comes back at ≤ 0.20 dB; P1 at ≤ 0.60 dB. That is the LF noise floor of this dataset, and it is
what rules out the "NAM cannot do low frequencies" explanation above. Build one of these wherever
the circuit forces an answer you already know.

**M5 — even-dominance confirmed, but the cubic is NOT settled, and a floor audit is why.** In every
capture, at every probe frequency, **H4 comes back above H3.** That is impossible for a mild
polynomial nonlinearity, so everything above H2 is the NAM model's error floor (≈ −57 dBc for P1,
≈ −77 dBc for P2) and no cubic can be read off it. What survives: on P2, H2 rises **0.75 dB per dB**
of input across the top four level cells and reaches −47.6 dBc — a square law gives 1.0 dB/dB, so
this is the real quadratic. On P1, H2 does not move with level at all, so P1's harmonic data is
floor throughout and must not be fitted. Compression is under 0.01 dB until the very top cell, then
−0.09 dB (Dark) and −0.18 to −0.35 dB (Bright); **more compression in Bright at the same input is
the right sign**, since bypassing the source lets the full input swing appear across the gate-source
junction. So: the cubic reads **compressive**, on the fundamental's own gain only, and its magnitude
cannot be fitted here. ⛔ `nonlinear-component-modeling.md` §2 finding (b) — check the sign of the
cubic before choosing a limiter — is **only weakly answered**; treat it as provisional.
⚠ Do not fit the shaper to a straight line through the whole compression ladder: the curve is flat
and then bends in the last cell, so a linear fit dilutes a real 0.1–0.35 dB knee into ≈ 0.001 dB/dB
and reads as noise. The first pass did exactly that.

**M6 — the pass/fail band must come from the differential, not the absolute response.** P1 versus P2
in DARK, level-normalised, spans **13.8 dB** at 18 kHz. That is almost entirely the P2 rig problem
above, not two pedals disagreeing. On the rig-cancelling mode differential the same two units agree
to **0.28 dB mean, 0.33 dB RMS, 0.67 dB worst** across 20 Hz–20 kHz. ➡ **Set the project tolerance
from ~0.3 dB RMS / ~0.8 dB peak on the mode differential.** There is no measurement of absolute
unit-to-unit spread in this dataset and there cannot be one — three units means three rigs.
📌 This also answers `build-plan.md` §9.2's open question: `FeatureProfile`'s `kHfBudgetDb` should
**not** be widened. The 13.8 dB top-octave figure is rig, and real units agree to ~0.5 dB at 10 kHz.

### 6. ✅ Topology re-verification pass (2026-09-07) — what was checked and what changed

The whole signal path was re-traced node-by-node from `schematic.png` and the crops (not from this
file), and every computed figure in it was re-derived independently. Result: **the topology is
correct as recorded — no node connection changed.** Specifically re-confirmed against the image:

- `R3` genuinely in series with IN (not a pulldown); `R4` is the pulldown; `C3` shunts node A.
- Q1 drain at top (to `R6` → VA and `C10`), source at bottom (to `R5`, `C1`, `C2`) — the gate arrow
  points into the channel, i.e. N-channel, consistent with the supply polarity.
- MODE lug 3 → `C1` (22 nF) and lug 1 → `C2` (10 nF), common lug 2 → GND. The lug↔cap mapping is
  confirmed; only the lever↔lug mechanical mapping remains open (note #2).
- VOLUME wiper (lug 2) → GND, lug 1 → node E, lug 3 → `R8` → OUT; `R9` bridges E → OUT.
- Node E is one node: `C10`, `R10`, `R9` and VOLUME lug 1 all meet there (the drawing shows two
  junction dots on that same wire — they are not separate nodes).
- `D1` cathode to +9 V, anode to GND — correct reverse-polarity shunt, as recorded.
- LT1054 pins 3 and 5 to GND, pins 1 and 8 jumpered to +9 V, pin 2 driving the flying caps, pins
  4/6/7 open — as recorded (and still immaterial to the audio model).

**Four numeric claims were wrong and have been corrected in place** (all in note #1 and the stage-3
section): the ideal-current-source invariance claim, the "matches point-for-point" claim, the
bolded peak cell, and the `C10` corner figure. The input-network and MODE-corner figures all
re-derived exactly. The lesson matches this file's own gotcha list: re-reading component values
found nothing, re-solving the derived numbers found four errors. **Spend future passes on the
derived quantities, not on the values.**

### 8. ⭐⭐ Step 4b refit (2026-09-08) — what implementing the measurements found

Full write-up in `docs/build-plan.md` §10; the code and its reasoning are in `src/dsp/JfetStage.h`.
Two things came out of it, and the second is the one to carry forward.

**(a) The fit itself.** `gm` = 1553 µS from M2's K0, the two shelf time constants from M1's fits
(85.369 µs / 38.263 µs, ~7 % above the drawn R5·C), and — new here — the observation that a measured
`gm` collapses the square-law self-bias solve to a **one-parameter family**, `|Vp|/Vov = 1 + gm·R5/2`
and `IDSS/Id = (1 + gm·R5/2)²`. That is what turns the datasheet from a vague 5:1 spread into a
genuine bracket on the operating point (stage 2 above).

**(b) ⚠⚠ THE STRUCTURAL BUG: degeneration suppresses distortion TWICE, not once.** Worth
**16.4 dB = 20·log10(K0)**. Expanding `id = gm·u + c·u²` with `u = vg − id·Zs` to second order gives
`id2 = c·(u1²)/k(s)` — the squared term is filtered by the *same* `1/k(s)` that set the drive — so
`H2/H1 = A/(4·Vov·k²)`. The model applied the shelf once, to the drive, and shipped that way. Checked
against an exact per-sample implicit solve: shelf-only over-produced H2 by **+16.3 / +16.1 / +15.7 dB**
at 0.2 / 0.5 / 0.8 V of gate swing; the corrected two-shelf structure lands at **−0.04 / −0.25 /
−0.72 dB**.

⭐ **The method lesson, which sits beside the phase-testing one in this file's history.**
`ChainTest`'s mode differential is the test that exists to catch this class of error, and it passed
the whole time — because the differential is a **linear** measurement and the linear path was
correct. A correct frequency response is not evidence that the nonlinear path is right. Only an
independent solve of the equation the model approximates could settle it, and building that oracle
took less time than the two magnitude tests that could never have found it.

**Two consequences that are now testable predictions, not modelling choices:**
- **MODE moves distortion as `k²`.** Fully bypassed, H2 rises **32.7 dB** relative to the fundamental
  versus DARK, because the drive rises by K0 *and* the suppression vanishes. M5's "more compression
  in Bright at the same input" is the qualitative shape of this. The two-way pedal's session should
  measure it.
- **The reference renders were driven at ≤ 0.41 V/FS**, i.e. **≥ 6.6 dB below `kInputRef` = 0.87.**
  This is a bound, not an assumption: H2 amplitude goes as `aEven × drive`, `aEven = 1/Vov`, and the
  datasheet caps Vov at 0.447 V. ➡ **Binding on step 9: A/B the harmonics at matched DRIVE, not at
  matched digital level.** Note this bound only exists because of finding (b) — under the old
  structure, `kInputRef` = 0.87 demanded Vov = 6.3 V, which the 22 V rail and R6 = 22 k forbid, and
  the impossibility was the first hint that the structure was wrong.

⚠ **What is still NOT measured, so nobody re-derives it as if it were:** the shaper's curvature is
degenerate 1:1 with the trainers' unknown reamp level, and no ratio in this dataset breaks that. The
shipped `Vov` is the datasheet-capped end of the family, chosen because the maker's "cherry picked"
claim points there and because it is the minimum-curvature admissible point. `beta` (the cubic) stays
0 — M5 fixes its sign and nothing more.

### 9. ⛔⭐⭐ Evaluation sweep 2026-09-08 — P3 does NOT corroborate the input pole, and P2 is INVERTED

Full report: https://claude.ai/code/artifact/90256712-20e2-426e-96c1-d9bd20750bb8 ·
raw numbers `analysis/reports/{comprehensive_data,phase_sweep}.json` · new script
`analysis/phase_sweep.py`. Two of this file's recorded conclusions change.

**(a) ⛔ P3's "7.2 kHz input pole" is an ARTEFACT. Note #7's "confirmed twice" is now "confirmed
once".** The M3 estimator fits `(one free low-pass) × (gain)` and has **no mode-shelf term**. P3 has
only a MID capture. Run that same estimator on the PLUGIN, whose input corner is 7300 Hz *by
construction*, and it separates cleanly:

| Plugin render | M3 fitted LP corner | Fit residual |
|---|---|---|
| DARK (no bypass, no shelf) | **7295 Hz** — recovers the truth | **0.01 dB** |
| MID (10 nF engaged) | 9 × 10¹² Hz — runs away | 1.14 dB |
| BRIGHT (22 nF engaged) | 1 × 10¹⁰ Hz — runs away | 2.35 dB |

The shelf's lift cancels the pole's rolloff, so a single-pole model cannot describe a bypassed mode
at all. P3's capture fitted *better* (0.61 dB) than the plugin does in the same mode — because P3's
rig loses HF, which flattens the shelf back down. Its agreement with 7.3 kHz is a coincidence
between two errors. ➡ **P1, fitted in DARK, is the ONLY absolute-response anchor this dataset
contains.** P3 joins P2 as differential-only.

⭐ **The method lesson, which is this file's own trap one level up.** Note #7 says *fit a model and
report the residual, don't threshold*. That was followed — and the fit still lied, because the model
was missing a term the capture contained. The check that caught it costs nothing: **run every
capture-side estimator on the PLUGIN first, where the answer is known by construction.** A capture
cannot tell you its estimator is blind; a render whose true value you set can.

**(b) ⭐⭐ All three P2 captures are POLARITY-INVERTED** relative to P1, P3 and the plugin. Midband
phase reads +13.6° / −2.7° / +1.7° for P2's bright/dark/mid, against ≈ −178° for P1, P3 and every
render; the best-fit constant offset lands at −168 to −171° on all three. Stage 2 is a single
common-source stage, which **must** invert, so P1/P3/the model are right and P2's capture chain
flipped somewhere. It cancels in the mode ratio, which is why note #7 never saw it. This is the
**third** independent axis on which P2's rig is disqualified (cable pole, absolute response, now
polarity).
⚠ `analyze.null_depth()` gain-matches with a **signed** least-squares scalar and reports `|g|`, so a
flip is absorbed into a negative gain and **P2's inversion is invisible in its null figure**. Run
`polarity()` first — this file's existing warning, now with a concrete instance behind it.

**(c) ✅ The MODE shelf is confirmed in PHASE, not only in magnitude.** Mode-vs-DARK differential
phase residuals are **0.2–2.6°** across 30 Hz–15 kHz. The floor is *measured*, not assumed, using the
phase twin of note #7's free known-answer probe: below the shelf zero every mode has `Zs = R5`, so
the differential must read **0.00°**, and what it reads instead (1.03–5.29°) is that capture's own
phase error. Three of the four differentials sit at or below their own floor across the entire band.
Only P1's mid above 12 kHz exceeds it (−4.1 to −7.0° against a 2.75° floor).

**(d) ⚠ The LF gap is real, is the C10 corner again, and is still NOT to be acted on.** Against P1
the plugin runs **+2.4 dB at 40 Hz, +2.9 dB at 32 Hz, and −35° of phase at 50 Hz** — the plugin has
more low end than the capture, which is the same sign and size as note #7's M4 finding that every
unit's measured corner reads 1.3–2.0× above prediction. Note #7 ruled that confounded; nothing here
un-confounds it. ⛔ Still needs the within-rig VOLUME sweep.

### 10. ⭐⭐ P1's input calibration is known (−12 dBu) — the level degeneracy is broken for one unit

Owner-reported 2026-09-08; **not** recorded in any `.nam` file (none of the seven carries
`input_level_dbu`/`output_level_dbu` — checked).

⚠⚠ **SAY WHERE IN THE CHAIN A LEVEL IS MEASURED — this ambiguity was worth 24 dB.** The same rig is
also described as **+12.2 dBu = 3.156 V RMS = 4.46 V peak at 0 dBFS**, and both figures are correct:
+12.2 dBu is the **interface's output at full scale**, while NAM's `input_level_dbu` is measured **at
the gear's input jack, after the reamp box**. The 24.2 dB between them is an ordinary reamp
attenuation, and `4.4626 / 10^(24.2/20) = 0.2752 V`, which is the −12 dBu figure exactly.

⭐ **The circuit brackets the pedal-side value independently, and rules the interface-side one out.**
Solving `H2/H1 = A_gate/(4·Vov·k²)` for the rig's V/FS from P1's measured H2, across the datasheet's
admissible `Vov` range, gives **V/FS ∈ [0.103, 0.999] V = [−20.6, −0.8] dBu**. That is wide — it does
not pin −12 dBu against, say, −5 — but it fixes the order of magnitude from measurement alone, and it
excludes 4.46 V/FS by 13 dB. ➡ Note #10's blocker (1), "nothing in the data can re-derive it", is
therefore **partly discharged**: the recollection is now corroborated in magnitude rather than taken
on trust. Full reasoning in `src/dsp/JfetStage.h`; the
per-order data is `analysis/reports/harmonic_audit.json`.

`V/FS = 0.7746 × 10^(−12/20) × √2 = 0.2752 V` per full scale. ✅ Inside the ≤ 0.41 V/FS bound that
note #8 derived independently from H2, which is why it is believed. **This lifts build-plan limit L2
for P1's INPUT only** — there is still no output anchor, so `kOutputMakeup` is untouched.

**Consequence for stage 2's shaper.** At the calibrated drive the model's H2 is short by
**9.47 dB mean / 9.65 median (sd 2.98)** over P1's 24 usable cells, implying **Vov ≈ 0.150 V** against
the shipped 0.447 V — near the bottom of the admissible [0.131, 0.447] bracket. A probe build at
0.1502 V confirmed it (mean −0.17 dB). The implied bias point is **Id = 117 µA, IDSS = 1.68 mA,
|Vp| = 0.570 V, Vd = 19.4 V** — i.e. the drain sits **2.6 V under the rail**, not 7.6 V, so the
headroom asymmetry recorded in stage 2 above would reverse. ⛔ **Not applied** — four reasons in
`JfetStage.h`, the load line being the one that needs real work.

⭐ **A harmonic-domain known-answer probe, free and reusable.** Below the shelf zero every MODE has
`Zs = R5`, so **H2 in dBc must be identical across modes** — the harmonic twin of note #7's 0.00 dB
magnitude probe. It reads **1.5–21.3 dB (typically 4–9)**. That is these models' harmonic error
floor, measured with no reference capture, and it is what bounds Vov to about a factor of 1.4.

⚠ **Note #7's M5 line "everything above H2 is the models' error floor" is too strong.** It is true of
P1 (H3 rises 0.75–1.81 dB/dB where a cubic needs 3.0; H4 ≥ H3 in 1–3 of 4 cells) but **false of
P2-bright and P3**, whose H3 rises 2.65–2.74 dB/dB with 0–1 inversions and reaches −36.7 / −41.2 dBc.
Real cubic content exists; the model has none (`beta` = 0). It cannot be fitted until one of those
two units' reamp levels is known.

### 11. ⭐⭐ Compression audit (2026-09-08) — the shaper's truncation, not its coefficients, is the gap

`analysis/compression_audit.py`. Compression carries the cubic's sign, so it was run to settle
`beta`; it settled something bigger.

**Compression is the best-conditioned nonlinear measurement in this dataset**, because it reads the
FUNDAMENTAL rather than a harmonic 40–60 dB down. Its known-answer floor — the same probe as notes #7
and #10, since below the shelf zero every mode has `Zs = R5` and must compress identically — is
**0.145 dB (P1) / 0.210 dB (P2)**, against 4–9 dB for the harmonics.

**The model produces exactly 0.000 dB of compression at every band and level.** The captures do not:
P3 reads −0.42 dB at 794 Hz and −0.61 dB at 3.1 kHz in its top cell, P2-bright −0.18/−0.35, and
**Bright compresses more than Dark** — the sign stage 2 predicts, since bypassing the source puts the
full swing across the gate-source junction.

⭐⭐ **The cause is the Volterra truncation, not a missing cubic.** An exact solve of
`id = gm·g(vg − id·R5)` with `g` a pure square law **plus its cutoff clamp** produces both compression
and H3; the shipped shelf-on-the-excess form drops them. ➡ **Keep `beta` = 0.** ⚠ Any oracle written
for this must carry the clamp — the bare parabola turns over at `w = −Vov` and the solve goes
non-monotone above ~0.4 V of gate swing.

⚠⚠ **Do not apply note #10's `Vov` = 0.150 without also replacing the truncation.** At a 0 dBFS input
the exact solve compresses −0.032 dB at the shipped `Vov` but **−1.05 dB at 0.150**, with H3 at −57.8
vs −24.5 dBc. Lowering the curvature alone would leave the stage quantitatively wrong in its own
normal operating range.

📌 **RECONCILED 2026-09-08, and the discrepancy is itself a measurement.** Note #12 replaced the
truncation, and the shipped stage now reads **−0.0324 dB** at the shipped `Vov`, matching this
oracle's −0.032 to three decimals. At `Vov` = 0.150 it reads **−0.808 dB**, not −1.05. The two are
not in conflict: **this oracle used a PURE parabola, and the shipped shaper uses the `tanh²` even
bump**, which saturates where the true parabola does not. That gap is 23 % of the compression at
`Vov` = 0.150 and only 4.5 % at the shipped value, because truncation error and shaper error both
grow with curvature. ➡ **The `tanh²` bump is now the largest remaining shaper approximation**, and it
is the thing to replace alongside `Vov` — `JfetStage.h` already flags it as a known 0.4 dB of H2 at
0 dBFS, deferred at the time because the truncation was larger. It no longer is.


### 12. ⭐⭐ Path A (2026-09-08) — the loop is SOLVED now, not expanded. Compression and H3 are real.

Code and reasoning in `src/dsp/JfetStage.h`; full write-up `docs/build-plan.md` §13; new instrument
`analysis/band_audit.py`. All 11 tests pass. **No fitted constant changed** — `gm`, both shelf τ,
`aEven`, `Vov`, `beta` = 0 are all exactly as note #8 left them. Only the structure changed.

**What note #11 identified is now fixed.** The stage solves `id = gm·g(vGate − Zs(z)·id)` by Newton,
per sample, instead of expanding it to second order and filtering the squared term once. Compression
and third-harmonic content are higher-order terms of that same expansion, so the truncation could not
produce them **at all** — the model read exactly 0.000 dB of compression in every band at every level.
It now reads −0.032 dB at a 0 dBFS input and −0.178 dB at +6 dB of trim, with H3 at −57.6 and
−41.2 dBc against the truncation's −79.5 and −60.9. `beta` stays 0: this cubic is the LOOP's.

⭐⭐ **The design decision that makes it safe, and the obvious alternative that would have been
wrong.** The discrete source one-port is **derived from the shipped shelf coefficients**, by inverting
`1/(1 + gm·Zs(z)) = H(z)`, rather than by discretising R5 ∥ C afresh. So the stage's small-signal
response is unchanged to **1.1e-11 dB and 4.8e-15°** across three modes × four rates × five
frequencies, and everything validated against it stands — the mode differential `gm` was fitted to,
note #8's shelf τ, the three-point discretisation that removed the 3.09 dB bilinear spread, the phase
residuals of note #9, `OsDroopRestore`'s premise. ⛔ **Discretising R5 ∥ C directly would have
silently reintroduced plain bilinear on a shelf whose pole sits above Nyquist at base rate** — the
exact 3.5 dB error the previous session removed. Two facts fall OUT of the algebra rather than being
imposed, which is the evidence it is right: **Zs(z=1) = R5 to 1e-16 in every mode at every rate**
(the caps block DC, so the DC feedback path must be the bare resistor), and **DARK collapses to the
bare resistor with no state at all**.

⭐ **Independent cross-check.** `analysis/compression_audit.py`'s Python oracle, written weeks earlier
in another language, reported −0.032 dB of compression at 0.783 V of gate drive at the shipped `Vov`.
The C++ solve reads **−0.0324**. Neither can inherit the other's bug.

⚠ **A coupling that is now load-bearing.** Newton needs no damping and no bracketing here only
because `g` is monotone, and `g` is monotone only because `limitNeg` was set 1.15× past the exact
cutoff to stop a fold-back (note in `JfetParams`). **A future refit that restores the physically exact
`limitNeg = (2/3)·Vov` would break the solve's convergence guarantee as well as ADAA.**

📌 **CPU: 2.07 → 4.78 % of realtime at the 4× default, 3.51 → 8.95 % at 8×.** Comfortable, but
`dsp.md`'s "the FIR costs nothing worth recovering" was decided when the chain was 2 %.

### 13. ⚠⚠ The low-frequency THD "deficit" is the REFERENCE's, and the treble one too

`analysis/band_audit.py`, raw `analysis/reports/band_audit.json`, write-up `build-plan.md` §13.1–13.2.

Below ~100 Hz the captures read up to **50 % THD at 20 Hz** and 12–37 dB more H2 than the model.
Three independent reasons none of it is the pedal:

1. ⭐⭐ **H3 comes back ABOVE H2 at 20 and 31.5 Hz in every P1 mode.** A square-law device cannot do
   that — the order-inversion test that disqualified everything above H2 in note #7, localised.
2. **This file's own known-answer probe reads 20.2 dB at 20 Hz where the circuit forces 0.00.**
3. **50 % THD is physically impossible** at P1's calibrated ~0.14 V of gate drive into a 22 V rail.

Mechanism: a **132 ms receptive field is 2.6 periods at 20 Hz**, and the output high-pass the models
are trying to reproduce sits at 24–44 Hz. ⛔ **Do not fit anything to a capture below ~100 Hz.**
⚠ And the floor probe measures mode SPREAD, so an error common to all three modes reads zero — an LF
receptive-field artefact is exactly that, since the high-pass is identical in every mode. **At low
frequency the probe systematically under-reports**, so its 8.3 dB at 50 Hz is a lower bound.

**From 125 Hz up the deficit is real but FLAT** — P1-dark reads −7.0 to −11.6 dB across 125 Hz–1.6 kHz
with no trend, mean ≈ −9.6 dB. That is note #10's `Vov` scalar, unchanged. ➡ **There is no missing
frequency-dependent distortion mechanism.** The treble claim goes the same way: P1-dark's −18 dB at
8 kHz cannot be physical (in DARK `k` is constant, so H2 in dBc must not move with frequency) and its
5 kHz cell fails the order-inversion test.

📌 ⚠ **"Rises 2 dB/dB with level" is NOT sufficient evidence that a harmonic is real.** P3's H2 rises
at a clean 1.9–2.1 dB/dB in every band — and its THD is flat at ≈ −30 dBc from 20 Hz to 8 kHz, which
the circuit forbids outright since distortion moves as k². Pair the level-slope test with a
frequency-shape test before believing a harmonic.

### 14. ⚠⚠ The compression known-answer probe needs 3f below the shelf zero, not f

The probe used throughout this file — below the shelf zero every MODE has `Zs = R5`, so all three must
agree — has **three flavours with different validity conditions**, which had not been written down.
Magnitude (note #7) and harmonic (#10) are exact as soon as f is below the 1.86 kHz zero.
**Compression is a THIRD-ORDER quantity, so it reads the loop at 3f**, and is not exact until 3f is.

Measured on the PLUGIN, where the answer is known by construction (`JfetStageTest` 8e):

| probe f | 23 Hz | 94 Hz | 492 Hz | 797 Hz |
|---|---|---|---|---|
| model's own spread | 0.0017 dB | 0.0015 | 0.0201 | **0.0585** (3f = 2391 Hz, past the zero) |

✅ On this dataset it reaches nothing — dropping the 800 Hz row leaves both capture floors unchanged
(0.145 dB P1, 0.210 P2), so a lower band binds. Both figures are now reported by the script.

⭐ **A second effect hides under the first.** At the lowest probe frequency the model's spread stops
falling, at 0.0017 dB. It is not the 3f effect (it does not scale with f) and not a modelling error:
it is the **fixed Newton iteration count leaving a mode-dependent bias**, because `Rd·gm` differs
~30× between Dark and Bright so two iterations converge to different depths per mode. Converging the
solve collapses it to 0.00005 dB. It is 85× below the capture floor and changes nothing — but it is
exactly the residue that would later be mistaken for a real mode asymmetry.

### 15. ⭐⭐ The reference is NOISE-FREE — so every "floor" in this file is systematic, not noise

`analysis/imd_and_floors.py`, raw `analysis/reports/imd_and_floors.json`, write-up
`docs/build-plan.md` §14. Two helpers had sat uncalled in `analyze.py` since the harness was built.

Measured across all seven captures: **noise floor −99 to −132 dBFS**, and **repeatability −114 to
−135 dB** (the residual between a cell and a byte-identical duplicate placed elsewhere in the
signal). These models are deterministic to well below anything else here.

➡ **The 4–9 dB harmonic floor, the 0.145–0.210 dB compression floor and the 1.9–20.2 dB per-band THD
floor are all SYSTEMATIC MODEL ERROR, not measurement noise.** Three consequences:

1. ⛔ **None of them will average down.** More cells, longer dwells, repeated measurement: no gain.
2. They are fixed functions of the signal, not random variables, so a ± on them is misleading.
3. ⚠ The mode-spread probe measures a **difference of two systematic errors**, which can partially
   cancel — a second way it under-reports, on top of the common-mode blindness in note #13.

**Also settled, from the same run:** the reference models put **nothing off the 220 Hz harmonic
grid** in the twin-tone segment, to −136.6 dBc, which is the files' own quantisation floor. Their
error is wrong amplitudes on the right bins, not spurious junk, and there is no evidence of memory
effects or aliasing artefacts in the references.

⚠⚠ **And the twin-tone segment cannot measure intermodulation at all:** it is 220 + 660 Hz and
660 = 3 × 220 exactly, so every product lands on the 220 Hz harmonic grid and neither input tone is a
clean amplitude reference. Fixing it needs a NEW segment with an inharmonic pair, since the signal is
append-only. Worth doing at the next signal revision; not worth a re-capture on its own.

📌 **A third route reaches note #10's split, independently.** Level slopes of the twin-tone products
put P1's at 0.1–0.9 dB/dB where 1.0 and 2.0 are required, i.e. flat and therefore floor, while
P2-bright reaches 1.64 dB/dB on a third-order product and P3 returns 1.00/1.03/1.02 on the
second-order ones. ⭐ The PLUGIN returns 1.00/1.00/1.00/2.00/2.00 exactly, which is the known-answer
check that the instrument works before any capture is read. **The calibrated unit's nonlinear data is
floor and the units with real nonlinear data are uncalibrated — confirmed now by three separate
signals. Asking the P2/P3 trainers for `input_level_dbu` is still the highest-value one-message ask.**

### 16. ⭐⭐ `Vov` is FITTED (2026-09-09) — twice, agreeing — and deliberately still not applied

`analysis/vov_fit.py`, raw `analysis/reports/vov_fit.json`, consequences in `src/dsp/JfetStage.h`.
This is the fit note #10 asked for and the cross-check it said it lacked. **No constant changed.**

**Two observables, different ORDERS, different signals, different floors:**

| Route | What it reads | Fitted `Vov` |
|---|---|---|
| A | H2 in dBc, 125–800 Hz, all three modes, 60 cells (near-inverse in `Vov`) | **0.126** (per mode: dark 0.136, mid 0.133, bright below the grid) |
| B | compression GROWTH over the top 10 dB, 5–8 kHz (third order, so ~1/`Vov`²) | **0.165** (per cell 0.127–0.182) |

They agree to a factor of **1.31**, inside the ~1.5 the floor allows, and they bracket note #10's
independently-derived 0.150. The shipped 0.4469 is outside both by a factor of ~3.

⭐ **The estimator is validated by construction BEFORE any capture is read** (note #9's standing
rule). `--self-test` puts a plugin render at an off-grid `Vov` = 0.2200 where the capture goes, and
both routes recover it — route A exactly, route B at 0.2215–0.2217.

⚠⚠ **ROUTE B ONLY WORKS ON THE INCREMENT, AND THE FIRST VERSION READ `comp_db` AND FOUND NOTHING.**
The reference models carry a level-INDEPENDENT gain error per band, which `comp_db` reports as
compression that was never there — P1-dark reads −0.089 / −0.087 / −0.088 dB over the top 10 dB at
5 kHz, i.e. flat, so not compression at all. Differencing across level cancels that offset exactly
and drops the route's own floor from `compression_audit.py`'s **0.145 dB to ~0.013 dB** — the worst
DARK increment, where the circuit permits almost nothing. ➡ When an
observable is contaminated by a constant, difference it rather than widen the tolerance.

⚠⚠ **AND DO NOT POOL BY MEDIAN ACROSS MODES.** In DARK `Zs = R5` at every frequency, so there is
almost no compression to measure and its cells do not move with `Vov` at all. A median over the
three modes sits on the insensitive one: pooled that way, route B's statistic moved **0.01 dB across
the entire admissible range** and read as a route with no leverage. It has leverage — bright's 8 kHz
cell spans 0.85 dB over the same range. **The statistic was flat, not the observable.**
📌 Read "flat" apart from "floor" here: dark's *offset* is the reference's error, but its flat
*increment* is the circuit — `--self-test` shows the PLUGIN's dark cells are just as flat.

⚠ **The H2 floor shows up as a systematic per-MODE BIAS, not as scatter.** Below the shelf zero every
mode has `Zs = R5`, so H2 in dBc must be mode-independent — yet bright reads **2.4 dB lower than
dark and mid at every point on the grid**. That is note #15's "systematic, will not average down"
made visible: 60 cells buy no precision beyond the single-cell floor of 4.74 dB.

⛔ **NOT APPLIED — and reason 3 of note #10's four is discharged and REPLACED by a stronger one.**
The load line does not move (`triodeOnsetGateVolts()` reads 1.691 V at the shipped value and 1.737 V
at 0.150, because lowering `Vov` raises `Vds_q` by almost as much as it lowers the current). What
moves is CUTOFF, and it overtakes triode:

| `Vov` | `Vds_q` | cutoff | triode | first to clip |
|---|---|---|---|---|
| 0.4469 (shipped) | 13.12 V | −2.7 dBFS | −7.5 dBFS | **triode** |
| 0.2050 | 17.92 V | −9.5 dBFS | −6.9 dBFS | cutoff (crossover near here) |
| 0.1500 | 19.02 V | −12.2 dBFS | −7.3 dBFS | **cutoff, by 4.9 dB** |

So applying the fit would not invalidate the load line — it would make the load line nearly
unreachable and swap the stage's clipping MECHANISM. That is a qualitative voicing change, decided
by a parameter pinned to a factor of 1.3–1.5 from the one capture whose harmonic data clears its own
floor by 4.7 dB. Reasons 1, 2 and 4 of note #10 stand unchanged. ➡ The +12.2 dBu session settles it
by measuring the clipping mechanism directly instead of inferring it from a parameter.

### 17. ⛔⭐⭐ The "4–8 kHz dip" is P1's rig — and P2 CONFIRMS the 7.3 kHz input pole to 0.02 dB

`analysis/hf_shape_fit.py`, raw `analysis/reports/hf_shape_fit.json`. This answers the one FR item
`goal_check.py` flagged as unexplained, and it reverses part of note #9.

**It is not a dip.** Printed as a whole curve rather than as four core-band cells, the plugin-minus-
P1 error is one continuous shape: **+3.4 dB at 25 Hz → 0 near 1 kHz → −0.6 dB at 5 kHz → +0.95 dB at
16 kHz**, in all three modes. The 4064–8127 Hz cells are the bottom of that curve, not a feature.
⚠ Any mechanism therefore has to produce a lift that PEAKS AND TURNS OVER. A single-pole difference
is monotone and saturates, which is why moving the input pole cannot describe it (fitting it free
gives 7185–7558 Hz and improves the residual by 0.003 dB).

**What the fit says, modelling the capture as the plugin × a high-pass × two low-passes:**

| capture | LF high-pass | lower pole | upper pole | residual |
|---|---|---|---|---|
| **P2, all 3 modes** | 20–21 Hz | **3.18–3.29 kHz** | **7.34–7.46 kHz** | **0.021–0.034 dB** |
| P3 (mid) | 18 Hz | 4.53 kHz | 7.95 kHz | 0.222 dB |
| P1, all 3 modes | 28–31 Hz | 12.4–12.9 kHz | 12.4–12.9 kHz | 0.176–0.263 dB |

⭐⭐ **P2 recovers the pedal's own input pole — 7.3 kHz, from `R3 ∥ R4` into `C3` — to within 2 %,
from a capture, with no prior, in every mode, at a 0.02 dB residual.** Pinning it at 7300 and fitting
only the extra pole costs nothing (0.022–0.034 dB), so the confirmation is not an artefact of a free
parameter. Its second pole, 3.2 kHz, reproduces note #7's independently-fitted cable pole (542 pF).
P3 accommodates 7300 Hz just as freely. **P1 alone cannot be described by any cascade containing a
7.3 kHz pole**; it wants two coincident poles at 12.8 kHz, and since an extra pole can only DARKEN,
P1's capture is *brighter* at 4–8 kHz than the circuit as drawn can be. ➡ **The plugin is not too
dark there. P1 is too bright.** Do not touch `C3`, `R3`, or the input network.

⭐ **Why this estimator is not shelf-blind where note #9's M3 was.** It fits the capture against the
**plugin**, not against the raw sweep, so the mode shelf appears on both sides and cancels. The
evidence is in the table: P2's fitted poles are mode-independent to 3 % across bright/dark/mid, and
note #9's whole finding was that a shelf-blind estimator cannot fit a bypassed mode at all.
➡ **Note #9's disqualification of P2 and P3 was for ABSOLUTE LEVEL, POLARITY and their own extra
poles. It does not extend to pole STRUCTURE measured this way** — a known, cleanly-fitted extra pole
can be fitted out; 0.25 dB of unexplained model error cannot.

⚠ **P1 is the NOISIEST NAM model in the set, and it is the designated absolute anchor.** The residual
ranking here (P2 0.02 ≪ P3 0.22 ≈ P1 0.19–0.26) matches note #2's mode-shelf fit residuals (P2
0.03–0.08, P1 0.24–0.25) — two unrelated fits agreeing on which model is cleaner. Even P1's best
model leaves a structured **+0.3 dB hump at 3–5 kHz and −0.25 dB dish at 60–160 Hz**, which is the
same size as the core-band miss being investigated. ➡ `goal_check.py`'s HF core-band miss against P1
is inside P1's own error and is not a model defect.

⚠⚠ **A BOUND THAT THE FIT SITS ON IS NOT A FIT, and it hid this result completely.** The first
version floored the "extra" pole at 8 kHz, on the reasoning that it would sit above the pedal's own.
P2's fit then rested exactly on that bound in all three modes and reported 3.1 kHz + 8 kHz at a
2.1 dB residual. Freed, the same data gives 3.2 + 7.4 kHz at 0.02 dB. The bound had encoded an
assumption about which pole was which that the algebra does not support — the two terms enter the
model identically, so they are interchangeable and neither is "the input pole" until something else
says so.

### 18. ✅ The two phase instruments RECONCILE — the recorded "2.4°" was mislabelled

`analysis/phase_reconcile.py`, raw `analysis/reports/phase_reconcile.json`. Neither instrument is
broken and no model change follows; one number was described as covering a band it did not.

The record said "within 2.4° over 200 Hz–12 kHz" (from `phase_sweep.py`) while `goal_check.py`
reported 6.66–8.22° over the same band. Computing ONE residual curve per capture and re-reading it
under each differing choice, one at a time, worst |residual| in 200 Hz–12 kHz against P1:

| configuration | bright | dark | mid |
|---|---|---|---|
| `phase_sweep` as recorded | 2.15 | 2.41 | 6.21 |
| + include the 200 Hz report point | 6.24 | 5.85 | 6.47 |
| + every bin, not 10 interpolated points | 6.24 | 7.94 | 6.78 |
| + raw cross-spectrum, not Farina-gated | 6.20 | 7.13 | 6.72 |
| + unsettled segment | 6.20 | 7.13 | 6.72 |
| + fit window 200 Hz–12 kHz | 7.30 | 6.89 | 8.05 |
| = `goal_check`, also 8× OS | 7.14 | 6.79 | 7.97 |

⚠⚠ **The dominant rung is the first: the recorded figure is a max over six hand-listed report
frequencies starting at 500 Hz.** The same instrument reads −5.85 to −6.47° at its own 200 Hz report
point, which the claim's band label includes and the claim's arithmetic did not. **A max over a
handful of interpolated points is not a max over a band, and the gap is unbounded in one direction.**
The target is "within 5° across all bands", which is a per-band claim — so `goal_check`'s statistic
is the one that answers it. 📌 The OS factor was never the explanation: 4× → 8× moves it by 0.2°.

✅ **And the miss is entirely at the bottom.** Worst |residual| by sub-band under that configuration:
**200–500 Hz: 6.8–8.0°**, 500 Hz–2 kHz: 1.6–2.6°, 2–8 kHz: 1.4–2.3°, 8–12 kHz: 1.8–4.3°. So the 5°
target is met everywhere above 500 Hz, and what fails is the tail of the missing LF high-pass pole
(note #9d) — already confounded across units and already blocked on the VOLUME sweep. ⛔ Nothing new
to act on, and note #17 rules out the minimum-phase companion the handover suspected: there is no
model-side magnitude dip at 4–8 kHz for phase to be carrying.

### 19. ⛔⭐⭐ The LF bass excess is the TRAINERS' RIGS, not the pedal — do NOT bake it in

`analysis/lf_pole_attribution.py`, raw `analysis/reports/lf_pole_attribution.json`. This closes the
LF item that notes #7 (M4), #9d and #18 all left as "real, confounded, blocked on the VOLUME sweep".
**No constant changed, and none should be.**

**The question asked:** all seven captures have less bass than the plugin, uniformly in SIGN. If the
real pedal has less bass, some circuit value must be wrong — so which, and can it be fixed now?

⭐⭐ **The dataset has a free discriminator nobody had used: the three units sit at three VOLUME
settings.** The pedal's own output high-pass (C10 into the volume network) moves with the knob;
anything in the capture chain does not. So fit each candidate as ONE global value across all seven
and watch whether it needs a *different* value per capture:

| candidate | best single value | worst residual | per-capture spread |
|---|---|---|---|
| C10 smaller | 61.4 nF (drawn 100) | 0.74 dB | 1.45× |
| R10 smaller | 33.0 kΩ (drawn 240) | 0.86 dB | **2.99×** |
| resistive load at OUT (the rig's input Z) | 3.2 kΩ, runs to the bound | **1.96 dB** | 1.00× |
| drain Norton impedance | 0.10 kΩ, runs to the bound | 1.39 dB | **66.8×** |
| **an input-side high-pass (volume-INDEPENDENT)** | **24.0 Hz** | **0.69 dB** | 1.51× |

As drawn the worst error is 2.59 dB. ⛔ **Two candidates are eliminated outright** — the rig's input
impedance and the drain impedance both run to their sweep bounds and still fit 2–3× worse, so the
loading explanations are dead. R10 needs a 3× different value per volume setting, so it is not one
component.

⭐⭐ **What survives is a PER-RIG high-pass ahead of the pedal, and the fit is at the models' own
error floor with the pedal left exactly as drawn:**

| rig | fitted corner (3 modes) | worst residual |
|---|---|---|
| P1 | 29.4 / 28.6 / 30.2 Hz | 0.38 / 0.19 / 0.18 dB |
| P2 | 20.7 / 21.7 / 20.7 Hz | 0.09 / 0.10 / 0.10 dB |
| P3 | 20.0 Hz | 0.13 dB |

**Within a rig the three modes agree to 5 %; between rigs it is 29 / 21 / 20 Hz.** That is a per-rig
constant. The obvious physical candidate is the **reamp transformer** every NAM training chain
contains: right sign, right magnitude (passive reamp boxes roll off in the 20–40 Hz decade), common
to all three protocols, level- and box-dependent so it varies rig to rig, and *ahead* of the pedal so
it is volume-independent — which is exactly the axis that fits best.

⛔ **Therefore do NOT change C10 (or anything else) to close this.** The two in-pedal candidates that
fit at all need **38 % (C10) or 3.3× (the input HP)** changes to components legible at high zoom on
`schematic.png` — far outside any tolerance. When two structurally different in-pedal explanations
each demand an implausible value AND fit no better than one out-of-pedal explanation that demands
nothing, the common factor is outside the pedal. Baking in ~2 dB at 25 Hz would import one trainer's
reamp box as a permanent voicing — the same error already refused for P2's cable capacitance
(stage 3) and the guitar source impedance (stage 1), for the same reason: **the plugin's signal path
has no transformer in it.**

⚠⚠ **A LOW NAM `ESR` DOES NOT ARGUE AGAINST THIS, and the reasoning is worth not re-deriving.** ESR
measures the model against **its own training target** — the recorded output of that rig. A perfect
model of a rig containing a reamp transformer has ESR ≈ 0. So the observed ESR ≈ 3e-4 confirms the
models faithfully reproduce what was recorded, which is precisely what makes these corner fits
*trustworthy as measurements of the rig*; it is silent on whether the rig was flat. ESR is also a
broadband energy-weighted time-domain figure, so a 2 dB error at 25 Hz would barely register in it
even as a model-fidelity metric. 📌 This sits with note #15: the references are deterministic and
noise-free, so what is left is systematic — and here the systematic part is identified.

⛔ **Cable capacitance cannot be the answer either, on structure alone.** A shunt capacitance at the
output is a LOW-pass — it darkens the top octave (note #7's P2 finding, 542 pF at 3.2 kHz). Removing
bass needs a *series* element. Wrong axis; no amount of it produces this.

➡ **What this changes for the capture session.** The bypassed capture (`CLAUDE.md` ask #2) is now
the direct test rather than a general anchor: capture bypassed **through the identical reamp chain**
and its LF rolloff IS the rig's high-pass, measured rather than inferred. ⭐ If the owner's own rig
turns out to have a corner in this same 20–30 Hz decade, that is confirmation — and the correct
response is still to leave the model alone and *deconvolve the rig out of the reference*, not to add
a pole to the pedal.

📌 The VOLUME sweep is no longer load-bearing for THIS item (it is still load-bearing for the taper,
the 3.9 vs 1–2 dB fall-back and the C10 corner of note #7's M4). Notes #9d and #18's "blocked on the
VOLUME sweep" for the LF magnitude and phase misses should be read as **blocked on the bypassed
capture** instead.

#### 19a. ⚠⚠ CORRECTION to note #19 — the volume lever does NOT discriminate as claimed

Note #19 says the three VOLUME settings discriminate between the candidates. **That is true of the
loading explanations and of R10, and FALSE of the one that matters.** Challenged on it (three
independent rigs agreeing in sign is not what arbitrary rig scatter looks like), the right test is a
joint fit: pin the pedal's C10 at a trial value, then let EACH RIG have its own free high-pass, and
profile the worst residual against C10. If the pedal is genuinely taking part of the deficit, that
profile must minimise well below 100 nF.

| C10 | worst residual | rig poles then needed (P1/P2/P3) |
|---|---|---|
| **100 nF (as drawn)** | **0.372 dB** | 29.3 / 21.4 / 19.9 Hz |
| 85 nF | 0.382 dB | 25.1 / 19.4 / 14.6 Hz |
| 70 nF | 0.331 dB | 19.1 / 16.2 / 0.5 Hz |
| 61.4 nF (note #19's global fit) | 0.484 dB | 14.1 / 13.3 / 0.5 Hz |
| 50 nF | 1.427 dB | rig poles pinned at the floor |

⛔ **The profile is FLAT from 100 nF to 70 nF — the penalty for leaving C10 exactly as drawn is
0.041 dB.** So the pedal-side term is **unidentifiable** from this dataset, not refuted. Three volume
settings spanning Ra = 45–281 kΩ are not enough leverage, because R10 = 240 kΩ sits across node E and
compresses the swing the knob actually produces. ➡ **The correct statement is "no measurement here
assigns any of this to the pedal", not "it is the rigs".** Note #19's reamp-transformer mechanism
remains a plausible, UNMEASURED hypothesis and must not be cited as established.

⭐ **A second common-cause candidate is at least as strong and is not a rig at all: the NAM
architecture's finite receptive field.** It is shared by all seven models by construction, which
explains one-signedness better than three independent reamp boxes do, and truncation can only REMOVE
low-frequency energy, never add it. Note #13 already found these models misbehaving below ~100 Hz
from an unrelated route (H3 above H2 at 20–31 Hz, impossible for a square law; the known-answer probe
reading 20.2 dB at 20 Hz where the circuit forces 0.00).

📌 **And "three independent rigs agree" is weaker evidence than it feels: three of three sharing a
sign is p ≈ 0.25.** It rules out arbitrary scatter — correctly — but it cannot distinguish *which*
common cause, and there are three (the rigs' shared protocol, the shared architecture, the pedal).
📌 Modern interfaces ARE flat to 20 Hz; conceded. A reamp box is a transformer, not an interface.

➡ **Decision unchanged, but for a cleaner reason: changing C10 buys 0.041 dB, so there is nothing to
act on.** The bypassed capture through the owner's own chain settles it in one pass — if that chain
is flat to 10 Hz and the pedal capture still rolls off near 25 Hz, the pole IS the pedal's and the
value change gets made with evidence behind it.

### 20. ⭐⭐ The LF pole is LINEAR and FIRST-ORDER — my reamp-transformer story is refuted

`analysis/lf_mechanism_probe.py`, raw `analysis/reports/lf_mechanism_probe.json`. Asked whether the
LF story can be advanced at all without new captures. It can: two properties of the extra pole are
measurable from what we hold, and neither needs the level calibration that blocks everything else.
**No constant changed.** ✅ `--self-test` passes first (plugin in the capture's place → no pole
found, corner spread 1.00×, residual 0.003–0.012 dB), so the instrument does not manufacture a pole.

**TEST 1 — LEVEL INVARIANCE.** The signal carries four full-range sweeps at −41/−26/−16/−6 dBFS.
Measured as capture-minus-plugin so the pedal's own compression cancels, the fitted corner moves by
**1.01× (P2) to 1.18× (P1) over 25 dB of drive.** ⛔ **The extra loss is LINEAR.** That refutes
note #19's reamp-transformer mechanism outright — core saturation moves the LF corner with flux,
i.e. with level — and it equally rules out any *nonlinear* neural artefact. ⚠ The −6 dBFS row is
excluded from the statistic: the pedal is audibly distorting there and the two sides do not distort
identically while `Vov` and `kInputRef` are unsettled. P1's rows visibly misbehave there (fitted
order jumps to 1.8–2.1 where the clean rows sit at 0.4–0.5); P2's do not move at all.

**TEST 2 — ORDER.** Against the plugin at −26 dBFS, RMS residual:

| capture | 1-pole | 2-pole | **shelf** | capture's corner | plugin's implied corner | depth |
|---|---|---|---|---|---|---|
| P1 bright / dark / mid | 0.156 / 0.143 / 0.100 | 0.217 / 0.209 / 0.172 | **0.061 / 0.031 / 0.034** | 35.1 / 34.4 / 31.8 Hz | 17.1 / 16.5 / 12.6 Hz | −6.2 / −6.4 / −8.0 dB |
| P2 bright / dark / mid | 0.052 / 0.052 / 0.043 | 0.086 / 0.088 / 0.076 | **0.015 / 0.018 / 0.019** | 23.8 / 24.0 / 22.8 Hz | 10.6 / 10.3 / 9.3 Hz | −7.1 / −7.4 / −7.8 dB |
| P3 mid | 0.142 | 0.160 | **0.022** | 34.4 Hz | 25.0 Hz | −2.8 dB |

⚠⚠ **A SHELF BEATING A LONE POLE IS THE EXPECTED RESULT, NOT A FINDING — and reading it as one was
the trap here.** The comparison is capture-MINUS-PLUGIN and the plugin has its own LF high-pass, so
the difference of two first-order poles is *already* a first-order shelf, flat above and levelling
off below at `20·log10(fc_plugin/fc_capture)`. The first pass fitted a free fractional exponent,
got 0.30–0.66, and nearly recorded "the rolloff is gentler than any RC can be". It is not: the shelf
is one pole against one pole. ➡ **Read the shelf's POLE as the capture's own corner and its ZERO as
the plugin's.** 2-pole is worse than 1-pole in all seven, so nothing steeper is present either.

⭐ **And the zero is a free validation of the output network.** The implied plugin corner must track
VOLUME, and it does, in the right order and roughly the right spacing: 17 / 10 / 25 Hz fitted
against 24.8 / 13.3 / 28 Hz computed for P1 / P2 / P3. The consistent downward bias is expected —
the plugin's LF is TWO poles (the 7.2 Hz input high-pass as well as C10's), so a single-pole
approximation of it must sit below the output pole alone.

➡ **NET: the extra loss is a real, linear, single first-order pole.** With note #18's finding that
magnitude and phase independently agree on its corner (a minimum-phase signature a neural artefact
need not have), the balance moves AWAY from both note #19's transformer and note #19a's
receptive-field candidate, and TOWARD an ordinary RC — a coupling capacitor, somewhere.

⛔ **It still does not say WHERE.** Both readings leave the same residual spread: as an extra
volume-independent pole per rig, 29 / 21 / 20 Hz (1.45× spread); as the pedal's own corner scaled
up, ×1.36 / 1.77 / 1.23 (1.44× spread). Note #19a's profile said this and it is unchanged — three
volume settings are not enough leverage. **The bypassed capture through the owner's own chain is
still the one measurement that separates them**, and it is now a sharper test than before: we know
what to look for is a single linear pole in the 20–35 Hz decade, not a transformer's level-dependent
rolloff.

### 20a. ⛔ Is the LF corner a VOLUME effect? P1 and P3 say maybe; P2, the only unit with leverage, says no

Prompted by a good observation: P1 and P3 sit at nearly the same VOLUME (10:30 / 10:00) and their
measured corners are nearly identical (33.8 / 34.4 Hz), while P2 sits far away (2:30) and reads much
lower (23.5 Hz). That is exactly the pattern an in-pedal, volume-dependent pole would make, so it
was tested directly: with the pedal EXACTLY as drawn, can the knob alone produce the corners?

| unit | stated | x | Ra | plugin's corner | measured | knob position it would need |
|---|---|---|---|---|---|---|
| P1 | 10:30 | 0.35 | 61 k | 23.7 Hz | 33.8 Hz | **9:25** |
| P2 | 2:30 | 0.75 | 281 k | 13.1 Hz | 23.5 Hz | **10:31** |
| P3 | 10:00 | 0.30 | 45 k | 27.6 Hz | 34.4 Hz | **9:22** |

⚠⚠ **THE PAIR THAT AGREES IS THE PAIR WITH NO LEVERAGE.** P1 and P3 are 30 minutes apart on the
knob; two points that close cannot determine a slope, so *any* common effect — rig, architecture, or
pedal — puts them at the same corner. All the discriminating power in this dataset sits in P2, and
P2 refuses: for volume to be the whole story it would have to have been captured at **10:31 rather
than 2:30**, a four-hour reporting error. Back-solving the taper exponent instead of the position:

**p = 2.70 (P1) / 7.24 (P2) / 2.39 (P3).**

📌 That independently reproduces note #7's M4 result (2.5 / 3.2 / 7.8) from a different estimator on
a different band — including which unit is the outlier. Two arrivals at the same answer.

⭐ **What the observation DOES earn: P1 and P3 agree the taper is slightly steeper than shipped**
(p ≈ 2.4–2.7 against 2.0), from two units, two rigs and two trainers. Weak — they are close together
so a single common error would look the same — but it is the right sign and worth carrying into the
VOLUME sweep, which is the measurement that settles `p`. ⛔ Do NOT move `p` on it now: p = 2.0 is
what puts the volume peak at the maker's stated 1–2 o'clock, and note #7's M4 confound is unchanged.

⭐ **A free by-product: validation note #3's rotation direction is corroborated.** Testing the
reversed wiring (x → 1−x) against the measured corners gives an RMS error of 17.5 Hz versus **9.3 Hz**
for the shipped CW-is-louder sense, and it puts P1 and P3 nearly 21 Hz out. Not proof, but the
reversed hypothesis is clearly worse on all three units. **CW = louder stands.**

### 31. ⭐⭐ THE OUTPUT LOAD (2026-09-11) — the maker's four published claims were never wrong,
### and three "refutations" on this file's record were all made against an UNLOADED model

Instruments: `analysis/lf_pole_attribution.out_network(..., rl=)`, `OutputNetworkTest` section 6,
`OfflineRender --load`. **A new user control, `output_load`, defaulting to 68 k.**
⛔ **This note SUPERSEDES the "refuted" verdicts in validation note #1 and in note #21's fall-back
paragraph.** Both were correct arithmetic about an OPEN-CIRCUIT pedal, and neither claim was ever
about an open-circuit pedal.

**The observation that forced it.** P4's own captures put the pedal at **+10.60 dB (dark) /
+11.88 dB (bright) at VOLUME 1:30** into the interface's 1 MΩ — and that measurement is a ratio
against a bare loop, so it depends on no calibration figure at all and is very hard to be wrong
(notes #21, #23). The maker publishes ~+3 dB and unity at 10–11 o'clock. ⭐ The owner then checked
many independent user reviews and videos and reports they **all** agree with the maker. So the
model and the world disagreed by ~7 dB with a solid measurement on each side.

**⭐⭐ ONE LOAD VALUE RECONCILES EVERY PUBLISHED CLAIM, and the reason it can is a property of this
circuit rather than a free parameter.** The VOLUME wiper is grounded and R9 = 110 kΩ bridges node E
to the jack, so the output impedance is **59–102 kΩ** (measured, note #22) instead of the few kΩ a
normal pedal presents — and it MOVES with the knob (101 kΩ at 10:30, 59 kΩ at full CW). A load
therefore does not merely lower the curve, it **TILTS** it:

| claim | maker | open circuit | into 68 k | into 75 k |
|---|---|---|---|---|
| unity gain | 10:00–11:00 | ~8:39 | **10:14** | **10:05** |
| max boost, 1–2 o'clock | "3 dB+" | +10.49 dB | **+2.72** | **+3.21** |
| 3–5 o'clock level | +1 to +2 dB | +6.58 | **+1.13** | **+1.51** |
| peak-to-full-CW fall-back | 1–2 dB | 3.91 dB | **1.59** | **1.70** |
| peak POSITION | 1–2 o'clock | 1:30 | 1:30 | 1:30 |

⚠⚠ **COUNT THE DEGREES OF FREEDOM BEFORE CALLING THAT FOUR-FOR-ONE.** A load does exactly two
things: a ~7 dB level shift and a ~2.3 dB tilt. Unity and peak-level are both set by the shift, so
they are ONE constraint; the peak position was already right unloaded, so it is free. The genuinely
independent test is the **fall-back**, which is pure tilt and goes 3.91 → 1.70 dB. So this is one
parameter satisfying **two** independent observables — real evidence, not proof.

**⛔ WHAT THE LOAD DOES NOT CHANGE, measured, because this is what makes it safe to add.** With
level removed, a 75 kΩ load is worth **exactly 0.00 dB above 100 Hz and 0.00° above ~500 Hz** (a
resistive load is flat there), and **exactly 0.000 dB of H2 below 2 V of gate drive**. It reaches
the model through only two routes:
- **C10's high-pass corner** rises (14.6 → 18.3 Hz at 1:30), worth −0.79 dB at 20 Hz and +6.4° at
  20 Hz, +4.4° at 40 Hz, +1.9° at 100 Hz. Zero at 7:30, worst at 1:30–3:00.
- **The DRAIN-node impedance** falls up to 6.3 % (17.4 → 16.3 kΩ at 1:30), which IS the load line's
  slope — so triode onset moves **+0.45 dB later**, and H2 in triode (≥3 V gate) rises 0.75–2.1 dB.
  ⚠ That is why `OutputNetwork::drainNodeImpedance()` had to take the load too: a load that changed
  the linear response but not the clipping behaviour would be audible in only half the model.
📌 Both of those land in the two places the model is least constrained (the confounded LF pole, and
the triode branch which rests on one capture cell — note #27 §4), so the existing validation cannot
*rule out* a ~70 kΩ load in the maker's rig either. Suggestive, not settled.

**⚠ THE 68 kΩ IS FITTED, NOT IDENTIFIED — do not record a component as its explanation.** The
owner's candidate, a guitar amp's 68 kΩ grid stopper, was tested and does NOT load: a stopper sits
in **series** with a near-infinite grid, so the amp still presents ~1 MΩ and costs 0.81 dB, leaving
+9.7 dB of boost. Its real job is a low-pass with the tube's Miller capacitance (12–47 kHz).
⭐ What DOES load is the **classic two-jack Fender input**, whose unused jack grounds a *second*
68 kΩ stopper: that is a real 68k-series/68k-shunt divider, presenting 132 kΩ and costing 11.15 dB
in total — which overshoots to **−0.66 dB** at the volume peak, i.e. below unity.

**➡ SO THE REAL FINDING IS THAT THERE IS NO SINGLE ANSWER: this pedal's boost spans ~10 dB across
ordinary rigs** (+9.7 dB into a modern 1 MΩ input, roughly unity into a vintage Fender front end),
entirely because of its own output impedance. That is why `output_load` is a **control** rather than
a constant, and why reviewers can honestly disagree about how much boost it has.

**📌 What shipped.** `kLoadChoices = { "68k", "1M", "None" }`, default 68k.
⚠⚠ **"None" is not decorative.** `captures.render_args()` pins `--load none` for every capture
comparison, because P4's captures were taken into 1 MΩ and `p4_corners.loading_correction_complex()`
already corrects that out of the CAPTURE side. Rendering at "1M" instead is not close enough to
skip: 1 MΩ still costs 0.50–0.84 dB depending on the knob, against a ±0.5 dB FR target and a
`kOutputMakeup` anchored to an sd of 0.178 dB. ⛔ And OfflineRender deliberately follows the
plugin's default rather than defaulting to none — it drives a real `PedalAudioProcessor` and must
not describe a different pedal than a user hears.

**✅ Validated against an independent nodal solve**, not the same WDF tree twice: `OutputNetworkTest`
section 6 agrees with the closed form of the same three-node network to **0.00003 dB** over 4 loads
× 3 knob positions, and asserts the drain-node impedance actually moves (−6.2 %).

**⚠⚠ AND IT EXPOSED A STALE CONSTANT THAT HAD BIASED SIX SCRIPTS.** The first WDF-vs-nodal
comparison disagreed by 0.0246 dB — **identically at every load**, which is the signature of a
constant mismatch rather than a topology error. `lf_pole_attribution.py` had `RO = 1.44e6`, the
value `ro` held before note #26 re-derived it to 1.1921e6, and six scripts route through
`out_network()`. It parses `JfetStage.h` now, and the agreement went to 0.00012 dB. Same fault as
note #23's `TAPER_P`, one file over. ➡ **A load-independent disagreement is a constant; a
load-dependent one is the topology.** That distinction is what identified it in one step.

### 2a. 📌 BRIGHT and DARK are the two REAL Echoplex voicings; MID is the maker's invention

Confirmed by the owner 2026-09-09, and it matches the maker's published naming already quoted in
note #2: **Early 1970s = BRIGHT · Late 1970s = DARK · Hybrid Early & Late = MID.** The "hybrid" is
the maker's own estimation of a midway point, not a historical EP-3 setting.

⭐ **This makes the owner's two-position unit a better reference than it first appeared.** Their
pedal carries both authentic voicings; the position their capture cannot reach is the synthesised
one. So the model's authenticity is fully testable against their captures, and only the invented
position stays inferred from P1/P2 (scale MID by the measured cap RATIO of 2.24, not by an absolute
τ — see the CLAUDE.md two-position block).
⚠ It also means their BRIGHT is the same physical position as P1/P2's, so its shelf zero should land
near the measured 1.86 kHz. **Fit it rather than assuming it** — note #2's label reasoning was
confidently wrong once already.

### 21. ⭐⭐ P4's RAW CAPTURES (2026-09-10) — the taper, the fall-back and kOutputMakeup all settle

Instruments: `analysis/p4_corners.py`, `analysis/unit_compare.py`, `analysis/p4_component_fit.py`,
`analysis/volume_sweep.py` (+ their JSON in `analysis/reports/`). **Every one passes a `--self-test`
that puts plugin renders where the captures go**, recovering the shelf zero to 0.04 %, the taper to
p = 2.00 exactly, C10 to 100.0 nF, the load capacitance to 0 pF and the LF corner ratio to 1.000.
**No DSP constant changed yet** — this note is the measurement; the decisions are in CLAUDE.md.

Twenty-one P4 captures, one pedal, one rig, one session, both calibration figures written down.
The complete VOLUME rotation 7:30 → 17:00 in DARK and BRIGHT, all pad 12, plus three pad-0 takes.
`check_capture.py` passes all of them: no truncation, no clipping, polarity −1 on every active
capture and +1 on both references, noise floor −102 to −105 dBFS.

**⭐⭐ THE TAPER IS p ≈ 2.3, MEASURED, AND IT IS THE SINGLE PARAMETER THAT EXPLAINS EVERYTHING.**
Six knob positions within one rig, 7:30 excluded:

| band | statistic | p = 2.0 as drawn | p fitted |
|---|---|---|---|
| midband control law, 1 kHz | worst / RMS | 2.076 / 0.956 dB | **p = 2.33** → 0.267 / 0.160 dB |
| LF corner, 15–400 Hz | worst / RMS | 1.035 / 0.139 dB | **p = 2.28** → 0.406 / 0.041 dB |

⭐ **Two independent bands, one parameter, agreeing to 2 %** — and note #20a's weak signal was
right about the direction: P1 and P3 back-solved 2.4–2.7 and the note said "right sign, carry it
into the VOLUME sweep". The sweep says 2.28–2.33.

**⛔⭐⭐ C10 IS AS DRAWN, AND A THREE-POSITION FIT SAID OTHERWISE — a degeneracy, caught by adding
knob positions.** At three positions C10 = 85.7 nF looked decisive (RMS 0.191 → 0.021, a 9×
improvement, and the joint fit left the taper at p = 2.01 as if the two were cleanly separated). At
six positions the taper alone beats it (0.041 vs 0.063 RMS) and the joint fit returns **C10 =
97.8 nF, i.e. nominal**. ⚠ **The three-position result was not wrong arithmetic — it was two
parameters that a three-point span cannot separate**, wearing the appearance of a sharp fit
*including a joint fit that appeared to confirm it*. Note #17's "a bound the fit sits on is not a
fit" has a sibling: **a joint fit that leaves the second parameter at its start value has not
necessarily separated them; it may just have no leverage on either.** Add data before believing it.
⛔ The out-of-pedal alternative is refuted outright: a per-rig high-pass fits **1.014 / 0.110 dB**,
2.7× worse in RMS, and jointly with C10 it collapses to its 1 Hz lower bound. And note #19's whole
premise is now moot — **the rig is measured and has no LF pole at all** (bypass-minus-loop is
−0.047 dB at 20 Hz).

**⭐⭐ THE 3.9 dB vs 1–2 dB FALL-BACK DISCREPANCY IS CLOSED, AND THE CIRCUIT WAS RIGHT.**
Measured peak-to-full-CW: **3.86 dB**. As-drawn prediction: **3.91 dB**. Agreement **0.05 dB**.
The peak sits at **13:30 measured / 13:28 fitted at p = 2.33**, inside the maker's stated 1–2
o'clock. ➡ Note #1's "~2 dB unexplained, invariant to every assumption tested" had nothing to
explain: **the maker's published 1–2 dB is simply marketing copy and is refuted.** That is now the
THIRD of the maker's four published control points to fail (after the +3 dB peak level and the
implied stage gain), leaving only the peak POSITION, which this sweep independently confirms.

**⭐⭐ `kOutputMakeup` HAS AN ANCHOR AT LAST: +1.332 dB = ×1.1658**, sd 0.160 dB over six knob
positions, taper-corrected. It has been exactly 1.0 and unanchored since the project began.
⚠⚠ **It is only meaningful because the two calibration figures DIFFER and the difference was
undone.** Play side +12.20 dBu = 4.4626 V/FS; record side +14.29 dBu = 5.6767 V/FS; so a capture's
digital gain is **2.090 dB away from the pedal's actual voltage gain**, and getting that backwards
is a 4.2 dB error in the control law (it happened, first attempt). The check that caught it is free
and must be re-run whenever the rig changes: **a loop is a wire and this bypass is true bypass, so
both must read exactly 0.000 dB of voltage gain.** They read **−0.026 and −0.015 dB**.
⚠ `kOutputMakeup` and `gm` are BOTH level scalars in the DARK path, so they are not independent.
What separates them is that `gm` also sets the mode differential, which is rig-free and already
measured — so fit `gm` from the differential FIRST and let the makeup absorb the remainder.
**If `gm` moves, re-measure the makeup.**

**⚠⚠ ~99 pF LOADS THE PEDAL'S OUTPUT, AND THE BYPASS DECONVOLUTION CANNOT REMOVE IT.** Fitting one
global load capacitance across the DARK rotation takes the 3–19 kHz error from **2.036 / 0.992 dB
to 0.361 / 0.071 dB**. The reason deconvolution misses it is structural and worth not re-deriving:
in bypass the source driving the cable is the interface's own low output impedance, but in an
active capture it is the pedal's **59–102 kΩ** (circuit.md stage 3), so the pole exists in one path
and not the other. ➡ **This is a CAPTURE-side correction, not a model change** — the 2026-09-08
decision to ship no load capacitance stands, because the plugin's output goes to a DAW digitally.
Correct the capture, or stop comparing above ~8 kHz. It is P2's 542 pF cable pole again at 1/5 the
size, and MEASURED this time rather than inferred.
📌 It is also why BRIGHT cannot be used for the HF fit: the model's `K0` is wrong for this unit and
that error lives in exactly the same band, so the fit returns 0 pF at a 4.6 dB residual. The guard
works; DARK is the only valid HF probe until `gm` is settled.

**⛔ 7:30 IS UNUSABLE FOR ITS CORNER TOO — this reverses CLAUDE.md's "read its CORNER, never its
LEVEL".** Its two takes fit LF corners **68.79 and 63.19 Hz, 8.9 % apart**, and mode shelves whose
K0 spans 4.82–6.08 across takes and sweeps, where 9:00's two takes agree to **0.1 %** and its shelf
to 0.05. At Ra = 1.25 kΩ the network is on its steepest slope, so the physical knob-setting error at
the very bottom of the rotation is larger than the ±10 min the level warning assumed. ⭐ The tell was
the RESIDUAL, exactly as this file's own rule says: 0.13–0.31 dB at 7:30 against 0.024–0.044 dB
everywhere else. Exclude it from everything except the fact that it is clean and reaches the load
line.

**✅ The linear fits are drive-independent, checked rather than assumed.** The 9:00 DARK LF corner
reads 48.41 / 49.07 / 48.77 / 49.18 Hz at pad 12 and 48.66 / 49.13 / 49.25 / 49.32 Hz at pad 0 —
across a gate swing from **0.009 V to 2.013 V**, a 47 dB span that ends past triode onset. Spread
1.9 %. So using a quiet sweep for the corners is safe, and using a hot one would have been too.

**⭐ The known-answer probe is 7× cleaner than the best NAM model.** Below the shelf zero every mode
has `Zs = R5`, so the mode differential must read 0.00 dB: the raw captures read **≤ 0.08 dB** at
100–200 Hz, against 0.20 dB (P2) and 0.60 dB (P1). Note #15's systematic NAM floor is gone.

#### 21a. ✅ DRIFT AND KNOB REPEATABILITY ARE ALREADY MEASURED — no repeat take is needed

A dedicated end-of-session repeat was proposed and declined, on the reasoning that the gear is
modern and stable. **The captures already on disk settle it, and they agree.**

`p4_V0900_dark` (pad 0, 07:27) and `p4_V0900_dark_pad12` (08:19) are the same knob position **52
minutes apart, with the VOLUME knob moved to 10:30 and to 7:30 twice in between and returned** —
i.e. exactly the knob-away-and-back repeat the proposed take would have been. With the 12 dB pad
removed, 100 Hz–10 kHz:

| | level | shape, level removed |
|---|---|---|
| **9:00 dark, 52 min apart, knob returned** | **+0.061 dB** | **0.019 dB RMS, 0.053 dB peak** |
| 7:30 dark, same treatment | +6.402 dB | 0.168 dB RMS, 0.460 dB peak |
| 7:30 bright, same treatment | +10.381 dB | 0.392 dB RMS, 1.801 dB peak |

➡ **Rig drift, JFET thermal drift and knob repeatability TOGETHER are ≤ 0.061 dB at a normal knob
position.** So none of them is a candidate explanation for anything measured in note #21, and the
control law's 0.160 dB RMS residual after fitting `p` is **model or estimator error, not setting
error** — which is what an error budget was wanted for.

⭐⭐ **And the same comparison quantifies the 7:30 problem instead of merely asserting it.** The two
7:30 pairs disagree by **6.4 dB (dark) and 10.4 dB (bright)** under identical treatment. That is not
drift — the 9:00 row rules drift out at 0.06 dB — it is **knob-setting error alone**, on the
network's steepest slope. Note #21 excluded 7:30 on the strength of its fit residuals; this is the
direct measurement, and it is far larger than the ±10 min the original level warning assumed.
📌 It also explains the shape column: 0.168 and 0.392 dB RMS of *shape* difference at 7:30, because
moving Ra moves the C10 corner as well as the level.

### 22. ⭐⭐ THE PROBE CAPTURES (2026-09-10) — Zout measured, and the NAM `Vov` fit is REFUTED

New signal `analysis/probe_signal_48k.wav` (56 s, self-contained), analysers
`analysis/probe_analyse.py`, `analysis/probe_compare.py`, `analysis/output_impedance.py`, captures
in `analysis/captures/probe/`. **No DSP constant changed.** Every instrument passes a `--self-test`
against plugin renders where the answer is known by construction.

**⭐⭐ THE OUTPUT IMPEDANCE IS MEASURED, AND STAGE 3's MODEL IS CONFIRMED TO ~2 %.**
Two captures into a 10 kΩ line input against the existing 1 MΩ instrument-input takes:

| knob | measured Zout | modelled | error |
|---|---|---|---|
| 10:30 | **99.8 kΩ** | 101.8 kΩ | **−2.0 %** |
| 17:00 (full CW) | **60.8 kΩ** | 59.4 kΩ | **+2.3 %** |

⭐ **These errors are AT OR BELOW the parts tolerance, so this is agreement, not a discrepancy.**
The unit's resistors are carbon (the maker says "aged carbon film"), where 2–5 % between one unit
and the nominal value is ordinary. A 2 % result on a quantity built from four resistors and a pot is
as close as this can be measured.
➡ This validates, all at once: stage 3's output network, the derived `ro` = 1.44 MΩ, and the
interface-loading correction applied to every capture in note #21 — none of which had ever been
measured. 📌 It also extends stage 3's quoted "92–139 kΩ, barely moves with VOLUME": full CW was not
among the three NAM knob positions, and at 60.8 kΩ it sits well below that range. The impedance
moves by 1.7× across the rotation, not "barely".

⚠⚠ **THE MAXED LINE-INPUT GAIN COST NOTHING, AND THE METHOD IS THE REUSABLE PART.** The line input
needed full gain, which confounds an unknown gain G with the loading drop being measured. Two
independent routes remove it: (a) a BYPASSED capture through the same input at the same gain —
bypass presents the reamp's near-zero source impedance, so loading does nothing to it and it
measures G alone (flat to **0.008 dB** across four tones); (b) the RATIO of the two knob positions,
in which G cancels identically and no bypass is needed at all. ⭐ And the fit is OVERDETERMINED:
one line-input impedance must serve both knob positions, and it does, to **1.040×** — that is the
discriminator saying the model's *Zout* is right rather than the load being flattered.

**⭐⭐⭐ `Vov` IS AT THE TOP OF ITS ADMISSIBLE RANGE, NOT THE BOTTOM. NOTE #16 IS REFUTED.**
Matched-drive H2 against P4, 56 cells at −12 dBFS and above:

| `Vov` | mean H2 delta | median | sd |
|---|---|---|---|
| 0.2200 | −13.52 dB | −14.29 | 4.91 |
| 0.3000 | −9.46 | −7.98 | 3.74 |
| **0.4469 (shipped)** | **−3.36** | **−3.01** | **1.45** |
| 0.7000 | +3.59 | +2.37 | 4.56 |

Negative means the pedal makes LESS distortion than the model. ⭐ **The sd is minimised at the
shipped value and roughly triples either side**, so the shipped `Vov` already gives the most
*consistent* fit across cells even though its mean is 3.4 dB out. Interpolating the mean to zero
gives `Vov` ≈ 0.57 at the model's `gm`.

⚠ **Corrected for P4's own `gm`, which the model does not carry.** H2 ∝ 1/(`Vov`·k²) and P4's
K0 = 5.12 against the model's 6.59, so the invariant is `Vov`·K0² = 24.7 and P4's own value is
**`Vov` ≈ 0.94** — right at the datasheet ceiling (with P4's `gm`, IDSS ≤ 5 mA caps it at 0.930).

⛔⛔ **SO NOTE #16's FITTED 0.126–0.165 MUST NOT BE APPLIED. Applying it would have made the model
13–20 dB WRONG in H2.** Note #16's own stated condition for applying it was a capture reaching the
load line; that capture now exists and it refutes the fit rather than confirming it. The cause is
already on the record: note #7's M5 found **P1's H2 does not move with level at all**, i.e. it is
floor throughout — and note #16 fitted route A to P1's H2 anyway, on the grounds that the deficit
cleared its per-band floor. Clearing a floor in magnitude is not the same as carrying signal.
⭐ **The general lesson, and it is the sharpest one in this file: a fit whose input is floor returns
a confident number with a good residual.** Only a better-conditioned measurement can expose it, and
here that took a calibrated raw capture that reaches the nonlinearity — which is exactly what the
NAM set structurally could not provide (note #15: its floors are systematic, so they never average
down).

⚠ **One `Vov` does NOT fix the shape.** The H2 delta grows with drive — about −2.5 dB at the −12
cell, −3.0 at −8, −6.2 at −4 — so the model's clipping ONSET is misplaced as well as its curvature.
That residual is what the sd column is measuring, and it is the next thing to model.

**⭐ Intermodulation is measurable at last, and the model's order structure is right.** Second-order
products come back at slopes of 2.05 and 2.14 absolute against a required 2.00, on P4 at 7:30 and
9:00. ⚠ Third-order is floor at low volume settings and only clears at 9:00 and above, because
7:30 records ~16 dB down.

⚠⚠ **A FIXED-AMPLITUDE ARTEFACT FLOOR, AND THE INSTRUMENT MUST BOUND ITS FIT AT BOTH ENDS.** At 7:30
the quiet cells read a flat −41 dBc with H3 at or above H2, which no square-law device can produce,
and the first version of `probe_analyse.py` fitted a slope through them and reported −0.09 where
1.00 was required — a broken measurement of a good capture. The fit window is now bounded BELOW by
whether a product actually grows with drive and ABOVE by whether the fundamental has begun to
compress, both measured per cell. ⭐ Omitting the upper bound was equally fatal: it made the
self-test read 2.39 against a required 2.00 and flag a correct instrument as broken.

**✅ Drift is a non-issue and is measured, not assumed** — see note #21a. Within-take repeats on the
probe captures read ≤ 0.02 dB on every take above 7:30.

#### 22a. ⚠⚠ CORRECTION to note #22 — the `Vov` sweep CANNOT reach P4's value, because `gm` is wrong

The sweep in note #22 was run to completion and its tail is NOT monotone:

| `Vov` | 0.2200 | 0.3000 | **0.4469** | 0.7000 | 0.9300 | 1.2000 |
|---|---|---|---|---|---|---|
| mean H2 delta | −13.52 | −9.46 | **−3.36** | +3.59 | −4.88 | −23.22 |
| sd | 4.91 | 3.74 | **1.45** | 4.56 | **12.71** | 9.26 |

⭐⭐ **The tail is the BIAS POINT COLLAPSING, and it is an artefact of sweeping `Vov` while holding
P1/P2's `gm`.** The square-law self-bias solve is a one-parameter family, so raising `Vov` raises
`Id`, which drops the drain:

| `Vov` at gm = 1553 µS | 0.4469 | 0.7000 | 0.9300 | 1.2000 |
|---|---|---|---|---|
| IDSS | 5.00 mA | 7.83 | 10.40 | 13.42 |
| Vds | 13.12 V | 8.08 | 3.51 | **−1.86 V** |

At 1.2 the quiescent Vds is NEGATIVE — the stage is fully in triode at rest — and everything above
0.4469 exceeds the datasheet's IDSS ≤ 5 mA. 📌 **The shipped 0.4469 is exactly the datasheet
ceiling at this `gm`** (IDSS = 5.00 mA), which is how it was chosen (note #8).

⭐⭐ **BUT AT P4's OWN `gm` THE SAME `Vov` IS PERFECTLY HEALTHY, and this is the coupling that
matters.** A lower `gm` needs a smaller `Id` for the same `Vov`, so it raises BOTH the admissible
`Vov` and the quiescent drain:

| at `Vov` = 0.93 | gm = 1553 µS (P1/P2, shipped) | gm = 1146 µS (P4 measured) |
|---|---|---|
| IDSS | 10.40 mA — over datasheet | **5.00 mA — exactly at it** |
| Vds | 3.51 V (Vds/Vov = 3.8, near triode at idle) | **8.36 V (Vds/Vov = 9.0, healthy)** |

➡ **So note #22's "`Vov` ≈ 0.94 at P4's own `gm`" is physically admissible and stands — but the
sweep cannot demonstrate it, because every point above ~0.7 was run with the wrong `gm` and is
measuring a collapsing bias point rather than a curvature.** The two parameters must be moved
TOGETHER.

⛔ **What this does NOT change:** the refutation of note #16's 0.126–0.165 is untouched — those
points are on the clean, monotone part of the sweep and read −9.5 to −13.5 dB. **Do not apply them.**

➡ **What the next session needs: a `--gm` measurement flag on `OfflineRender`**, exactly parallel to
`--vov` and for the same stated reason ("every other quantity in the device model is derived from it,
so it cannot be swept from outside the stage"). Then sweep the (gm, `Vov`) pair with `gm` pinned at
P4's measured 1146 µS. ⚠ Note that pinning `gm` to P4 conflicts with the recorded decision to voice
to P1/P2 — which is fine, because this is a MEASUREMENT of P4's `Vov`, not a voicing choice. Convert
the result back to the shipped `gm` through the invariant `Vov`·K0², and sanity-check that the
converted value keeps Vds/Vov above ~6.

### 23. ⭐⭐ THREE CONSTANTS APPLIED (2026-09-10) — and FOUR HARNESS FAULTS found on the way in

Instruments: `analysis/volume_sweep.py`, `analysis/p4_component_fit.py`, `analysis/absolute_gain.py`,
`analysis/unit_compare.py`. Code: `src/dsp/CircuitValues.h`, `src/PluginProcessor.h`,
`analysis/offline_render.cpp`. **All 11 tests pass, warning-free.**

This is the session that finally moved constants rather than measuring them. Two of the four harness
faults below would have silently corrupted the very numbers being applied, so they are recorded
first — the fixes are the load-bearing part.

#### ⚠⚠ FAULT 1 — THE RENDER CACHE DID NOT INCLUDE THE PLUGIN BINARY

Every analysis script renders the plugin per capture and caches the result. The cache key was a hash
of the render ARGUMENTS — which was itself a fix, for a key that had been a display tag (note #21's
`--self-test` reading −1.382 dB where the answer is exactly 0.000). **This is the same fault one
level up: the BINARY is an implicit input to every render, and it was in no key.** Change a DSP
constant, rebuild, re-run a fit, and the arguments are identical — so every script compares the new
captures against the OLD plugin and reports it as a measurement.

⭐ **The tell was a column that did not move.** After the taper went 2.0 → 2.30, `volume_sweep.py`'s
plugin levels came back **byte-identical** to the pre-change run. A constant that changes the model
and moves nothing is the signature. With the cache fixed, the same column moved by up to 5.1 dB.
➡ `captures.render_bin_key()` now hashes the binary into every cache key
(`p4_corners.render`, `probe_compare.render`). **Any future measurement flag must go in the key too.**

#### ⚠⚠ FAULT 2 — THE PUBLISHED FITS WERE STALE: THREE CAPTURES LANDED AFTER THEM

`p4_V0800_dark.wav` (10:02), `p4_V0800_bright.wav` (10:07) and `p4_V1030_dark_pad4p5.wav` (10:31)
post-date `p4_component_fit_dark.json` (10:00) and `volume_sweep_dark.json` (10:01); the bright
component fit (09:30) also predates the 15:00 and 17:00 bright takes. So the taper numbers this
session inherited were fitted on an incomplete set. ➡ **Check capture mtimes against report mtimes
before trusting any fit that a previous session wrote down.** `*.wav` is gitignored, so version
control cannot show this.

#### ⚠⚠ FAULT 3 — 8:00 IS x = 0.100 EXACTLY, AND TWO SCRIPTS DISAGREED ON WHETHER THAT IS EXCLUDED

`volume_sweep.py` and `absolute_gain.py` exclude on `x > 0.1`; `p4_component_fit.py` excluded on
`volume_clock <= 730`. The two rules differ on **precisely one capture**, and it is the one that
arrived. Including 8:00 pulled the LF taper fit **2.281 → 2.201** and sent the joint C10 fit from
97.8 nF to **114.3 nF, 14 % ABOVE nominal** — i.e. it would have reopened note #21's closed C10
question with a wrong answer.

⭐ **8:00 is excluded on its RESIDUAL, not on its leverage, which is the criterion this file keeps
insisting on.** At the pooled fit it reads **0.185 dB RMS / 0.692 worst** against 0.020–0.087 /
0.147–0.528 for every other position, and dropping it more than **halves** the pooled RMS
(0.0871 → 0.0414) where every other leave-one-out moves it by under 0.01. The physical cause is
7:30's: the 1 kHz control law moves **±6.75 dB (7:30) and ±2.98 dB (8:00) per ±10 min of knob
error**, against ±1.02 at 9:00 and ±0.27 at 10:30. ➡ The threshold is now `x <= 0.1` in all three.

#### ⚠ FAULT 4 — `TAPER_P` WAS A SECOND DEFINITION OF A SHIPPED CONSTANT

`lf_pole_attribution.py` hardcoded `TAPER_P = 2.0`, and six scripts route their output network
through that module. Worse, the LF and midband taper fits are both **reparameterisations off it**
(`x ** (taper / TAPER_P)`), so a stale copy returns a confident, well-fitting, wrong exponent.
It now parses `kVolumeTaperP` and `kC10` out of `CircuitValues.h`, the same way
`p4_corners.plugin_known()` parses `JfetStage.h`. Same rule as `OsDroopRestore`'s one definition.

#### 1. ✅ `gm` RE-CHECKED UNDER THE RECORDED DECISION — NO CHANGE, AND THAT IS THE FINDING

Voicing to P1/P2 means `gm` comes from the rig-free mode differential, so P4's better rig buys
nothing here. A **second, independent estimator** (`unit_compare.py`'s two-parameter shelf fit)
against phase-1's M2 plateau fit:

| | P1 | P2 | mean | gm |
|---|---|---|---|---|
| phase-1 M2 (plateau) | K0 6.462 | 6.721 | **6.5912** | 1553.1 µS ← shipped |
| unit_compare (shelf fit) | K0 6.309 | 6.906 | **6.6072** | 1557.5 µS |

⭐ **The two agree on the MEAN to 0.021 dB while disagreeing on the SPLIT by 0.15–0.19 in K0.** So
the quantity the model actually uses is well determined and the per-unit attribution is not. **`gm`
stays at 1.5531069 mS.** Checked, not assumed — which is what the handover asked for.

#### 2. ⭐⭐ `kVolumeTaperP` 2.0 → **2.30**, from two independent bands

| band | knobs | fitted p | worst | RMS |
|---|---|---|---|---|
| midband control law, 1 kHz | 6 | **2.330** | 0.263 | 0.157 |
| LF corner shape 15–400 Hz, DARK | 6 | **2.281** | 0.408 | 0.0414 |
| LF corner shape 15–400 Hz, BRIGHT | 6 | **2.276** | 0.404 | 0.0403 |

⭐ The two LF rows are **the known-answer probe, not a third sample**: below the 1.9 kHz shelf zero
every mode has `Zs = R5`, so the LF band must return the same taper whichever mode it is fitted from.
They agree to **0.005 in p and 0.001 dB in RMS**. Shipping 2.30 costs +0.001 dB RMS against the LF
optimum and +0.026 dB against the midband one. The old 2.0 cost 2.076 / 0.956 dB on the control law.
✅ Peak still at **13:25–13:28 computed, 13:30 measured**, inside the maker's 1–2 o'clock — the one
published control point still load-bearing, and it survives. ✅ Fall-back 3.86 measured / 3.91
predicted.

#### 3. ⭐⭐ `kOutputMakeup` 1.0 → **1.1562 (+1.261 dB)** — the first anchor it has ever had

Six knob positions 9:00→17:00, **sd 0.175 dB, spread 0.424 dB**, duplicate takes averaged per
position. ✅ **Verified by re-measurement**: with the constant applied the same instrument reads
**+0.000 dB = ×1.0000**, and the scatter is unchanged, as it must be for a pure level shift.

⚠⚠ **FITTED FROM DARK ONLY, and that is not fussiness.** P4's JFET is ~25 % weaker than this model's
(K0 5.06–5.19 vs 6.59) and the recorded decision is to voice to P1/P2, so the gap is permanent. It
lives in the mode shelf, whose 1.9 kHz zero sits inside the top of the 200–2000 Hz fit band; in DARK
the shelf does not exist at all. It shows up exactly as predicted — **bright reads 0.09–0.18 dB lower
at every knob position from 9:00 up** (mean +1.147 vs dark's +1.261). Pooling the modes would fold a
voicing decision into a level constant.

⚠ **It differs from the +1.332 dB previously recorded, and the difference is real, not a discrepancy
to reconcile.** That figure was taken with p = 2.0 in the plugin and a taper correction applied
externally, over a pooled set, before the three late captures existed. ⭐ **The taper is what makes
this measurable at all: at p = 2.0 the per-position scatter of the makeup was 2.73 dB; at 2.30 it is
0.42 dB.** A 6.5× collapse in the scatter of a quantity that must be constant is the strongest
independent confirmation the taper is right — stronger than either taper fit's own residual.
➡ **`gm` and `kVolumeTaperP` both feed this. If either moves, re-run `analysis/absolute_gain.py`.**

#### 4. ⭐ `OfflineRender --gm S`, AND A BIAS-POINT GUARD THAT REFUSES THE COLLAPSED CORNER

`--gm` sits beside `--vov` for the reason note #22a gives. ⭐ Setting it also rebuilds the shelf,
because `setParams()` calls `updateShelf()` and the discrete source one-port is derived from the
shelf coefficients (note #12) — so a `gm` sweep moves the model's mode differential to match the
unit under comparison, which is precisely what fitting `Vov` against P4 requires.

⭐⭐ **AND IT NOW PRINTS THE IMPLIED OPERATING POINT AND REFUSES AN IMPOSSIBLE ONE.** Note #22a's
whole finding was that a sweep can spend an hour rendering points whose bias has collapsed and then
be read as a curvature measurement. Reproduced exactly, from the flag itself:

| (gm, Vov) | Id0 | IDSS | Vds_q | verdict |
|---|---|---|---|---|
| shipped, 0.93 | 722 µA | 10.40 mA | 3.51 V | runs, **2 warnings** (Vds_q/Vov = 3.78; IDSS over datasheet) |
| shipped, 1.20 | 932 µA | 13.42 mA | **−1.86 V** | **REFUSED, exit 1, no file written** |
| **1146 µS, 0.93** | 533 µA | **4.999 mA** | **8.36 V** | healthy — note #22a predicted 5.00 mA / 8.4 V |

`Vds_q <= 0` is refused outright (it is not an amplifier); `Vds_q/Vov < 6` and an IDSS outside the
datasheet's 1–5 mA are warnings on stderr, so a sweep's own log carries the reason a tail point is
untrustworthy. `probe_compare.py --gm` passes it through.

### 24. ⭐⭐ THE (gm, Vov) PAIR SWEPT TOGETHER AT LAST — `Vov` STAYS, and the ONSET is now mapped

`analysis/probe_compare.py --gm --vov`, against P4's 15 probe captures at matched drive.
**No constant changed.** This is the measurement note #22a asked for and it answers it three ways.

#### ⭐ 1. The sweep is MONOTONE at P4's own `gm`, which confirms note #22a's diagnosis outright

Mean H2 delta (pedal minus plugin) over 80 cells at −12 dBFS and above, `gm` pinned at P4's
measured 1146 µS:

| `Vov` | 0.30 | 0.45 | 0.60 | 0.75 | 0.93 |
|---|---|---|---|---|---|
| mean | −13.19 | −8.03 | −3.89 | −0.26 | **+2.52** |
| sd | 4.58 | 2.60 | **1.09** | 2.64 | 3.75 |

No reversal anywhere. The tail non-monotonicity note #22 recorded at the shipped `gm`
(−3.36 → +3.59 → **−4.88** → −23.22) really was the bias point collapsing, not a curvature optimum.
✅ And it is now impossible to reproduce silently: `OfflineRender` refuses a pair whose quiescent
Vds is negative and warns when Vds_q/Vov < 6 or the implied IDSS leaves the datasheet.

#### ⛔ 2. THREE CRITERIA, THREE ANSWERS, SPANNING 2× — so no single `Vov` is the answer

At `gm` = 1146 µS the mean H2 delta crosses zero at **`Vov` ≈ 0.78**, the scatter minimises at
**0.60**, and the drive SLOPE crosses zero near **0.4**. The drive slope is the new one, and it is
the diagnostic:

| at gm = 1146 µS | −12 dBFS | −8 | −4 | −1 | slope |
|---|---|---|---|---|---|
| `Vov` = 0.45 | −7.21 | −8.93 | −8.89 | −7.08 | +0.14 dB |
| `Vov` = 0.60 | −4.04 | −4.21 | −4.13 | −3.18 | +0.87 dB |
| `Vov` = 0.77 | −1.59 | −1.19 | +0.73 | +3.40 | **+4.99 dB** |
| **shipped pair** (1553 µS, 0.4469) | −2.56 | −3.14 | −4.16 | −3.62 | **−1.06 dB** |

⭐⭐ **The drive dependence CHANGES SIGN between the shipped pair and P4's**, so a pair that flattens
it exists — the onset is a fittable two-parameter problem, not a structural gap. ⛔ **But it is not
the same pair that zeroes the mean.** Flattening the slope costs 8–9 dB of mean H2; zeroing the mean
costs 5 dB of slope. ➡ **The model's H2-versus-drive CURVE has the wrong SHAPE, not merely the wrong
scale**, which is note #22's "one `Vov` does not fix it" made quantitative. Fixing it is a modelling
change to the clipping onset, and `Vov` must not absorb it.

#### ✅ 3. Converted back, the measurement LANDS ON THE SHIPPED VALUE

Through the invariant `Vov·K0²` (K0 = 5.126 at P4's gm, 6.591 at the shipped one):

| criterion | `Vov` at P4's gm | converted to the shipped gm |
|---|---|---|
| drive slope zero | ~0.40 | ~0.24 |
| scatter minimum | 0.60 | 0.363 |
| **mean H2 zero** | **0.78** | **0.47** |

**The shipped 0.4469 sits inside that bracket and within 4 % of the mean-zero answer**, from a route
note #22 could not run because it held the wrong `gm`. ➡ **`Vov` stays at 0.4469.** Every reason
note #10 gave for not moving it is now either discharged or resolved in favour of the shipped value.

⚠ **The invariant is APPROXIMATE in the load-line region and must not be used to transfer a fitted
value silently.** Direct sweeps put mean-zero at ≈0.57 at the shipped gm and 0.78 at P4's; the
conversion maps 0.78 → 0.47, i.e. **22 % low**. `Vov·K0²` is derived from the small-signal
`H2/H1 = A/(4·Vov·k²)` and the captures that matter here are past that regime by construction.
Convert to get a bracket; re-run the sweep to get a value.

### 25. ⭐⭐ `goal_check.py` ANCHORS TO P4 NOW — and DARK meets every linear target

Rewritten this session: `--unit` (default **p4**), the rig deconvolved, the interface load undone in
MAGNITUDE **and PHASE**, matched drive, capture names printed, and a new **absolute-level** section.

⚠⚠ **THE CAPTURE-SIDE CABLE LOAD HAD TO GO IN OR THE RIG READS AS A MODEL DEFECT.** Note #21
measured ~99 pF at the pedal's output and recorded that the bypass deconvolution **structurally
cannot remove it** — in bypass the cable is driven by the interface's low output impedance, in an
active capture by the pedal's 59–102 kΩ, so the pole exists in one path and not the other. Without
it, `goal_check` reported HF misses **growing with the VOLUME knob** (up to +2.69 dB at 10 kHz),
which is exactly what a real knob-dependent model error would look like — the pedal's Zout moves
1.7× across the rotation. With it:

| DARK, core band 80 Hz–12 kHz | before | after |
|---|---|---|
| 9:00 | 0.06 RMS / 0.15 worst | **0.02 / 0.04** |
| 10:30 | 0.32 / 1.18 | **0.03 / 0.07** |
| 12:00 | 0.42 / 1.46 | **0.08 / 0.22** |
| 17:00 | 0.42 / 1.40 | **0.07 / 0.20** |

⭐⭐ **Every DARK capture from 9:00 to 17:00 now PASSES the ±0.5 dB core-band target outright, at
0.04–0.22 dB** — an order of magnitude inside it. ⛔ This is a CAPTURE-side correction only; the
2026-09-08 decision to ship no load capacitance in the plugin stands, because the plugin's output
goes to a DAW digitally.

**Where the three linear targets stand against the owner's own pedal, 8× OS:**

| target | result |
|---|---|
| FR ±0.5 dB, 80 Hz–12 kHz | ✅ **DARK passes at every knob position 9:00–17:00 except 13:30** (0.64) |
| phase ±5° | ✅ **worst 3.90° over 200 Hz–12 kHz; every DARK row passes across 20 Hz–20 kHz** |
| absolute level ±0.5 dB | ✅ **all 15 captures pass**; dark mean −0.039 dB, bright −0.069 |

⭐ The absolute-level row is new and could not have existed before this session: it is the anchored
`kOutputMakeup` checked per capture rather than as the mean it was fitted from.

#### ⭐⭐ `phase_sweep.py` NEEDED THE SAME CORRECTION, AND UNCORRECTED IT READ 15× TOO HIGH

Run against P4 with no capture-side correction, its absolute phase residuals came back **17.4–22.7°
RMS**, against P1's 2.7–4.0° — which reads as the model being far worse on the owner's own unit than
on a NAM model. It is the same ~99 pF: the interface's 1 MΩ shunted by that cable is **29.8° at
10 kHz and 40.7° at 15 kHz** off the pedal's 59–102 kΩ output impedance, and the band runs to 15 kHz.
With the rig and the load removed:

| P4 absolute phase residual, 30 Hz–15 kHz | uncorrected | corrected |
|---|---|---|
| DARK 9:00 | 20.56° RMS | **0.13°** |
| DARK 10:30 | 20.68 | **0.23** |
| DARK 12:00 / 13:30 / 15:00 / 17:00 | 19.8–22.7 | **0.72 / 1.88 / 0.89 / 0.65** |
| BRIGHT 9:00 → 17:00 | 17.4–19.6 | **1.05–2.90** |

⭐ **Corrected, P4's DARK rows beat P1's by roughly 10×** (0.13–1.88° against 2.74–4.01°), which is
what a raw capture through a measured rig should do against a neural model.

⭐⭐ **AND THE REFACTOR VALIDATES ITSELF.** Both corrections now live in `p4_corners`
(`loading_correction_complex`, `CABLE_PF`) and `goal_check.py` and `phase_sweep.py` only call them —
one definition, per note #23's fault 4. Two free checks confirm it: `goal_check`'s whole output came
back **byte-identical** after being re-pointed at the shared function, and **the MODE DIFFERENTIAL
did not move at all** (P4 1.39° SD, 4.94° peak, before and after). The differential is a ratio within
one unit, so the rig and the cable must cancel in it identically — a correction that changed it would
have been applied inconsistently between the two modes.
⚠ **`loading_correction_db`'s cable term is OPT-IN (`cable_pf = 0` by default) and that is
deliberate.** The taper, C10 and `kOutputMakeup` were all fitted against the resistive-only
correction, and `p4_component_fit.py` fits the cable itself as a free parameter on top of it —
folding it in by default would double-count there and silently perturb three applied constants. It is
worth 0.014 dB at 1 kHz and 2.08 dB at 10 kHz, so the two bands genuinely need different answers.

⭐ **The MODE differential is confirmed in phase on the owner's own unit**: P4 bright/dark reads
**1.39° SD against its own measured 3.19° floor**, i.e. below the floor, sitting between P1
(1.34–1.63°) and P2 (0.59–0.69°). Same known-answer probe as always — below the shelf zero every mode
has `Zs = R5`, so the differential must read 0°.

⛔ **BRIGHT misses at 6.5–10 kHz (up to +2.69 → +1.51 dB after the cable fix) and that is the
RECORDED VOICING DECISION, not a defect.** P4's K0 is 5.06–5.19 against the shipped 6.59, the model
is voiced to P1/P2, and the whole difference lives in the mode shelf's own band. **Do not move `gm`
to close it.** ➡ Read DARK as the model's error and BRIGHT's HF as the unit difference.
⚠ 7:30 and 8:00 miss in every mode — knob-slope error (note #23), not model error.

### 26. ⭐⭐ THE TRANSFER LAW IS NOT SQUARE (2026-09-10). m = 1.60, and |Vp| replaces Vov.

`analysis/onset_fit.py` (new), `analysis/reports/onset_fit.json`, code and reasoning in
`src/dsp/JfetStage.h`. This closes note #24's one remaining substantive modelling gap — "the model's
H2-versus-drive CURVE has the wrong SHAPE, not the wrong scale" — and the answer is that the device
law itself was wrong, not any of its constants.

⭐⭐ **THE INSTRUMENT IS WHAT MADE IT TRACTABLE, AND IT IS REUSABLE.** In DARK the source one-port is
the bare resistor with **no state** (note #12's algebra collapses to it), so the whole stage is
MEMORYLESS. One period of gate sine through the solve is therefore the EXACT steady-state spectrum:
no oversampling, no discretisation, no filter state. That oracle reproduces the shipped plugin at
8× to **0.01 dB** — which is the known-answer check note #9 demands — and it runs in 6 ms per cell
against ~40 s for a render, so a two-parameter family can actually be explored.

**THE OBSERVATION.** Against P4's DARK tone ladder no `Vov` removes the residual, because the two
ends of the drive range want different values:

| cells | what they pin | Vov they want, at P4's gm |
|---|---|---|
| below cutoff | small-signal curvature | ~0.90 |
| past cutoff | clipping onset | ~0.63 |

⛔ **Drain-side mechanisms are eliminated outright, and the dataset does it for free.** The residual
at a given GATE DRIVE is the same across the whole rotation — 15× in drain load (0.50 kΩ at 7:30
against 7.53 kΩ at 9:00) moves it by ≤ 0.07 dB. Anything acting through the load line must scale
with that impedance. ⛔ **A softened cutoff is refuted by measurement too**: a subthreshold
(soft-plus) knee was swept from 0 to 0.22 V of slope at every Vov and is best at exactly zero.

⭐ **WHY AN EXPONENT, AND WHY THE FIT IS WELL POSED.** Three parameters, three independent
observables, no shared leverage:

- **Cutoff onset is exactly |Vp| in gate volts**, whatever gm and m are — at cutoff the device
  passes nothing, so the source sits at −R5·Id0 and the gate must reach −(Vov + R5·Id0) = −|Vp|.
  Every subset of the captures puts it at **1.90–2.02 V**.
- **Small-signal curvature then pins m**: H2/H1 goes as (m−1)(m + gm·R5) / (m·|Vp|·K0²).
- **gm is already measured**, from the rig-free mode differential.

⛔ **AND THE SQUARE LAW CANNOT BE RESCUED BY MOVING gm.** Freeing gm with m pinned at 2 leaves the
residual **36 % worse** than freeing m (RMS 1.36 dB against 1.00) and demands **K0 = 6.79 against
P4's measured 5.126** — a 2.4 dB error in the mode plateau, seven times M6's entire unit-to-unit
band, from a fit whose own residual is 0.03 dB. With m free, gm wants to stay within 0.04 dB of
where it was measured. So the two are not degenerate.

**⭐⭐ THE EVIDENCE THAT IT IS STRUCTURE.** Note #22 is the cautionary case: a free parameter always
improves the observable it was fitted to, and `Vov` was once fitted confidently off a floor. Two
things separate this:

1. **Fitted on H2 at 220 Hz only**, then scored against five observables that were not fitted. All
   five improve, and their OFFSETS go to zero rather than merely shrinking:

   | observable | shipped square law | m = 1.60 | (offset / shape-RMS, dB) |
   |---|---|---|---|
   | H2 @220 Hz (fitted) | −8.28 / 2.55 | **+0.15 / 0.63** | |
   | H3 @220 Hz | −11.04 / 6.29 | **+0.66 / 1.35** | a different ORDER |
   | H2 @3150 Hz | −9.06 / 2.52 | **−0.36 / 0.62** | a 14× different FREQUENCY |
   | H3 @3150 Hz | −11.10 / 6.39 | **−0.21 / 1.47** | |
   | compression @220 Hz | +0.61 / 0.42 | **+0.10 / 0.09** | read on the FUNDAMENTAL |
   | compression @3150 Hz | +0.74 / 0.44 | **+0.21 / 0.11** | |

2. ⭐ **It makes the OTHER parameter agree across disjoint subsets**, which a spent degree of freedom
   does not do. Fitting the pad-0 captures and the padded ones separately — no shared cells,
   different knob positions, different drives:

   | | pad-0 only | padded only | apart |
   |---|---|---|---|
   | m pinned at 2.0 | \|Vp\| = 2.237 | 2.960 | **32 %** |
   | m free | 2.023 (m 1.65) | 1.904 (m 1.53) | **6 %** |

   Across all nine subsets tried — per frequency, per pad, per volume half, per drive region, and
   H3 alone — **m returns 1.46–1.71 and |Vp| 1.70–2.02**.

📌 **The residual profiled against a PINNED m has a real minimum, not a plateau** (which is what
`dsp.md` says to check before believing a fit): 1.4 → 1.96 dB RMS, 1.5 → 1.25, 1.55 → 1.07,
**1.60 → 1.00**, 1.65 → 1.04, 1.7 → 1.13, 1.8 → 1.40, 2.0 → 1.98. So m is identified to ~±0.1.

⚠ **WHAT THIS IS NOT.** `m` is an empirical transfer-law exponent, not a derived physics constant.
Shichman-Hodges' m = 2 is itself an approximation to the gradual-channel solution, and real JFET
transfer curves are routinely fitted with exponents from ~1.5 to ~2.5; this unit sits at the low
end. **Nothing here claims to know why.** The triode branch `beta*(Vov_i^m − (Vov_i − Vds_i)^m)` is
the unique generalisation that keeps the solve's guarantees (it reduces to Shichman-Hodges at m = 2,
meets saturation with dI/dVds = 0 so the map stays C1, and keeps both partials non-negative so the
root stays unique) — forced, not chosen to look smooth.

#### 26a. ⭐ |Vp| REPLACES Vov AS THE PARAMETER, AND IT RETIRES NOTE #22a's TRAP BY CONSTRUCTION

Self-bias gives `Vov = |Vp| / (1 + gm·R5/m)`, hence `Id0 = gm·|Vp| / (m + gm·R5)` — **bounded above
by |Vp|/R5 however large gm gets.** Under the old (gm, Vov) parameterisation `Id0 = gm·Vov/m` grew
without limit, which is exactly how a Vov sweep at a fixed gm walked the quiescent drain NEGATIVE
and had an hour of renders read as a curvature measurement. ➡ **The bias point can no longer
collapse at all**, so `OfflineRender`'s guard is belt-and-braces rather than load-bearing.

⚠⚠ **AND A RECORDED CLAIM IS WRONG: THE STAGE CLIPS AT CUTOFF FIRST, NOT TRIODE.** `JfetStage`'s
`triodeOnsetGateVolts()` solved for the gate-SOURCE drive and converted to gate volts with the
**small-signal K0** — but at triode onset the stage is nowhere near small signal, and the real
conversion is `vGate = w + R5·i` with the large-signal current. It read **1.691 V** where the true
onset at the shipped drain load is ~2.4 V. 📌 **And 1.691 V is very nearly the true CUTOFF onset**
(1.696 V under the old constants), so "triode is entered first, at −7.5 dBFS" — repeated through
notes #16 and #22 and into the capture-session planning — was a mis-converted triode figure that
coincided with the correct cutoff figure. Fixed, and `cutoffOnsetGateVolts()` now exists and is
exactly `|Vp|`, with no solve at all.

#### 26b. THE SOLVE: one unknown, Halley, and a bracket that was inverted

The closed form is gone from production: it is exact only for a SQUARE law, where substituting the
source one-port and the load line leaves a quadratic in the drain current. At m ≠ 2 the same
substitution leaves a transcendental equation.

⭐ **But in SATURATION the whole stage collapses to one scalar equation** in the instantaneous
overdrive: `u + c·u^m = P` with `c = Rd·beta` and `P = Vov + vGate − vOff + Rd·Id0`, after which
`i = (A − u)/Rd` needs no further `pow`. That function is convex and increasing for m > 1 and the
bracket (0, P] is free. Against a 300-step bisection over −14 V to +14 V of gate drive at five
values of Rd:

| iterations | 2 | 3 | 4 |
|---|---|---|---|
| Newton | 9.6e-08 | 8.9e-11 | 7.6e-17 |
| **Halley** | 6.9e-11 | **1.6e-18** | 1.6e-18 |

⭐ Halley gets h, h′ and h″ out of the SAME single `pow`, so its extra order of convergence is free.
**Three Halley steps is exact.** Two were measured and rejected: 6.9e-11 A is inaudible, but it
costs 1 point of chain CPU to buy a solve that can be asserted at machine precision, and every
measurement this project makes compares the model against something else.

⚠⚠ **THE WORST CASE IS AT THE CUTOFF KNEE, NOT AT FULL DRIVE** — every iteration count fails worst
at a gate of about −1.78 V, just inside cutoff, where P is small, the linear-root start is 2.3×
above the root and h″ goes as u^(m−2) → ∞. The first version of this solve was checked over
0.05–4.016 V and read exact.

⚠⚠ **A REAL BUG, CAUGHT BY THE BISECTION ORACLE:** the triode fallback bracketed `[iSat, B/R]`,
which is the shape the saturation branch suggests and is INVERTED whenever the saturation root
exceeds the drain-bottomed current — which is what every loud half-cycle does. It returned `iSat`
and the stage read **980 µA against the oracle's 458** at a 0 dBFS peak. Both ends are upper bounds;
the bracket is `[−Id0, min(iSat, B/R)]`.

⚠ **AND A TEST WAS BRIEFLY VACUOUS.** `JfetStageTest` 8c(b) sweeps an iteration count, but the fast
solve's counts are compile-time constants, so sweeping `setSolveIters()` moved only the oracle: it
printed 0.0000 dB in every row, which reads as a converged solve and was a test comparing the
shipped path with itself. It now compares the shipped solve against a **different algorithm** run to
convergence, plus a dense per-sample sweep — **and it measures that reference's OWN floor first**,
because on this comparison the reference is the less accurate side (the generic Newton is 7e-11 A
short at the +12 V step where the shipped solve is exact). Same asymmetric-comparison trap that
failed a correct shelf design and a correct droop restore.

📌 **CPU: 6.7–7.5 % of realtime at the 4× default and 12.8–14.4 % at 8×**, against 2.3 % for the
square law's closed form. It is all `std::pow` — three per sample. `exp2((m−1)·log2(u))` was
benchmarked at 6.3 ns against `pow`'s 9.2 ns, about 1.4 points of chain CPU, and NOT taken: it costs
a digit or two of the machine-precision agreement, and `build.md`'s rule is that the profile decides.

#### 26c. ⚠⚠ WHAT IT COSTS AGAINST THE OWNER'S OWN PEDAL — a voicing consequence, now measured

The device parameters are measured on P4; the recorded decision voices the model to P1/P2, whose gm
is 36 % higher. Transplanting P4's (m, |Vp|) onto their gm re-solves the bias to **Vov = 0.4328 V,
Id0 = 420 µA, IDSS = 4.65 mA, Vds_q = 11.25 V** — every one inside the datasheet. ⭐ It is coherent
as a PARTS BIN: one pinch-off and one exponent, with **IDSS the only thing that differs** between
units (P1 → 4.31 mA, shipped mean → 4.65, P2 → 5.03), which is exactly what "cherry picked to
cream-of-the-crop specs" selects on.

➡ The consequence, measured over 64 probe cells (both modes, 220 and 3150 Hz, every knob position):

| model | vs P4 at P4's own gm | vs P4 at the SHIPPED gm |
|---|---|---|
| square law, as shipped before | mean −7.96 dB, sd 2.73 | mean −3.36, sd 1.36 |
| **m = 1.60, \|Vp\| = 1.942** | **mean −0.44 dB, sd 0.63** | **mean +1.26, sd 1.53** |

**At the unit's own gm the device model is now within half a dB with a 0.63 dB scatter.** The
+1.26 dB at the shipped gm is the voicing decision, not model error: small-signal H2/H1 goes as
(m−1)/(Vov·K0²), and moving to P1/P2's gm raises K0 while LOWERING Vov, the two partly cancelling to
**−2.39 dB** predicted against +1.9 to +2.8 dB measured at the sub-cutoff cells.
⚠ **NOT 4.4 dB**, which is what K0² alone gives and what a first draft of `JfetStage.h` said — that
figure belongs to transplanting P4's *Vov* rather than its *|Vp|*, and is wrong for what was built.
➡ Same class as BRIGHT's 6.5–10 kHz miss (note #25). **If the owner would rather the plugin match
their own pedal's distortion, the change is one constant (`gm`) and the decision is theirs.**

⭐ **Compression came along for free and was never fitted**: at 10:30, DARK, the top cell now reads
**−0.326 dB against the pedal's −0.285** (delta +0.04) where the square law read −0.802 (delta
+0.52); at 3150 Hz the delta goes +0.51 → +0.10.

#### 26d. 📌 `parse_capture()`'s `load_ohms` field must be `None`, not `float("inf")`

⚠ Several scripts `json.dump()` the parsed
settings dict straight into a report, and Python writes a bare `Infinity` there — which is not valid
JSON and which `JSON.parse()` in the dashboard rejects outright. It had already put 28 of them into
`comprehensive_data.json` before it was caught. `None` round-trips as `null`. ➡ **Anything added to
`parse_capture()`'s return value is serialised by callers you did not write; keep it JSON-safe.**

### 27. ⭐⭐ REVALIDATION AFTER THE TRANSFER-LAW CHANGE (2026-09-11) — and the TRIODE BRANCH rests on ONE cell

Note #26 replaced the device law (square → `m` = 1.60, `Vov` → `|Vp|`), which also moved `ro` and the
bias point. **Every downstream validation and the `kOutputMakeup` anchor were measured under the OLD
law**, so nothing about the shipped model had actually been checked end to end. This note is that
check. **No constant changed, and none should.** All 11 tests pass, warning-free.

#### ✅ 1. `kOutputMakeup` SURVIVES THE CHANGE — re-measured, not assumed

Note #23 records the coupling: `gm` and `kVolumeTaperP` feed this constant, so it must be re-run if
either moves. The device law is a third input nobody had listed — it moved `ro` 1.4407 → 1.1921 MΩ
and the whole operating point. Re-measured (`analysis/absolute_gain.py`, DARK only, x > 0.1, n = 6):

| | residual | sd | spread |
|---|---|---|---|
| with `kOutputMakeup` = 1.1562 applied | **+0.019 dB (×1.0022)** | 0.178 | 0.437 |
| recorded under the square law | +0.000 dB | 0.175 | 0.424 |

**The whole device-law change is worth 0.019 dB of absolute level — one tenth of the scatter.** So the
constant stays at 1.1562, and note #26's "the `ro` re-derivation is worth 0.03 dB" is confirmed by
measurement at 0.019 dB. ✅ Both known answers still hold: the loop reads −0.026 dB and the true
bypass −0.015 dB, where a wire and a true bypass must each read exactly 0.000.

⭐ **And two independent instruments agree on that 0.019 dB to a millidB.** `goal_check.py`'s
absolute-level section is a per-capture check rather than the mean the makeup was fitted from, and its
DARK mean moved **−0.039 → −0.021 dB, i.e. +0.018 dB**, against `absolute_gain.py`'s +0.019. Neither
can inherit the other's arithmetic.

#### ✅ 2. THE LINEAR TARGETS ARE BIT-FOR-BIT UNMOVED, which is the prediction

Note #26 asserts the small-signal response is preserved to 1.6e-10 dB / 4.7e-10° — but that was
measured at the STAGE, against its own prototype. This is the whole chain against real captures:

| DARK core band 80 Hz–12 kHz, RMS / worst | note #25 (square law) | now (m = 1.60) |
|---|---|---|
| 9:00 | 0.02 / 0.04 | 0.02 / 0.05 |
| 10:30 | 0.03 / 0.07 | **0.03 / 0.07** |
| 12:00 | 0.08 / 0.22 | **0.08 / 0.22** |
| 17:00 | 0.07 / 0.20 | **0.07 / 0.20** |

Phase worst over 200 Hz–12 kHz: **3.90° both times.** Absolute level: **all 15 captures PASS**.
Every DARK row passes the FR core band from 8:00 to 17:00 except 13:30 (0.64), exactly as recorded.

⭐ **One thing is BETTER than note #25 claimed, and only because the full-band column was read this
time: DARK passes the whole 20 Hz–20 kHz ±1.0 dB target — zero bands over — at 9:00, 10:30, 12:00,
15:00 and 17:00.** Note #25 only ever asserted the 80 Hz–12 kHz core. 20 Hz reads −0.16 to −0.24 dB at
those positions, so note #21's taper and as-drawn C10 really did close the LF story; the remaining
misses are 8:00 dark marginally (−1.08 dB at 20 Hz, and 8:00 is excluded for knob slope anyway),
13:30's core HF, and BRIGHT's voicing gap. ➡ **There is no general LF defect left to chase.**
➡ **A nonlinear re-derivation that leaves the linear targets identical is the evidence it was
confined to the nonlinear path.** Worth re-running for that reason alone, not just for the anchor.

#### ✅ 3. COMPRESSION IS CLOSED — and the raw column is NOT the statistic

Note #26 reported compression as a free by-product, from the memoryless oracle. Confirmed here
through real plugin renders (`probe_compare.py --unit p4`, shipped parameters):

⚠ **The captures' raw `comp_cap` reads POSITIVE (expansive) at mid levels — 0.06 to 0.19 dB — which
the circuit forbids.** That is note #16's level-independent reference gain error again, and reading
the raw column would score the model against it. The floor-free statistic is the **increment**:

| compression increment, −18 dBFS → top cell | value |
|---|---|
| plugin minus pedal, median | **−0.024 dB** |
| mean / sd | −0.126 / 0.214 |
| cells within ±0.29 dB | **14 of 16** |

⭐ **And the H2 figure reproduces the oracle exactly.** 64 cells at −12 dBFS and above:
**mean +1.26 dB, median +1.57, sd 1.53** — identical to note #26c's table, from a full plugin render
rather than the one-period solve. Two implementations, no shared code path.

#### ⛔⭐⭐ 4. THE FINDING: THE TRIODE BRANCH IS EXERCISED BY EXACTLY ONE CAPTURE CELL

The two cells that break the compression table are both `p4_V1700_dark_pad1p5` (plugin
over-compresses by **0.67 / 0.59 dB** at 220 / 3150 Hz, and under-produces H2 by **6.2 / 5.3 dB**).
⚠⚠ **That is not an outlier among comparable cells — it has no comparables.** Computed from the
shipped constants (throwaway probe, ~30 lines over `JfetStage.h`; `triodeOnsetGateVolts()` depends on
the drain load, which VOLUME sets):

| knob | drain load | gate at −1 dBFS | cutoff onset | triode onset | region |
|---|---|---|---|---|---|
| 7:30 | 0.50 kΩ | 3.58 V | 1.94 | 9.64 V | cutoff only |
| 8:00 | 2.22 kΩ | 3.58 V | 1.94 | 6.99 V | cutoff only |
| 9:00 | 7.57 kΩ | 3.58 V | 1.94 | 3.80 V | cutoff only |
| 10:30 | 13.61 kΩ | 2.13 V | 1.94 | 2.52 V | cutoff only |
| 13:30 | 17.58 kΩ | 1.90 V | 1.94 | 2.07 V | linear |
| **17:00** | **17.80 kΩ** | **3.01 V** | 1.94 | **2.05 V** | **TRIODE + cutoff** |

⭐⭐ **The rig ANTI-CORRELATES drive depth with drain load, because VOLUME sets both the load line and
the recorded level.** Every deep-drive capture sits at a low drain load; every high-load capture is
shallow. So one cell in the matrix enters triode, and it is the one that disagrees.

⚠⚠ **It is also the largest residual in the m-fit that shipped, and that was absorbed into an sd.**
`onset_fit.json`'s own cells: its delta is **+6.196 dB against a next-worst 2.82**, and dropping it
takes the reported scatter from **sd 1.358 to 0.966** — one cell in twenty carrying 29 % of it.
➡ So note #26's `m` and `|Vp|` are a **SATURATION-branch fit**. The triode branch
`beta*(Vov_i^m − (Vov_i − Vds_i)^m)` — which note #26 correctly describes as forced by the solve's
guarantees rather than chosen — has **essentially no measurement support**, and the single cell that
tests it is where the model is worst.

⛔ **Do NOT fit it. One cell cannot constrain a branch**, and the direction of both observables is
consistent (plugin over-compresses AND under-produces H2 = plugin clipping harder than the pedal),
so a curvature constant would absorb a structural error — the exact failure note #22 records.

⚠ **And a deeper cell CANNOT be captured at this VOLUME setting.** `p4_V1700_dark_pad1p5.wav` peaks
at **−1.18 dBFS**, 1.2 dB below the converter ceiling. The pedal's own compression (−1.52 dB at that
cell) is what keeps it inside at all. At 17:00 the pedal's gain is +6.7 dB, so driving the gate to
0 dBFS/pad 0 (4.02 V) would put ~8.7 V into a 5.68 V converter. **This is the same structural ceiling
already recorded for BRIGHT above 9:00, reached from the other direction.**

⭐ **It matters in play, so it is a real gap rather than a curiosity**: at 17:00 a user at 0 dBFS puts
**4.02 V on the gate against a 2.05 V triode onset**, i.e. the top of a normal take at a high VOLUME
setting is in the region with one supporting measurement.

➡ **WHAT WOULD SETTLE IT, and it is one item for any future capture session: a known RESISTIVE PAD ON
THE PEDAL'S OUTPUT, not on the input.** Input padding cannot work — it lowers drive and recorded
level together, which is what created the anti-correlation. An output pad decouples them, and a known
load is already handled (the `_load10k` takes measure `Zout` exactly that way, and
`loading_correction_complex` already undoes a load in magnitude and phase). ⚠ Record its value: it
loads a 59–102 kΩ source, so it changes the transfer and must be modelled, not ignored.

#### 📌 5. MID needs no action under the recorded decision

`tauBright`/`tauMid` = 85.369 / 38.263 µs is a ratio of **2.2311**, between the drawn 2.20 and the
measured 2.24 — and both values are P1/P2's own fits, which is the unit the model is voiced to. ➡ The
"scale MID by the measured cap RATIO 2.24" instruction bites only if the model is retuned to P4,
which the recorded decision declines. **No position is a blend of two pedals, as note #23 states.**

### 28. ⭐⭐ THE VOICING RULE IS SETTLED (2026-09-11) — and it is FORCED by the data, not chosen

Owner's decision, confirmed 2026-09-11. It does not supersede note #21's recorded decision — it is
the same decision given an operational form and an explicit tie-break, and it closes
`docs/build-plan.md` §19 item 2. **No constant moved; the shipped model already obeys it.**

**THE RULE.**
> Voice to the NEWER three-position units (P1/P2). Fall back to P4 — the owner's own two-position
> unit — wherever the newer units' data does not make sense, is ambiguous, or is a calibration
> question NAM structurally cannot answer.

⭐⭐ **Measurement sharpens that into something testable: P1/P2 win ONLY where the observable is a
within-unit RATIO. On every absolute axis P4 wins BY DEFAULT, not as a tie-break.**
`analysis/voicing_compare.py` compares the PEDALS against each other (no plugin render at all), and
the pairwise RMS agreement splits cleanly:

| axis | P1 vs P2 | P1 vs P4 | P2 vs P4 |
|---|---|---|---|
| **mode shelf (bright−dark) — rig-cancelling** | **0.30 dB** | 0.74 | 0.85 |
| absolute DARK FR shape, normalised at 1 kHz | **5.06 dB** | **1.02** | 4.16 |
| H2 @800 Hz, cells ≥ −16 dBFS, dark | 6.39 dB | **2.58** | 8.49 |
| H2 @800 Hz, cells ≥ −16 dBFS, bright | 13.14 dB | **1.83** | 11.49 |

⚠⚠ **Only the first row is a pedal comparison.** The lower three are absolute measurements across
three different rigs, and the THD rows are additionally NOT matched-drive (P2/P3's reamp levels are
unknown — note #10, and `voicing_compare.py`'s own docstring). **Their SIZE is the finding, not
their value:** "the newer pedals agree with each other" is true on the rig-free differential
(0.30 dB) and false by 5–13 dB everywhere else, because those axes are measuring trainer rigs, not
pedals. P4 lands next to P1 on all three, which is what a measured rig should do against an unknown
one. ➡ **The only axis on which P1/P2 constitute evidence at all is the one the rule assigns to
them.** That is why this is not a preference.

**WHAT IT BINDS. The split is clean — no position is a blend of two pedals (note #23).**

| from P1/P2 — the VOICING, all from the rig-free differential | from P4 — everything ABSOLUTE or STRUCTURAL |
|---|---|
| `gm` = 1.5531069 mS (K0 = 6.5912) | `kVolumeTaperP` = 2.30, `kC10` as drawn, `kOutputMakeup` = 1.1562, `kInputRef` = 4.4626 |
| `tauBright` = 85.369 µs, `tauMid` = 38.263 µs | `ro` = 1.1921 MΩ, the load line, the device law `m` = 1.60 / `\|Vp\|` = 1.942 |

⚠ **THE SEAM: the device law is P4's transistor transplanted onto P1/P2's `gm`**, and it is covered
by the rule's third clause rather than its first. NAM cannot measure it — P1's harmonic data is
floor throughout (note #7's M5), and the one time `Vov` was fitted off it the answer was refuted by
13–20 dB (note #22). P4 is the only unit that reaches the nonlinearity at all. Note #26c shows the
transplant is coherent as a PARTS BIN: one pinch-off, one exponent, IDSS the only thing differing.

**⛔ THE ACCEPTED PRICE — understood and accepted by the owner. These are NOT defects and must not
be "fixed", nor re-raised as open questions:**
- **BRIGHT runs up to +1.51 dB bright at 6.5–10 kHz** against the owner's own pedal (note #25).
- **The model makes ~1.26 dB less H2** than their pedal; −2.39 dB predicted at small signal (#26c).

Both are the same measured unit difference — P4's JFET is ~25 % weaker, K0 5.06–5.19 against 6.59
(note #21) — surfacing on two axes. ➡ **Reversing the decision is ONE constant, `gm` → ~1146 µS,
plus a re-run of `analysis/absolute_gain.py`** (`gm` and `kOutputMakeup` are both level scalars in
the DARK path). Nothing else moves: `m` and `|Vp|` came from P4's probe captures either way.

📌 **Averaging the units is ruled OUT as a voicing target.** `voicing_compare.py`'s `avg_*` curves
are a report reference line only. Note also that P4's MID there is an EXTRAPOLATION (P4 has no MID
position), so an average would import an estimate into the one position no capture can ever check.

### 29. ⭐⭐ THE 13:30 DARK OUTLIER IS THE HARNESS (2026-09-11) — `goal_check` was reading the QUIETEST sweep

`docs/build-plan.md` §19 item 3 asked whether 13:30's core-band FR miss was a real narrow model gap
or a capture artefact. **It is neither: it is the instrument.** No DSP constant changed, and none
should. ⚠ **This supersedes the DARK FR figures in notes #25 and #27**, which were all measured on
`sweep_clean`.

**⛔ FIRST, WHAT IT IS NOT — four routes, two instruments, and the strongest one never renders the
plugin at all.**
1. **Not the mode shelf.** 13:30's rig-free mode differential deviates **0.103 dB RMS** from the
   six-position median — normal, beside 9:00's 0.099 and 12:00's 0.095. ⭐ The differential outlier
   is **15:00** (0.284), which PASSES. So the fault is common-mode at that position.
2. **Not the model.** The model's DARK response above 2 kHz is position-independent except through
   the loading correction, so every corrected DARK capture must lie on top of every other. At
   `sweep_clean` 13:30 deviates **0.476 dB RMS** against 0.110–0.243 for the rest — and **BRIGHT
   shows the same deficit** (0.361 RMS), same sign, same size, which a mode-dependent model error
   cannot do.
3. **Not knob-setting error.** At 13:30 a ±10 min error moves the 8–10 kHz loading correction by
   **0.012–0.015 dB**; 15:00 is 7× more sensitive and passes. (13:30 is also the volume peak, where
   the control law's level slope is ~0 by construction.)
4. **The sign is right for a capture deficit**: `goal_check` read **+0.54 dB @8127, +0.64 @10240**,
   i.e. the PLUGIN brighter than the capture.

**⭐⭐ WHAT IT IS: `goal_check.py` analysed `sweep_clean`, and `sweep_clean` is the −41 dBFS sweep —
the QUIETEST of the signal's four.** It used it for the captures, the renders AND the bypass
deconvolution. ⚠⚠ **The NAME is the trap: `sweep_clean` means clean of DISTORTION, so it reads as
"the good one" while being the worst SNR in the set by 25 dB.** `p4_corners.FIT_SWEEP` already
avoided it for exactly this reason (`sweep_-26`, "well clear of the noise floor"), and CLAUDE.md
already said "use the reference's −6 or −16 dBFS sweep, never its quietest". The rule existed; one
script did not follow it.

13:30's capture-side excess loss, by sweep: **−0.50 / −0.22 / −0.11 / −0.11 dB at 8.1 kHz** for
clean / −26 / −16 / −6.

**➡ THE FIX: `--sweep`, defaulting to `sweep_-16`** — *the loudest sweep that is still LINEAR for
every capture in the matrix.* ⛔ Not `sweep_-6`: at pad 0 that is 2.013 V on the gate against a
1.942 V cutoff onset, i.e. **0.3 dB PAST cutoff**. `sweep_-16` leaves 9.7 dB of headroom at pad 0
and 21.7 at pad 12.

| DARK core worst, dB | `sweep_clean` | `sweep_-26` | `sweep_-16` |
|---|---|---|---|
| 7:30 pad12 | 0.90 ⛔ | 0.39 | 0.36 |
| 9:00 | 0.05 | 0.14 | 0.18 |
| 10:30 | 0.07 | 0.30 | 0.34 |
| 12:00 | 0.22 | 0.52 ⛔ | 0.38 |
| **13:30** | **0.64 ⛔** | **0.63 ⛔** | **0.41 ✅** |
| 17:00 | 0.20 | 0.18 | 0.19 |

⚠⚠ **READ THAT AS VARIANCE COLLAPSING, NOT AS "LOUDER IS BETTER" — the already-good rows get
WORSE.** The honest statistic is the SPREAD: **0.05–0.90 (18×) at `sweep_clean` becomes 0.18–0.43
(2.4×) at −16**, and on the capture side the worst position's deviation goes **0.476 → 0.131 dB**
with every position tightening 2–4×. A quantity that must be similar across knob positions becomes
similar — the same argument note #23 used for the taper, and stronger than any single row.

✅ **At `sweep_-16` EVERY DARK capture passes the core band (0.18–0.43 dB) and NO dark capture
exceeds 1.0 dB anywhere** — so note #27 §3's remaining LF misses (8:00's −1.08 dB at 20 Hz) and the
13:30 core-HF miss are both gone. 13:30's phase improves **2.33° → 1.02°**.
⭐ **And the control: BRIGHT is UNCHANGED** (13:30 bright 1.51 → 1.57 dB at 10 kHz). A sweep change
that improved everything would be suspicious; this one fixed what is measurement-limited and left
the recorded voicing gap (note #28) exactly where it was.

#### 29a. ✅ The linear response IS level-independent — measured, because it was worth checking

Asked directly: should the model not be accurate at any volume? It is, and so is the pedal. Within
`p4_V1030_dark_pad12`, where every sweep is genuinely linear, the measured shape moves **0.02 dB at
1 kHz, 0.13 dB at 8.1 kHz and 0.22 dB at 16 kHz across a 35 dB span**. That residual is the
measurement floor, the same size as the position scatter above. ➡ **So a sweep must be chosen for
SNR, not for realism** — choosing for realism would be choosing inside the noise, and would tacitly
concede a level-dependence the linear path does not have.

⭐ **Two free confirmations fell out.** (a) `sweep_-26` reads **identically** in a pad-0 and a pad-12
capture (+0.02 / −0.02 / −0.06 / −0.07 / −0.09 dB) despite those sitting at *different* gate drives
(−26 vs −38 dBFS equivalent) — so the residual tracks the sweep's DIGITAL level, not the drive,
which is what a measurement artefact does and a pedal does not. (b) The linearity bound was
confirmed without being told: pad-0's `sweep_-6` is the largest deviation in the table (+0.23 dB at
16 kHz) while pad-12's same sweep, 12 dB less drive, is +0.10 — independently reproducing the
cutoff arithmetic that set the default.

📌 ⚠ **And the sweep is NOT the playing zone, which is worth not misremembering.** A pad eats drive
and recorded level together, so at pad 12 — 22 of the 28 main captures — `sweep_-16` is equivalent
to a user playing at **−28 dBFS**, 16 dB below the −12 dBFS playing floor. Even pad 0 reaches only
−16 dBFS. **The playing zone is measured by the NONLINEAR instruments** (`probe_compare.py` on the
pad-0 and probe captures, which do reach the load line — note #27), never by `goal_check`.

#### 29b. ⚠ STILL OPEN: the fitter and the verifier are on DIFFERENT sweeps

`absolute_gain.py`, which anchored `kOutputMakeup`, calls `P.load_fr(path)` with no segment and so
uses `p4_corners.FIT_SWEEP` = **`sweep_-26`**. `goal_check.py` was on `sweep_clean` and is now on
`sweep_-16`. Three scripts, three sweeps. The symptom is `goal_check`'s DARK level mean moving
**−0.021 → +0.151 dB** (both inside the ±0.5 target, but a 0.13 dB systematic disagreement with the
fitter's own +0.019). ➡ **Not yet resolved**: it needs `goal_check --sweep sweep_-26`'s level
section to see whether it reproduces the fitter, after which either the fitter moves to −16 (and
`kOutputMakeup` is re-measured, per note #23's coupling list) or both settle on one sweep.
⛔ Do not move `kOutputMakeup` before that comparison exists.

#### 📌 29c. Harness notes from the same session
- ⭐ **`goal_check` now renders through `p4_corners.render`** instead of a fresh `tempfile.mkdtemp`
  each run: **600 s → 21 s**, output verified **byte-identical** across all 106 lines. ⛔ Guarded so
  the cache is used ONLY when `--bin` is the default — `P.render`'s key hashes `C.RENDER_BIN`, so
  calling it with a different binary would file that render under a key claiming otherwise, which is
  note #23's fault 1 one level further in.
- ⚠ **`p4_corners.render` writes straight to its final cache path with no temp-and-rename**, so two
  concurrent runs sharing a key can have one read a TRUNCATED wav. Not hit yet; do not run two
  analysis scripts against a cold cache at once until it is fixed.
- 📌 A comment-only edit to a header invalidates every cached render (the key hashes the binary).
  Correct and deliberate, but "rebuild then re-measure" always pays full price once.

#### 29d. ✅ ITEM 3a CLOSED — the fitter and the verifier AGREE; the gap was the sweep mismatch alone

| | DARK level delta |
|---|---|
| `absolute_gain.py` (the FITTER) @ `sweep_-26` | **+0.019 dB** |
| `goal_check.py` @ `sweep_-26` | **+0.040 dB** |
| `goal_check.py` @ `sweep_-16` | +0.151 dB |
| `goal_check.py` @ `sweep_clean` | −0.021 dB |

**At a common sweep they agree to 0.021 dB** — an eighth of the fitter's own sd (0.178). There was
never a disagreement between the instruments; note #29b's 0.13 dB was the sweep mismatch and nothing
else. **No constant moves.**

⭐ **And the residual is understood rather than merely tolerated: it trends MONOTONICALLY with
drive** (−0.021 → +0.040 → +0.151 as the sweep rises), so it is a real drive-dependence, not
scatter. The sign says the MODEL loses more level than the pedal as drive rises, i.e. it
over-compresses slightly — the same direction as note #27 §4's triode finding, reached from an
unrelated measurement.

➡ **THE DESIGN RULE THIS SETTLES: `kOutputMakeup` is a LINEAR scalar, so it must be fitted where the
pedal is most linear.** `absolute_gain.py` staying at `sweep_-26` is therefore correct, and
`goal_check`'s FR/phase sections staying at `sweep_-16` (chosen for SNR, note #29) is also correct —
**the two scripts SHOULD use different sweeps, for different reasons, and that is not an
inconsistency to tidy away.** ⛔ Do NOT "fix" it by moving the fitter to −16: that would bake
0.15 dB of compression mismatch into a constant that has none.

#### 29e. ⚠⚠ THE PHASE BAND — a figure was quoted against a band it did not cover, AGAIN

The owner's stated phase band is **40 Hz – 16 kHz minimum** (2026-09-11). The figure on record was
"worst 4.05°", which is the **200 Hz – 12 kHz** statistic — narrower at both ends. This is note #18's
failure mode repeating, so `goal_check` now prints the owner's band as its own column and headline.

**Over 40 Hz – 16 kHz, `x > 0.1`, at the 4× default... (8× OS, `sweep_-16`):**

| mode | worst | verdict |
|---|---|---|
| **DARK** (the model's own error) | **2.74 dB→deg, at 12:00** (0.56–2.74 across 8:00–17:00) | ✅ **MET, ~2× margin** |
| BRIGHT | 6.14° at 12:00, 5.95° at 13:30 (rest 1.93–4.29) | ⛔ misses at two positions |

⭐ **BRIGHT's two misses are the note #28 voicing gap**, at the same two knob positions and from the
same cause as its FR miss — not a phase defect. The headline is split by mode for exactly the reason
the LEVEL section already was.

⚠⚠ **AND THE HEADLINE EXCLUDES 7:30/8:00 (`x <= 0.1`), WHICH IT PREVIOUSLY DID NOT.** Unfiltered it
reads **11.70°**, all of it `p4_V0730_bright_pad12` — a position excluded from every other fit in
this project for knob-slope error (±6.75 dB of control law per ±10 min, note #23). **A headline that
maxes over captures the project already excludes reports a setting error as a model error.** Both
figures are printed.

📌 The best-fit delay is still fitted over 200 Hz–12 kHz and NOT over the owner's band — note #18
measured the fit window as one of the rungs that moves the number, so it is held fixed for
continuity. The 40 Hz–16 kHz column therefore contains some extrapolation of that fit at both ends;
it is the honest statistic for the target, not a flattering one.

### 30. ⭐⭐ THE VOICING DECISION'S PRICE, PROPERLY MEASURED (2026-09-11) — it is FOUR axes, not two

`analysis/thd_band_audit_p4.py --gm 1146e-6`, raw `analysis/reports/thd_band_audit_p4_gm1146u.json`.
**No constant changed. The note #28 decision stands, unchanged and unreopened** — but the price it
buys was quantified at "+1.51 dB bright and +1.26 dB H2", and that was an UNDER-count.

Rendering at P4's own measured `gm` = 1146 µS removes the voicing offset and leaves only floor plus
real error:

| | shipped (P1/P2 voice) | at P4's own gm |
|---|---|---|
| per-band THD, **DARK core** | 2.27 dB RMS | **0.41** (target 0.42) |
| per-band THD, BRIGHT core | 1.61 | **0.64** |
| per-band THD, BRIGHT LF<200 | 2.00 | **0.45** |
| per-band THD, DARK LF<200 | 2.15 | 1.04 |
| H2 overall (note #26c) | +1.26 dB | ~0 |

Per band, DARK collapses from 2.0–2.5 dB to **0.31–0.51 dB across 80 Hz–8 kHz**. ⭐⭐ **So
essentially the ENTIRE per-band THD deficit against P4 is the voicing offset, not a per-band
modelling error** — and it is the same single cause as BRIGHT's 6.5–10 kHz FR miss (note #25) and
BRIGHT's two phase misses at 12:00/13:30 (note #29e). **One constant moves all four axes at once, in
both modes.**

➡ **What this means for the record: the decision costs the 5 % per-band THD target.** At the shipped
`gm` that target is missed by ~5×; at P4's it is met in DARK and close in BRIGHT. That is a bigger
consequence than note #28 priced, and a future reader weighing the decision should see this table
rather than note #28's two numbers alone. ⛔ It is still not a defect and `gm` must not be moved to
close it — it is the recorded decision, made deliberately and re-confirmed 2026-09-11 with this
table in hand.

⚠ **UNCONFIRMED, and flagged rather than assumed:** the BRIGHT FR and phase rows above are
*expected* to pass at P4's `gm` because they share the single cause — **they have NOT been measured
that way.** `goal_check.py` has no `--gm`. Anyone acting on this should run that first.

#### 30a. ⚠⚠ THE KNOWN-ANSWER FLOOR WAS OVERSTATED 12× BY QUOTING IT AS RMS

Note #29's THD discussion used the floor probe's **RMS, 2.67 dB**, and concluded the dataset could
not resolve a 0.42 dB target at all. That is wrong, and the distribution says so:

| statistic | mean | RMS | **median \|·\|** | p75 | p90 | worst |
|---|---|---|---|---|---|---|
| floor (bright vs dark below the shelf zero) | +1.44 | 2.67 | **0.23** | 1.56 | 5.49 | 7.54 |

**It is heavy-tailed: the typical cell agrees to 0.23 dB and ~10 % of cells are wild.** So the
measurement resolves ~0.25 dB, the 0.42 dB target sits just above that, and the gm-corrected
0.31–0.51 dB residual is REAL SIGNAL rather than floor. ➡ **Quote a heavy-tailed floor by its
MEDIAN; an RMS floor is set by its tail and will talk you out of a measurement you can actually
make.** This sits beside note #7's "fit a model, don't threshold" as a statistic-choice trap.
📌 Likely cause of the tail: the floor compares BRIGHT against DARK, i.e. two DIFFERENT takes, while
every per-band statistic is WITHIN one take — so the floor carries take-to-take variation the thing
it is bounding does not.

#### 30b. 📌 PER-ORDER CONFIDENCE, asked directly and answered as a judgement

| order | confidence | basis |
|---|---|---|
| **H1** | **High** | measured on three axes — FR passes, all 15 captures pass absolute level ±0.5 dB, compression median −0.024 dB |
| **H2** | **High**, with a known deliberate offset | note #26c/#30 |
| **H3** | **Good** | note #26's out-of-sample check: `m` was fitted on **H2 @220 Hz alone**, yet H3 @220 Hz went −11.04 → **+0.66 dB** offset (1.35 shape RMS) and @3150 Hz → −0.21/1.47 |
| **H4, H5** | ⚠ **UNVERIFIED** | the floor gates drop them in nearly every cell — **THD ≡ H2 to 0.01 dB in every band is the tell**. There is essentially no capture-side data on them. |

⭐ **The structural argument for H4/H5 nonetheless being about right: they are NOT free
parameters.** They fall out of the device law plus the implicit loop, so there is no coefficient to
get wrong independently of H2/H3 — the risk is a structural error, which would surface in H2/H3
first, and does not.
⚠ **The genuine exception is the TRIODE branch** (note #27 §4): it rests on one capture cell, and
hard-clipping regions generate high orders disproportionately. ➡ **H4/H5 in the clean region:
probably fine. In the load-line region — the top ~7 dB of a hot take at a high VOLUME setting —
genuinely unknown.**

### 32. ⭐⭐ The band edges below 200 Hz (2026-09-11) — there was never an LF defect, and note #30a
### diagnosed the floor's spread as the wrong KIND of problem

Instruments: `analysis/goal_check.py --gm` (new flag), `analysis/thd_band_audit_p4.py` (validity
condition, per-cell dump, corrected gate). Write-up `docs/build-plan.md` §21. **No DSP constant
changed, and none should.** This closes build-plan §19 items 4 and 5a.

#### ⛔⭐⭐ (a) The known-answer probe was being read outside its own validity region

This file's most-reused instrument is the BRIGHT-vs-DARK probe: below the shelf zero both modes see
`Zs = R5`, so their H2 in dBc must read identical, and whatever they read instead is the dataset's
own error. It was being evaluated over **every band below the shelf zero (1800 Hz)**, which is the
wrong condition — the premise needs the bypass cap effectively OUT of circuit, `|1/jωC| >> R5`. At
the zero itself the two are equal; at 800 Hz the cap is still only 2.5× R5.

The contamination is computable from shipped constants with no free parameters,
`40·log10(k_dark/k_bright)` with `k = 1 + gm·Zs` (H2/H1 goes as `1/k²` at matched gate drive —
note #8's "suppressed twice"):

| Hz | 20 | 50 | 125 | 200 | 315 | 500 | 800 | 1250 | 1600 |
|---|---|---|---|---|---|---|---|---|---|
| **predicted** (circuit) | 0.00 | 0.01 | 0.03 | 0.08 | 0.24 | 0.59 | 1.43 | 3.14 | 4.65 |
| **measured "floor"** | 0.02 | 0.01 | 0.09 | 0.21 | 0.52 | 1.21 | 2.70 | 5.44 | 7.32 |

⭐⭐ **A measurement floor does not track a circuit prediction across a 4-decade span.** Every band
from ~200 Hz up was reporting the mode shelf as though it were error. Restricted to where the
premise holds (≤125 Hz), the floor is **0.034 dB median / 0.177 p90 / 0.32 worst**.

⚠⚠ **THAT CORRECTS NOTE #30a, WHICH WAS RIGHT BY ACCIDENT.** #30a read the same distribution as
heavy-tailed (median 0.23, RMS 2.67, p90 5.49) and prescribed quoting the MEDIAN rather than the
RMS. The median is indeed the better statistic — but the spread is **not a tail, it is a monotone
frequency TREND**, so no choice of robust statistic fixes it: a robust statistic over a mixture of
valid and invalid cells still reports the invalid ones, just more quietly. ➡ **Before choosing a
statistic to tame a spread, check whether the spread is a distribution at all.** A two-population
mixture and a heavy tail look identical in a summary and want opposite fixes — restrict the
measurement versus change the estimator.
📌 Consequence: the dataset resolves **~0.03 dB**, not ~0.25, so the 0.42 dB (5 %) per-band THD
target is comfortably measurable and note #30a's stated worry about it is withdrawn.
⚠ The prediction under-reads the measurement by a consistent ~2× (it is a small-signal expansion
evaluated on hot cells). That is fine for its one job — deciding where the probe stops being a
floor — and makes the cut conservative. ⛔ Do NOT repurpose it as a correction to subtract off.

#### ⚠⚠ (b) An H3 inversion disqualifies H2 as well — one cell carried a whole band's worst case

`dark LF<200` was the worst group in the per-band THD audit (2.15 dB RMS / **6.34 worst**) and the
one group that did not collapse when the voicing offset was removed. A per-cell dump puts **all of
it in one capture at one level cell**: `p4_V0900_dark`'s `tone_20_-1`, decaying with frequency
(+6.70 / +4.80 / +2.45 / +0.87 / +0.37 dB at 20 / 31.5 / 50 / 80 / 125 Hz) against ≤0.83 dB for
every other cell in the band. It is the deepest-clipping cell in the whole dataset — pad 0 at
−1 dBFS is **3.58 V at the gate, 5.3 dB past the 1.942 V cutoff onset** — and note #27 §4's
anti-correlation (VOLUME sets both the load line and the recorded level) is why nothing else is
near it.

**Two known-answer arguments say the CELL is corrupted and the MODEL is right there:**
1. The output high-pass (48.8 Hz at this knob setting, measured — note #21) is passive and sits
   AFTER the JFET, so it attenuates `f` more than `2f` and must **LIFT** H2 in dBc at 20 Hz by
   ~4.0 dB whatever the transistor does. The plugin tracks that to 0.5 dB (+3.50 measured against
   +4.01 predicted, re 125 Hz); the capture **FALLS** 2.9 dB. No transistor model can produce the
   capture's sign.
2. In DARK, `Zs = R5` at every frequency, so `k` is frequency-independent and the harmonic
   **ORDERING** cannot change with frequency. The capture is H2-dominant at 125 Hz and H3-dominant
   at 20 Hz — H3 sitting **8.2 dB above H2** — at the same drive. The circuit forbids it.

⚠⚠ **The gate detected the inversion and kept the cell anyway.** The order-inversion test truncated
the series above H3 and then reported that cell's H2 as a valid +6.70 dB model error. Truncating
assumes the contaminant lives only in the orders ABOVE the inversion — but `H3 >= H2` means the
contaminant is at least as large as H2 itself, so **H2 is inside it, not above it**. The gate now
drops H2 too when the inversion is at H3 specifically; an inversion at H4 or above remains a plain
truncation, which is the case the test was written for.
⛔ **Both tests are CAPTURE-INTERNAL, and that is the whole safety argument.** They compare a
capture against itself (H3 vs H2 in one cell; one capture's H2 at two frequencies) or against a
topology constant measured independently of the transistor. **A gate that dropped cells for
disagreeing with the model would be circular and must never be added.**

#### ✅ (c) Where the sub-200 Hz band now stands

Read at P4's own `gm` so the voicing offset is not sitting in the middle of it:

| axis, below 200 Hz | result |
|---|---|
| FR | ✅ every DARK capture passes; no band over ±1.0 dB anywhere; 20 Hz −0.26 to +0.29 dB |
| phase | ✅ DARK worst **2.74°** over 40 Hz–16 kHz; the 23 Hz cells read −3.3° |
| THD / H2 | ✅ **0.48 dB RMS** (core 0.41); **0.42 RMS / 0.83 worst** excluding the deep-clip capture |
| compression | ✅ median **−0.117 dB**, RMS 0.181 — *better* than the core band's 0.206 |
| absolute level | ✅ all 15 captures pass ±0.5 dB |

⭐⭐ **At the SHIPPED `gm` the LF residual is 2.00 dB RMS and FLAT with frequency** — 2.37 / 2.19 /
2.05 / 2.06 / 2.10 dB at 20 / 31.5 / 50 / 80 / 125 Hz, continuing 2.10–2.48 dB across the core to
8 kHz. **A residual constant across three decades is not a band-edge defect; it is note #28's
voicing offset.** ➡ **There is no frequency-dependent LF distortion mechanism missing from the
model**, which extends note #13's finding (valid only from 125 Hz up, on NAM captures whose
sub-100 Hz data was unusable) down to 20 Hz on a raw capture.
📌 Note #18's 6.8–8.0° phase miss at 200–500 Hz was against **P1, a NAM model**; anchored to P4 the
same band passes with ~2× margin. The confounded LF pole of notes #19/#19a/#20/#20a is moot — note
#21 measured the rig as having no LF pole at all and C10 as drawn.

#### ⭐ (d) `goal_check.py --gm` — and note #30's FR/phase halves are measured now, not expected

Note #30 recorded that BRIGHT's 6.5–10 kHz FR miss and its two phase misses at 12:00/13:30 share a
single cause with the per-band THD offset, and flagged that the FR/phase halves were **expected
rather than measured** because `goal_check.py` could not move `gm`. Measured:

| BRIGHT vs P4 | shipped `gm` | at P4's own `gm` = 1146 µS |
|---|---|---|
| FR core worst, 80 Hz–12 kHz | 1.10–1.57 dB | **0.27–0.59 dB** |
| core bands over the ±0.5 dB target | 4–5 per capture | **0–1** |
| phase worst, 40 Hz–16 kHz | **6.14° / 5.95°** (12:00 / 13:30) | **3.60° / 3.43° — both PASS** |
| phase worst, 200 Hz–12 kHz | 4.05° | **1.67°** |

⭐ **The flag validates itself three ways, which is what makes the result evidence rather than a
favourable render.** (a) DARK's FR and phase come back **byte-identical** — `gm` only moves the mode
shelf, so anything else moving would mean the flag did more than it claims. (b) DARK's *level*
shifts **+0.456 dB**, against note #28's independently-derived "P4's weaker JFET makes it ~0.46 dB
quieter", a prediction already on record. (c) The known-answer floor does not move, as it must not,
since it never touches a render.
⚠ **`--gm` is a MEASUREMENT flag. The absolute-level section is not meaningful under it** —
`kOutputMakeup` was fitted at the shipped `gm` and note #23 records them as coupled, so DARK's mean
moves +0.151 → +0.607 dB. The help text and the run banner both say so.
⛔ **None of this is a reason to move `gm`.** Note #28 records that decision as closed with exactly
this as its accepted price.
