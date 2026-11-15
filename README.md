<img src="docs/pebble.png" width="96" alt="Pebble">

# Keystone

Kevin's signature live bass rig in software: clean low end, three levels of dirt, and drone and grain textures, controlled from footswitches, an expression pedal and a phone.

Version 0.1 runs on a Windows 11 laptop with no screen, lid closed, on top of the amp. Download the installer and the phone app from [Releases](../../releases).

## Signal chain

Bass in, split by a phase-matched crossover (100 Hz by default):

- **Low band**, clean and mono: tight shelf below 45 Hz, weight around 80 Hz, light compression with a firmer slap mode.
- **Drone**, from the clean signal: tap to grab a note, tap again to stack. Rebuilt as sine partials (under a cent out), ducks while you play, tone, octave up, its own deeper mono dimension with the sub kept dry.
- **High band**: Edge and Drive voices through your NAM capture (A1 or A2) or a modelled fallback, Bitrot (fuzz and bit crush, can go first), SVT-style Growl and Darkglass-style Cut in parallel with one blend, Quack's envelope filter, a mono-voiced dimension, pitched-up grains high-passed into the open 1 to 4 kHz space, delay, reverb and shimmer.
- **Bloom**: one expression sweep brings in the drone, then the grains, then the wash, with a limiter in every feedback loop.
- **Output**: mono, level, limiter, Amp or PA voicing.

## Layout

| Folder | What's in it |
|---|---|
| `core/` | The portable engine and controller: plain C++20, no Windows-only code. Builds for Windows x64 and Linux ARM64. `core/tests` holds the render tests and CPU benchmark. |
| `app/` | The headless app (JUCE): WASAPI exclusive or ALSA audio, direct MIDI footswitches, Keeper, the phone link. |
| `android/` | The phone controller: Flip Fella screens bundled in a small Android shell. The same page can be served by the Pi later. |
| `installer/` | The Windows installer (Inno Setup). |

## The phone link

JSON lines over TCP port 47801, with Nagle off. Keystone sends a beacon on UDP 47800 every second and answers `KEYSTONE?` probes, so the phone finds it on its own hotspot. Every Tone page is built from the parameter list Keystone sends (`core/include/keystone/Params.h`), so a new control appears on the phone without a phone update.

## Building

GitHub Actions builds everything on each push to `main` and puts the installer, APK and Linux build on the release. To build locally: `cmake -S . -B build && cmake --build build --config Release`. CMake fetches JUCE 8 and NAM core.

Third-party code: JUCE (AGPLv3 / JUCE licence), NeuralAmpModelerCore (MIT), Eigen (MPL2), nlohmann/json (MIT), Roboto Condensed (OFL).
