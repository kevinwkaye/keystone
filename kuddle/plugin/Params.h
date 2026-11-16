#pragma once
// Every Kuddle parameter in one list: the host, the panel, presets and Keeper all read it,
// and a remote controller (the Keystone app, later the Pi) can read and set the same ids.

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace kp
{
enum class Kind { Float, Bool, Choice };

struct Def
{
    const char* id;
    const char* name;          // as shown on the panel
    Kind kind;
    float min, max, def, step;
    const char* unit;          // "", "dB", "Hz", "kHz", "s", "ms"
    bool inPreset;             // saved in presets (everything except the live and global ones)
    std::vector<const char*> choices {};
};

// ids
namespace id
{
constexpr const char* melt = "melt";
constexpr const char* meltWah = "meltWah";
constexpr const char* reachMute = "reachMute";
constexpr const char* reachTape = "reachTape";
constexpr const char* reachTreble = "reachTreble";
constexpr const char* reachRoom = "reachRoom";
constexpr const char* inGain = "inGain";
constexpr const char* gate = "gate";
constexpr const char* tunerRef = "tunerRef";
constexpr const char* muteOn = "muteOn";
constexpr const char* foam = "foam";
constexpr const char* thump = "thump";
constexpr const char* strings = "strings";
constexpr const char* wahOn = "wahOn";
constexpr const char* wahMode = "wahMode";
constexpr const char* wahSense = "wahSense";
constexpr const char* wahRange = "wahRange";
constexpr const char* wahSpeed = "wahSpeed";
constexpr const char* wahPeak = "wahPeak";
constexpr const char* wahPos = "wahPos";
constexpr const char* diDrive = "diDrive";
constexpr const char* diAir = "diAir";
constexpr const char* diSag = "diSag";
constexpr const char* diLowCut = "diLowCut";
constexpr const char* optoOn = "optoOn";
constexpr const char* optoSqueeze = "optoSqueeze";
constexpr const char* optoSpeed = "optoSpeed";
constexpr const char* optoBlend = "optoBlend";
constexpr const char* optoHold = "optoHold";
constexpr const char* tapeOn = "tapeOn";
constexpr const char* tapePush = "tapePush";
constexpr const char* tapeWow = "tapeWow";
constexpr const char* tapeFlutter = "tapeFlutter";
constexpr const char* tapeBump = "tapeBump";
constexpr const char* tapeHiss = "tapeHiss";
constexpr const char* toneOn = "toneOn";
constexpr const char* bass = "bass";
constexpr const char* bassHz = "bassHz";
constexpr const char* mids = "mids";
constexpr const char* midsHz = "midsHz";
constexpr const char* treble = "treble";
constexpr const char* roomOn = "roomOn";
constexpr const char* roomType = "roomType";
constexpr const char* roomSize = "roomSize";
constexpr const char* roomDecay = "roomDecay";
constexpr const char* roomPre = "roomPre";
constexpr const char* roomMix = "roomMix";
constexpr const char* output = "output";
constexpr const char* expTarget = "expTarget";
} // namespace id

// Percent-style controls run 0..100 on the panel, like the mockup's flip cards.
inline const std::vector<Def>& all()
{
    static const std::vector<Def> defs {
        // Melt and its reach
        { id::melt,        "Melt",            Kind::Float, 0, 100, 0, 1, "", false },
        { id::meltWah,     "Pedal works the wah in manual", Kind::Bool, 0, 1, 1, 1, "", true },
        { id::reachMute,   "Mute reach",      Kind::Float, 0, 100, 60, 1, "", true },
        { id::reachTape,   "Tape push reach", Kind::Float, 0, 100, 50, 1, "", true },
        { id::reachTreble, "Treble roll-off reach", Kind::Float, 0, 100, 70, 1, "", true },
        { id::reachRoom,   "Room reach",      Kind::Float, 0, 100, 0, 1, "", true },
        // input
        { id::inGain,      "Gain",            Kind::Float, -12, 12, 0, 0.5f, "dB", true },
        { id::gate,        "Gate",            Kind::Float, -90, -40, -70, 1, "dB", true },
        { id::tunerRef,    "Tuner",           Kind::Float, 430, 450, 440, 1, "Hz", false },
        // Mute
        { id::muteOn,      "Mute",            Kind::Bool, 0, 1, 1, 1, "", true },
        { id::foam,        "Foam",            Kind::Float, 0, 100, 40, 1, "", true },
        { id::thump,       "Thump",           Kind::Float, 0, 100, 55, 1, "", true },
        { id::strings,     "Strings",         Kind::Choice, 0, 2, 2, 1, "", true, { "NEW", "WORN", "OLD" } },
        // Auto wah
        { id::wahOn,       "Auto wah",        Kind::Bool, 0, 1, 1, 1, "", true },
        { id::wahMode,     "Mode",            Kind::Choice, 0, 1, 0, 1, "", true, { "AUTO", "MAN" } },
        { id::wahSense,    "Sense",           Kind::Float, 0, 100, 55, 1, "", true },
        { id::wahRange,    "Range",           Kind::Choice, 0, 1, 0, 1, "", true, { "LOW", "MID" } },
        { id::wahSpeed,    "Speed",           Kind::Float, 0, 100, 30, 1, "", true },
        { id::wahPeak,     "Peak",            Kind::Float, 0, 100, 40, 1, "", true },
        { id::wahPos,      "Position",        Kind::Float, 0, 100, 50, 1, "", true },
        // Valve DI
        { id::diDrive,     "Drive",           Kind::Float, 0, 100, 30, 1, "", true },
        { id::diAir,       "Air",             Kind::Float, 0, 100, 20, 1, "", true },
        { id::diSag,       "Sag",             Kind::Float, 0, 100, 35, 1, "", true },
        { id::diLowCut,    "Low cut",         Kind::Float, 20, 120, 30, 1, "Hz", true },
        // Opto
        { id::optoOn,      "Opto",            Kind::Bool, 0, 1, 1, 1, "", true },
        { id::optoSqueeze, "Squeeze",         Kind::Float, 0, 100, 45, 1, "", true },
        { id::optoSpeed,   "Speed",           Kind::Choice, 0, 1, 0, 1, "", true, { "SLOW", "FAST" } },
        { id::optoBlend,   "Blend",           Kind::Float, 0, 100, 80, 1, "", true },
        { id::optoHold,    "Hold D dB",       Kind::Float, 0, 12, 6, 1, "dB", true },
        // Tape
        { id::tapeOn,      "Tape",            Kind::Bool, 0, 1, 1, 1, "", true },
        { id::tapePush,    "Push",            Kind::Float, 0, 100, 35, 1, "", true },
        { id::tapeWow,     "Wow",             Kind::Float, 0, 100, 10, 1, "", true },
        { id::tapeFlutter, "Flutter",         Kind::Float, 0, 100, 15, 1, "", true },
        { id::tapeBump,    "Bump",            Kind::Float, 0, 100, 40, 1, "", true },
        { id::tapeHiss,    "Hiss",            Kind::Float, 0, 100, 0, 1, "", true },
        // Tone
        { id::toneOn,      "Tone",            Kind::Bool, 0, 1, 1, 1, "", true },
        { id::bass,        "Bass",            Kind::Float, -12, 12, 2, 0.5f, "dB", true },
        { id::bassHz,      "Bass Hz",         Kind::Float, 40, 200, 80, 1, "Hz", true },
        { id::mids,        "Mids",            Kind::Float, -12, 12, 1, 0.5f, "dB", true },
        { id::midsHz,      "Mids Hz",         Kind::Float, 200, 1500, 400, 10, "Hz", true },
        { id::treble,      "Treble",          Kind::Float, 1, 12, 3.2f, 0.1f, "kHz", true },
        // Room
        { id::roomOn,      "Room",            Kind::Bool, 0, 1, 0, 1, "", true },
        { id::roomType,    "Type",            Kind::Choice, 0, 1, 0, 1, "", true, { "DARK", "SPRING" } },
        { id::roomSize,    "Size",            Kind::Float, 0, 100, 30, 1, "", true },
        { id::roomDecay,   "Decay s",         Kind::Float, 0.2f, 3.0f, 0.8f, 0.1f, "s", true },
        { id::roomPre,     "Pre ms",          Kind::Float, 0, 60, 10, 1, "ms", true },
        { id::roomMix,     "Mix",             Kind::Float, 0, 100, 20, 1, "", true },
        // output and the pedal's job
        { id::output,      "Output",          Kind::Float, -24, 12, -3, 0.5f, "dB", true },
        { id::expTarget,   "Expression",      Kind::Choice, 0, 3, 0, 1, "", true, { "MELT", "WAH", "VOLUME", "ROOM" } },
    };
    return defs;
}

inline const Def* find (const juce::String& pid)
{
    for (const auto& d : all()) if (pid == d.id) return &d;
    return nullptr;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& d : all())
    {
        const juce::ParameterID pid { d.id, 1 };
        if (d.kind == Kind::Bool)
            l.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def > 0.5f));
        else if (d.kind == Kind::Choice)
        {
            juce::StringArray names;
            for (auto* c : d.choices) names.add (c);
            l.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, names, (int) d.def));
        }
        else
            l.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, juce::NormalisableRange<float> (d.min, d.max), d.def,
                                                                juce::AudioParameterFloatAttributes().withLabel (d.unit)));
    }
    return l;
}

// How a value reads on a flip card: short, no units (the label under the card says them).
inline juce::String shortText (const Def& d, float v)
{
    if (d.kind == Kind::Bool) return v > 0.5f ? "ON" : "OFF";
    if (d.kind == Kind::Choice) return d.choices[(size_t) juce::jlimit (0, (int) d.choices.size() - 1, (int) std::lround (v))];
    if (juce::String (d.id) == id::gate && v <= -89.5f) return "OFF";
    if (d.step < 1.0f)
    {
        if (std::string (d.unit) == "dB") { const float r = std::round (v * 2.0f) / 2.0f; return (r > 0 ? "+" : "") + (std::fmod (r, 1.0f) == 0.0f ? juce::String ((int) r) : juce::String (r, 1)); }
        return juce::String (v, 1);
    }
    const int r = (int) std::lround (v);
    if (std::string (d.unit) == "dB" && r > 0) return "+" + juce::String (r);
    return juce::String (r);
}
} // namespace kp
