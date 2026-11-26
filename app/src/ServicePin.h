#pragma once
// Reads the service button's pin on the Pi (0.6.2): GPIO 26, physical pin 37, with the pull-up
// on, through the kernel's GPIO character device (no extra libraries). The Pi 5's header is the
// RP1 chip, labelled pinctrl-rp1 (gpiochip0 on newer kernels, gpiochip4 on older ones), so it's
// found by label. Keystone needs to be in the gpio group, which keystone-maintain.sh sees to.
// Anywhere else (the laptop) there's no pin and read() says so.

#include <juce_core/juce_core.h>

#if JUCE_LINUX
 #include <fcntl.h>
 #include <linux/gpio.h>
 #include <sys/ioctl.h>
 #include <unistd.h>
 #include <cerrno>
 #include <cstring>
#endif

class ServicePin
{
public:
    static constexpr unsigned kLine = 26;

    ~ServicePin() { close(); }

    // the pin's level: 1 high (pressed, or nothing fitted), 0 low, -1 can't be read here
    int read (double now)
    {
       #if JUCE_LINUX
        if (lineFd < 0)
        {
            if (now - triedAt < 10.0 && triedAt >= 0) return -1;   // try again every 10 s
            triedAt = now;
            open();
            if (lineFd < 0) return -1;
        }
        gpio_v2_line_values v {};
        v.mask = 1;
        if (ioctl (lineFd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) { problem = "Couldn't read the pin"; close(); return -1; }
        return (int) (v.bits & 1);
       #else
        juce::ignoreUnused (now);
        return -1;
       #endif
    }

    bool available() const { return lineFd >= 0; }
    // why it can't be read, for the log and Health; empty when it can
    juce::String problem;
    bool here() const
    {
       #if JUCE_LINUX
        return foundChip;
       #else
        return false;
       #endif
    }

private:
    int lineFd = -1;
    double triedAt = -1;
    bool foundChip = false;

    void close()
    {
       #if JUCE_LINUX
        if (lineFd >= 0) ::close (lineFd);
       #endif
        lineFd = -1;
    }

    void open()
    {
       #if JUCE_LINUX
        problem = "Not a Raspberry Pi 5";
        for (int i = 0; i < 16; ++i)
        {
            const auto path = "/dev/gpiochip" + std::to_string (i);
            const int chip = ::open (path.c_str(), O_RDWR | O_CLOEXEC);
            if (chip < 0)
            {
                if (errno == EACCES) { problem = "No permission for the Pi's pins yet: restart Keystone"; foundChip = true; }
                continue;
            }
            gpiochip_info info {};
            if (ioctl (chip, GPIO_GET_CHIPINFO_IOCTL, &info) < 0 || std::strcmp (info.label, "pinctrl-rp1") != 0) { ::close (chip); continue; }
            foundChip = true;
            gpio_v2_line_request req {};
            req.offsets[0] = kLine;
            req.num_lines = 1;
            req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
            std::strncpy (req.consumer, "keystone-service", sizeof (req.consumer) - 1);
            if (ioctl (chip, GPIO_V2_GET_LINE_IOCTL, &req) < 0)
                problem = errno == EBUSY ? "Pin 37 is in use by something else" : "Couldn't claim pin 37";
            else { lineFd = req.fd; problem = {}; }
            ::close (chip);
            return;
        }
       #endif
    }
};
