# The pedal's lights and floor screen

The lid has 18 lights and a 2.0" screen. The lights are one strand of WS2812 dots (Adafruit NeoPixel LED Dots Strand, 20 at 2" pitch) driven by an Adafruit Pixel Trinkey on one of the Pi's USB ports. The screen is the Adafruit 2.0" 320 x 240 IPS breakout (ST7789) on the Pi's header. No soldering anywhere.

The logic is pure C++ with no hardware: `core/src/PebbleEyes.h` (the eyes), `core/src/PedalLights.h` (every light) and `core/src/FloorScreen.cpp` (the screen's pictures). The drivers are `app/src/PixelLink.h` (lights) and `app/src/FloorPanel.h` (screen). `floor_preview` draws every screen to a PNG for checking against the designs on the Keystone Pedal screens canvas.

## The lights

| Light | Shows |
|---|---|
| Pebble's eyes | Stomp mode: the bank, Grit orange, Groove purple, Glow blue. Song mode: green and purple, swapping on every beat. Tuner: left red when flat, right red when sharp, both green within 2 cents. Kuddle: warm amber. The tuner wins over everything. |
| Phone | on while a phone is connected |
| Watch | dim while a watch is connected, bright while it's live (C held in Grit) |
| Projector | on while the projector's HDMI is connected |
| Audio | green fine, amber when the Pi is busy or hot or the bass input is silent, red with no audio or a dropout in the last minute |
| Meter (7) | the expression pedal's (or the watch's) amount in the bank's colour, for 3 seconds after it moves. In Song mode otherwise a four-bar phrase counter from the moment the part was picked: green filling over three bars, purple flashing on every beat of the fourth. Off in the tuner. |
| Footswitches A to C | Stomp mode: the bank's colour, bright when that effect is on, faint when it's off. Off here but on in another bank: a faint glow in that bank's colour (on in both other banks: they take turns, a second each). |
| Footswitch D | warm white while the drone holds a note |
| In Song mode | A and C faint green, B green while there's a next part |
| In the tuner | all four faint white: any tap leaves it |
| In Kuddle | amber, bright for each switch's effect (Mute, Auto wah, Room, Tape) when on |

Overall brightness starts at 40%.

### Where they sit, and the wire's order

Lid coordinates in mm, x from the left, d from the front edge:

| Group | Positions |
|---|---|
| Pebble's eyes | (124.5, 103) and (135.5, 103), 7 mm holes |
| Status | x 158: Phone d 100, Watch d 88, Projector d 76, Audio d 64 |
| Meter | d 28, x 94 to 166 every 12 mm (meter 1 is the left) |
| Footswitches | d 9, x 26.5, 79.5, 132.5 and 185.5, above the Chocolate Plus's A to D |
| Hidden spacer | (53, 12), under the plate, always off |

The strand runs: Trinkey, left eye, right eye, Phone, Watch, Projector, Audio, meter 7, D, meter 6, meter 5, C, meter 4, meter 3, meter 2, meter 1, B, spacer, A. The last two dots stay coiled underneath as spares. The Chocolate Plus's switches are 53 mm apart, a little more than the 51 mm of wire between dots, so a dot that never lights bridges A and B.

### Setting up the Pixel Trinkey (once)

1. Plug the Trinkey into a computer. If a drive called CIRCUITPY appears, go to step 3.
2. Otherwise it needs CircuitPython: download the .uf2 for the Pixel Trinkey from circuitpython.org, double-tap the Trinkey's reset button so a drive called TRINKEYBOOT (or similar) appears, and drag the .uf2 onto it. CIRCUITPY appears a few seconds later.
3. Copy `pi/trinkey/boot.py` and `pi/trinkey/code.py` onto CIRCUITPY, replacing what's there, then unplug it.
4. Screw the strand's three input wires into the Trinkey's terminals: 5V, Data and GND (Clock stays empty). Plug the Trinkey into the Pi with the short USB extension.

The Trinkey's own little light blinks amber while it waits for Keystone, goes dim teal once Keystone is talking to it, and shows red if boot.py is missing. If Keystone stops, the strand goes dark after five seconds. Keystone's log says when it finds the Trinkey.

## The floor screen

Five screens, each with one big thing to read from standing height:

- **Splash**: Pebble and KEYSTONE for a couple of seconds while Keystone starts
- **Song mode**: the part (big), the song above it, NEXT and what's coming below, the setlist place and tempo along the top. A song without parts shows its name big. The first entry of a song with parts says START.
- **Stomp mode**: the bank you're on on the left (name, the Drive capture and four big A to D blocks), the other two banks on the right, their blocks filling with the bank's colour when an effect is on there
- **Tuner**: the note, huge, and the cents beside it, amber, or green within 2 cents. MUTED in the corner when the tuner mutes the output.
- **Alert**: two red cards (amber for warnings) for five seconds over whatever's on screen: WATCH LOST, AUDIO LOST, AUDIO DROPOUT, PI HOT

Long names shrink to fit, then cut off with an ellipsis.

### The Drive capture's short name

Capture files often have long names. Set a short one for the screen and Keystone keeps it with that capture: the phone link message `{"t":"captureShort","file":"<the .nam file>","name":"SVT"}` (an empty name clears it). Without one, the screen uses the capture's own name.

### Wiring

Through the EYESPI breakout, or the header pins on the board:

| Pi pin | Pi name | Screen |
|---|---|---|
| 1 | 3.3 V | Vin |
| 6 | GND | GND |
| 13 | GPIO 27 | Rst |
| 19 | GPIO 10, SPI0 MOSI | MOSI |
| 22 | GPIO 25 | DC |
| 23 | GPIO 11, SPI0 SCLK | SCK |
| 24 | GPIO 8, SPI0 CE0 | TCS |
| 32 | GPIO 12 | Lite (backlight) |

`pi/install.sh` adds `dtparam=spi=on` to `/boot/firmware/config.txt` and puts the keystone user in the `spi`, `gpio` and `dialout` groups. Keystone drives the ST7789 itself through spidev, at 40 MHz, sending only the rows that change. If the picture is upside down, create an empty file called `screen-upside-down` in Documents/Keystone and restart Keystone.

## Fonts

Archivo ExtraBold and Medium (SIL Open Font License, `core/fonts/OFL.txt`), drawn with stb_truetype (public domain, `core/third_party/stb`).
