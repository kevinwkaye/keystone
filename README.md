<img src="docs/hev.png" width="96" alt="Heavy for his Size">

# Keystone

Kevin's live bass rig in software: a clean low end, three levels of dirt, synths, drones and grain textures, played from a footswitch, an expression pedal, a watch and a phone, with the band's visuals projected onto the amp.

It runs headless on a Raspberry Pi 5 in the pedal's housing, with a Behringer UM2 as the audio interface. The same app also runs on a Windows laptop. Update firmware on the phone installs new releases on the Pi, and [Releases](../../releases) has the Pi image, the Pi and laptop packages, and the phone and watch apps.

## Signal chain

Bass in, split by a phase-matched crossover (100 Hz by default):

- **Low band**, clean and mono: tight shelf below 45 Hz, weight around 80 Hz, light compression with a firmer slap mode.
- **Drone**, from the clean signal: tap to grab a note, tap again to let it go. Rebuilt as sine partials, ducks while you play, with its own deeper mono dimension.
- **High band**: Edge and Drive through your NAM capture (A1 or A2) or a modelled fallback, Bitrot (a tracking glitch synth after the EarthQuaker Data Corrupter), Gloss (a lush pad that follows whole notes), Growl and Cut preamp voices, an envelope filter, a wah, a mono dimension, then Speckle (the Hologram Microcosm's granular effects), a TimeLine-style delay and a BigSky-style reverb.
- **Bloom**: one expression sweep brings in Speckle, then pushes the delay and reverb, with a limiter in every feedback loop.
- **Output**: mono, output iron, level, limiter, Amp or PA voicing.

Kuddle, a second rig for home, and Kaleidoscope, fourteen of Kevin's pedal plugins hosted one at a time, share the same app.

## Layout

| Folder | What's in it |
|---|---|
| `core/` | The portable engine and controller in plain C++20: every effect, the setlist, footswitch jobs, the recorder and the phone protocol. `core/src/spk` and `core/src/bit` are Speckle and Bitrot as in their plugins, kept identical so they null against them. `core/tests` holds the render tests, the sound bench and the CPU benchmark. |
| `app/` | The headless app (JUCE): ALSA on the Pi or WASAPI and ASIO on the laptop, direct MIDI footswitches, the phone link, the projector, the service button, Send report and Kaleidoscope's plugin host. |
| `pi/` | Everything the Pi needs: the systemd services, the A/B slot updater, Keystone's own Wi-Fi, the power and maintenance helpers, and the image builder. |
| `android/` | The phone controller (`app`, a web page in a small Android shell) and the Wear OS watch controller (`watch`). |
| `shared/` | Code shared with the Flip Fella plugins: footswitch settings and Keeper. |
| `kuddle/` | The Kuddle plugin, whose engine Keystone also runs. |
| `tools/` | The laptop's setup scripts, a link tester and the Speckle null test. |
| `installer/` | The Windows installer (Inno Setup). |
| `docs/` | The footswitch map (`foot-control-plan.md`) and the release notes. |

## The phone link

JSON lines over TCP port 47801, with Nagle off. Keystone sends a beacon on UDP 47800 every second and answers `KEYSTONE?` probes, so the phone finds it on Keystone's own Wi-Fi or a hotspot with no typing. Every Tone page is built from the parameter list Keystone sends (`core/include/keystone/Params.h`), so a new control appears on the phone without a phone update. The watch updates itself from the Pi on TCP 47802.

## Building

GitHub Actions builds everything on each push to `main` and puts the Pi image and package, the laptop installer and both Android apps on the release. To build locally: `cmake -S . -B build && cmake --build build --config Release`. CMake fetches JUCE 8 and NAM core. `-DKEYSTONE_BUILD_APP=OFF` builds just the engine and its tests.

Third-party code: JUCE (AGPLv3 / JUCE licence), NeuralAmpModelerCore (MIT), Eigen (MPL2), nlohmann/json (MIT), Roboto Condensed (OFL).
