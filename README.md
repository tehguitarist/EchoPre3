# Echo Pre 3

![Build](https://github.com/tehguitarist/EchoPre3/actions/workflows/ci.yml/badge.svg?branch=master)
[![License](https://img.shields.io/badge/License-AGPLv3-blue.svg)](https://opensource.org/license/agpl-v3)
[![Downloads](https://img.shields.io/github/downloads/tehguitarist/EchoPre3/total)](https://somsubhra.github.io/github-release-stats/?username=tehguitarist&repository=EchoPre3&page=1&per_page=30)

Echo Pre 3 is a circuit-level emulation of the Echoplex EP-3 preamp (AU/VST3), traced from the
**Chase Tone Secret Preamp** schematic. It's a single-JFET, high-headroom clean preamp — no
clipping diodes anywhere in the signal path — so the entire character comes from a 2N5457
common-source gain stage and a three-position source-bypass tone switch, both solved sample by
sample as a [Wave Digital Filter](https://en.wikipedia.org/wiki/Wave_digital_filter) network
rather than curve-fit.

> Echo Pre 3 is an independent circuit emulation built from schematic analysis and is not
> affiliated with or endorsed by Echoplex, Oberheim, or Chase Tone.

<img src="docs/images/screenshot.png"/>

## How close is it?

Every constant in the model is measured rather than assumed, including the two absolute level
anchors and the VOLUME taper. They come from a dedicated capture session on a real unit: a full knob
rotation in both switch positions through a measured, near-flat recording chain, with both of that
chain's calibration figures written down, plus loop and true-bypass references and a set of
tone and twin-tone probe captures that reach the circuit's clipping region.

Measured against that pedal at the shipped 4×/8× oversampling, with the rig deconvolved and the
interface's input loading undone, at matched drive:

| target | result |
|---|---|
| frequency response, ±0.5 dB over 80 Hz–12 kHz | ✅ **0.18–0.43 dB** worst, every knob position |
| frequency response, ±1.0 dB over 20 Hz–20 kHz | ✅ no band over target at any knob position |
| phase, ±5° over 40 Hz–16 kHz | ✅ **2.74°** worst |
| absolute level, ±0.5 dB | ✅ all 15 captures pass |
| per-band THD/H2, ±5 % (0.42 dB), 20 Hz–8 kHz | ✅ **0.41 dB RMS** core, 0.48 dB below 200 Hz † |

Those are the DARK figures, which are the model's own error. **BRIGHT runs up to ~1.5 dB bright at
6.5–10 kHz and makes ~1.3 dB less second harmonic, and that is deliberate:** the model is voiced to
the newer three-position units, measured from a rig-cancelling differential across two of them,
while the unit used for the absolute calibration above is an earlier two-position variant whose JFET
is about 25 % weaker. One constant reverses that choice if you want the other voicing; the trade is
recorded in [`.claude/rules/circuit.md`](.claude/rules/circuit.md) note #28.

† The THD row is the one figure that needs that voicing difference taken out before it means
anything, since a transconductance difference is a distortion difference: it is measured with the
model rendered at the calibration unit's own transconductance. Against the shipped voicing the same
measurement reads 2.0–2.3 dB, essentially flat from 20 Hz to 8 kHz — and a residual that is constant
across three decades is the voicing offset, not a band-edge defect. The other four rows are at the
shipped voicing as-is.

Two things rest on thinner evidence than the rest, and are documented rather than hidden: the
transistor's **triode branch** is constrained by a single capture cell, because the recording chain
couples drive depth to output level and only one knob setting reaches that region; and the **MID**
switch position is scaled from the three-position units, since the calibration unit does not have
one. The per-band THD figure is also structurally unmeasurable above 8 kHz at 48 kHz, because a
tone's second harmonic is past Nyquist there. [`.claude/rules/circuit.md`](.claude/rules/circuit.md) records
every value the model uses and what pins it; [`docs/build-plan.md`](docs/build-plan.md) is the dated
development log, including the conclusions that had to be reversed along the way.

## Overview

The signal path is one gain stage: `input LPF → JFET common-source stage (2N5457) → 3-way
source-bypass tone switch → coupled output/VOLUME network`. There's no VREF divider and no
op-amps — the JFET self-biases off a 22 V charge-pumped rail, and the transistor's own transfer
curve is the only nonlinearity in the circuit. `chowdsp_wdf` has no native JFET element, so the
stage solves the device's equations directly: a Norton current source (not a voltage source — a
degenerated common-source stage is a current source), with the transistor's transfer law, its
source-degeneration network and its drain load all solved together, per sample, by a safeguarded
implicit solve that is asserted against an independent oracle at machine precision.

A few things this pedal's build surfaced that were worth solving properly rather than
approximating:

- **The MODE switch's "brighter" position isn't a bigger shelf, it's a lower one.** Both cap
  positions reach the same HF plateau; they only differ in where the lift starts. Fitting that
  shelf (rather than reading a plateau off an FFT) also caught that the switch's own printed
  labels were backwards.
- **Degeneration suppresses distortion twice, not once.** The feedback network that sets the JFET's
  gain also filters the harmonic product it generates, and an early version of the stage applied it
  only once — worth 16 dB of excess distortion. Only an independent solve of the transistor equation
  caught it; every linear frequency-response test passed the whole time. That finding is what
  eventually replaced the fitted shaper with the solved device equations described above.
- **The transfer law isn't square.** Textbook JFET models use an exponent of 2; fitted against tone
  captures that actually reach the clipping region this unit comes out at **1.60**, and the fit is
  load-bearing rather than cosmetic — it was made on the second harmonic at one frequency, and the
  third harmonic, a 14× different frequency and the compression of the fundamental all improved
  out-of-sample, with their offsets going to zero rather than merely shrinking. A happy accident
  followed: 1.60 is exactly 8/5, so a change of variable turns the per-sample solve into a
  polynomial one and removes the transcendental from the inner loop — an identity, not an
  approximation, and worth 2× the chain's CPU.
- **A source-port WDF read silently inverted the input network's polarity.** Magnitude was
  perfect at every frequency; only a phase/DC-step check caught the missing 180°, which would have
  cancelled the JFET's own inversion and left the plugin backwards.
- **The low-OS top-octave droop is derived, not fitted.** A trapezoidal-cap WDF is the bilinear
  transform of its own analog prototype, so the correction shelf's coefficients fall out of that
  transfer function in closed form and self-scale to whatever sample rate the host supplies.

## Features

- **Single solved JFET gain stage** — 2N5457 common-source stage with a measured transconductance,
  a measured transfer-law exponent and a measured pinch-off voltage, solved implicitly against both
  its source-degeneration network and its VOLUME-dependent drain load, so compression, the harmonic
  series and the clipping onset all fall out of the device rather than being fitted separately
- **Three-position MODE switch** (Bright / Dark / Mid) — three precomputed source-bypass
  topologies, each a fitted first-order shelf against the un-bypassed stage
- **Non-monotonic VOLUME control**, faithfully reproduced — the pot grounds its wiper and shunts
  both node E and OUT, so gain rises to a peak around 1–2 o'clock and genuinely falls back past
  it, exactly as the original EP-3's volume wiring does
- **Oversampling on the linear-phase FIR path** (1×/2×/4×/8×, separate live/render factors) with a
  closed-form low-OS top-octave droop restore, so the base-rate response tracks the oversampled
  one instead of needing a high factor to sound right
- **Output load selector** (68k / 1M / None) — this pedal's output impedance is 59–102 kΩ and moves
  with the knob, so how much boost it actually delivers depends on what it drives, by about 10 dB
  end to end. See [Output load](#output-load) below
- **A per-sample implicit solve with no tolerance to tune** — the solve's iteration counts are set
  on audibility (a full-plugin null against a fully-converged build sits at −120 to −150 dB), while
  the tests separately assert the *algorithm* against an independent oracle at machine precision, so
  a structural error still shows up immediately instead of hiding inside a loosened bound
- **Calibrated I/O** — input and output trim with VU-style metering, Trim Link to hold overall
  loudness while pushing drive
- **True bypass** with a crossfade and a deterministic oversampler reset, so post-bypass output
  never depends on how long the pedal was off
- **Resizable UI** from 50% to 250% — remembered per session, and clamped to what the
  display can actually show, so a large preset can't put the resize corner off-screen

## Where to find things

```
src/
  PluginProcessor.{h,cpp}    Plugin entry point, parameter layout, processBlock
  PluginEditor.{h,cpp}       Top-level UI layout
  ui/PedalFace.{h,cpp}       The one-knob-plus-switch centre pedal face
  dsp/
    InputNetwork.h             Input LPF / gate-bias network
    JfetStage.h                 The 2N5457 stage — measured transfer law, solved implicitly
    OutputNetwork.h             Coupled output/VOLUME network (not a simple divider)
    OsDroopRestore.h             Derived low-OS top-octave shelf
    EchoPreDsp.h                 Wires the stages into the full signal chain
  utils/TaperUtils.h          Potentiometer taper curves

tests/                      13 CTest targets: per-stage transfer functions and phase, the JFET
                            solve against independent oracles, bypass clicks, VOLUME automation,
                            oversampling fidelity, CPU, a full control sweep, a UI snapshot
analysis/                   Offline render tool + Python harness used to compare the plugin
                            against real-pedal captures (FR, THD, phase, compression, null),
                            including the capture set the calibration constants are fitted from
schematics/                 Source schematic images

docs/                       Development log, the capture dataset, and the generic engineering
                            references (measurement discipline, nonlinear modelling, calibration)
.claude/rules/              Circuit/DSP/architecture/UI/build references — circuit.md is the
                            source of truth for every component value and measured constant
```

## Building

Requires CMake 3.15+, a C++17 compiler, and the JUCE, chowdsp_wdf, and xsimd submodules
(xsimd accelerates the WDF matrix math). Builds as an Audio Unit + VST3 on macOS, VST3 on
Windows/Linux.

```bash
git clone --recurse-submodules https://github.com/tehguitarist/EchoPre3
cd EchoPre3
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target EchoPre3_AU    # macOS only
cmake --build build --target EchoPre3_VST3  # all platforms
```

The AU is copied into `~/Library/Audio/Plug-Ins/Components/` automatically after the build.
Logic caches AU components — bump the project `VERSION` in `CMakeLists.txt` to force a rescan.
Validate without opening a DAW:

```bash
auval -v aufx Ep3p Lprc
```

### Running the test suite

Each circuit stage has a standalone validation executable checked against an analytic transfer
function, an independent implicit solve, or a known-answer probe derived from the circuit itself.
They're registered with CTest, so the whole suite runs as one pass/fail gate — this is also what
CI runs on every push/PR:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

### Output load

The pedal's output impedance is **59–102 kΩ** — unusually high, because the VOLUME wiper is grounded
and a 110 kΩ resistor bridges to the jack. So how much boost it delivers genuinely depends on what
it drives, by about **10 dB end to end**. The `LOAD` selector in the bottom strip says what to model:

| LOAD | boost at 1:30 (DARK) | peak-to-full-CW fall-back | represents |
|---|---:|---:|---|
| **68k** (default) | +2.7 dB | 1.6 dB | a typical amp front end — matches the maker's published control law |
| 1M | +9.7 dB | 3.6 dB | a modern amp or line input |
| None | +10.5 dB | 3.9 dB | open circuit — the raw measured circuit |

Above 100 Hz the load is a pure level change (0.00 dB of shape), and it does not affect the
harmonics at ordinary playing levels. What it does move is the low-frequency corner (−0.8 dB at
20 Hz into 68k) and, through the drain-node impedance, the clipping onset (+0.45 dB later).

### Performance

CPU usage (% of realtime, stereo, 4 s render) and algorithmic latency at each oversampling factor,
measured by `PerfBenchmark` (`tests/PerfBenchmark.cpp`) on an Apple Silicon Mac, Release build.
Figures will vary by machine. The range across the three MODE positions is given because BRIGHT
drives the JFET solve hardest.

| OS factor | CPU % of realtime | Latency (samples) | Latency (ms @ 48 kHz) |
|-----------|-------------------:|-------------------:|-----------------------:|
| 1×        | 0.65–0.79%         | 0                   | 0.00                   |
| 2×        | 1.82–2.00%         | 49                  | 1.02                   |
| 4× (default) | 3.16–3.54%      | 60                  | 1.25                   |
| 8×        | 5.85–6.58%         | 64                  | 1.33                   |

Those are measured with a swept sine. **Real programme material costs about 0.35 points more — a
plucked-chord proxy at the same RMS reads 3.58% against the sine's 3.23% at 4×**, roughly 11%.
`PerfBenchmark` reports five stimuli (sine, tone, pink noise, white noise, chord) at matched RMS so
that is measured rather than assumed: a periodic stimulus is the best case for the branch predictor,
and this DSP branches every sample. Add ~0.35 points to the table for a realistic worst case.

CPU is also mildly level-dependent, because the JFET stage's triode branch — entered only by the top
few dB at a high VOLUME setting — costs more than its saturation branch. Measured at the stage,
192 kHz: 56 ns/sample at ordinary playing levels against 93 ns at a 0 dBFS peak, against 148 and
226 ns before the solve was reworked.

Bypass is a flat **~0.01%** at every factor (the DSP chain, including the oversampler, is skipped
rather than run and crossfaded).

VOLUME is applied every 16 samples while it is moving, rather than once per host block, because the
control law is steep enough near full CCW that a once-per-block update is an audible staircase — a
one-second automated sweep stepped 2.4 dB per block at a 512-sample buffer and 10.8 dB at 2048.
Chunked, the step is 0.08–0.10 dB and no longer depends on the host's buffer size at all. The cost is
paid only while the control is actually moving, so none of it appears in the table above, and the
static render is bit-for-bit identical to the unchunked one.

## License

Echo Pre 3 is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

## Credits

Built by Leigh Pierce, using:

- [JUCE](https://juce.com/) — plugin framework and UI toolkit
- [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) — Wave Digital Filter modelling
  library
- [xsimd](https://github.com/xtensor-stack/xsimd) — SIMD acceleration for the circuit's matrix
  math

See also [Tommy](https://github.com/tehguitarist/Tommy), an overdrive plugin built the same way.
