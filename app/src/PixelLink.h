#pragma once
// The pedal's lights on the Pi: Pebble's eyes, the status lights, the meter and the
// footswitch lights, all one strand of WS2812 dots driven by an Adafruit Pixel Trinkey on
// a USB port. The Trinkey does the level shifting and the timing; Keystone sends it a
// frame of colours over its USB serial data port (pi/trinkey/code.py reads them).
//
// A frame is 'K' 'S', the number of lights, three bytes per light in the strand's order
// (green, red, blue, as WS2812s want), then one checksum byte: the sum of those colour
// bytes, modulo 256.
//
// The Trinkey shows up as /dev/serial/by-id/usb-Adafruit_Pixel_Trinkey...-if02 (its second
// USB serial port, which boot.py switches on). Plugged in late or replugged, it's picked up
// within a few seconds. Anywhere else (Windows, a Pi with no Trinkey) it does nothing.

#include "PedalLights.h"

#include <cstdint>
#include <string>
#include <vector>

#if defined(__linux__)
 #include <dirent.h>
 #include <fcntl.h>
 #include <termios.h>
 #include <unistd.h>
 #include <cerrno>
#endif

class PixelLink
{
public:
    ~PixelLink() { off(); close(); }

    // Sends the frame. Unchanged frames aren't sent again, but are refreshed every couple of
    // seconds (the Trinkey goes dark if Keystone stops). Looks for the Trinkey every few
    // seconds while it's missing.
    void show (const ks::lights::Frame& f, double nowSeconds)
    {
#if defined(__linux__)
        if (fd < 0)
        {
            if (nowSeconds - lastLook < 3.0) return;
            lastLook = nowSeconds;
            if (! open()) return;
            sent = false;
        }
        if (sent && f == last && nowSeconds - lastSent < 1.0) return;
        if (write (f)) { last = f; lastSent = nowSeconds; sent = true; }
#else
        (void) f; (void) nowSeconds;
#endif
    }

    void off() { if (fd >= 0) write ({}); }
    bool connected() const { return fd >= 0; }
    std::string device() const { return path; }

private:
    int fd = -1;
    std::string path;
    ks::lights::Frame last {};
    double lastSent = 0, lastLook = -10;
    bool sent = false;

    // the Trinkey's data port: its second serial interface; the first is the Python console
    static std::string find()
    {
#if defined(__linux__)
        const std::string dir = "/dev/serial/by-id/";
        std::string any;
        if (DIR* d = opendir (dir.c_str()))
        {
            while (auto* e = readdir (d))
            {
                const std::string n = e->d_name;
                if (n.find ("Trinkey") == std::string::npos) continue;
                if (n.size() > 5 && n.compare (n.size() - 5, 5, "-if02") == 0) { closedir (d); return dir + n; }
                if (any.empty()) any = dir + n;
            }
            closedir (d);
        }
        (void) any;   // only the console port: boot.py isn't on the Trinkey yet, nothing to send to
#endif
        return {};
    }

    bool open()
    {
#if defined(__linux__)
        path = find();
        if (path.empty()) return false;
        fd = ::open (path.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return false;
        termios t {};
        if (tcgetattr (fd, &t) == 0)
        {
            cfmakeraw (&t);
            cfsetospeed (&t, B115200);   // USB ignores it, but raw mode matters
            tcsetattr (fd, TCSANOW, &t);
        }
        return true;
#else
        return false;
#endif
    }

    void close()
    {
#if defined(__linux__)
        if (fd >= 0) ::close (fd);
#endif
        fd = -1;
    }

    bool write (const ks::lights::Frame& f)
    {
#if defined(__linux__)
        if (fd < 0) return false;
        std::vector<uint8_t> buf { 'K', 'S', (uint8_t) f.size() };
        uint8_t sum = 0;
        for (const auto& c : f)
            for (uint8_t v : { c.g, c.r, c.b }) { buf.push_back (v); sum = (uint8_t) (sum + v); }
        buf.push_back (sum);
        const auto n = ::write (fd, buf.data(), buf.size());
        if (n == (ssize_t) buf.size()) return true;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;   // busy: the next frame will do
        close();   // unplugged: look for it again
        return false;
#else
        (void) f;
        return false;
#endif
    }
};
