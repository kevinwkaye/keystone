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

    float outDb = -100, limDb = 0;
    float cpu = 0;
    int dropouts = 0, dropoutsRecent = 0;
    bool light = false;

    int phones = 0;
    juce::String phoneAddresses, laptopAddresses;

    juce::String toJson() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("t", "health");
        o->setProperty ("version", version);
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
        o->setProperty ("exp", obj ({ { "pos", expPos }, { "ago", expAgoSec < 0 ? -1 : (int) expAgoSec }, { "cal", expCalibrated } }));
        o->setProperty ("out", obj ({ { "db", juce::roundToInt (outDb) }, { "lim", limDb } }));
        o->setProperty ("laptop", obj ({ { "cpu", juce::roundToInt (cpu * 100) }, { "dropouts", dropouts }, { "recent", dropoutsRecent }, { "light", light } }));
        o->setProperty ("link", obj ({ { "phones", phones }, { "laptop", laptopAddresses } }));
        return juce::JSON::toString (juce::var (o), true);
    }
};
