## Engine measurements

- Crossover sum: flat within 1.13 dB from 40 Hz to 4 kHz
- Drone on low E: 0.230 cents, 51.7 dB pure
- Envelope filter: hard notes open to 0.93, softer ones to 0.72

### Sound bench

| Measure | Result | Mark |
|---|---|---|
| Noise floor, song default | -240.0 dBFS | under -120 |
| Noise floor, everything on | -240.0 dBFS | under -120 |
| Aliasing, Clean path (reference) | -79.9 dB | under -75 |
| Aliasing, Drive, model, 2.1 kHz hard | -86.9 dB | under -80 |
| Aliasing, Drive, model, 330 Hz hard | -82.2 dB | under -78 |
| Aliasing, Valve preamp, 2.1 kHz hard | -68.9 dB | under -65 |
| Aliasing, Valve preamp, 330 Hz hard | -69.6 dB | under -65 |
| Aliasing, Drive into preamp, 1.1 kHz | -68.8 dB | under -65 |
| Aliasing, Output iron at full, 110 Hz | -84.1 dB | under -80 |
| Aliasing, Limiter working hard, 2.1 kHz | -69.7 dB | under -66 |
| Level jump, Drive on | +0.6 dB | within 1.5 |
| Level jump, Valve preamp on | +1.3 dB | within 2.0 |
| Level jump, Dimension on | -2.1 dB | within 2.6 |
| Level jump, Low compressor on | +1.0 dB | within 1.5 |
| Level jump, Envelope filter on | -7.7 dB | within 8.2 |
| Level jump, Wah on | -3.9 dB | within 4.4 |
| Level jump, Grains on | +0.2 dB | within 2.0 |
| Level jump, Delay on | +0.0 dB | within 2.0 |
| Level jump, Reverb on | +0.1 dB | within 2.0 |
| Clicks, worst switch (Reverb off) | 1.02x steady | 1.6x or 3 |
| Tail block cost against playing | 0.98x | under 1.5x |

| Case | 64 samples | 128 samples |
|---|---|---|
| Song default, modelled drive | 3.5% | 3.6% |
| Everything on, Bloom at 80% | 15.2% | 13.1% |
| Everything on, light effects | 9.4% | 9.4% |
| Everything on, tape delay and Haze grains | 14.5% | 12.9% |
| Everything on, Cloud reverb | 12.2% | 13.1% |
| Everything on, Impulse reverb (2 s) | 12.2% | 12.8% |
| Everything on, Galaxy delay and Arp grains | 15.0% | 15.8% |
| Groove: wah, auto filter and Dimension | 3.9% | 4.8% |
| Kuddle, Voodoo (Keystone asleep) | 2.3% | 2.3% |
| Kuddle with the room on | 2.3% | 2.3% |

CPU is the share of one core for one Keystone at 48 kHz, measured on the build machine.
