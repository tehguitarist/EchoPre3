# Clipped captures — DO NOT USE

Recorded 2026-09-10 at the session's nominal drive, before it was discovered that the pedal's own
boost puts BRIGHT over the interface's input at every volume position from 9:00 up.

| file | peak | samples at/over 0.999 |
|---|---|---|
| `p4_V0900_bright.wav` | 0.00 dBFS | 11,172 |
| `p4_V1030_bright.wav` | 0.00 dBFS | 290,877 |

⚠⚠ **Kept only so they are not re-recorded by accident, and moved out of `captures/` so nothing
picks them up.** They are not partially usable: flat-topped peaks look exactly like the pedal
saturating, and saturation is what the load-line work measures — so using them would contaminate
the single most important measurement in the session with an artefact that mimics it.

Superseded by the `_pad9` re-takes.

## `aborted_take_Audio-Bus256.wav`

A 135.9 s partial take (the signal is 289.5 s) that landed in `captures/` under the DAW's default
bus name, timestamped alongside `p4_V0730_dark.wav`. Polarity −1 and peak −21.9 dBFS, so it is an
aborted pedal take rather than anything unique. Moved here because an unparseable filename in
`captures/` makes `find_captures()` raise and takes every analysis script down with it.

📌 Worth watching for: the DAW names an export after the bus, not the take. Rename on export.
