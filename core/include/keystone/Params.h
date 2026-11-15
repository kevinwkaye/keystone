#pragma once
// Keystone parameter list: the single source of truth.
//
// Every control, footswitch job and preset value is listed here once. The engine reads
// it, presets save it, the link sends it to the phone, and the phone builds every Tone
// page from it. Add a parameter here and it appears everywhere.

#include <cstdint>

namespace ks
{
enum class Kind : uint8_t
{
    Toggle,   // on / off
    Choice,   // segmented buttons, value is the index
    Flip,     // a number on a flip card
    Slider,   // a wide slider with end labels (choices holds "left|right")
};

enum Flags : uint8_t
{
    None = 0,
    Log = 1,      // log scale (frequencies, times)
    Global = 2,   // not stored in song presets
    Hidden = 4,   // not shown on Tone pages
};

struct ParamInfo
{
    const char* id;
    const char* label;
    const char* module;
    const char* section;
    Kind kind;
    float min, max, def;
    const char* unit;
    const char* choices;   // "A|B|C" for Choice, "left|right" for Slider
    uint8_t flags;
};

// clang-format off
#define KS_PARAMS(P) \
    /* ---- Low band ---- */ \
    P(SplitFreq,      "split.freq",      "Crossover",     "split",   "Split",          Flip,   70,   150,  100,  "Hz", "",   Log) \
    P(SplitTight,     "split.tight",     "Tight shelf",   "split",   "Split",          Flip,   -9,   0,    -4,   "dB", "",   None) \
    P(SplitWeight,    "split.weight",    "Weight",        "split",   "Split",          Flip,   0,    4,    1.5f, "dB", "",   None) \
    P(SplitLowLevel,  "split.lowLevel",  "Low level",     "split",   "Levels",         Flip,   -12,  6,    0,    "dB", "",   None) \
    P(SplitHighLevel, "split.highLevel", "High level",    "split",   "Levels",         Flip,   -12,  6,    0,    "dB", "",   None) \
    P(CompOn,         "lowcomp.on",      "On",            "lowcomp", "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(CompAmount,     "lowcomp.amount",  "Squeeze",       "lowcomp", "Compression",    Flip,   0,    1,    0.35f,"%",  "",   None) \
    P(CompRelease,    "lowcomp.release", "Release",       "lowcomp", "Compression",    Flip,   40,   400,  150,  "ms", "",   Log) \
    P(CompSlap,       "lowcomp.slap",    "Slap mode",     "lowcomp", "Slap",           Toggle, 0,    1,    0,    "",   "",   None) \
    P(CompSlapAmount, "lowcomp.slapAmt", "Slap squeeze",  "lowcomp", "Slap",           Flip,   0,    1,    0.7f, "%",  "",   None) \
    P(DroneOn,        "drone.on",        "On",            "drone",   "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(DroneLevel,     "drone.level",     "Level",         "drone",   "Drone",          Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(DroneSustain,   "drone.sustain",   "Sustain",       "drone",   "Drone",          Flip,   0,    1,    1,    "%",  "",   None) \
    P(DroneSwell,     "drone.swell",     "Swell",         "drone",   "Drone",          Flip,   0.05f,4,    1.2f, "s",  "",   Log) \
    P(DroneDuck,      "drone.duck",      "Duck",          "drone",   "Movement",       Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(DroneTone,      "drone.tone",      "Tone",          "drone",   "Movement",       Flip,   100,  2000, 450,  "Hz", "",   Log) \
    P(DroneSend,      "drone.send",      "To reverb",     "drone",   "Movement",       Flip,   0,    1,    0,    "%",  "",   None) \
    P(DroneOctave,    "drone.octave",    "Octave up",     "drone",   "Voice",          Toggle, 0,    1,    0,    "",   "",   None) \
    P(DroneDim,       "drone.dim",       "Dimension",     "drone",   "Voice",          Choice, 0,    4,    3,    "",   "Off|1|2|3|4", None) \
    P(DroneTuning,    "drone.tuning",    "Capture window","drone",   "Voice",          Choice, 0,    1,    0,    "",   "Standard E|Drop D", None) \
    /* ---- High band ---- */ \
    P(DriveOn,        "drive.on",        "On",            "drive",   "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(DriveVoice,     "drive.voice",     "Voice",         "drive",   "Editing voice",  Choice, 0,    1,    0,    "",   "Edge|Drive", None) \
    P(DriveSource,    "drive.source",    "Source",        "drive",   "Source",         Choice, 0,    1,    0,    "",   "Capture|Model", None) \
    P(EdgeGain,       "drive.edge.gain", "Gain",          "drive",   "Voice settings", Flip,   0,    10,   3,    "",   "",   None) \
    P(EdgeTone,       "drive.edge.tone", "Tone",          "drive",   "Voice settings", Flip,   0,    10,   6.5f, "",   "",   None) \
    P(EdgeLevel,      "drive.edge.level","Level",         "drive",   "Voice settings", Flip,   -12,  12,   0,    "dB", "",   None) \
    P(EdgeLowCut,     "drive.edge.lowcut","Low cut",      "drive",   "Voice settings", Flip,   20,   300,  120,  "Hz", "",   Log) \
    P(EdgePresence,   "drive.edge.presence","Presence",   "drive",   "Voice settings", Flip,   -6,   6,    2,    "dB", "",   None) \
    P(EdgeGate,       "drive.edge.gate", "Gate",          "drive",   "Voice settings", Flip,   -90,  -30,  -70,  "dB", "",   None) \
    P(EdgeMode,       "drive.edge.mode", "Clipping",      "drive",   "Clipping (model only)", Choice, 0, 2, 1,  "",   "LED|Clean|Silicon", None) \
    P(DriveGain,      "drive.drive.gain","Gain",          "drive",   "Voice settings", Flip,   0,    10,   6.5f, "",   "",   None) \
    P(DriveTone,      "drive.drive.tone","Tone",          "drive",   "Voice settings", Flip,   0,    10,   6.5f, "",   "",   None) \
    P(DriveLevel,     "drive.drive.level","Level",        "drive",   "Voice settings", Flip,   -12,  12,   0,    "dB", "",   None) \
    P(DriveLowCut,    "drive.drive.lowcut","Low cut",     "drive",   "Voice settings", Flip,   20,   300,  120,  "Hz", "",   Log) \
    P(DrivePresence,  "drive.drive.presence","Presence",  "drive",   "Voice settings", Flip,   -6,   6,    2,    "dB", "",   None) \
    P(DriveGate,      "drive.drive.gate","Gate",          "drive",   "Voice settings", Flip,   -90,  -30,  -60,  "dB", "",   None) \
    P(DriveMode,      "drive.drive.mode","Clipping",      "drive",   "Clipping (model only)", Choice, 0, 2, 0,  "",   "LED|Clean|Silicon", None) \
    P(DriveFade,      "drive.fade",      "Switch fade",   "drive",   "Behaviour",      Flip,   5,    50,   15,   "ms", "",   None) \
    P(DriveMatch,     "drive.match",     "Level match",   "drive",   "Behaviour",      Choice, 0,    1,    1,    "",   "Off|Auto", None) \
    P(DriveMorph,     "drive.morph",     "Exp morph",     "drive",   "Behaviour",      Choice, 0,    1,    0,    "",   "Off|On", None) \
    P(DriveLowInto,   "drive.lowInto",   "Low end into drive","drive","Behaviour",     Slider, 0,    1,    0.35f,"%",  "None|Full", None) \
    P(BitrotOn,       "bitrot.on",       "On",            "bitrot",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(BitrotFuzz,     "bitrot.fuzz",     "Fuzz",          "bitrot",  "Fuzz and crush", Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BitrotBits,     "bitrot.bits",     "Bits",          "bitrot",  "Fuzz and crush", Flip,   3,    16,   8,    "",   "",   None) \
    P(BitrotRate,     "bitrot.rate",     "Crush rate",    "bitrot",  "Fuzz and crush", Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(BitrotTone,     "bitrot.tone",     "Tone",          "bitrot",  "Output",         Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(BitrotMix,      "bitrot.mix",      "Mix",           "bitrot",  "Output",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BitrotLevel,    "bitrot.level",    "Level",         "bitrot",  "Output",         Flip,   -12,  12,   0,    "dB", "",   None) \
    P(BitrotFirst,    "bitrot.first",    "Before drive",  "bitrot",  "Order",          Toggle, 0,    1,    0,    "",   "",   None) \
    P(PreOn,          "preamp.on",       "On",            "preamp",  "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(PreBlend,       "preamp.blend",    "Blend",         "preamp",  "Blend",          Slider, 0,    1,    0.7f, "%",  "Growl|Cut", None) \
    P(PreGrowlFreq,   "preamp.growlFreq","Growl centre",  "preamp",  "Growl",          Flip,   150,  250,  200,  "Hz", "",   None) \
    P(PreGrowl,       "preamp.growl",    "Growl",         "preamp",  "Growl",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(PreMidFreq,     "preamp.midFreq",  "Mid focus",     "preamp",  "Cut",            Flip,   700,  1500, 1000, "Hz", "",   Log) \
    P(PreMid,         "preamp.mid",      "Mid push",      "preamp",  "Cut",            Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(PreDrive,       "preamp.drive",    "Drive",         "preamp",  "Output",         Flip,   0,    1,    0.25f,"%",  "",   None) \
    P(PreDetail,      "preamp.detail",   "Detail",        "preamp",  "Output",         Flip,   0,    6,    2,    "dB", "",   None) \
    P(PreLevel,       "preamp.level",    "Level",         "preamp",  "Output",         Flip,   -12,  12,   0,    "dB", "",   None) \
    P(FiltOn,         "filter.on",       "On",            "filter",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(FiltMode,       "filter.mode",     "Mode",          "filter",  "Mode",           Choice, 0,    1,    0,    "",   "Envelope|Manual", None) \
    P(FiltType,       "filter.type",     "Shape",         "filter",  "Mode",           Choice, 0,    1,    0,    "",   "Low-pass|Band-pass", None) \
    P(FiltDir,        "filter.dir",      "Direction",     "filter",  "Mode",           Choice, 0,    1,    0,    "",   "Up|Down", None) \
    P(FiltSens,       "filter.sens",     "Sensitivity",   "filter",  "Sweep",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(FiltLow,        "filter.low",      "From",          "filter",  "Sweep",          Flip,   150,  1000, 300,  "Hz", "",   Log) \
    P(FiltHigh,       "filter.high",     "To",            "filter",  "Sweep",          Flip,   800,  5000, 2200, "Hz", "",   Log) \
    P(FiltQ,          "filter.q",        "Resonance",     "filter",  "Sweep",          Flip,   0,    1,    0.55f,"%",  "",   None) \
    P(FiltSpeed,      "filter.speed",    "Response",      "filter",  "Sweep",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(FiltMix,        "filter.mix",      "Mix",           "filter",  "Sweep",          Flip,   0,    1,    1,    "%",  "",   None) \
    P(FiltPos,        "filter.pos",      "Manual position","filter", "Manual",         Slider, 0,    1,    0.5f, "%",  "Low|High", None) \
    P(DimOn,          "dim.on",          "On",            "dim",     "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(DimMode,        "dim.mode",        "Mode",          "dim",     "Mode",           Choice, 0,    3,    1,    "",   "1|2|3|4", None) \
    P(DimMix,         "dim.mix",         "Mix",           "dim",     "Mix",            Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(GrOn,           "grains.on",       "On",            "grains",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(GrPitch,        "grains.pitch",    "Pitch",         "grains",  "Pitch",          Choice, 0,    3,    1,    "",   "Octave|Oct + 5th|2 octaves|Scatter", None) \
    P(GrScatter,      "grains.scatter",  "Movement",      "grains",  "Pitch",          Slider, 0,    1,    0.4f, "%",  "Steady|Scattered", None) \
    P(GrSize,         "grains.size",     "Size",          "grains",  "Grains",         Flip,   20,   300,  80,   "ms", "",   Log) \
    P(GrDensity,      "grains.density",  "Density",       "grains",  "Grains",         Flip,   1,    40,   12,   "/s", "",   Log) \
    P(GrSpray,        "grains.spray",    "Spray",         "grains",  "Grains",         Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(GrReverse,      "grains.reverse",  "Reverse",       "grains",  "Grains",         Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(GrFeedback,     "grains.feedback", "Feedback",      "grains",  "Grains",         Flip,   0,    0.9f, 0.25f,"%",  "",   None) \
    P(GrShape,        "grains.shape",    "Shape",         "grains",  "Grains",         Choice, 0,    1,    0,    "",   "Bell|Smooth", None) \
    P(GrHighPass,     "grains.hp",       "High-pass",     "grains",  "Placement",      Flip,   300,  4000, 1000, "Hz", "",   Log) \
    P(GrBright,       "grains.bright",   "Brightness",    "grains",  "Placement",      Flip,   -6,   6,    2,    "dB", "",   None) \
    P(GrMix,          "grains.mix",      "Mix",           "grains",  "Placement",      Flip,   0,    1,    0.35f,"%",  "",   None) \
    P(GrSource,       "grains.source",   "Source",        "grains",  "Source",         Choice, 0,    1,    0,    "",   "Clean|Dirty", None) \
    P(BloomGrains,    "bloom.grains",    "Bloom reach",   "grains",  "Bloom reach",    Slider, 0,    1,    1,    "%",  "None|Full", None) \
    P(DlOn,           "delay.on",        "On",            "delay",   "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(DlTime,         "delay.time",      "Time",          "delay",   "Delay",          Flip,   40,   1500, 420,  "ms", "",   Log) \
    P(DlFeedback,     "delay.feedback",  "Repeats",       "delay",   "Delay",          Flip,   0,    0.95f,0.35f,"%",  "",   None) \
    P(DlTone,         "delay.tone",      "Darkness",      "delay",   "Delay",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(DlMod,          "delay.mod",       "Wobble",        "delay",   "Delay",          Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(DlMix,          "delay.mix",       "Mix",           "delay",   "Delay",          Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(RvOn,           "reverb.on",       "On",            "reverb",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(RvSize,         "reverb.size",     "Size",          "reverb",  "Space",          Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(RvDecay,        "reverb.decay",    "Decay",         "reverb",  "Space",          Flip,   0.3f, 20,   3,    "s",  "",   Log) \
    P(RvDamp,         "reverb.damp",     "Darkness",      "reverb",  "Space",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(RvPredelay,     "reverb.predelay", "Pre-delay",     "reverb",  "Space",          Flip,   0,    150,  20,   "ms", "",   None) \
    P(RvMix,          "reverb.mix",      "Mix",           "reverb",  "Space",          Flip,   0,    1,    0.25f,"%",  "",   None) \
    P(RvShimmer,      "reverb.shimmer",  "Shimmer",       "reverb",  "Shimmer",        Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(RvInterval,     "reverb.interval", "Interval",      "reverb",  "Shimmer",        Choice, 0,    1,    0,    "",   "Octave|Fifth", None) \
    /* ---- Control ---- */ \
    P(BloomDrone,     "bloom.drone",     "Drone reach",   "bloom",   "Reach",          Flip,   0,    1,    1,    "%",  "",   None) \
    P(BloomWash,      "bloom.wash",      "Wash reach",    "bloom",   "Reach",          Flip,   0,    1,    0.9f, "%",  "",   None) \
    P(BloomTails,     "bloom.tails",     "Heel back",     "bloom",   "Behaviour",      Choice, 0,    1,    1,    "",   "Cut tails|Let tails ring", None) \
    P(BloomAutoCap,   "bloom.autocap",   "Capture drone on swell", "bloom", "Behaviour", Toggle, 0,  1,    1,    "",   "",   None) \
    P(OutLevel,       "output.level",    "Level",         "output",  "Output",         Flip,   -24,  12,   0,    "dB", "",   None) \
    P(OutLimit,       "output.limit",    "Limiter",       "output",  "Output",         Toggle, 0,    1,    1,    "",   "",   None) \
    P(OutVoicing,     "output.voicing",  "Into",          "output",  "Output",         Choice, 0,    1,    0,    "",   "Amp|PA", None) \
    P(OutQuality,     "output.quality",  "Effects quality","output", "Setup",          Choice, 0,    2,    0,    "",   "Auto|Full|Light", Global) \
    P(OutInput,       "output.input",    "Bass input",    "output",  "Setup",          Choice, 0,    2,    0,    "",   "Auto|Input 1|Input 2", Global) \
    P(TunerMute,      "tuner.mute",      "Mute output while tuning","tuner","Tuner",   Toggle, 0,    1,    1,    "",   "",   Global | Hidden) \
    P(TunerRef,       "tuner.ref",       "Reference A",   "tuner",   "Tuner",          Flip,   430,  450,  440,  "Hz", "",   Global | Hidden) \
    P(FsACycle,       "fs.aCycle",       "Footswitch A",  "footswitches","Footswitch A", Choice, 0, 1,    0,    "",   "Edge and Drive|Off, Edge, Drive", Global)
// clang-format on

enum class P : int
{
#define KS_ENUM(e, ...) e,
    KS_PARAMS(KS_ENUM)
#undef KS_ENUM
        Count
};

constexpr int kNumParams = (int) P::Count;

inline const ParamInfo* paramTable()
{
#define KS_INFO(e, id, label, mod, sect, kind, mn, mx, df, unit, ch, fl) \
    ParamInfo { id, label, mod, sect, Kind::kind, (float) (mn), (float) (mx), (float) (df), unit, ch, (uint8_t) (fl) },
    static const ParamInfo table[] = { KS_PARAMS(KS_INFO) };
#undef KS_INFO
    return table;
}

inline const ParamInfo& info (P p) { return paramTable()[(int) p]; }

int findParam (const char* id);   // -1 if unknown

// Module list in signal order, with titles and the band they sit in.
struct ModuleInfo
{
    const char* id;
    const char* title;
    const char* group;
    const char* summary;
};

const ModuleInfo* moduleTable (int& count);
} // namespace ks
