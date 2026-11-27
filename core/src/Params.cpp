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
        case P::GrScatter: return "grains.pitch=4";
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
        // reverb: Room 0, Hall 1, Plate 2, Spring 3, Chamber 4, Cloud 5, Bloom 6, Chorale 7,
        // Shimmer 8, Magneto 9, Nonlinear 10, Impulse 11
        case P::RvDecay: return "reverb.type!=10|11";
        case P::RvDiffusion: case P::RvSize: return "reverb.type=0|1|2|4|5|6|7|8|9";
        case P::RvShimmer: case P::RvInterval: return "reverb.type=0|1|2|4|5|6|8";
        case P::RvSprings: case P::RvTension: return "reverb.type=3";
        case P::RvVowel: case P::RvVoices: return "reverb.type=7";
        case P::RvEchoTime: case P::RvEchoes: return "reverb.type=9";
        case P::RvShape: case P::RvLength: return "reverb.type=10";
        case P::RvFreeze: case P::RvMod: case P::RvGrit: return "reverb.type!=3|10|11";
        case P::BitrotSubMix: return "bitrot.subRoot=1";
        case P::BitrotFloorHz: return "bitrot.floor=1";
        case P::BitrotFmDepth: return "bitrot.fm=1";
        case P::BitrotPeak: return "bitrot.pedal=2|3";
        case P::WatchReverse: return "watch.motion=1";
        default: return "";
    }
}

const ModuleInfo* moduleTable (int& count)
{
    // signal order, as on the Tone page
    static const ModuleInfo mods[] = {
        { "split", "Split", "Three paths", "Bottom dry, bottom wet, full sound" },
        { "lowcomp", "Low compressor", "Bottom dry", "Light, slap mode on hold D" },
        { "drone", "Drone", "Bottom wet", "Capture, duck, octave, dimension" },
        { "drive", "Drive", "Full sound", "Edge and Drive voices" },
        { "bitrot", "Bitrot", "Full sound", "Tracking synth: square, sub, oscillator" },
        { "gloss", "Gloss", "Full sound", "Lush pad on the notes you hold. Hold B in Grit" },
        { "preamp", "Preamp", "Full sound", "Valve, tone stack, Cut and tape" },
        { "filter", "Filter", "Full sound", "Envelope or manual" },
        { "wah", "Wah", "Full sound", "Pedal wah, tap C in Groove" },
        { "dim", "Dimension", "Bottom wet", "Mono safe chorus" },
        { "grains", "Speckle", "Full sound", "Eleven effects, filter, mod and space" },
        { "delay", "Delay", "Full sound", "Twelve types, tape machine, tap" },
        { "reverb", "Reverb", "Full sound", "Twelve spaces, shimmer, your own impulses" },
        { "pedal", "Pedal jobs", "Control", "Dirt blend or Bitrot, wah, volume, Bloom" },
        { "bloom", "Bloom sweep", "Control", "Grains then wash, per song" },
        { "watch", "Watch", "Control", "Hold C in Grit: your wrist works the pedal" },
        { "tempo", "Tempo", "Control", "Saved per song, tap on Play" },
        { "recorder", "Recorder", "Control", "Rehearsal takes, room from the phone" },
        { "footswitches", "Footswitches", "Control", "Tap and hold jobs" },
        { "output", "Output", "Control", "Level, limiter, amp or PA" },
        // Kuddle, the home rig, in playing order
        { "k.input", "Input", "Kuddle", "Gain and gate" },
        { "k.mute", "Mute", "Kuddle", "Flatwound and foam feel" },
        { "k.wah", "Auto wah", "Kuddle", "Warm and vocal, lows kept dry" },
        { "k.di", "Valve DI", "Kuddle", "Always on" },
        { "k.opto", "Opto", "Kuddle", "Slow, smooth squeeze" },
        { "k.tape", "Tape", "Kuddle", "Push, wow, flutter, bump" },
        { "k.tone", "Tone", "Kuddle", "Bass, mids, treble roll-off" },
        { "k.room", "Room", "Kuddle", "Small dark room or spring" },
        { "k.melt", "Melt sweep", "Kuddle", "The pedal: open to melted" },
        { "k.output", "Output", "Kuddle", "Level" },
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
    if (p == P::RvSwell && v < 0.01f) return "Off";
    if (p == P::PedalVolFloor && v <= -59.5f) return "Silent";
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
