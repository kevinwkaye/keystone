#pragma once
// Kaleidoscope: the pedals Keystone can load, one at a time, for messing about.
// Each is the same plugin you use in Ableton, built for this machine and kept in the
// Pedals folder beside Keystone. This table gives each one its name on screen, what it
// was modelled on, its colour, and what its footswitches do (from each plugin's own
// foot control plan). A pedal that isn't in the table still loads, with plain labels.
//
// Portable: the phone gets it in the schema, the floor screen reads it directly.

#include <array>
#include <cctype>
#include <string>

namespace ks
{
struct PedalSwitch
{
    const char* label;   // short, for the floor screen tile
    const char* tap;     // what a tap does
    const char* hold;    // what a hold does ("" for nothing)
};

struct PedalInfo
{
    const char* id;       // lower case, no spaces: matches the .vst3 file name the same way
    const char* name;     // as shown
    const char* from;     // the pedal it recreates
    const char* colour;   // its colour on the phone, the floor screen and Pebble's eyes
    std::array<PedalSwitch, 4> sw;
    const char* combos;   // the two-switch jobs worth knowing, in a line
};

inline const std::array<PedalInfo, 14>& pedalTable()
{
    static const std::array<PedalInfo, 14> t { {
        { "murmur", "Murmur", "Chase Bliss MOOD", "#9B8CFF",
          { { { "WET", "Wet channel on or off", "Freeze" }, { "LOOP", "Loop: record, play, stop", "Overdub" },
              { "TAP", "Tap tempo", "Save to the slot" }, { "SLOT", "Next preset slot", "Previous slot" } } },
          "A and B together switch both channels. Hold B and C for Murk." },
        { "afterglow", "Afterglow", "Gamechanger Audio PLUS", "#F2C14E",
          { { { "CATCH", "Brass pedal: catch a layer", "" }, { "ON", "Footswitch on or off", "Wet, Latch or Hide" },
              { "KILL DRY", "Kill Dry on or off", "Clear every layer" }, { "PRESET", "Next preset", "Previous preset" } } },
          "A and B together bypass it." },
        { "oilslick", "OilSlick", "EarthQuaker Rainbow Machine", "#5FD3C6",
          { { { "ON", "On or off", "On while held" }, { "MAGIC", "Magic on or off", "Runaway" },
              { "JUMP", "Jump a third up", "Dive a fourth down" }, { "FREEZE", "Freeze on or off", "Freeze while held" } } },
          "A and B together tame the feedback." },
        { "changeling", "Changeling", "Meris Enzo", "#FF7AA8",
          { { { "ON", "On or off", "Saw or square" }, { "TAP", "Tap tempo", "Runaway delay" },
              { "MODE", "Next synth mode", "Ring chaos" }, { "SUSTAIN", "Sustain latch", "Sustain while held" } } },
          "" },
        { "backtrack", "Backtrack", "Montreal Assembly Count to 5", "#E8935A",
          { { { "ON", "On or off", "Transcend" }, { "SOFT", "Soft switch", "Freeze, sample or record" },
              { "REVERSE", "Reverse", "Reverse while held" }, { "PRESET", "Next preset", "Previous preset" } } },
          "A and B together change mode." },
        { "speckle", "Speckle", "Hologram Microcosm", "#85B7EB",
          { { { "ON", "On or off", "Reverse while held" }, { "HOLD", "Hold on or off", "Hold while held" },
              { "REC", "Loop record or overdub", "Undo" }, { "PLAY", "Loop stop or play", "Erase" } } },
          "A and B together: next effect." },
        { "jetstream", "Jetstream", "Hologram Infinite Jets", "#6FA8FF",
          { { { "JET A", "Catch", "Sustain while held" }, { "JET B", "Catch", "Sustain while held" },
              { "MEMORY", "Hold Memory on or off", "Bypass" }, { "PRESET", "Next preset", "Previous preset" } } },
          "A and B together bypass it." },
        { "prismpatch", "Prism Patch", "Hologram Chroma Console", "#C792EA",
          { { { "SLOT 1", "Module 1 on or off", "Bypass everything" }, { "SLOT 2", "Module 2 on or off", "Capture" },
              { "SLOT 3", "Module 3 on or off", "Gestures" }, { "SLOT 4", "Module 4 on or off", "Tap tempo" } } },
          "A and B together: next preset." },
        { "bitrot", "Bitrot", "EarthQuaker Data Corrupter", "#9BE564",
          { { { "ON", "On or off", "On while held" }, { "SUB", "Next subharmonic", "Previous subharmonic" },
              { "FREEZE", "Freeze on or off", "Freeze while held" }, { "OCT UP", "Octave up on or off", "Octave up while held" } } },
          "A and B together: next preset." },
        { "monolith", "Monolith", "Meris Mercury7", "#B8C4D6",
          { { { "ON", "On or off, with trails", "Clear the tail" }, { "SWELL", "Auto swell on or off", "Swell while held" },
              { "INFINITE", "Infinite on or off", "Infinite while held" }, { "SHIMMER", "Shimmer on or off", "Shimmer while held" } } },
          "A and B together: next preset." },
        { "seasick", "SeaSick", "Fairfield Shallow Water", "#4FC1A6",
          { { { "ON", "On or off", "On while held" }, { "SEASICK", "Seasick on or off", "Swell into it" },
              { "FREEZE", "Freeze on or off", "Freeze while held" }, { "RE-ROLL", "A fresh lurch", "Pluck" } } },
          "A and B together calm it down." },
        { "lathe", "Lathe", "Chase Bliss Warped Vinyl", "#D9A066",
          { { { "ON", "On or off", "On while held" }, { "TAP", "Tap tempo", "Brake" },
              { "RAMP", "Run the ramp once", "Ramp while held" }, { "PRESET", "Preset A or B", "Save to the slot" } } },
          "A and B together reset the ramp." },
        { "grotto", "Grotto", "EarthQuaker Afterneath", "#7E9CD8",
          { { { "ON", "On or off, the cave rings out", "On while held" }, { "RUNAWAY", "Runaway on or off", "Runaway while held" },
              { "FREEZE", "Freeze on or off", "Freeze while held" }, { "DRY KILL", "Dry Kill on or off", "Dry Kill while held" } } },
          "A and B together tame it." },
        { "divebomb", "Divebomb", "Treadle and stomp pitch", "#FF6B5B",
          { { { "ON", "On or off", "Classic or chords" }, { "WHAMMY", "Whammy while held", "" },
              { "NEXT", "Next setting", "Previous setting" }, { "SHIFT", "Shift layer", "Shift while held" } } },
          "A and B together swap Treadle and Stomp. Hold B and C for the next model." },
    } };
    return t;
}

// "Prism Patch.vst3" -> "prismpatch"
inline std::string pedalIdFor (const std::string& fileStem)
{
    std::string id;
    for (char c : fileStem)
        if (std::isalnum ((unsigned char) c)) id += (char) std::tolower ((unsigned char) c);
    return id;
}

inline const PedalInfo* findPedal (const std::string& id)
{
    for (const auto& p : pedalTable()) if (id == p.id) return &p;
    return nullptr;
}
} // namespace ks
