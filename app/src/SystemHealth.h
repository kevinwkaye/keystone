#pragma once
// What the machine Keystone runs on is doing, for the Health page (0.5.8): the Pi's
// temperature, fan and power flags, plus free storage and memory on any machine.
// Read on the message thread a few times a minute, never from the audio thread.

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

struct SystemHealth
{
    float tempC = -1;            // -1: not known here (the laptop)
    int fanRpm = -1;             // -1: no fan reading
    bool underVoltNow = false, underVoltEver = false;   // the supply dipped below 4.63 V
    bool throttledNow = false, throttledEver = false;    // slowed for heat or power
    bool hasPower = false;       // the power flags could be read (the Pi only)
    juce::int64 freeMb = -1;     // free space where Keystone keeps takes
    int memUsedMb = -1, memTotalMb = -1;

    static SystemHealth read (const juce::File& dataDir)
    {
        SystemHealth s;
        s.freeMb = dataDir.getBytesFreeOnVolume() / (1024 * 1024);
       #if JUCE_LINUX
        s.tempC = readInt ("/sys/class/thermal/thermal_zone0/temp", -1000) / 1000.0f;
        if (s.tempC < -0.5f) s.tempC = -1;
        for (auto& f : juce::File ("/sys/devices/platform/cooling_fan/hwmon").findChildFiles (juce::File::findDirectories, false))
        {
            const int rpm = readInt (f.getChildFile ("fan1_input").getFullPathName(), -1);
            if (rpm >= 0) { s.fanRpm = rpm; break; }
        }
        int flags = -1;
        const auto sysfs = throttledFile();
        if (sysfs.existsAsFile()) flags = sysfs.loadFileAsString().trim().getHexValue32();
        else flags = vcgencmdThrottled();
        if (flags >= 0)
        {
            s.hasPower = true;
            s.underVoltNow = (flags & 0x1) != 0;
            s.throttledNow = (flags & 0x6) != 0 || (flags & 0x8) != 0;
            s.underVoltEver = (flags & 0x10000) != 0;
            s.throttledEver = (flags & 0x60000) != 0 || (flags & 0x80000) != 0;
        }
        juce::int64 totalKb = -1, availKb = -1;
        juce::StringArray lines;
        lines.addLines (juce::File ("/proc/meminfo").loadFileAsString());
        for (auto& l : lines)
        {
            if (l.startsWith ("MemTotal:")) totalKb = l.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue();
            if (l.startsWith ("MemAvailable:")) availKb = l.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue();
        }
        if (totalKb > 0 && availKb >= 0) { s.memTotalMb = (int) (totalKb / 1024); s.memUsedMb = (int) ((totalKb - availKb) / 1024); }
       #elif JUCE_WINDOWS
        MEMORYSTATUSEX m {};
        m.dwLength = sizeof (m);
        if (GlobalMemoryStatusEx (&m))
        {
            s.memTotalMb = (int) (m.ullTotalPhys / (1024 * 1024));
            s.memUsedMb = (int) ((m.ullTotalPhys - m.ullAvailPhys) / (1024 * 1024));
        }
       #endif
        return s;
    }

private:
    static int readInt (const juce::String& path, int fallback)
    {
        const juce::File f (path);
        if (! f.existsAsFile()) return fallback;
        const auto t = f.loadFileAsString().trim();
        return t.isEmpty() ? fallback : t.getIntValue();
    }
   #if JUCE_LINUX
    // the firmware's own flags file, wherever this kernel puts it (looked up once)
    static juce::File throttledFile()
    {
        // /sys/devices/platform/<soc>/<soc>:firmware/get_throttled (the Pi 5 names its soc by
        // address). Two levels by hand: a recursive search of /sys follows links round in circles.
        static const juce::File found = []
        {
            for (auto& soc : juce::File ("/sys/devices/platform").findChildFiles (juce::File::findDirectories, false))
                for (auto& fw : soc.findChildFiles (juce::File::findDirectories, false, "*firmware"))
                    if (fw.getChildFile ("get_throttled").existsAsFile()) return fw.getChildFile ("get_throttled");
            return juce::File();
        }();
        return found;
    }
    static int vcgencmdThrottled()
    {
        juce::ChildProcess p;
        if (! p.start ("vcgencmd get_throttled", juce::ChildProcess::wantStdOut)) return -1;
        const auto out = p.readAllProcessOutput();   // throttled=0x50000
        if (! p.waitForProcessToFinish (2000) || ! out.contains ("=")) return -1;
        return out.fromFirstOccurrenceOf ("=", false, false).trim().getHexValue32();
    }
   #endif
};
