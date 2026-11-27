## Engine measurements

- Crossover sum: flat within 1.13 dB from 40 Hz to 4 kHz
- Drone on low E: 0.230 cents, 51.7 dB pure
- Envelope filter: hard notes open to 0.93, softer ones to 0.72

### Sound bench

| Measure | Result | Mark |
|---|---|---|
| Noise floor, song default | -240.0 dBFS | under -120 |
| Noise floor, everything on | -145.8 dBFS | under -120 |
| Aliasing, Clean path (reference) | -122.3 dB | under -110 |
| Aliasing, Drive, model, 2.1 kHz hard | -89.8 dB | under -80 |
| Aliasing, Drive, model, 330 Hz hard | -83.5 dB | under -78 |
| Aliasing, Valve preamp, 2.1 kHz hard | -77.4 dB | under -74 |
| Aliasing, Valve preamp, 330 Hz hard | -72.9 dB | under -70 |
| Aliasing, Drive into preamp, 1.1 kHz | -71.8 dB | under -69 |
| Aliasing, Preamp flipped to Cut, 2.1 kHz | -73.2 dB | under -70 |
| Aliasing, Output iron at full, 110 Hz | -83.0 dB | under -80 |
| Aliasing, Limiter working hard, 2.1 kHz | -77.5 dB | under -74 |
| Aliasing, Gloss pad saw, 220 Hz, bright | -67.0 dB | under -64 |
| Aliasing, Gloss pad square, 330 Hz, bright | -65.7 dB | under -64 |
| Level jump, Drive on | +0.5 dB | within 1.5 |
| Level jump, Valve preamp on | +1.3 dB | within 2.0 |
| Level jump, Dimension on | +0.0 dB | within 1.0 |
| Level jump, Low compressor on | +1.5 dB | within 2.0 |
| Level jump, Envelope filter on | -1.1 dB | within 2.0 |
| Level jump, Wah on | -1.7 dB | within 2.5 |
| Level jump, Grains on | +0.4 dB | within 1.0 |
| Level jump, Delay on | +0.0 dB | within 1.0 |
| Level jump, Reverb on | +0.0 dB | within 1.0 |
| Clicks, worst switch (Reverb off) | 1.14x steady | 1.6x or 3 |
| Tail block cost against playing | 0.93x | under 1.5x |

| Case | 64 samples | 128 samples |
|---|---|---|
| Song default, modelled drive | 5.7% | 5.2% |
| Everything on, Bloom at 80% | 18.0% | 20.4% |
| Everything on, light effects | 14.7% | 14.5% |
| Everything on, tape delay and Haze grains | 15.9% | 21.3% |
| Everything on, Cloud reverb | 17.7% | 18.1% |
| Everything on, Impulse reverb (2 s) | 15.9% | 18.7% |
| Everything on, Galaxy delay and Arp grains | 21.7% | 16.4% |
| Groove: wah, auto filter and Dimension | 5.1% | 5.3% |
| Kuddle, Voodoo (Keystone asleep) | 2.7% | 2.6% |
| Kuddle with the room on | 2.9% | 2.9% |

CPU is the share of one core for one Keystone at 48 kHz, measured on the build machine.
