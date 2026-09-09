# Capture session checklist — Echo Pre 3 (owner's two-position unit)

Written 2026-09-09 against the owner's own plan, which is sound. This records the four gaps in it
and the reasoning behind each, so the session does not have to be repeated. Everything here assumes
the owner's rig: **interface output → pedal → interface input, with NO reamp box and no transformer
anywhere.** That is better than this project's earlier ask (`CLAUDE.md` #4, "reamp at unity") — it
removes the component that was the leading suspect for the LF discrepancy (circuit.md note #19),
which is now refuted but was only refuted by measurement.

---

## ⭐⭐ 0. THE ONE THING THAT CHANGES EVERYTHING: keep the RAW WAVs

**Record `analysis/test_signal_48k.wav` through the pedal and keep the returned WAV. Train NAM too
if you want, but the raw file is the reference from now on.**

Every "floor" this project has fought is NAM model error, not measurement noise
(circuit.md note #15 — the models are deterministic to −114 dB, so **none of it averages down**):

| quantity | floor with NAM models | floor with a raw capture |
|---|---|---|
| harmonic level (H2 in dBc) | 4–9 dB, up to 21 | the converter's, ~−100 dBFS |
| compression | 0.145–0.210 dB | negligible |
| per-band THD below 100 Hz | 1.9–20.2 dB (a 132 ms receptive field is 2.6 periods at 20 Hz) | none — no receptive field |
| LF magnitude | the whole open question | directly measured |

`Vov` is pinned only to a factor of 1.3–1.5 today (note #16) **because of that harmonic floor and
nothing else**. A raw capture removes the floor outright.

- Signal is **4 min 50 s at 48 kHz**, so 14 captures ≈ **70 minutes** of recording.
- **48 kHz, no sample-rate conversion anywhere.** Same rate in and out.
- **No plugins, no dither, no normalisation** on the return. 32-bit float if the DAW offers it.
- Do not trim the head or tail — the analysis aligns on the leading marker.

## ⚠ 1. Your DMM stops at 400 Hz — calibrate at 220 Hz, not 1 kHz

Jaycar **QM1529**, manual page 10, AC Voltage table: **"Frequency Range: 40~400Hz"**. 1 kHz is 2.5×
outside spec, and outside spec on a rectifier-based AC front end means it reads LOW by an unstated
amount. It is not on the datasheet at all, only the manual.

- ⇒ **Use a 220 Hz sine for the level set.** Inside spec with margin, and deliberately NOT a
  multiple of 50 Hz, so mains pickup cannot sit on the measurement. The interface is flat to within
  hundredths of a dB from 20 Hz up, so a level measured at 220 Hz is the 1 kHz figure — declaring
  `input_level_dbu` from it is exact, not a fudge.
- ✅ **The meter is almost certainly average-responding, and that does not matter here.** Such meters
  are calibrated to read correct RMS *for a sine*, which is exactly what we are feeding it. Just keep
  the tone a clean sine — no square waves, no noise, nothing clipped.
- 📌 The meter has no frequency counter, so it cannot confirm the tone. You are generating it
  digitally, so you already know it.

### ⭐ Measure HIGH in the 2 V range, not at −12 dBFS

AC accuracy is **±(1.0% of reading + 10 digits)**, and on the 2 V range a digit is 1 mV — so the
10-digit term is a **fixed ±10 mV** regardless of level. Measuring a small voltage wastes it:

| tone level | target V RMS | range | total error | in dB |
|---|---|---|---|---|
| −12 dBFS | 0.7926 | 2 V | 2.26 % | ±0.194 |
| −6 dBFS | 1.5815 | 2 V | 1.63 % | ±0.141 |
| **−5 dBFS** | **1.7745** | **2 V** | **1.56 %** | **±0.135** |
| −4 dBFS | 1.9910 | 2 V | 1.50 % | ±0.130 |

⛔ **Do not go past −4 dBFS.** The 2 V range tops out at 1.999 V; one step above and it autoranges to
20 V, where a digit is 10 mV and the fixed term becomes **±100 mV** — ten times worse.

➡ **Play a 220 Hz sine at −5 dBFS and set the interface output until the meter reads 1.775 V RMS.**
That fixes `kInputRef` = 4.4626 V/FS (0 dBFS = 3.1555 V RMS = 4.4626 V peak) and
`input_level_dbu` = **+12.20 dBu**.

✅ **±0.135 dB is comfortably good enough** — `Vov` is currently pinned only to a factor of 1.3–1.5,
i.e. ±2.8 dB, so the meter is nowhere near the limiting uncertainty. Don't buy a better one for this.
⛔ And the DC ranges, which are three times more accurate, are not an option: interface outputs are
AC-coupled, so there is no DC to measure.

## ⚠⚠ 2. The record side will clip at unity — and a DI input may clip anyway

The pedal **boosts up to about +3 dB** at its volume peak (1–2 o'clock), and the test signal's
hottest cell sits near **−1 dBFS**. At unity loop gain that lands at **+2 dBFS and clips**. Worse, a
DI/instrument input at minimum gain often cannot accept 3.1 V RMS at all.

- **Use a LINE input if the DI cannot take ~+13.5 dBu**, or drop the record path by a known amount.
- `output_level_dbu` does **not** have to be unity — it only has to be **measured**. Feed a known
  voltage into the input, note the dBFS it reads, and
  `output_level_dbu = 20·log10(V/0.7746) − (reading in dBFS)`.
- **Set the input gain once, verify no cell clips, then do not touch it for the rest of the session.**
- ⭐ `output_level_dbu` is the **only** thing that can ever anchor `kOutputMakeup`, which is still
  exactly 1.0 and has no other route. It is the single highest-value number of the session.

## ⚠⚠ 2b. The 1.775 V is a SETUP TONE, not the capture — and the capture is MEANT to run hot

Two things that sound contradictory and are not.

- **0.78 V is a guitar's RMS at ordinary playing level** (−12 dBFS RMS). A real guitar's PEAKS sit
  about 12 dB above its RMS, so the same instrument hits **3–4 V** on a hard-picked transient. The
  calibration puts 0 dBFS at 4.4626 V peak precisely so the signal spans **0.02 V to ~4 V at the
  pedal — quiet playing right through hard picking.**
- **The 1.775 V tone is measured once, during setup, and never recorded.** It is at −5 dBFS only
  because that is where the meter is most accurate.

⛔ **So do NOT attenuate "to be safe".** The stage enters triode at about **−7.5 dBFS** and cuts off
near −2.7 dBFS, so the top ~7 dB of the signal is the load-line region — **the region no existing
capture reaches and the one the model most needs measured.** Backing off by even 6 dB throws away the
entire reason for capturing at this calibration.

✅ **Nothing is at risk.** `R3` = 110 kΩ sits in series with the input, so even if the gate junction
conducts on the loudest peaks it draws microamps into a 22 V rail. ⭐ **And watch for exactly that:**
gate conduction is real JFET behaviour that `JfetStage` does NOT model (it has cutoff and triode
only). If the hottest cells show asymmetry the model cannot follow, that is the likely cause, and it
is a finding rather than a fault in the capture.

## ⚠⚠ 2c. The interface's INPUT IMPEDANCE will bias the result — check it

This one is easy to miss. The pedal's output impedance is **92–139 kΩ** (circuit.md stage 3 — the
wiper is grounded and R9 bridges to the jack, so the pot cannot pull it down). That is 15–20× a
normal pedal's, so the input you record into forms a real divider with it, at 1 kHz:

| input Z | mean level error | **spread across the volume sweep** |
|---|---|---|
| 470 kΩ | −1.54 dB | **0.67 dB** |
| 1 MΩ | −0.76 dB | **0.34 dB** |
| 2 MΩ | −0.39 dB | 0.18 dB |
| 10 MΩ | −0.08 dB | 0.04 dB |

⚠ The **mean** lands straight in `kOutputMakeup`. The **spread** is worse, because it varies with the
knob and would therefore corrupt the taper fit — the very thing this session exists to settle.

- ➡ **Use the highest-impedance input the interface has** (instrument/Hi-Z, not line/mic) and **write
  down its rated impedance.** Both errors are then correctable, because the pedal's own output
  impedance is known from the circuit — but only if the number is recorded.
- ⛔ At 470 kΩ the volume-sweep spread is 0.67 dB, twice the whole unit-to-unit tolerance band
  (0.33 dB RMS, note #7's M6). Do not record the sweep into a 470 kΩ input if anything better exists.

## ✅ 2e. SSL 2+ MkII: use the Hi-Z input at MINIMUM gain. Not line, and NOT the RNDI.

⛔ **Do not try to simulate a guitar amp's input.** The plugin models an **ideal load** by decision
(circuit.md stage 3: it feeds a DAW digitally, so there is no cable and no amp after it). Loading the
capture the way an amp would bakes that amp into the reference — the same error already refused for
P2's cable capacitance. **We want the LIGHTEST load available**, not a realistic one.

| option | verdict | why |
|---|---|---|
| **Hi-Z / instrument, 1 MΩ** | ✅ **use this** | −0.76 dB mean, 0.34 dB sweep spread — and both are **calculable**, since the pedal's own output impedance is known |
| Line input | ⛔ no | line inputs are ~10 kΩ; against the pedal's 92–139 kΩ that is >20 dB of loss and heavily volume-dependent |
| RNDI in line | ⛔ **no** | ⚠⚠ it has a **transformer**, and an unknown ~20–35 Hz linear pole in the reference chain is the exact open question (notes #19a/#20). A transformer's LF response is not calculable and would have to be measured and deconvolved. **A known resistive divider is far better than an unknown magnetic one.** |

**The levels work, with margin worth knowing:**

| | needed | available | margin |
|---|---|---|---|
| line OUT (0 dBFS = 3.1555 V RMS) | +12.20 dBu | +14.5 dBu | **2.30 dB** |
| Hi-Z IN (est. peak, drain ±9 V through the network into 1 MΩ ≈ 5.15 V pk) | ≈ +13.4 dBu | +15 dBu | **≈1.6 dB** |

- ⭐ **Set the Hi-Z gain to its MINIMUM stop and leave it there.** That is a hard, repeatable position
  (no knob to drift or bump), it gives the maximum headroom, and it puts `output_level_dbu` at
  roughly **+15 dBu** — measure the exact figure with the §2d loop, which lands around −7.8 dBFS.
- ⚠ **1.6 dB is tight, so verify before committing.** Run the hottest case first — signal through the
  pedal, louder mode, volume around 1–2 o'clock — and check the recorded peaks. If it clips, the
  fallback is to note the affected cell, **not** to attenuate the drive.
- ⚠ **The output side needs the monitor knob, which is NOT a hard stop.** Mark its position, and
  **re-measure the 1.775 V at the end of the session** to prove it did not move. Everything downstream
  of `kInputRef` rests on that one setting.
- 📌 **Verify the 1 MΩ rather than trusting it** (it is from a review, not SSL's spec sheet): patch
  output to input through a **100 kΩ resistor** and compare the recorded level with a direct patch.
  The drop gives `Zin` outright — `Zin = 100k · g/(1−g)` — at the impedance that actually matters,
  in thirty seconds, with no meter.

## ⭐ 2d. Let the LOOP measure `output_level_dbu` — no meter needed

Unity end-to-end is **not** required, and it is fine that the reference is not matched. The analysis
works in absolute volts on both sides, so all that matters is that the record path's calibration is
*known*. Getting it from the loop is both easier and more accurate than a second meter reading:

1. Set the record gain for headroom on the **hottest case** — the test signal through the pedal, in
   the louder mode, at the volume position with the most boost (around 1–2 o'clock). Verify no cell
   clips. **Then do not touch it again for the rest of the session.**
2. With that gain fixed, patch output straight back to input and play the 220 Hz tone at −5 dBFS.
3. It records at some level `X` dBFS. Then
   `V_fullscale_in = 3.1555 × 10^(−5/20) × 10^(−X/20)` and
   `output_level_dbu = 20·log10(V_fullscale_in / 0.7746)`.

⭐ The clean-loop capture you already planned **is** this measurement, so it costs nothing extra —
and it is doing three jobs at once: the M0 unity check, the LF-pole test in §3, and this.
⚠ One caveat: in the loop the source is the interface's ~100 Ω output, not the pedal's ~130 kΩ, so
the loop does **not** include the §2c loading error. That correction is separate and still needed.

## ⚠⚠ 2f. If the level "drops" when you plug into the input — diagnose before compensating

Seen on the first setup attempt: 1.775 V measured on an unloaded output, then a **3.8 dB drop**
after plugging into the Hi-Z input. Do **not** just turn the source up. Work out which of two very
different things it is, because they need opposite responses.

**It is almost certainly NOT loading.** For a 3.8 dB loss into 1 MΩ the source impedance would have
to be **549 kΩ**. A headphone output drives 32 Ω cans, so its source impedance is under ~50 Ω, which
into 1 MΩ costs **0.0004 dB**. The far likelier explanation is that the drop was measured against an
*expected* recorded dBFS, and the Hi-Z input's real sensitivity at minimum gain is simply ~3.8 dB
below the +15 dBu figure. **That is benign and needs no compensation at all** — §2d's loop measures
the true `output_level_dbu` and absorbs it exactly.

**The 30-second test that settles it.** Put any resistor from 100 kΩ to 1 MΩ across the output and
re-read the DMM:
- **Reading does not move** → no loading. **Leave the output level alone.** The 3.8 dB is record-side
  scaling; the loop capture already accounts for it.
- **Reading drops** → something really is loading it. ⚠ Check for a **TS cable in a TRS jack**: a
  headphone out is stereo, and a mono plug shorts ring to sleeve. Fix the cause; do not compensate.

⚠⚠ **And if there IS real source impedance, compensating against the interface would still be wrong**
— the pedal's input is **1.11 MΩ**, not 1 MΩ, so it loads differently (549 kΩ of source would give
−3.49 dB into the pedal against −3.8 dB into the interface). ➡ **In that case calibrate in situ:**
Y-split at the pedal's input jack, pedal connected, DMM on the other leg, and set 1.775 V there.
The calibration is defined **at the pedal's input**, so that is the only place it is unambiguous.

📌 **Why the headphone output was needed at all, and it is worth checking:** the SSL's line outputs
are **balanced TRS**, and on a simple differential output a TS cable delivers only one leg — **6 dB
down**, dropping the usable maximum from +14.5 to +8.5 dBu, below the +12.20 dBu required. If that is
what happened, a proper balanced-to-unbalanced connection recovers the line output and is preferable
to the headphone amp. ✅ The headphone output is otherwise acceptable here: low source impedance,
ample level, and `output_level_dbu` is measured rather than assumed. Just keep its knob marked and
re-verify at the end of the session, exactly as for the monitor knob.

⛔ **Do not solve this by lowering the target.** `kInputRef` = 4.4626 V/FS is what puts triode onset
at −7.5 dBFS. At 2.0 V/FS it moves to −0.5 dBFS and only the single hottest cell reaches the load
line at all — which is the region the whole session exists to capture.

## ✅ 2g. A high headphone-amp setting is fine — and noise is not the risk. Distortion is.

Measured on the first setup: **noise floor −95.8 dBFS.** That is comfortable everywhere:

| cell | recorded | raw SNR | with the ESS processing gain |
|---|---|---|---|
| sweep −6 | −6 dBFS | 89.8 dB | 146 dB |
| sweep −26 | −26 dBFS | 69.8 dB | 126 dB |
| sweep clean (−41) | −41 dBFS | 54.8 dB | 111 dB |

(A 20 s exponential sweep over 22 kHz carries **56 dB** of processing gain: `10·log10(T·BW)`.)
Against what the measurements need — ~40 dB for 1/6-octave FR, ~70 dB to see H2 at −45 dBc with room
to spare, ~60 dB for compression to 0.01 dB — every one is met with large margin.

⭐ **And turning the amp UP is the right move, not a compromise.** A headphone volume control sits
*ahead* of the amp stage, so at low settings the amp's own noise dominates and SNR is *worse*. High
setting = better SNR.

⭐⭐ **Keep this in proportion: −95.8 dB of RANDOM noise is a transformative improvement over the
existing reference.** Note #15 established the NAM models' 4–9 dB harmonic error is **systematic**,
so it does not average down no matter how much data you take. Noise does. Even at −95.8 dBFS this
capture is in a different class.

⚠⚠ **The real risk at a high output setting is SOURCE DISTORTION, not noise** — a headphone amp near
its limit adds harmonics, and harmonics are exactly what we are measuring, so it contaminates the
result directly rather than merely burying it.

➡ **So: do the LOOP capture FIRST, analyse it, and only then commit to the 14-capture matrix.** It
measures the source distortion and the noise floor together, it is already on the plan (§2d, §3), and
it costs five minutes against an hour. Check the loop's THD is well under the ~0.5 % the pedal itself
produces; if it is not, the line output via a proper balanced-to-unbalanced connection is the fix.

📌 Also worth confirming whether the −95.8 dBFS was measured through the loop or with the pedal in
circuit. The pedal will add its own; Johnson noise from its 139 kΩ output impedance alone is only
−116 dBFS, so any meaningful rise is the JFET stage and worth knowing either way.

## ⭐ 3. Capture the clean loop AND the pedal bypassed — they are different tests

- **Loop only (no pedal):** the M0 unity check, which has never run. It also **directly settles the
  LF question**: notes #19a/#20 establish a real, linear, single ~20–35 Hz pole that this dataset
  cannot place. **If the loop is flat to 10 Hz and the pedal capture still rolls off near 30 Hz, the
  pole is the pedal's** and `C10` gets changed with evidence behind it.
- **Pedal in circuit, bypassed:** pins `kInputRef` directly and catches anything in the bypass path.
  If the bypass is true bypass these two will be identical, which is itself the confirmation. One
  extra 5-minute bounce.

## 📌 4. The volume matrix

7:00 · 9:00 · 10:30 · 12:00 · 13:30 · 15:00 · 17:00, both modes — a good spread, and every one of
them **within one rig**, which is what dissolves the confound that wrecked the existing dataset
(note #7's M4: volume is 1:1 confounded with unit AND trainer there, and back-solved taper exponents
came out 2.5 / 3.2 / 7.8).

- ⚠ **7:00 is full CCW and the circuit genuinely silences** (Ra → 0 shorts node E). **Verify it goes
  silent, but do not spend a capture on it** — there is no signal to train or analyse.
- ⇒ 6 positions × 2 modes + loop + bypassed = **14 captures ≈ 70 minutes**.
- ⭐ What this settles that nothing else can: the **taper exponent** (shipped `p` = 2.0; notes #20a's
  P1 and P3 both want 2.4–2.7), the **3.92 dB vs the maker's 1–2 dB fall-back**, the **C10 corner**,
  and the **volume peak position** (predicted at `Ra` = 176 k = 35 % of the pot, i.e. 1–2 o'clock).

## ✅ 5. Write it all into a text file beside the captures

Interface make/model and which input was used · the DMM reading and the tone frequency used ·
`input_level_dbu` · `output_level_dbu` · the input gain setting · sample rate · anything unusual.

⚠⚠ **And say WHERE each level was measured.** The existing dataset's +12.2 dBu (interface output)
versus −12 dBu (pedal jack, after a reamp box) is a 24.2 dB difference between two correct numbers,
and confusing them would invert every harmonic conclusion on record (note #10).

---

## What the session cannot fix, and does not need to

**MID is the maker's own invention, not an Echoplex position.** The owner's unit is the two-position
original: **BRIGHT = early-1970s EP-3, DARK = late-1970s EP-3**, matching the maker's published
"Early 1970s / Late 1970s / Hybrid Early & Late" naming. So the missing switch position is the
*synthesised* one, and the two real voicings are both captured. MID's τ = 38.263 µs stays inferred
from P1/P2 — scale it by the measured **cap ratio** (2.24) rather than transplanting an absolute
value, since the ratio is the robust quantity and the absolute is not.

⚠ Since BRIGHT is the same physical position on both variants, its shelf zero should land near the
**1.86 kHz** P1 and P2 measured. **Fit it rather than assuming it** — note #2's label reasoning was
confidently wrong once already, and a two-parameter shelf fit returns ≤ 0.25 dB residuals.


---

# Session results (2026-09-10)

## ⭐⭐ Use the BYPASS capture as the deconvolution reference, NOT the bare loop

Both reference captures pass every integrity check. But they are **not interchangeable**, and the
difference is exactly the size of the project's whole HF target.

**`p4_V1030_bypass.wav` minus `loop_V0000_none.wav`**, level-matched, shape only:

| 20 Hz | 1 kHz | 4 kHz | 8 kHz | 16 kHz | 18 kHz |
|---|---|---|---|---|---|
| −0.047 dB | −0.013 | +0.153 | +0.294 | +0.367 | +0.372 |

Best description: **the bare loop carries one extra pole at 48.4 kHz** (residual 0.079 dB). A
bypassed pedal cannot ADD treble, so the loop path had something the bypass path did not — almost
certainly a different or longer cable between the two takes.

➡ **The bypass capture has the SAME cabling as every pedal capture; the bare loop does not.**
Deconvolving against the loop would import **0.37 dB of HF error at 18 kHz** into every result.
⛔ The loop's remaining jobs are the M0 unity check and nothing else.

## ✅ The bypass is genuinely TRUE bypass — nothing to model

- Nulls against the loop at **−70.8 dB**.
- Broadband level: loop −14.001 dBFS, bypass −13.993 dBFS — **0.008 dB of insertion loss**.
- ⇒ No buffer, no loading, no bypass-path component in the model. §2c's input-loading correction
  therefore does **not** apply to this capture; it applies only to the ACTIVE pedal captures, where
  the source becomes the pedal's ~130 kΩ instead of the interface's output.

## ⭐⭐ The chain has NO low-frequency pole, confirmed twice

| capture | 20–40 Hz, re midband |
|---|---|
| bare loop | −0.006 .. +0.065 dB |
| bypassed pedal | −0.053 .. +0.018 dB |
| bypass minus loop at 20 Hz | −0.047 dB |

➡ **This is the measurement circuit.md notes #19/#19a/#20 were blocked on.** The real, linear,
single ~20–35 Hz pole seen in all seven NAM captures is **not** in this rig. So when the P4 pedal
captures land, any LF pole they show belongs to the **pedal**, and `C10` (or whatever carries it)
gets changed with evidence — or they show none, and it was the other three trainers' rigs.

## 📌 Calibration is stable across both takes

`output_level_dbu` = **+14.30 dBu** (loop) and **+14.29 dBu** (bypass) — 0.01 dB apart, which says
the play-side setting did not drift between them. Record full scale ≈ **4.02 V RMS**.

## ⚠ One unexplained, immaterial discrepancy — recorded so it is not rediscovered

The loop's quietest sweep reads 1.72 % THD where the bypass reads 0.029 %, yet **their time-domain
residuals against the played signal are identical** (median −66 dBFS, worst −44 dBFS, same blocks
flagged in both). So neither capture is defective; the discrepancy lives in the harmonic gate at a
level where it is at its own floor — the bypass figure sits *below* its own noise-only prediction of
0.052 %. ⛔ Not worth chasing: the quietest sweep is the linear FR reference, and no harmonic result
is read from it.


## ⭐ The 7:30 bonus capture is worth taking — but for its CORNER, not its LEVEL

`p4_V0730_bright.wav` / `p4_V0730_dark.wav` parse fine (x = 0.05, Ra = 1.25 kΩ at p = 2.0).

⭐⭐ **What it genuinely adds: LF corner leverage.** The C10 corner moves with VOLUME, and 7:30 puts
it at **67.6 Hz** — right where the sweep has plenty of energy and a corner is easy to fit — against
12.9–39.8 Hz for the rest of the matrix. **Span 3.09× → 5.25×.** That is direct leverage on the one
question the whole session exists to settle: is the extra ~20–35 Hz pole volume-DEPENDENT (the
pedal's) or not (the chain's)? And the corner is **robust to knob-setting error**: ±10 minutes of
rotation moves it only 65.1–69.6 Hz, about ±3 %.

⚠⚠ **What it does NOT add, contrary to the obvious reading: taper leverage.** At 7:30 the network
gain spans **27 dB** across the plausible exponent range (p = 1.6 → +69.2 dB, p = 2.7 → +42.2 dB),
which looks like enormous discrimination. It is not, because **the same steepness amplifies the
knob-position error by the same mechanism**: ±10 minutes there is worth **−6.75 / +4.60 dB**.

| knob | taper signal (p 1.6→2.7) | position noise (±10 min) | ratio |
|---|---|---|---|
| **7:30** | 27 dB | ±5.7 dB | **≈4.7** |
| **10:30** | ≈2.8 dB | ±0.18 dB | **≈15** |

The algebra: sensitivity to the exponent goes as `ln(x)·dp`, sensitivity to position as `p·dx/x`, and
`dx/x` blows up faster than `ln(x)` grows. ➡ **Fit the taper from the MIDDLE of the sweep and use
7:30's corner, not its level.** ⛔ Do not treat a level mismatch at 7:30 as a taper error.

📌 Output at 7:30 sits ~22 dB below 10:30, so the quietest sweep records near −63 dBFS: 37 dB raw
SNR plus 56 dB of sweep processing gain. Fine for frequency response; its harmonics will be floor,
which does not matter — nothing reads harmonics from the bottom of the volume range.


## 🔧 Measuring the interface's input impedance (checklist §2c)

**Series-resistor substitution.** Record a tone twice — once patched straight in, once through a
known resistor in series with the tip — and the level drop gives `Zin` outright:

```
g   = 10^((B - A)/20)        A = direct patch dBFS, B = through-R dBFS
Zin = R * g / (1 - g)
```

**Procedure**
1. **Measure the resistor with the DMM first** and use that value, not the marked one. Its
   resistance accuracy is ±(1.0 % + 2 digits) ≈ ±1.2 %, far better than a 5 % part.
2. Play a **100 Hz** sine at a moderate level (≈ −20 dBFS: clear of the noise floor, nowhere near
   clipping). Record ~5 s. Note the RMS in dBFS = `A`.
3. Put the resistor in series with the **tip** conductor, **right at the input jack** so the cable
   capacitance sits before it, not after. Record the same tone = `B`.
4. Cross-check at **400 Hz**. The two must agree; if 400 Hz shows a bigger drop, stray capacitance is
   intruding and the 100 Hz figure is the one to trust.

**Which resistor** — anything from 100 kΩ to 470 kΩ. Expected drop against a 1 MΩ input:

| R | drop if Zin = 470 k | 1 M | 2 M | Zin precision from ±0.05 dB |
|---|---|---|---|---|
| 100 kΩ | −1.68 dB | −0.83 | −0.42 | ±6.7 % |
| 220 kΩ | −3.34 dB | −1.73 | −0.91 | ±3.3 % |
| **470 kΩ** | −6.02 dB | −3.35 | −1.83 | **±1.8 %** |
| 1 MΩ | −9.90 dB | −6.02 | −3.52 | ±1.2 % |

✅ **Precision is not the constraint — do not overthink the choice.** The correction we apply is
`Zin/(Zin+Zout)` with `Zout` ≈ 130 kΩ, so **±7 % on `Zin` moves it by only ±0.07 dB**, against the
0.34 dB of sweep spread being corrected. Even the 100 kΩ case is comfortably good enough.

⚠ **The real constraint is stray capacitance, and it pushes the other way.** The series R plus the
capacitance after it makes a low-pass, which reads as a falsely LOW `Zin`. Corner frequencies:

| R | 50 pF | 150 pF | 500 pF |
|---|---|---|---|
| 100 kΩ | 32 kHz | 11 kHz | 3.2 kHz |
| 470 kΩ | 6.8 kHz | 2.3 kHz | 677 Hz |
| 1 MΩ | 3.2 kHz | 1.1 kHz | **318 Hz** |

⛔ A 1 MΩ resistor with a normal cable after it puts the corner at 318 Hz — **−0.5 dB at 220 Hz**,
which would read as a much lower `Zin` than the truth. ➡ **220 kΩ or 470 kΩ at 100 Hz, resistor at
the jack** is the sweet spot: good precision, corner safely above the tone.

⚠ Check for hum pickup: record silence with the resistor inline. Several hundred kΩ in a high-Z
circuit is an antenna. If the noise floor jumps, shield the joint or drop to 100 kΩ.

📌 **Cost of skipping it and just assuming 1 MΩ:** if the input is really 470 kΩ, the mean error of
0.78 dB is harmless (it is absorbed into `kOutputMakeup`, which is fitted anyway), but the **0.34 dB
of spread across the volume sweep is not** — that is the measurement the taper fit depends on.
⛔ Do NOT try to read `Zin` with the DMM's resistance range: on a powered input the meter's test
current meets active circuitry, and powered down it meets protection diodes. Neither is the answer.


## ⚠⚠ PAD REVISED TO 12 dB (2026-09-10) — and where the load line survives

9 dB was not enough: 13:30 still clipped, and a 10:30 bright retake landed at **−0.01 dBFS with 6
samples over**. The model's own table (worst case +7.14 dBFS at 13:30 bright) **under-predicts the
real pedal by ~4 dB** — which is expected and not a fault: **`kOutputMakeup` is exactly 1.0 and
unanchored**, so the plugin has never had its absolute output level calibrated. ➡ **Set the pad from
the meter, never from the model's table.** That 4 dB is itself the first crude measurement of the
constant this session exists to pin.

⇒ **`_pad12` for the ENTIRE matrix**, every position and both modes. `input_level_dbu` = **+0.20 dBu**
for that group. One pad across the matrix, so the taper fit compares like with like.

### ⛔ At pad 12 the matrix has NO load-line coverage at all
Triode onset is −7.5 dBFS unpadded and the hottest cell is −1 dBFS, so a pad eats the 6.5 dB of
coverage 1:1. **Pad 12 leaves −5.5 dB: the stage never leaves its linear region.** That is fine —
the matrix exists for the taper, the FR and the LF pole, all linear. But it makes the pad-0 captures
the ONLY nonlinear data in the session:

| file | peak | what it is |
|---|---|---|
| `p4_V0730_bright.wav` | −10.83 dBFS | ⭐ load line, bright |
| `p4_V0730_dark.wav` | −16.37 dBFS | ⭐ load line, dark |
| `p4_V0900_dark.wav` | −2.46 dBFS | ⭐ load line at a SECOND drain load |

⭐⭐ **`p4_V0900_dark` is more valuable than it looks.** The drain load runs 9.8–17.9 kΩ across the
rotation and moves the triode onset by ~4 dB, so 7:30 alone samples one end of it. 9:00 gives a
second point, which turns that caveat into a measurement. ⛔ **Do not overwrite any of these three.**
📌 **Optional, one capture, completes the set: `p4_V0900_bright_pad4.wav`.** 9:00 bright clipped at
pad 0, but pad 4 still leaves +2.5 dB of coverage — the only way to get bright at a second drain load.

### 📌 FOLDER LAYOUT: everything lives in `captures/`, distinguished by the SUFFIX
The `hotdrive/` folder has been folded back in. Now that `_pad<N>` is in the filename the folder was
redundant, and worse: ⚠⚠ **`p4_V1030_bypass.wav` had been moved into it, which hid the deconvolution
reference from `find_reference_captures()` entirely.** A capture the harness cannot see is a capture
that silently does not exist. Only `clipped/` stays separate, because those files must never be
loaded at all.

⚠ **The captures are NOT in git** (`.gitignore` excludes `*.wav`), so nothing here is recoverable
from version control. Back up `captures/` before any reorganisation.
