# Probe-signal captures

Captures of **`analysis/probe_signal_48k.wav`** (56 s), NOT of the main 290 s test signal.

⚠⚠ **THEY LIVE IN THEIR OWN DIRECTORY ON PURPOSE.** `captures.py::find_captures()` globs
`analysis/captures/*.wav` non-recursively, so nothing here reaches the main-signal comparison
scripts. That matters because those scripts render the plugin through the MAIN signal and diff it
against the capture — handed a probe capture they would not error, they would return a confident
wrong answer. Same hazard as the reference captures, handled the same way: make it structurally
impossible rather than remembered.

Analyse them with the matching tool, which takes an explicit path:

```bash
.venv/bin/python analysis/probe_analyse.py analysis/captures/probe/<file>.wav
```

## Naming

Identical grammar to the main captures — `<unit>_V<HHMM>_<mode>[_pad<N>][_take<N>].wav` — so
`parse_capture()` reads them and `probe_analyse.py` picks the pad up automatically. `p` stands in
for a decimal point (`_pad5p5` = 5.5 dB). No pad suffix means pad 0.

## Why this signal exists

Two things the main matrix structurally cannot measure:

1. **Intermodulation.** The main signal's twin tone is 220 + 660 Hz and 660 = 3 × 220 exactly, so
   every product lands on the harmonic grid (circuit.md note #15). This one uses 220 + 611 Hz.
2. **The load line above 9:00.** VOLUME sits after the stage so it does not change the drive, but
   it moves the drain load from 1.9 kΩ to ~18 kΩ, and that shifts triode onset. Every main capture
   from 10:30 up is pad 12, which never clips.

It is deliberately **self-contained** — its own marker, its own 1 kHz and 220 Hz cal tones, its own
noise-floor tail, and a repeat of each top cell at the end as a within-take drift check. Everything
it reports is a RATIO, so it needs no rig reference and no deconvolution.

📌 Its peak is −1.00 dBFS, identical to the main signal's, so **pads transfer 1:1** between the two.

## ⛔ BRIGHT ABOVE 9:00 CANNOT REACH THE LOAD LINE, and no signal fixes it

The pedal's own output clips the converter before the JFET reaches triode. Best case at each
position, at the lowest pad that still fits: 10:30 −4.0 dB re triode, 12:00 −5.5, 13:30 −6.0,
15:00 −5.5, 17:00 −3.0. That is the rig's ceiling (converter max 5.68 V against a stage that needs
~11 V out to be clipping), not a gap in the plan. Bright's nonlinear data is capped at 9:00.
