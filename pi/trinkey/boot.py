# Keystone's pedal lights: give the Pixel Trinkey a second USB serial port for Keystone's
# frames, beside the usual Python console. Runs when the Trinkey starts.
import usb_cdc

usb_cdc.enable(console=True, data=True)
