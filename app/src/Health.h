#pragma once
// A snapshot of how Keystone is doing, for the laptop status window and the phone's
// Health page. Built once a second on the main thread.

#include <juce_core/juce_core.h>

struct Health
{
    juce::String version, song;
    int songNumber = 0;
    double uptimeSec = 0;

    bool audioOk = false;
    juce::String audioText;
    float inDb = -100;            // bass input peak
    int inChannel = 1;            // 0 = input 1, 1 = input 2
    bool inputDead = false;       // exact digital silence: unplugged, or Windows blocking the input

    bool fsOk = false;
    juce::String fsText, fsLast;
    double fsAgoSec = -1;         // -1 = never pressed

    float expPos = 0;
    double expAgoSec = -1;        // -1 = never moved
    bool expCalibrated = false;
    bool expReversed = false;

    float outDb = -100, limDb = 0;
    float cpu = 0;
    int dropouts = 0, dropoutsRecent = 0;
    bool light = false;

    // the machine itself (0.5.8): temperature, fan and power on the Pi, storage and memory anywhere
    float tempC = -1;
    int fanRpm = -1;
    bool hasPower = false, underVoltNow = false, underVoltEver = false, throttledNow = false, throttledEver = false;
    juce::int64 freeMb = -1;
    int memUsedMb = -1, memTotalMb = -1;
    int reconnects = 0;          // times the interface dropped and came back since start
    juce::String projJson;       // the projector (0.6.0)
    bool svcOn = false, svcOk = false, svcFitted = false;   // the service button on the back (0.6.2), the Pi only
    juce::String svcText;

    int phones = 0;
    juce::String phoneAddresses, laptopAddresses;
    juce::String watchVersion, watchAddress;   // empty: no watch connected
    bool watchUpdate = false;                  // Keystone has offered the watch a newer app (0.6.6)

    juce::String toJson() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("t", "health");
        o->setProperty ("version", version);
       #if JUCE_WINDOWS
        o->setProperty ("device", "laptop");   // which release Update firmware fetches
       #else
        o->setProperty ("device", "pi");
       #endif
        o->setProperty ("uptime", (int) uptimeSec);
        auto obj = [] (std::initializer_list<std::pair<const char*, juce::var>> items)
        {
            auto* d = new juce::DynamicObject();
            for (auto& [k, v] : items) d->setProperty (k, v);
            return juce::var (d);
        };
        o->setProperty ("audio", obj ({ { "ok", audioOk }, { "text", audioText } }));
        o->setProperty ("in", obj ({ { "db", juce::roundToInt (inDb) }, { "ch", inChannel + 1 }, { "dead", inputDead } }));
        o->setProperty ("fs", obj ({ { "ok", fsOk }, { "text", fsText }, { "ago", fsAgoSec < 0 ? -1 : (int) fsAgoSec }, { "last", fsLast } }));
        o->setProperty ("exp", obj ({ { "pos", expPos }, { "ago", expAgoSec < 0 ? -1 : (int) expAgoSec }, { "cal", expCalibrated }, { "rev", expReversed } }));
        o->setProperty ("out", obj ({ { "db", juce::roundToInt (outDb) }, { "lim", limDb } }));
        // "laptop" is the machine Keystone runs on (the Pi too): the name stays so older
        // phone apps keep reading it
        o->setProperty ("laptop", obj ({ { "cpu", juce::roundToInt (cpu * 100) }, { "dropouts", dropouts }, { "recent", dropoutsRecent }, { "light", light } }));
        o->setProperty ("sys", obj ({ { "temp", std::round (tempC * 10.0f) / 10.0f }, { "fan", fanRpm },
                                      { "power", hasPower }, { "uv", underVoltNow }, { "uvEver", underVoltEver },
                                      { "thr", throttledNow }, { "thrEver", throttledEver },
                                      { "free", (juce::int64) freeMb }, { "memUsed", memUsedMb }, { "memTotal", memTotalMb },
                                      { "reconnects", reconnects } }));
        if (projJson.isNotEmpty()) o->setProperty ("proj", juce::JSON::parse (projJson));
        if (svcOn) o->setProperty ("svc", obj ({ { "ok", svcOk }, { "fitted", svcFitted }, { "text", svcText } }));
        o->setProperty ("link", obj ({ { "phones", phones }, { "laptop", laptopAddresses } }));
        o->setProperty ("watch", obj ({ { "on", watchVersion.isNotEmpty() }, { "v", watchVersion }, { "ip", watchAddress }, { "upd", watchUpdate } }));
        return juce::JSON::toString (juce::var (o), true);
    }
};
