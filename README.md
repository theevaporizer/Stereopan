# Stereo Tool — Workshop System Computer card

Euclidean trigger generator **plus** a panner / crossfader, selected by the 3-way switch.
Fixed-point only in the 48 kHz audio interrupt (no float, no division, no heap).

## Panel

```
 [ LEDs 0 1 ]  In1/In2 level      [ LEDs 2 3 ]  Out1/Out2 level
 [ LEDs 4 5 ]  Euclid trigger / rest

   MAIN knob : pan / crossfade (Up, Middle)  |  pattern rotation (Down)
   X knob    : Euclid length 1..16            (+ CV In 1)
   Y knob    : Euclid hits 1..length          (+ CV In 2)
   Switch    : UP = pan   MID = crossfade   DOWN = rotate pattern

 Audio In 1 / 2   audio sources            Audio Out 1 / 2   processed audio
 CV In 1 / 2      added to X / Y knobs     CV Out 1 / 2      unused
 Pulse In 1       clock (none = 120 BPM)   Pulse Out 1       Euclid hits
 Pulse In 2       reset to step 1          Pulse Out 2       complement (rests)
```

## Behaviour

| Switch | Audio | Main knob |
|--------|-------|-----------|
| Up | One input: equal-power pan to Out 1/2. Both inputs: stereo balance (In1→Out1, In2→Out2) | CCW = Out 1 only, CW = Out 2 only |
| Middle | Equal-power crossfade In1↔In2, same signal on Out 1 and 2 | CCW = In 1, CW = In 2 |
| Down | Pass-through (mono-normalled) | Rotates the Euclid pattern; setting is kept when you leave |

The sequencer runs in every switch position. One clock = one step; the internal clock is
120 BPM (change `STEPS_PER_BEAT` for faster steps). Triggers are 10 ms. A reset on Pulse In 2
makes the next clock play step 1. CV is added to the knob in raw counts (about 340 counts per volt).

## Build

Push to GitHub; the Actions workflow clones `pico-sdk` and `Workshop_Computer`,
builds, and uploads `stereo_tool.uf2` as an artifact. Hold BOOTSEL, plug in USB, drop the UF2 on the drive.

Local build: copy `ComputerCard.h` into `lib/`, set `PICO_SDK_PATH`, then
`cmake -B build && cmake --build build`.

Requires a recent `ComputerCard.h` (`Connected()` and `EnableNormalisationProbe()`).
