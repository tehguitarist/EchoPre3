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
| `C1` | 22 nF | Source-bypass cap, "MID" branch (→ MODE lug 3) |
| `R1` | 1 MΩ | Pulldown holding C1's bottom node at GND when not switched (anti-pop) |
| `C2` | 10 nF | Source-bypass cap, "BRIGHT" branch (→ MODE lug 1) |
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
- ⚠ **Datasheet spread is huge:** 2N5457 is specified IDSS 1.0–5.0 mA and Vgs(off) −0.5 to −6.0 V.
  Every amplitude parameter (gm, bias point, shaper curvature) **must be fitted to a capture** of
  the actual unit; only the R/C corners and the polarity are trustworthy in advance.
- 📌 **TODO: `docs/refs/` has a J201 datasheet but no 2N5457 one.** Fetch the onsemi/Fairchild
  2N5457 datasheet into `docs/refs/` *before* starting the DSP stage, per the template's rule to
  gather non-WDF-native data up front rather than mid-build.

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

⚠ **Plugin-vs-pedal note:** in hardware this LP corner sits *above* a real guitar's source
impedance and pickup resonance, so the pedal is darker in situ than the maths suggests. A plugin's
input is an ideal voltage source (Z = 0), so the modelled corner will land at the full ~7.3 kHz and
read brighter than the real pedal fed from a guitar. Decide explicitly whether to model a source
impedance; either way, **A/B against captures made through the same interface**, not against theory.

### Stage 2 — JFET gain stage (**Nonlinear** — external model, NOT a WDF element)

```
Node G:  Q1 gate      (see stage 1)
Node D:  Q1 drain, R6 leg 1, C10 leg 1 — no other connection
Node VA: R6 leg 2     (22 V supply rail, AC ground)
Node S:  Q1 source, R5 leg 1, C1 leg 1, C2 leg 1 — no other connection
Node GND: R5 leg 2
```

DC bias (**estimate only — confirm by measurement / fit**): with R5 = 3.6 k self-bias and a typical
mid-spread 2N5457, Id lands in the few-hundred-µA range, putting the drain near **mid-rail (≈ 11 V)**
with VA = 22 V and R6 = 22 k. That is a deliberately generous, tube-like headroom — this stage is
meant to stay clean at guitar levels. **Do not fit the bias from these numbers**; fit it to a
capture (the device spread above swamps any nominal calculation).

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

| Lever | Label | Branch grounded | Bypass corner | Character |
|---|---|---|---|---|
| **UP** | **BRIGHT** | C2 = 10 nF (lug 1) | 1/(2π·3.6 k·10 n) ≈ **4.42 kHz** | lift confined to the top octaves → brightest balance |
| **MIDDLE** | **DARK** | none (centre-off) | — | no bypass: R5 fully degenerating → lowest gain, flat |
| **DOWN** | **MID** | C1 = 22 nF (lug 3) | 1/(2π·3.6 k·22 n) ≈ **2.01 kHz** | lift reaches down into the upper mids → fullest |

**Confirmed 3-position ON-OFF-ON.** The maker calls it the "Exclusive 3-Way EQ 1970s Era EP3
Mini-Toggle", naming the positions by era: **Early 1970s = BRIGHT · Late 1970s = DARK · Hybrid
Early & Late = MID** — matching the owner's up/middle/down labelling by treble content. The centre
"DARK" position is decisive evidence for the centre-off:
*no-bypass is the only state this circuit can produce that has less treble than both cap positions*,
and it only exists on a switch with a genuine centre-off. R1/R2 are therefore doing real work —
holding both cap bottom nodes at ground so the centre position neither floats nor pops.

⚠ **Note both cap positions reach the SAME HF plateau gain** (fully bypassed above their corners).
BRIGHT vs MID is not "more treble vs less treble" at the top — it is *where the lift starts*: the
22 nF lifts the 2–4.4 kHz upper-mid band as well, which reads as "mid"; the 10 nF leaves that band
alone, so the same top-end lift reads as "bright". Don't model BRIGHT as a higher-gain version of
MID — that's a different (and wrong) shape. See "Validation notes" #2 for the one residual unknown.

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

⭐ **This network is deliberately NON-MONOTONIC, and that is CORRECT — it reproduces the original
EP-3's volume wiring.** Output rises from silence to a peak around 1–2 o'clock, then *falls back*
a dB or two by full rotation. Confirmed against the pedal maker's own published description — see
"Validation notes" #1, which also gives four calibration points for the taper fit. Do not
"fix" it into a conventional divider.

`C10` HP corner: into R10 ∥ (the volume network's input resistance), so it **moves with the volume
setting** — roughly 1/(2π·75 k·100 n) ≈ **21 Hz** near the top of the range. It is not a fixed
corner; solve it inside the coupled network rather than pre-computing one number.

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

| Ra (lug 1 → wiper) | 0 | 10 k | 25 k | 50 k | 100 k | 150 k | **200 k** | 300 k | 400 k | 500 k |
|---|---|---|---|---|---|---|---|---|---|---|
| Network gain | −87 dB | −11.3 | −7.1 | −5.2 | −4.1 | −3.8 | **−3.8 (peak)** | −4.2 | −5.2 | −7.7 |

It peaks around 35–40 % of `Ra` and falls back by full rotation, with all the real attenuation in
the bottom ~10 %. The shape is unchanged if the drain is treated as an ideal current source, so it
is not an artefact of the source-impedance assumption.

**This was initially flagged as a probable schematic drawing error. It is not.** The pedal maker's
published description of the control matches the computed curve point-for-point:

> FULL COUNTER-CLOCKWISE = NO SIGNAL · 10 to 11 o'clock = UNITY GAIN (depends upon EQ setting) ·
> 1 to 2 o'clock = 3 dB+ MAXIMUM OUTPUT BOOST · 3 to 5 o'clock = 1 or 2 dB with FLAT EQ
> — "*Wired just like an original EP3*"

Silence at full CCW, a peak short of full rotation, and a deliberate fall-back past it are all
reproduced by the as-drawn topology and by nothing else. **Model it exactly as drawn.** A
conventional wiper-to-output divider would be monotonic and therefore wrong.

⭐ **This gives four free calibration points for the taper fit — use them.** `dsp.md` asks for at
least two knob positions to constrain a taper's shape; the maker's notes supply four across the
full range, before any capture is made:

| Knob position | Rotation (7→5 o'clock sweep) | Target output |
|---|---|---|
| Full CCW | 0 % | −∞ (no signal) |
| 10–11 o'clock | ≈ 25–33 % | 0 dB (unity) |
| 1–2 o'clock | ≈ 58–67 % | **+3 dB (maximum)** |
| 3–5 o'clock | ≈ 75–100 % | +1 to +2 dB |

The peak of the *network* sits at `Ra` ≈ 35–40 % of 500 k; for that to land at 1–2 o'clock
(≈60–65 % rotation) the 500 kA audio taper must rise slowly early — consistent with a standard
audio law. **Fit the taper so the peak lands at 1–2 o'clock**, then check the other three points.
Treat these as a sanity oracle, not gospel: they are marketing copy, rounded, and explicitly
qualified with "depends upon EQ setting". A real VOLUME sweep capture still supersedes them.

📌 **A level anchor falls out of this, and it disagrees with nominal-SPICE by several dB.** If the
whole pedal is +3 dB at the volume peak while the network there is −3.8 dB and the input network
−0.9 dB, the JFET stage's own voltage gain must be ≈ **+7 to +8 dB** (≈2.4–2.5×). A naive
nominal-2N5457 estimate gives ~+12 dB unbypassed, i.e. **~4 dB hotter than the real pedal**.
Expect the fitted `gm` to come out well below nominal — unsurprising given the maker specifies a
"cherry picked" vintage device (see #4). Use this as a coarse cross-check on the fit, not a target.

### 2. ✅ RESOLVED — MODE is a 3-position ON-OFF-ON: BRIGHT / DARK / MID

The schematic symbol alone (a plain SPDT) could not distinguish ON-OFF-ON from ON-ON. Resolved by
the owner's labelling: **up = BRIGHT, middle = DARK, down = MID**, ordered by treble content. The
centre "DARK" label settles it — an unbypassed R5 is the only state with less treble than either
cap position, and it requires a genuine centre-off. Modelled as three topologies (see stage 2b).

**Residual unknown (minor, mechanical):** which lug the lever's UP position actually closes. A
standard toggle connects the common to the lug *opposite* the lever throw, and the lug-to-PCB
mapping depends on how the switch is mounted — so "up = 10 nF (lug 1)" is inferred from the
electrical meaning of the labels, not traced. It does not affect the DSP (three topologies either
way), only which APVTS choice index maps to which cap. Confirm by ear or from one capture per
position before shipping the parameter, since `architecture.md` warns that reordering an
`AudioParameterChoice` after release breaks saved sessions silently.

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
