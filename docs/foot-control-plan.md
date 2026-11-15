# Keystone footswitch plan

Keystone reads the footswitch's MIDI device directly (it's a standalone app, so with nothing chosen it opens every real MIDI input). Defaults are the house ones: momentary CC 20, 21, 22, 23 for A to D and CC 11 for expression. Any channel is accepted from a device read directly, so Kevin's Chocolate Plus (which always sends on channel 1) works with no setup. Learn on the phone (Tone, Footswitches) remaps any switch or the pedal. Settings are shared with every Flip Fella plugin through `Flip Fella Pedals.xml`, and saving keeps attributes other plugins wrote.

| Switch | Tap | Hold |
|---|---|---|
| A | Edge / Drive (option: Off, Edge, Drive) | Previous song |
| B | Bitrot on or off | Next song |
| C | Filter on or off | Tuner |
| D | Drone capture, tap again to stack, double tap to clear | Slap compression |
| C + D | | Keep (held 1 s) |

- Taps fire on release, holds at 0.45 s, the C + D hold at 1 s. A + B is switched off as a combo, so A then B quickly is two taps.
- While the tuner is on, any tap leaves it and does nothing else.
- Expression is the Bloom sweep: heel to a third the drone swells in, a third to two thirds the grains join, two thirds to toe delay and reverb climb towards self-oscillation. Heel and toe calibration (Tone, Footswitches) fixes the EX-P reading backwards on the Chocolate Plus.
- Every footswitch job also has an on-screen button: the stomp tiles and song arrows on Play, Tuner in the nav, Keep in the nav, slap on the Low compressor page.

## Kevin's own setup

CubeSuite in Custom mode, footswitches A to D on CC 20 to 23, pedal jack as Expression Pedal on CC 11, power switch on U (USB MIDI). The EX-P needs a 3.5 mm to 6.35 mm TRS adapter.
