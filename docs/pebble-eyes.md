# Pebble's eyes and the floor screen: wiring

Two WS2812 pixels sit behind Pebble's eyes on the lid sticker, and the Adafruit 2.0" IPS screen (ST7789, EYESPI) sits in the lid. Both run from the Pi 5's header. Eye 0 is the left eye and eye 1 the right, as you see them standing at the board.

## What the eyes show

| When | Left eye | Right eye |
|---|---|---|
| Stomp mode, Grit | orange | orange |
| Stomp mode, Groove | purple | purple |
| Stomp mode, Glow | blue | blue |
| Song mode | green and purple, swapping on every beat of the song's tempo | the other one |
| Tuner, flat | red, brighter the further out | dark |
| Tuner, sharp | dark | red, brighter the further out |
| Tuner, within 2 cents | green | green |
| Tuner, no note | dark | dark |
| Kuddle | warm amber | warm amber |

The tuner wins over every mode. Overall brightness starts at 40%. The logic is `core/src/PebbleEyes.h` and the driver `app/src/EyeLights.h`.

## How the pixels are driven

The pixels hang off SPI1's data line (GPIO 20). Each WS2812 bit goes out as three SPI bits at 2.4 MHz, so the SPI hardware does the timing and nothing in Keystone has to bit-bang. The Pi's 3.3 V signal goes through a 74AHCT125 to reach the pixels at 5 V, as WS2812s want at least 3.5 V on their data line. `pi/install.sh` adds `dtoverlay=spi1-1cs` to `/boot/firmware/config.txt` and puts the keystone user in the `spi` group.

## Pi 5 header

| Pi pin | Pi name | Goes to |
|---|---|---|
| 1 | 3.3 V | Screen Vin |
| 2 | 5 V | 74AHCT125 pin 14 (VCC), both pixels' 5V |
| 6 | GND | Screen GND |
| 9 | GND | 74AHCT125 pin 7 (GND) and pin 1 (1OE), both pixels' GND |
| 13 | GPIO 27 | Screen Rst |
| 19 | GPIO 10, SPI0 MOSI | Screen MOSI |
| 22 | GPIO 25 | Screen DC |
| 23 | GPIO 11, SPI0 SCLK | Screen SCK |
| 24 | GPIO 8, SPI0 CE0 | Screen TCS |
| 32 | GPIO 12, PWM | Screen Lite (backlight, dimmable) |
| 38 | GPIO 20, SPI1 MOSI | 74AHCT125 pin 2 (1A) |
| 12 | GPIO 18 | Leave free: SPI1's chip select |

## The little level-shifter board

On a small piece of stripboard, with the chip in a 14-pin socket:

- 74AHCT125 pin 14 to 5 V, pin 7 to GND, pin 1 (1OE) to GND
- pin 2 (1A) from Pi pin 38
- pin 3 (1Y) through a 330 Ω resistor to the left eye's DIN
- a 100 nF capacitor across pins 14 and 7, close to the chip
- the left eye's DOUT to the right eye's DIN
- 5 V and GND to both pixels

The unused inputs (pins 5, 9 and 12) go to GND so they don't float.

## The screen

Wired as above through the EYESPI breakout. The screen gets its own overlay and Keystone's floor-screen code in a later version; the eyes don't depend on it.
