# Keystone footswitch plan

Keystone reads the footswitch's MIDI device directly (it's a standalone app, so with nothing chosen it opens every real MIDI input). Defaults are the house ones: momentary CC 20, 21, 22, 23 for A to D and CC 11 for expression. Any channel is accepted from a device read directly, so Kevin's Chocolate Plus (which always sends on channel 1) works with no setup. Learn on the phone (Tone, Footswitches) remaps any switch or the pedal. Settings are shared with every Flip Fella plugin through `Flip Fella Pedals.xml`, and saving keeps attributes other plugins wrote.

## Banks (0.3.3)

Four footswitches and one pedal, in three banks. Hold A to step Grit, Groove, Glow and back to Grit. The bank sets what taps A, B and C do and what the pedal does. Tap D and every hold are the same in every bank.

| | Grit | Groove | Glow |
|---|---|---|---|
| Tap A | Edge / Drive (option: Off, Edge, Drive) | Auto filter | Speckle |
| Tap B | Bitrot | Dimension | Delay |
| Tap C | Growl/Cut flip (Preamp, Flip to) | Wah on or off | Reverb |
| Pedal | Dirt blend | Wah, or volume with the wah off | Bloom |

| Switch | Hold, every bank |
|---|---|
| A | Next bank |
| B | Next song or part. Forwards only, stops at the last song |
| C | Tuner |
| D | Slap compression |
| C + D | Keep (held 1 s) |

Tap D is the drone in every bank: tap to hold the note, tap again to let it trail off.

- Taps fire on release, holds at 0.45 s, the C + D hold at 1 s. A + B is switched off as a combo, so A then B quickly is two taps.
- While the tuner is on, any tap leaves it and does nothing else, and hold A does nothing.
- Changing bank never changes the sound. Only taps and the pedal do.
- Each song (and each part) saves the bank it opens in (Footswitches, This song).
- Previous song isn't on the footswitches. Use the back arrow on the phone's Play screen.

### The pedal's jobs

- **Dirt blend (Grit).** Heel is clean, the drive is fully in by halfway, and Bitrot comes in from "Bitrot joins at" (25% by default, Pedal jobs page) to its full level at the toe. It only blends dirt that's switched on, so tap A and B choose the flavour and the pedal chooses how much.
- **Wah (Groove, tap C on).** A resonant band-pass from Heel (320 Hz) to Toe (2.2 kHz) on the full sound, so the bottom end stays solid under it. It's its own filter, so with the auto filter on as well, the two stack.
- **Volume (Groove, wah off).** A volume pedal's taper (-12 dB at halfway), before the delay and reverb so tails ring as you pull back. Heel is silent for swells, or set a floor per song (Pedal jobs, Heel volume).
- **Bloom (Glow).** Heel to halfway brings Speckle further in, halfway to toe climbs delay and reverb towards self-oscillation, held back by the feedback limiter. It only pushes effects you've switched on.

**Soft takeover.** After a bank change, a job the pedal set earlier in the song waits until the pedal passes its value, then follows. A job the pedal hasn't touched yet takes it on the first move. Volume starts full and always waits, so landing in Groove with your heel down never mutes you. A new song or part puts every job back to its saved sound until you move the pedal.

### Song parts

A song can be split into parts (Setlist, the dots, Add part, or Part of above on an existing song). Parts share their song's number and don't count towards the 15 songs (40 songs and parts in all). Hold B moves to the next part in one press: the whole sound changes at once, the delay and reverb tails ring over the change, and a held drone carries on. Between songs the drone trails off as before.

Example: part 1 opens in Glow, clean, with delay and reverb on and the pedal on Bloom. Part 2 opens in Grit with Drive on and the space off, so the pedal is already the dirt blend.

Every footswitch job also has an on-screen button: the bank buttons and stomp tiles and song arrows on Play, Tuner in the nav, Keep in the nav, slap on the Low compressor page.

## Kevin's own setup

CubeSuite in Custom mode, footswitches A to D on CC 20 to 23, pedal jack as Expression Pedal on CC 11, power switch on U (USB MIDI). The EX-P needs a 3.5 mm to 6.35 mm TRS adapter.

## Set up footswitch (0.2.1)

Start menu, Keystone, **Set up footswitch** (or tools\Set up footswitch.bat). With the footswitch plugged in by USB it closes Keystone, asks you to press A, B, C and D and rock the pedal, then writes what it heard into `Flip Fella Pedals.xml` (other plugins' settings kept): controller or note, channel, latching or momentary, the pedal's controller and its heel and toe ends, so a pedal that reads backwards is fixed too. Then it starts Keystone again and leaves a record in Documents\Keystone\footswitch setup.txt. It can't change the footswitch itself: if a switch sends program changes, or latches, it says what to set in CubeSuite (Custom mode, CC 20 to 23 momentary, pedal jack as Expression Pedal on CC 11). Latching switches still tap, but can't hold.
