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

DC bias — ✅ **narrowed by the measured `gm` (note #8), no longer a free estimate.** Once `gm` is
known the square-law self-bias solve is a ONE-parameter family: `|Vp|/Vov = 1 + gm·R5/2 = 3.796` and
`IDSS/Id = 14.41`, so choosing any one of (IDSS, |Vp|, Id, Vov) fixes the rest. The datasheet's
IDSS ≤ 5 mA caps it at **Vov = 0.447 V, Id = 347 µA, |Vp| = 1.70 V, Vs = 1.25 V, Vd = 14.4 V**; its
Vgs(off) ≥ 0.5 V floors it at Vov = 0.131 V; the load line would allow Vov up to 1.05 V, so the
datasheet binds first. The model ships the IDSS = 5 mA end (see `JfetStage.h`).

⚠ **The earlier "drain near mid-rail ≈ 11 V" estimate is superseded.** It assumed a nominal part; the
measured `gm` is ~2× nominal, and high `gm` at this device needs a small `Vov`, which needs a small
`Id` — so the drain sits **high (14.4 V), not mid-rail**. Headroom is therefore asymmetric: 7.6 V of
up-swing against 13.1 V down. Still deliberately generous — this stage is meant to stay clean at
guitar levels.

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

Drive the shaper with the **effective vgs** (real gate volts, order |Vp|), so `gm` alone sets the
gain and the shaper only adds curvature (slope exactly 1 at the origin).

⭐⭐ **AND THEN SUPPRESS WHAT IT GENERATES A SECOND TIME — see note #8.** The line above is necessary
and NOT sufficient, and the difference is worth 16.4 dB. Feeding `vgs = vg/k(s)` into the shaper
models the *drive* to the nonlinearity; the same local feedback also attenuates the distortion the
device makes *inside* the loop, so the second-order product comes out filtered by `1/k(s)` as well:

```
id2 = c·(u1²)/k(s)      ⇒   H2/H1 = A/(4·Vov·k²),  NOT  A/(4·Vov·k)
```

Every linear test passes with that factor missing. It was shipped, and only an independent implicit
solve of `id = gm·g(vg − id·Zs)` caught it.

⚠ **Expect a square-law, even-dominant character.** Per §2's finding (a), a `tanh` **structurally
cannot** produce an even-dominant stage — and a JFET is a square-law device, so H2 should dominate
H3 in captures. Use the linear-core-plus-even-bump shape, and **check the sign of the cubic**
(finding (b)) before choosing a limiter. With no downstream clipper on this pedal, the JFET's own
harmonic signature is the *only* thing to match — there is nothing else to hide a wrong shape
behind, which makes the capture-driven fit both easier to judge and unforgiving.

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

⚠ **One quantity does NOT match, and it is worth knowing before the capture session.** The maker
puts the peak at +3 dB and 3–5 o'clock at +1 to +2 dB — a fall-back of **1–2 dB**. The as-drawn
network falls back **3.9 dB** from peak to full CW, and as the table above shows that depth is
insensitive to the drive impedance, so it is not an artefact of any assumption we've made. Roughly
2 dB is unaccounted for. Candidate explanations, none confirmed: the taper compresses the top of
the rotation so 5 o'clock never reaches `Ra` = 500 k electrically; "3 to 5 o'clock" is a rounded
range quoted at 3 o'clock rather than at the stop; or the copy is simply approximate. **Treat this
as an open discrepancy to settle with the VOLUME sweep capture (measurement #1 below), not as a
confirmation.** Do not tune other constants to close a 2 dB gap that may not exist.

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

### 3. ⚠ VOLUME rotation direction

Confirm which physical rotation direction moves the wiper toward lug 3 (i.e. that CW = louder).
This is a wiring/`taper` detail that a capture sweeping VOLUME will settle at the same time as #1.

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

### 5. Things to measure on the real pedal, in priority order

1. **VOLUME sweep, everything else fixed** — settles #1 and #3, and gives the taper fit.
2. **One capture per MODE position at fixed volume** — gives the source-bypass corner fit (the
   single most character-defining measurement on this pedal) and confirms the lever→lug mapping
   left open in #2. The DARK position doubles as a clean measurement of the *undegenerated* stage
   gain, which pins `gm` with no bypass network in the way — capture it first.
3. **Harmonic spectrum at 2–3 input levels** (low-frequency tone) — fits the JFET shaper: confirms
   the expected even-dominant square-law signature and the sign of the cubic (§2 findings a/b).
4. **Bypass/unity anchor capture** — needed for `kInputRef` and output-makeup calibration.

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
| Input LP corner | ~7.3 kHz | 6.7 kHz (P1), 7.2 kHz (P3) | ✅ confirmed twice; **P2 unusable, see below** |
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
−8.6 to −10.7 dB/octave through the top two octaves. P1 and P3 — a different unit *and* a different
trainer each — independently fit first-order low-passes at 6.7 kHz and 7.2 kHz against the drawn
7.3 kHz, so two units confirm the input network and the third does not.

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

➡ Use P1 or P3 for anything above ~2 kHz. **P2 remains the best capture in the set for the mode
differential** (0.03–0.08 dB shelf residuals), because a post-JFET pole multiplies all three modes
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

**One bookkeeping correction.** `docs/build-plan.md` §1's table had the two units swapped. The
folders and the `.nam` metadata agree: **P1 = thelamehorse, VOLUME 10:30** and **P2 = danielnguyen,
VOLUME 2:30**. The capture filenames on disk were right; the table was not. §4's instruction to
"anchor absolute response to P1" was written meaning danielnguyen's unit — which is the one whose
rig response just disqualified it for exactly that job. See `build-plan.md` §4.

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
