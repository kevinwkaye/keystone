KEYSTONE 0.1
============

Keystone runs in the background with no window. It starts when you sign in to Windows,
plays your bass through the audio interface, listens to the footswitches and expression
pedal, and talks to the Keystone app on your phone.

First time setup (needs the projector or a screen once)
-------------------------------------------------------
1. Plug in the Behringer UM2 (bass into input 2, the instrument jack; Direct Monitor OFF)
   and the footswitch over USB.
2. Turn on your phone's hotspot. On the laptop, join the hotspot and tick
   "Connect automatically". Mobile data can stay off: Keystone needs no internet.
3. Sign in to Windows automatically, so Keystone starts with the lid closed:
   - Settings > Accounts > Sign-in options: turn off "For improved security, only allow
     Windows Hello sign-in".
   - Press Windows + R, type netplwiz, untick "Users must enter a user name and password",
     and enter your password once.
4. Open the Keystone app on the phone. It finds the laptop by itself.

Every gig
---------
Hotspot on, laptop on with the lid closed, wait for the phone to say Connected.

Footswitches
------------
Tap:  A Edge / Drive   B Bitrot   C Filter   D Drone capture (double tap D clears)
Hold: A previous song  B next song  C tuner  D slap compression
Hold C and D together for a second: Keep (saves the moment, with 8 seconds of audio,
to Documents\Flip Fella Keepers).
The expression pedal is the Bloom sweep: drone, then grains, then wash.

Your files
----------
Documents\Keystone\Captures   NAM captures (.nam). Pick one on the phone: Tone, Drive.
Documents\Keystone            songs (setlist.json), settings and keystone.log.

If something's wrong, keystone.log says what Keystone found: the audio device, buffer
size, latency, footswitches and the phone's address.
