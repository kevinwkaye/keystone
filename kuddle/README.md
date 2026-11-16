# Kuddle

Kevin's home rig: clean, warm, buttery bass for hip-hop, soul and jazz. The sibling of Keystone (the band rig).

```
in -> gate -> Mute -> Auto wah -> Valve DI -> Opto -> Tape -> Tone -> Melt roll-off -> Room -> out
```

- `engine/KuddleEngine.h`: the sound, plain C++ on Keystone's DSP helpers (`core/src/Dsp.h`). No JUCE, so the Keystone app and the Pi can host it later.
- `plugin/`: the VST3 and standalone (JUCE 8): parameters (`Params.h`), presets, footswitches, the tuner, Keeper and the panel. `PedalControl` and `Keeper` are the house copies (by way of Keystone's app).
- `tests/KuddleTests.cpp`: renders a played bass line through every preset and effect. Fails on NaN, clipping, effects that go quiet, soft notes that vanish, clicks when switching, a Melt sweep that doesn't darken, a wah that ignores dynamics, or latency that moves. Writes `bench-kuddle.md`.
- `installer/`: the Inno Setup installer and read-me.
- `docs/`: release notes and the footswitch plan.

Build: `cmake -S kuddle -B build-kuddle && cmake --build build-kuddle --config Release`. GitHub Actions (`.github/workflows/kuddle.yml`) builds the Windows installer and zip, an ARM64 Linux build, and publishes release `kuddle-vX.Y.Z`.
