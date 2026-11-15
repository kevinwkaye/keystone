Keystone 0.1.1: the phone now shows where it is looking for Keystone and lets you type the laptop's address. Keystone 0.1: the whole live chain, run from a laptop with no screen and controlled from your phone.

## Download

- **Keystone-0.1.1-Setup.exe**: the Windows 11 app. Starts with Windows, runs with the lid closed, plays through the UM2 in Windows' low-latency exclusive mode, listens to the footswitches and expression pedal over USB.
- **Keystone-0.1.1.apk**: the phone controller. Finds Keystone on your hotspot by itself, works with no internet, keeps the screen awake.
- **Keystone-0.1.1-linux-arm64.tar.gz**: the same app built for ARM64 Linux, for the Pi later. Built and tested here so portability problems show up early.

## Latency

- Engine latency: **15 samples (0.31 ms)**, from the drive's 2x oversampling. The low band is delayed to match, so the bands sum flat.
- Keystone asks Windows for exclusive mode at 48 kHz and the smallest buffer from 64 samples up. On the UM2 that should give **about 8 to 12 ms round trip** (Windows' USB audio driver usually allows 3 ms buffers, plus the interface's converters).
- The real figure for your laptop shows on the phone (Tone, Output) and in Documents\Keystone\keystone.log. The build machines have no audio interface, so they can't measure it.
- If the laptop struggles, Keystone switches the ambient effects to light mode before it would ever need a bigger buffer.

## Engine checks (all must pass for a release)

Crossover sums flat; held drone notes within 3 cents and over 18 dB pure on low E, A, G and drop D; the envelope filter opens less for softer notes; grains stay within 20 dB of the input, even when played 30 dB softer; Bloom at toe never passes 0 dBFS and heel back with Cut clears the tails; no clicks on Edge/Drive, Bitrot, Filter, Slap or Drive on; tuner reads within 1.5 cents from drop D to D3; every footswitch job, Keep and the setlist editor.
