#pragma once
// The floor screen on the Pi: the Adafruit 2.0" 320 x 240 IPS screen (ST7789) in the lid,
// on SPI0 with three GPIO lines. Wiring (docs/pedal-lights-and-screen.md):
//   SPI0 MOSI (pin 19), SCLK (pin 23), CE0 (pin 24); DC GPIO 25 (pin 22), reset GPIO 27
//   (pin 13), backlight GPIO 12 (pin 32)
// Needs dtparam=spi=on (pi/install.sh adds it). Anywhere without /dev/spidev0.0 it does
// nothing.
//
// The main thread hands over a View whenever it likes; a thread of its own draws it
// (FloorScreen) and sends only the rows that changed, so nothing on the main thread ever
// waits for the screen.

#include "FloorScreen.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <initializer_list>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
 #include <fcntl.h>
 #include <linux/gpio.h>
 #include <linux/spi/spidev.h>
 #include <sys/ioctl.h>
 #include <unistd.h>
#endif

class FloorPanel
{
public:
    ~FloorPanel() { stop(); }

    // upsideDown: the screen sits the other way round in the lid
    bool start (bool upsideDown = false)
    {
#if defined(__linux__)
        if (running) return true;
        flip = upsideDown;
        if (! openHardware()) { closeHardware(); return false; }
        running = true;
        thread = std::thread ([this] { run(); });
        return true;
#else
        (void) upsideDown;
        return false;
#endif
    }

    void stop()
    {
        if (! running) return;
        {
            std::lock_guard<std::mutex> l (lock);
            running = false;
        }
        wake.notify_all();
        if (thread.joinable()) thread.join();
#if defined(__linux__)
        // dark on the way out
        std::vector<uint8_t> black ((size_t) ks::floor::kWidth * ks::floor::kHeight * 2, 0);
        sendRows (black, 0, ks::floor::kHeight - 1);
        setLine (kBacklight, false);
#endif
        closeHardware();
    }

    bool ready() const { return running; }

    void show (const ks::floor::View& v)
    {
        {
            std::lock_guard<std::mutex> l (lock);
            if (haveNext && v == next) return;
            next = v;
            haveNext = true;
        }
        wake.notify_one();
    }

private:
    static constexpr int kDc = 25, kReset = 27, kBacklight = 12;
    static constexpr uint32_t kSpeed = 40000000;
    static constexpr size_t kChunk = 4096;   // spidev's default largest transfer

    std::thread thread;
    std::mutex lock;
    std::condition_variable wake;
    std::atomic<bool> running { false };
    ks::floor::View next;
    bool haveNext = false;
    bool flip = false;
    int spi = -1, lines = -1;

    void run()
    {
        ks::floor::Renderer renderer;
        ks::floor::Picture pic;
        std::vector<uint8_t> now, before;
        ks::floor::View v;
        bool first = true;
        while (true)
        {
            {
                std::unique_lock<std::mutex> l (lock);
                wake.wait (l, [this] { return ! running || haveNext; });
                if (! running) return;
                v = next;
                haveNext = false;
            }
            renderer.draw (v, pic);
            pic.toRgb565 (now);
            // only the band of rows that changed
            int top = 0, bottom = ks::floor::kHeight - 1;
            if (! first)
            {
                const size_t row = (size_t) ks::floor::kWidth * 2;
                while (top <= bottom && std::memcmp (&now[(size_t) top * row], &before[(size_t) top * row], row) == 0) ++top;
                while (bottom >= top && std::memcmp (&now[(size_t) bottom * row], &before[(size_t) bottom * row], row) == 0) --bottom;
            }
            if (top <= bottom) sendRows (now, top, bottom);
            if (first) setLine (kBacklight, true);   // light it once there's a picture
            first = false;
            std::swap (now, before);
            std::this_thread::sleep_for (std::chrono::milliseconds (30));   // at most about 30 a second
        }
    }

#if defined(__linux__)
    bool openHardware()
    {
        spi = ::open ("/dev/spidev0.0", O_RDWR);
        if (spi < 0) return false;
        uint8_t mode = SPI_MODE_0, bits = 8;
        uint32_t speed = kSpeed;
        if (ioctl (spi, SPI_IOC_WR_MODE, &mode) < 0 || ioctl (spi, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0
            || ioctl (spi, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0)
            return false;
        if (! openLines()) return false;
        // reset, then the ST7789's start-up
        setLine (kBacklight, false);
        setLine (kReset, false); pause (20);
        setLine (kReset, true); pause (120);
        command (0x01); pause (150);                       // software reset
        command (0x11); pause (120);                       // out of sleep
        command (0x3A, { 0x55 });                          // 16-bit colour
        command (0x36, { (uint8_t) (flip ? 0xA0 : 0x60) }); // landscape, 320 wide
        command (0x21);                                    // IPS panels want inversion on
        command (0x13);                                    // normal display
        command (0x29); pause (50);                        // on
        return true;
    }

    // the Pi 5's header GPIOs: the chip called pinctrl-rp1 (gpiochip0 or gpiochip4)
    bool openLines()
    {
        int chip = -1;
        for (int i = 0; i < 12 && chip < 0; ++i)
        {
            const int fd = ::open (("/dev/gpiochip" + std::to_string (i)).c_str(), O_RDWR);
            if (fd < 0) continue;
            gpiochip_info info {};
            if (ioctl (fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 && std::strstr (info.label, "rp1") != nullptr) chip = fd;
            else ::close (fd);
        }
        if (chip < 0) chip = ::open ("/dev/gpiochip0", O_RDWR);
        if (chip < 0) return false;
        gpio_v2_line_request req {};
        req.offsets[0] = kDc;
        req.offsets[1] = kReset;
        req.offsets[2] = kBacklight;
        req.num_lines = 3;
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        std::strncpy (req.consumer, "keystone-screen", sizeof (req.consumer) - 1);
        const bool ok = ioctl (chip, GPIO_V2_GET_LINE_IOCTL, &req) == 0;
        ::close (chip);
        if (! ok) return false;
        lines = req.fd;
        return true;
    }

    void setLine (int gpio, bool high)
    {
        if (lines < 0) return;
        const int index = gpio == kDc ? 0 : gpio == kReset ? 1 : 2;
        gpio_v2_line_values v {};
        v.mask = 1ull << index;
        v.bits = high ? v.mask : 0;
        ioctl (lines, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
    }

    static void pause (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }

    void transfer (const uint8_t* data, size_t n)
    {
        while (n > 0 && spi >= 0)
        {
            const size_t len = std::min (n, kChunk);
            spi_ioc_transfer t {};
            t.tx_buf = (unsigned long) data;
            t.len = (uint32_t) len;
            t.speed_hz = kSpeed;
            t.bits_per_word = 8;
            if (ioctl (spi, SPI_IOC_MESSAGE (1), &t) < 0) return;
            data += len;
            n -= len;
        }
    }

    void command (uint8_t c, std::initializer_list<uint8_t> args = {})
    {
        setLine (kDc, false);
        transfer (&c, 1);
        if (args.size() == 0) return;
        setLine (kDc, true);
        std::vector<uint8_t> a (args);
        transfer (a.data(), a.size());
    }

    void sendRows (const std::vector<uint8_t>& px, int top, int bottom)
    {
        if (spi < 0) return;
        const int w = ks::floor::kWidth - 1;
        command (0x2A, { 0, 0, (uint8_t) (w >> 8), (uint8_t) w });                                            // columns
        command (0x2B, { (uint8_t) (top >> 8), (uint8_t) top, (uint8_t) (bottom >> 8), (uint8_t) bottom });   // rows
        command (0x2C);                                                                                       // write
        setLine (kDc, true);
        const size_t row = (size_t) ks::floor::kWidth * 2;
        transfer (&px[(size_t) top * row], (size_t) (bottom - top + 1) * row);
    }

    void closeHardware()
    {
        if (spi >= 0) ::close (spi);
        if (lines >= 0) ::close (lines);
        spi = lines = -1;
    }
#else
    bool openHardware() { return false; }
    void setLine (int, bool) {}
    void sendRows (const std::vector<uint8_t>&, int, int) {}
    void closeHardware() {}
#endif
};
