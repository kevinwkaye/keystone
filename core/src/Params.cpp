#include "keystone/Engine.h"
#include "keystone/Params.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace ks
{
int findParam (const char* id)
{
    const auto* t = paramTable();
    for (int i = 0; i < kNumParams; ++i)
        if (std::strcmp (t[i].id, id) == 0) return i;
    return -1;
}

const char* showIf (P p)
{
    switch (p)
    {
        case P::GrVariant: case P::GrActivity: case P::GrRepeats: case P::GrContour:
            return "grains.effect!=0";
        case P::GrSize: case P::GrDensity: case P::GrSpray: case P::GrReverse: case P::GrFeedback: case P::GrShape:
            return "grains.effect=0";
        case P::DlTapeMachine: case P::DlTapeMode: case P::DlTapeAge: case P::DlWow: case P::DlCrinkle:
        case P::DlBias: case P::DlSpring: case P::DlBoost:
            return "delay.type=2";
        case P::DlDualRatio: case P::DlDualSeries: return "delay.type=3";
        case P::DlPattern: return "delay.type=4";
        case P::DlIceInterval: case P::DlIceBlend: return "delay.type=6";
        case P::DlSwellRise: return "delay.type=7|11";
        case P::DlTremSpeed: case P::DlTremDepth: case P::DlTremShape: return "delay.type=8";
        case P::DlFiltSpeed: case P::DlFiltRes: return "delay.type=9";
        case P::DlLofiBits: case P::DlLofiRate: return "delay.type=10";
        default: return "";
    }
}

const ModuleInfo* moduleTable (int& count)
{
    // signal order, as on the Tone page
    static const ModuleInfo mods[] = {
        { "split", "Split", "Low band", "Crossover and tight shelf" },
        { "lowcomp", "Low compressor", "Low band", "Light, slap mode on hold D" },
        { "drone", "Drone", "Low band", "Capture, duck, octave, dimension" },
        { "drive", "Drive", "High band", "Edge and Drive voices" },
        { "bitrot", "Bitrot", "High band", "Fuzz, crush, mix" },
        { "preamp", "Preamp", "High band", "Growl to Cut, mid focus" },
        { "filter", "Filter", "High band", "Envelope or manual" },
        { "dim", "Dimension", "High band", "Mono safe" },
        { "grains", "Grains", "High band", "Sparkle and eleven grain effects, all up top" },
        { "delay", "Delay", "High band", "Twelve types, tape machine, tap" },
        { "reverb", "Reverb and shimmer", "High band", "Size, shimmer interval" },
        { "bloom", "Bloom sweep", "Control", "Zones and reach per song" },
        { "footswitches", "Footswitches", "Control", "Tap and hold jobs" },
        { "output", "Output", "Control", "Level, limiter, amp or PA" },
    };
    count = (int) (sizeof (mods) / sizeof (mods[0]));
    return mods;
}

static std::string trimNum (float v, int decimals)
{
    char b[32];
    std::snprintf (b, sizeof (b), "%.*f", decimals, v);
    return b;
}

std::string formatValue (P p, float v)
{
    const auto& inf = info (p);
    if (inf.kind == Kind::Toggle) return v > 0.5f ? "On" : "Off";
    if (inf.kind == Kind::Choice)
    {
        std::string all = inf.choices;
        int idx = (int) std::lround (v), i = 0;
        size_t start = 0;
        while (true)
        {
            const size_t bar = all.find ('|', start);
            if (i == idx) return all.substr (start, bar == std::string::npos ? std::string::npos : bar - start);
            if (bar == std::string::npos) return all;
            start = bar + 1;
            ++i;
        }
    }
    const std::string u = inf.unit;
    if (p == P::DroneSustain && v >= 0.995f) return "Inf";
    if (p == P::EdgeGate || p == P::DriveGate) { if (v <= -89.0f) return "Off"; }
    if (p == P::DlLofiBits) return trimNum (v, 0) + " bits";
    if (u == "%") return trimNum (v * 100.0f, 0) + "%";
    if (u == "Hz") return v >= 1000.0f ? trimNum (v / 1000.0f, 1) + "k" : v < 10.0f ? trimNum (v, v < 1.0f ? 2 : 1) + "Hz" : trimNum (v, 0);
    if (u == "ms") return trimNum (v, 0) + "ms";
    if (u == "s") return trimNum (v, v < 10 ? 1 : 0) + "s";
    if (u == "dB") return (v > 0.05f ? "+" : "") + trimNum (v, std::fabs (v) < 10 ? 1 : 0);
    if (u == "/s") return trimNum (v, 0) + "/s";
    return trimNum (v, std::fabs (v) < 10 ? 1 : 0);
}
} // namespace ks
