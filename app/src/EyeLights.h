#pragma once
// Pebble's eyes on the Pi: two WS2812 pixels chained on SPI1's data line (GPIO 20, header
// pin 38), through a 74AHCT125 so the 3.3 V signal reaches the pixels at 5 V. Data goes to
// the left eye first, and the left eye's DOUT feeds the right eye.
//
// Each WS2812 bit goes out as three SPI bits at 2.4 MHz (100 for a 0, 110 for a 1), so the
// timing comes from the SPI hardware and audio threads can't upset it. Needs
//     dtoverlay=spi1-1cs
// in /boot/firmware/config.txt (pi/setup.sh adds it). Anywhere without /dev/spidev1.0, such
// as Windows or a Pi without the overlay, it does nothing.

#include "PebbleEyes.h"

#include <cstdint>
#include <string>
#include <vector>

#if defined(__linux__)
 #include <fcntl.h>
 #include <linux/spi/spidev.h>
 #include <sys/ioctl.h>
 #include <unistd.h>
#endif

class EyeLights
{
public:
    explicit EyeLights (std::string device = "/dev/spidev1.0") : path (std::move (device)) {}
    ~EyeLights() { off(); close(); }

    // true when the pixels are there to talk to
    bool open()
    {
#if defined(__linux__)
        if (fd >= 0) return true;
        fd = ::open (path.c_str(), O_WRONLY);
        if (fd < 0) return false;
        uint8_t mode = SPI_MODE_0, bits = 8;
        uint32_t speed = kSpeed;
        if (ioctl (fd, SPI_IOC_WR_MODE, &mode) < 0 || ioctl (fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0
            || ioctl (fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
        {
            close();
            return false;
        }
        sent = false;
        return true;
#else
        return false;
#endif
    }

    bool ready() const { return fd >= 0; }

    // Sends the two eyes. Unchanged colours aren't sent again, but are refreshed every
    // couple of seconds in case a pixel was knocked by a static spark.
    void show (const ks::eyes::Pair& eyes, double nowSeconds)
    {
        if (fd < 0) return;
        if (sent && eyes == last && nowSeconds - lastSent < 2.0) return;
        write (eyes);
        last = eyes;
        lastSent = nowSeconds;
        sent = true;
    }

    void off() { if (fd >= 0) write ({}); }

private:
    static constexpr uint32_t kSpeed = 2400000;   // 3 SPI bits per WS2812 bit = 800 kHz
    static constexpr int kLead = 4, kLatch = 120;   // quiet bytes before, and 400 us after to latch

    std::string path;
    int fd = -1;
    ks::eyes::Pair last {};
    double lastSent = 0;
    bool sent = false;

    void close()
    {
#if defined(__linux__)
        if (fd >= 0) ::close (fd);
#endif
        fd = -1;
    }

    // one colour byte as 24 SPI bits, most significant bit first
    static void encode (uint8_t v, std::vector<uint8_t>& out)
    {
        uint32_t bits = 0;
        for (int i = 7; i >= 0; --i) bits = (bits << 3) | ((v >> i) & 1 ? 0b110u : 0b100u);
        out.push_back ((uint8_t) (bits >> 16));
        out.push_back ((uint8_t) (bits >> 8));
        out.push_back ((uint8_t) bits);
    }

    void write (const ks::eyes::Pair& eyes)
    {
#if defined(__linux__)
        std::vector<uint8_t> buf (kLead, 0);
        for (const auto& c : eyes) { encode (c.g, buf); encode (c.r, buf); encode (c.b, buf); }   // WS2812 wants G, R, B
        buf.insert (buf.end(), kLatch, 0);
        spi_ioc_transfer t {};
        t.tx_buf = (unsigned long) buf.data();
        t.len = (uint32_t) buf.size();
        t.speed_hz = kSpeed;
        t.bits_per_word = 8;
        if (ioctl (fd, SPI_IOC_MESSAGE (1), &t) < 0) close();   // gone: stay quiet from now on
#else
        (void) eyes;
#endif
    }
};
