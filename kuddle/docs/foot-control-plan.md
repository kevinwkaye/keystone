# Kuddle footswitch plan

Same pattern as Keystone 0.3.3 so your feet don't relearn anything. Kuddle has no banks.

| Footswitch | Tap | Hold |
|---|---|---|
| A | Mute on/off | Previous preset |
| B | Auto wah on/off | Next preset |
| C | Room on/off | Tuner (any tap leaves it) |
| D | Tape on/off ("Tape push") | Squeeze while held (firmer compression, Opto's Hold D dB, Opto on) |
| C + D held 1 s | Keep | |
| A + B | Off in the plugin (switch rig, in the Keystone app later) | |

Expression (CC 11 by default) works the Melt sweep, or the wah, volume or room mix (Pedal page).
With the wah in manual and "Pedal works the wah when it's in manual" ticked, the pedal sweeps the wah and nothing melts.

Defaults: momentary CC 20 to 23 on channel 16, expression CC 11, all shared with every Flip Fella plugin in AppData\Kevin Kaye\Flip Fella Pedals.xml. The footswitch's own MIDI device is read directly (found by name), so no MIDI track is needed.
