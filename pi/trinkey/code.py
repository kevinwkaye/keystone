# Keystone's pedal lights on an Adafruit Pixel Trinkey (CircuitPython).
#
# Keystone on the Pi sends frames over the Trinkey's second USB serial port: b"KS", the
# number of lights, three bytes per light (green, red, blue, in the strand's order), then a
# checksum byte (the sum of the colour bytes, modulo 256). Each good frame goes straight
# out to the strand on the DATA terminal. No frame for five seconds (Keystone stopped or
# the Pi off): the strand goes dark.
#
# The Trinkey's own little light: dim teal while frames are arriving, a slow amber blink
# while it waits for Keystone, red if boot.py isn't on the Trinkey (no second port).

import time

import board
import digitalio
import neopixel_write
import usb_cdc


def pin(*names):
    for name in names:
        p = getattr(board, name, None)
        if p is not None:
            return p
    raise RuntimeError("no pin called any of " + ", ".join(names))


strand = digitalio.DigitalInOut(pin("DATA", "D4", "MOSI"))
strand.direction = digitalio.Direction.OUTPUT

status = None
if getattr(board, "NEOPIXEL", None) is not None:
    status = digitalio.DigitalInOut(board.NEOPIXEL)
    status.direction = digitalio.Direction.OUTPUT


def own_light(g, r, b):
    if status is not None:
        neopixel_write.neopixel_write(status, bytearray((g, r, b)))


TIMEOUT = 5.0
MAX_LIGHTS = 60

port = usb_cdc.data
buf = bytearray()
last_frame = -TIMEOUT
showing = False
lights = 20

while True:
    now = time.monotonic()
    if port is None:
        own_light(0, 8, 0)   # red: boot.py is missing
        time.sleep(1)
        continue

    waiting = port.in_waiting
    if waiting:
        buf += port.read(waiting)
        while True:
            start = buf.find(b"KS")
            if start < 0:
                buf = buf[-1:]   # keep a lone "K" in case the "S" is on its way
                break
            if start:
                buf = buf[start:]
            if len(buf) < 3:
                break
            count = buf[2]
            if count == 0 or count > MAX_LIGHTS:
                buf = buf[2:]
                continue
            need = 3 + count * 3 + 1
            if len(buf) < need:
                break
            body = buf[3:need - 1]
            if sum(body) & 0xFF == buf[need - 1]:
                neopixel_write.neopixel_write(strand, body)
                lights = count
                last_frame = now
                if not showing:
                    own_light(4, 0, 3)   # dim teal: Keystone is talking
                showing = True
                buf = buf[need:]
            else:
                buf = buf[2:]   # not a frame after all: look again past the "KS"
        if len(buf) > 1024:
            buf = bytearray()
    elif showing and now - last_frame > TIMEOUT:
        neopixel_write.neopixel_write(strand, bytearray(lights * 3))
        showing = False
    elif not showing:
        own_light(3 if int(now) % 2 == 0 else 0, 4 if int(now) % 2 == 0 else 0, 0)   # amber blink: waiting

    time.sleep(0.002)
