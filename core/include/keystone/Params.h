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
    Kuddle = 8,   // belongs to Kuddle, the home rig: saved in Kuddle's presets, not Keystone's songs
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
    P(SplitSubCut,    "split.subCut",    "Sub cut",       "split",   "Split",          Flip,   30,   150,  50,   "Hz", "",   Log) \
    P(SplitLowLevel,  "split.lowLevel",  "Bottom dry",    "split",   "Levels",         Flip,   -12,  6,    0,    "dB", "",   None) \
    P(SplitWetLevel,  "split.wetLevel",  "Bottom wet",    "split",   "Levels",         Flip,   0,    1,    1,    "%",  "",   None) \
    P(SplitHighLevel, "split.highLevel", "Full sound",    "split",   "Levels",         Flip,   -12,  6,    0,    "dB", "",   None) \
    P(CompOn,         "lowcomp.on",      "On",            "lowcomp", "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(CompAmount,     "lowcomp.amount",  "Squeeze",       "lowcomp", "Compression",    Flip,   0,    1,    0.35f,"%",  "",   None) \
    P(CompRelease,    "lowcomp.release", "Release",       "lowcomp", "Compression",    Flip,   40,   400,  150,  "ms", "",   Log) \
    P(CompSlap,       "lowcomp.slap",    "Slap mode",     "lowcomp", "Slap",           Toggle, 0,    1,    0,    "",   "",   None) \
    P(CompSlapAmount, "lowcomp.slapAmt", "Slap squeeze",  "lowcomp", "Slap",           Flip,   0,    1,    0.7f, "%",  "",   None) \
    P(DroneOn,        "drone.on",        "On",            "drone",   "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(DroneLevel,     "drone.level",     "Level",         "drone",   "Drone",          Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(DroneSustain,   "drone.sustain",   "Sustain",       "drone",   "Drone",          Flip,   0,    1,    1,    "%",  "",   None) \
    P(DroneSwell,     "drone.swell",     "Swell",         "drone",   "Drone",          Flip,   0.05f,4,    1.2f, "s",  "",   Log) \
    P(DroneRelease,   "drone.release",   "Trail off",     "drone",   "Drone",          Flip,   0.5f, 10,   2,    "s",  "",   Log) \
    P(DroneDuck,      "drone.duck",      "Duck",          "drone",   "Movement",       Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(DroneTone,      "drone.tone",      "Tone",          "drone",   "Movement",       Flip,   100,  2000, 450,  "Hz", "",   Log) \
    P(DroneSend,      "drone.send",      "To reverb",     "drone",   "Movement",       Flip,   0,    1,    0,    "%",  "",   None) \
    P(DroneOctave,    "drone.octave",    "Octave up",     "drone",   "Voice",          Toggle, 0,    1,    0,    "",   "",   None) \
    P(DroneDim,       "drone.dim",       "Dimension",     "drone",   "Voice",          Choice, 0,    4,    3,    "",   "Off|1|2|3|4", None) \
    P(DroneTuning,    "drone.tuning",    "Capture window","drone",   "Voice",          Choice, 0,    1,    0,    "",   "Standard E|Drop D", None) \
    /* ---- High band ---- */ \
    P(DriveOn,        "drive.on",        "On",            "drive",   "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(DriveVoice,     "drive.voice",     "Voice",         "drive",   "Editing voice",  Choice, 0,    1,    0,    "",   "Edge|Drive", None) \
    P(DriveSource,    "drive.source",    "Drive source",  "drive",   "Source",         Choice, 0,    1,    0,    "",   "Capture|Model", None) \
    P(EdgeSource,     "drive.edge.source","Edge source",  "drive",   "Source",         Choice, 0,    1,    1,    "",   "Capture|Model", None) \
    P(EdgeGain,       "drive.edge.gain", "Gain",          "drive",   "Voice settings", Flip,   0,    10,   3,    "",   "",   None) \
    P(EdgeTone,       "drive.edge.tone", "Tone",          "drive",   "Voice settings", Flip,   0,    10,   6.5f, "",   "",   None) \
    P(EdgeLevel,      "drive.edge.level","Level",         "drive",   "Voice settings", Flip,   -12,  12,   0,    "dB", "",   None) \
    P(EdgeLowCut,     "drive.edge.lowcut","Low cut",      "drive",   "Voice settings", Flip,   20,   300,  70,  "Hz", "",   Log) \
    P(EdgePresence,   "drive.edge.presence","Presence",   "drive",   "Voice settings", Flip,   -6,   6,    2,    "dB", "",   None) \
    P(EdgeGate,       "drive.edge.gate", "Gate",          "drive",   "Voice settings", Flip,   -90,  -30,  -70,  "dB", "",   None) \
    P(EdgeMode,       "drive.edge.mode", "Clipping",      "drive",   "Clipping (model only)", Choice, 0, 2, 1,  "",   "LED|Clean|Silicon", None) \
    P(DriveGain,      "drive.drive.gain","Gain",          "drive",   "Voice settings", Flip,   0,    10,   6.5f, "",   "",   None) \
    P(DriveTone,      "drive.drive.tone","Tone",          "drive",   "Voice settings", Flip,   0,    10,   6.5f, "",   "",   None) \
    P(DriveLevel,     "drive.drive.level","Level",        "drive",   "Voice settings", Flip,   -12,  12,   1.5f,   "dB", "",   None) \
    P(DriveLowCut,    "drive.drive.lowcut","Low cut",     "drive",   "Voice settings", Flip,   20,   300,  110, "Hz", "",   Log) \
    P(DrivePresence,  "drive.drive.presence","Presence",  "drive",   "Voice settings", Flip,   -6,   6,    4,    "dB", "",   None) \
    P(DriveGate,      "drive.drive.gate","Gate",          "drive",   "Voice settings", Flip,   -90,  -30,  -60,  "dB", "",   None) \
    P(DriveMode,      "drive.drive.mode","Clipping",      "drive",   "Clipping (model only)", Choice, 0, 2, 0,  "",   "LED|Clean|Silicon", None) \
    P(DriveFade,      "drive.fade",      "Switch fade",   "drive",   "Behaviour",      Flip,   5,    50,   15,   "ms", "",   None) \
    P(DriveMatch,     "drive.match",     "Level match",   "drive",   "Behaviour",      Choice, 0,    1,    1,    "",   "Off|Auto", None) \
    P(DriveMorph,     "drive.morph",     "Exp morph",     "drive",   "Behaviour",      Choice, 0,    1,    0,    "",   "Off|On", None) \
    P(DriveLowInto,   "drive.lowInto",   "Low end into drive","drive","Behaviour",     Slider, 0,    1,    0,    "%",  "None|Full", Hidden) \
    P(BitrotOn,       "bitrot.on",       "On",            "bitrot",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(BitrotSquare,   "bitrot.square",   "Square",        "bitrot",  "Voices",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BitrotSub,      "bitrot.sub",      "Sub",           "bitrot",  "Voices",         Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(BitrotOsc,      "bitrot.osc",      "Oscillator",    "bitrot",  "Voices",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BitrotClean,    "bitrot.clean",    "Clean bass",    "bitrot",  "Voices",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BitrotVol,      "bitrot.vol",      "Level",         "bitrot",  "Voices",         Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(BitrotSubInt,   "bitrot.subInt",   "Divide by",     "bitrot",  "Sub",            Choice, 0,    7,    0,    "",   "2|3|4|5|6|7|8|9", None) \
    P(BitrotSubRoot,  "bitrot.subRoot",  "Divides",       "bitrot",  "Sub",            Choice, 0,    1,    0,    "",   "Square|Oscillator", None) \
    P(BitrotSubMix,   "bitrot.subMix",   "Sub blend",     "bitrot",  "Sub",            Choice, 0,    1,    0,    "",   "Sum and difference|Plain divide", None) \
    P(BitrotFloor,    "bitrot.floor",    "Floor",         "bitrot",  "Sub",            Toggle, 0,    1,    1,    "",   "",   None) \
    P(BitrotFloorHz,  "bitrot.floorHz",  "Floor at",      "bitrot",  "Sub",            Choice, 0,    2,    1,    "",   "25 Hz|30 Hz|40 Hz", None) \
    P(BitrotOscInt,   "bitrot.oscInt",   "Harmonic",      "bitrot",  "Oscillator",     Choice, 0,    7,    1,    "",   "x1|x2|x3|x4|x5|x6|x7|x8", None) \
    P(BitrotOscRoot,  "bitrot.oscRoot",  "Root",          "bitrot",  "Oscillator",     Choice, 0,    2,    0,    "",   "Unison|Oct down|Two down", None) \
    P(BitrotBend,     "bitrot.bend",     "Bend",          "bitrot",  "Oscillator",     Flip,   0,    1,    0,    "%",  "",   None) \
    P(BitrotFm,       "bitrot.fm",       "Modulator",     "bitrot",  "Movement",       Choice, 0,    1,    0,    "",   "Glide|Vibrato", None) \
    P(BitrotFmRate,   "bitrot.fmRate",   "Rate",          "bitrot",  "Movement",       Flip,   0,    1,    0.75f, "%", "",   None) \
    P(BitrotFmDepth,  "bitrot.fmDepth",  "Depth",         "bitrot",  "Movement",       Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(BitrotCorrupt,  "bitrot.corrupt",  "Corrupt",       "bitrot",  "Tracking",       Flip,   0,    1,    0.25f, "%", "",   None) \
    P(BitrotGate,     "bitrot.gate",     "Sensitivity",   "bitrot",  "Tracking",       Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BitrotPlace,    "bitrot.place",    "Goes",          "bitrot",  "Order",          Choice, 0,    2,    1,    "",   "Before drive|After drive|After preamp", None) \
    P(BitrotPedal,    "bitrot.pedal",    "Pedal does",    "bitrot",  "Pedal (Grit)",   Choice, 0,    5,    0,    "",   "Blend|Rev|Sweep|Rev and sweep|Corrupt|Morph", None) \
    P(BitrotPeak,     "bitrot.peak",     "Peak",          "bitrot",  "Pedal (Grit)",   Flip,   0,    1,    0.5f, "%",  "",   None) \
    /* Gloss (0.6.1): the clean synth voice on hold B in Grit, Bitrot's well-behaved sibling */ \
    P(GlossOn,        "gloss.on",        "On",            "gloss",   "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(GlossVoice,     "gloss.voice",     "Voice",         "gloss",   "Layers",         Choice, 0,    2,    0,    "",   "Warm|Hollow|Soft", None) \
    P(GlossPlays,     "gloss.plays",     "Plays",         "gloss",   "Layers",         Choice, 0,    5,    1,    "",   "Octave up|Octave and fifth|Open sus2|Unison|Fifth up|Two octaves", None) \
    P(GlossSpread,    "gloss.spread",    "Spread",        "gloss",   "Layers",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(GlossShine,     "gloss.shine",     "Shine",         "gloss",   "Layers",         Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(GlossAir,       "gloss.air",       "Air",           "gloss",   "Layers",         Flip,   0,    1,    0.15f,"%",  "",   None) \
    P(GlossWeight,    "gloss.weight",    "Weight",        "gloss",   "Layers",         Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(GlossSettle,    "gloss.settle",    "Settle",        "gloss",   "Following",      Flip,   40,   600,  180,  "ms", "",   Log) \
    P(GlossMove,      "gloss.move",      "Moves by",      "gloss",   "Following",      Choice, 0,    1,    0,    "",   "Fading across|Gliding", None) \
    P(GlossChange,    "gloss.change",    "Change time",   "gloss",   "Following",      Flip,   50,   3000, 450,  "ms", "",   Log) \
    P(GlossRing,      "gloss.ring",      "When you stop", "gloss",   "Following",      Choice, 0,    1,    0,    "",   "Fades away|Rings on", None) \
    P(GlossSwell,     "gloss.swell",     "Swell",         "gloss",   "Envelope",       Flip,   20,   4000, 700,  "ms", "",   Log) \
    P(GlossFade,      "gloss.fade",      "Fade",          "gloss",   "Envelope",       Flip,   100,  8000, 3000, "ms", "",   Log) \
    P(GlossBloom,     "gloss.bloom",     "Bloom",         "gloss",   "Envelope",       Flip,   100,  6000, 2000, "ms", "",   Log) \
    P(GlossSens,      "gloss.sens",      "Sensitivity",   "gloss",   "Envelope",       Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(GlossBright,    "gloss.bright",    "Brightness",    "gloss",   "Tone",           Flip,   200,  8000, 1800, "Hz", "",   Log) \
    P(GlossBreathe,   "gloss.breathe",   "Breathe",       "gloss",   "Tone",           Flip,   0,    1,    0.35f,"%",  "",   None) \
    P(GlossWander,    "gloss.wander",    "Wander",        "gloss",   "Tone",           Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(GlossTouch,     "gloss.touch",     "Touch",         "gloss",   "Tone",           Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(GlossWarmth,    "gloss.warmth",    "Warmth",        "gloss",   "Tone",           Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(GlossChorus,    "gloss.chorus",    "Chorus",        "gloss",   "Space",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(GlossSend,      "gloss.send",      "To reverb",     "gloss",   "Space",          Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(GlossClean,     "gloss.clean",     "Clean bass",    "gloss",   "Mix",            Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(GlossLevel,     "gloss.level",     "Level",         "gloss",   "Mix",            Flip,   0,    1,    0.7f, "%",  "",   None) \
    P(GlossPlace,     "gloss.place",     "Goes",          "gloss",   "Mix",            Choice, 0,    2,    2,    "",   "Into drive|After drive|After preamp", None) \
    P(PreOn,          "preamp.on",       "On",            "preamp",  "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(PreGain,        "preamp.gain",     "Gain",          "preamp",  "Valve",          Flip,   0,    1,    0.45f,"%",  "",   None) \
    P(PreSag,         "preamp.sag",      "Sag",           "preamp",  "Valve",          Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(PreBass,        "preamp.bass",     "Bass",          "preamp",  "Tone stack",     Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(PreMiddle,      "preamp.middle",   "Middle",        "preamp",  "Tone stack",     Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(PreTreble,      "preamp.treble",   "Treble",        "preamp",  "Tone stack",     Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(PreUltraLo,     "preamp.ultraLo",  "Ultra Lo",      "preamp",  "Tone stack",     Toggle, 0,    1,    0,    "",   "",   None) \
    P(PreUltraHi,     "preamp.ultraHi",  "Ultra Hi",      "preamp",  "Tone stack",     Toggle, 0,    1,    0,    "",   "",   None) \
    P(PreBlend,       "preamp.blend",    "Blend",         "preamp",  "Modern",         Slider, 0,    1,    0.0f, "%",  "Valve|Cut", None) \
    P(PreMidFreq,     "preamp.midFreq",  "Mid focus",     "preamp",  "Modern",         Flip,   700,  1500, 1100, "Hz", "",   Log) \
    P(PreMid,         "preamp.mid",      "Mid push",      "preamp",  "Modern",         Flip,   0,    1,    0.9f, "%",  "",   None) \
    P(PreDrive,       "preamp.drive",    "Grit",          "preamp",  "Modern",         Flip,   0,    1,    0.6f,"%",  "",   None) \
    P(PreTape,        "preamp.tape",     "Tape",          "preamp",  "Tape",           Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(PreWow,         "preamp.wow",      "Wow",           "preamp",  "Tape",           Flip,   0,    1,    0.15f,"%",  "",   None) \
    P(PreDetail,      "preamp.detail",   "Detail",        "preamp",  "Output",         Flip,   0,    6,    2,    "dB", "",   None) \
    P(PreLevel,       "preamp.level",    "Level",         "preamp",  "Output",         Flip,   -12,  12,   0,    "dB", "",   None) \
    P(PreFlip,        "preamp.flip",     "Flipped",       "preamp",  "Flip (tap C in Grit)", Toggle, 0, 1,    0,    "",   "",   None) \
    P(PreBlendB,      "preamp.blendB",   "Flip to",       "preamp",  "Flip (tap C in Grit)", Slider, 0, 1,    1.0f, "%",  "Valve|Cut", None) \
    P(FiltOn,         "filter.on",       "On",            "filter",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(FiltMode,       "filter.mode",     "Mode",          "filter",  "Mode",           Choice, 0,    1,    0,    "",   "Envelope|Manual", None) \
    P(FiltType,       "filter.type",     "Shape",         "filter",  "Mode",           Choice, 0,    1,    1,    "",   "Low-pass|Band-pass", None) \
    P(FiltDir,        "filter.dir",      "Direction",     "filter",  "Mode",           Choice, 0,    1,    0,    "",   "Up|Down", None) \
    P(FiltRange,      "filter.range",    "Works on",      "filter",  "Mode",           Choice, 0,    1,    0,    "",   "Whole bass|High band", None) \
    P(FiltSens,       "filter.sens",     "Sensitivity",   "filter",  "Sweep",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(FiltLow,        "filter.low",      "From",          "filter",  "Sweep",          Flip,   80,   1000, 200,  "Hz", "",   Log) \
    P(FiltHigh,       "filter.high",     "To",            "filter",  "Sweep",          Flip,   800,  5000, 2200, "Hz", "",   Log) \
    P(FiltQ,          "filter.q",        "Resonance",     "filter",  "Sweep",          Flip,   0,    1,    0.7f, "%",  "",   None) \
    P(FiltSpeed,      "filter.speed",    "Response",      "filter",  "Sweep",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(FiltMix,        "filter.mix",      "Mix",           "filter",  "Sweep",          Flip,   0,    1,    1,    "%",  "",   None) \
    P(FiltPos,        "filter.pos",      "Manual position","filter", "Manual",         Slider, 0,    1,    0.5f, "%",  "Low|High", None) \
    P(WahOn,          "wah.on",          "On",            "wah",     "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(WahLow,         "wah.low",         "Heel",          "wah",     "Sweep",          Flip,   150,  800,  320,  "Hz", "",   Log) \
    P(WahHigh,        "wah.high",        "Toe",           "wah",     "Sweep",          Flip,   1000, 4000, 2200, "Hz", "",   Log) \
    P(WahQ,           "wah.q",           "Resonance",     "wah",     "Sweep",          Flip,   0,    1,    0.75f,"%",  "",   None) \
    P(WahMix,         "wah.mix",         "Mix",           "wah",     "Sweep",          Flip,   0,    1,    1,    "%",  "",   None) \
    P(DimOn,          "dim.on",          "On",            "dim",     "",               Toggle, 0,    1,    1,    "",   "",   None) \
    P(DimMode,        "dim.mode",        "Mode",          "dim",     "Mode",           Choice, 0,    3,    1,    "",   "1|2|3|4", None) \
    P(DimMix,         "dim.mix",         "Mix",           "dim",     "Mix",            Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(DimPath,        "dim.path",        "Works on",      "dim",     "Path",           Choice, 0,    1,    0,    "",   "Bottom end|Full sound", None) \
    P(GrOn,           "grains.on",       "On",            "grains",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(GrEffect,       "grains.effect",   "Effect",        "grains",  "Effect",         Choice, 0,    10,   3,    "",   "Mosaic|Seq|Glide|Haze|Tunnel|Strum|Blocks|Interrupt|Arp|Pattern|Warp", None) \
    P(GrVariant,      "grains.variant",  "Variation",     "grains",  "Effect",         Choice, 0,    3,    1,    "",   "A|B|C|D", None) \
    P(GrActivity,     "grains.activity", "Activity",      "grains",  "Movement",       Flip,   0,    1,    0.55f,"%",  "",   None) \
    P(GrRepeats,      "grains.repeats",  "Repeats",       "grains",  "Movement",       Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(GrContour,      "grains.contour",  "Shape",         "grains",  "Movement",       Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(GrRev,          "grains.rev",      "Reverse",       "grains",  "Movement",       Toggle, 0,    1,    0,    "",   "",   None) \
    P(GrSubdiv,       "grains.subdiv",   "Time",          "grains",  "Time",           Choice, 0,    8,    6,    "",   "1 bar|1/2|1/4 dot|1/4|1/4 trip|1/8 dot|1/8|1/8 trip|1/16", None) \
    P(GrHold,         "grains.hold",     "Hold",          "grains",  "Time",           Toggle, 0,    1,    0,    "",   "",   None) \
    P(GrFilter,       "grains.filter",   "Filter",        "grains",  "Filter and mod", Flip,   0,    1,    1,    "%",  "",   None) \
    P(GrRes,          "grains.res",      "Resonance",     "grains",  "Filter and mod", Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(GrModDepth,     "grains.modDepth", "Mod depth",     "grains",  "Filter and mod", Flip,   0,    1,    0.12f,"%",  "",   None) \
    P(GrModRate,      "grains.modRate",  "Mod rate",      "grains",  "Filter and mod", Flip,   0,    1,    0.45f, "%",  "",   None) \
    P(GrSpace,        "grains.space",    "Space",         "grains",  "Space",          Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(GrSpaceType,    "grains.spaceType","Space type",    "grains",  "Space",          Choice, 0,    3,    3,    "",   "Bright room|Dark medium|Large hall|Ambient", None) \
    P(GrPitch,        "grains.pitch",    "Pitch",         "grains",  "Pitch",          Choice, 0,    4,    0,    "",   "Off|Octave|Oct + 5th|2 octaves|Scatter", None) \
    P(GrScatter,      "grains.scatter",  "Movement",      "grains",  "Pitch",          Slider, 0,    1,    0.4f, "%",  "Steady|Scattered", None) \
    P(GrVolume,       "grains.volume",   "Volume",        "grains",  "Output",         Flip,   -12,  12,   0,    "dB", "",   None) \
    P(GrMix,          "grains.mix",      "Mix",           "grains",  "Output",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(BloomGrains,    "bloom.grains",    "Bloom reach",   "grains",  "Bloom reach",    Slider, 0,    1,    1,    "%",  "None|Full", None) \
    P(DlOn,           "delay.on",        "On",            "delay",   "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(DlType,         "delay.type",      "Type",          "delay",   "Type",           Choice, 0,    11,   0,    "",   "Digital|Bucket|Tape|Dual|Pattern|Reverse|Ice|Swell|Trem|Filter|Lo-fi|Galaxy", None) \
    P(Tempo,          "tempo.bpm",       "Tempo",         "tempo",   "This song",      Flip,   40,   240,  120,  "BPM","",   None) \
    P(SongKey,        "tempo.key",       "Key",           "tempo",   "This song",      Choice, 0,    12,   0,    "",   "None|C|C#|D|D#|E|F|F#|G|G#|A|A#|B", None) \
    P(DlSync,         "delay.sync",      "Time",          "delay",   "Time",           Choice, 0,    8,    0,    "",   "Free|1/4|1/8.|1/8|1/2|1/4.|1/4T|1/8T|1/16", None) \
    P(DlTime,         "delay.time",      "Time",          "delay",   "Time",           Flip,   40,   1500, 420,  "ms", "",   Log) \
    P(DlFeedback,     "delay.feedback",  "Repeats",       "delay",   "Repeats",        Flip,   0,    0.95f,0.35f,"%",  "",   None) \
    P(DlMix,          "delay.mix",       "Mix",           "delay",   "Repeats",        Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(DlTone,         "delay.tone",      "Filter",        "delay",   "Repeats",        Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(DlGrit,         "delay.grit",      "Grit",          "delay",   "Repeats",        Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(DlSmear,        "delay.smear",     "Smear",         "delay",   "Repeats",        Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(DlLowCut,       "delay.lowcut",    "Low cut",       "delay",   "Repeats",        Flip,   20,   500,  140,  "Hz", "",   Log) \
    P(DlModSpeed,     "delay.modSpeed",  "Mod speed",     "delay",   "Repeats",        Flip,   0.1f, 8,    0.6f, "Hz", "",   Log) \
    P(DlMod,          "delay.mod",       "Mod depth",     "delay",   "Repeats",        Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(DlDuck,         "delay.duck",      "Duck",          "delay",   "Repeats",        Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(DlTapeMachine,  "delay.tapeMachine","Machine",      "delay",   "Tape machine",   Choice, 0,    2,    0,    "",   "Fixed|Multi|Single", None) \
    P(DlTapeMode,     "delay.tapeMode",  "Heads",         "delay",   "Tape machine",   Choice, 0,    2,    0,    "",   "A|B|C", None) \
    P(DlTapeAge,      "delay.tapeAge",   "Tape age",      "delay",   "Tape machine",   Flip,   0,    1,    0.4f, "%",  "",   None) \
    P(DlWow,          "delay.wow",       "Wow and flutter","delay",  "Tape machine",   Flip,   0,    1,    0.35f,"%",  "",   None) \
    P(DlCrinkle,      "delay.crinkle",   "Crinkle",       "delay",   "Tape machine",   Flip,   0,    1,    0.1f, "%",  "",   None) \
    P(DlBias,         "delay.bias",      "Bias",          "delay",   "Tape machine",   Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(DlSpring,       "delay.spring",    "Spring",        "delay",   "Tape machine",   Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(DlBoost,        "delay.boost",     "Boost",         "delay",   "Tape machine",   Flip,   -6,   6,    0,    "dB", "",   None) \
    P(DlDualRatio,    "delay.dualRatio", "Second head",   "delay",   "Dual",           Choice, 0,    3,    1,    "",   "Half|Two thirds|Three quarters|Dotted", None) \
    P(DlDualSeries,   "delay.dualSeries","Routing",       "delay",   "Dual",           Choice, 0,    1,    0,    "",   "Side by side|One into the other", None) \
    P(DlPattern,      "delay.pattern",   "Pattern",       "delay",   "Pattern",        Choice, 0,    3,    0,    "",   "Gallop|Triplet|Shuffle|Cascade", None) \
    P(DlIceInterval,  "delay.iceInterval","Interval",     "delay",   "Ice",            Choice, 0,    2,    0,    "",   "Octave|Fifth|Oct + 5th", None) \
    P(DlIceBlend,     "delay.iceBlend",  "Blend",         "delay",   "Ice",            Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(DlSwellRise,    "delay.swellRise", "Rise",          "delay",   "Swell",          Flip,   50,   2000, 400,  "ms", "",   Log) \
    P(DlTremSpeed,    "delay.tremSpeed", "Speed",         "delay",   "Trem",           Flip,   0.5f, 12,   4,    "Hz", "",   Log) \
    P(DlTremDepth,    "delay.tremDepth", "Depth",         "delay",   "Trem",           Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(DlTremShape,    "delay.tremShape", "Wave",          "delay",   "Trem",           Choice, 0,    1,    0,    "",   "Smooth|Choppy", None) \
    P(DlFiltSpeed,    "delay.filtSpeed", "Sweep speed",   "delay",   "Filter",         Flip,   0.05f,6,    0.4f, "Hz", "",   Log) \
    P(DlFiltRes,      "delay.filtRes",   "Resonance",     "delay",   "Filter",         Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(DlLofiBits,     "delay.lofiBits",  "Bits",          "delay",   "Lo-fi",          Flip,   4,    16,   8,    "",   "",   None) \
    P(DlLofiRate,     "delay.lofiRate",  "Sample rate",   "delay",   "Lo-fi",          Flip,   2000, 24000,8000, "Hz", "",   Log) \
    P(DlHold,         "delay.hold",      "Hold the repeats","delay", "Hold",           Toggle, 0,    1,    0,    "",   "",   None) \
    P(RvOn,           "reverb.on",       "On",            "reverb",  "",               Toggle, 0,    1,    0,    "",   "",   None) \
    P(RvType,         "reverb.type",     "Type",          "reverb",  "Type",           Choice, 0,    11,   1,    "",   "Room|Hall|Plate|Spring|Chamber|Cloud|Bloom|Chorale|Shimmer|Magneto|Nonlinear|Impulse", None) \
    P(RvDecay,        "reverb.decay",    "Decay",         "reverb",  "Space",          Flip,   0.3f, 20,   3,    "s",  "",   Log) \
    P(RvPredelay,     "reverb.predelay", "Pre-delay",     "reverb",  "Space",          Flip,   0,    250,  20,   "ms", "",   None) \
    P(RvMix,          "reverb.mix",      "Mix",           "reverb",  "Space",          Flip,   0,    1,    0.25f,"%",  "",   None) \
    P(RvDamp,         "reverb.damp",     "Darkness",      "reverb",  "Space",          Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(RvLowCut,       "reverb.lowcut",   "Low cut",       "reverb",  "Space",          Flip,   20,   500,  180,  "Hz", "",   Log) \
    P(RvDuck,         "reverb.duck",     "Duck",          "reverb",  "Space",          Flip,   0,    1,    0.2f, "%",  "",   None) \
    P(RvMod,          "reverb.mod",      "Mod",           "reverb",  "Space",          Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(RvSwell,        "reverb.swell",    "Swell",         "reverb",  "Space",          Flip,   0,    2,    0,    "s",  "",   None) \
    P(RvGrit,         "reverb.grit",     "Grit",          "reverb",  "Space",          Flip,   0,    1,    0,    "%",  "",   None) \
    P(RvDiffusion,    "reverb.diffusion","Diffusion",     "reverb",  "Character",      Flip,   0,    1,    0.7f, "%",  "",   None) \
    P(RvSize,         "reverb.size",     "Size",          "reverb",  "Character",      Flip,   0,    1,    0.6f, "%",  "",   None) \
    P(RvShimmer,      "reverb.shimmer",  "Shimmer",       "reverb",  "Character",      Flip,   0,    1,    0.0f, "%",  "",   None) \
    P(RvInterval,     "reverb.interval", "Interval",      "reverb",  "Character",      Choice, 0,    2,    0,    "",   "Octave|Fifth|Oct + 5th", None) \
    P(RvSprings,      "reverb.springs",  "Springs",       "reverb",  "Character",      Choice, 0,    2,    1,    "",   "One|Two|Three", None) \
    P(RvTension,      "reverb.tension",  "Drip",          "reverb",  "Character",      Flip,   0,    1,    0.5f, "%",  "",   None) \
    P(RvVowel,        "reverb.vowel",    "Vowel",         "reverb",  "Character",      Choice, 0,    4,    0,    "",   "Ah|Eh|Ee|Oh|Oo", None) \
    P(RvVoices,       "reverb.voices",   "Voices",        "reverb",  "Character",      Flip,   0,    1,    0.7f, "%",  "",   None) \
    P(RvEchoTime,     "reverb.echoTime", "Echo time",     "reverb",  "Character",      Flip,   60,   800,  300,  "ms", "",   Log) \
    P(RvEchoes,       "reverb.echoes",   "Echoes",        "reverb",  "Character",      Flip,   0,    0.9f, 0.4f, "%",  "",   None) \
    P(RvShape,        "reverb.shape",    "Shape",         "reverb",  "Character",      Choice, 0,    3,    0,    "",   "Gate|Reverse|Swoosh|Ramp", None) \
    P(RvLength,       "reverb.length",   "Length",        "reverb",  "Character",      Flip,   50,   800,  300,  "ms", "",   Log) \
    P(RvFreeze,       "reverb.freeze",   "Freeze the tail","reverb", "Freeze",         Toggle, 0,    1,    0,    "",   "",   None) \
    /* ---- Control ---- */ \
    P(BloomWash,      "bloom.wash",      "Wash reach",    "bloom",   "Reach",          Flip,   0,    1,    0.9f, "%",  "",   None) \
    P(BloomTails,     "bloom.tails",     "Heel back",     "bloom",   "Behaviour",      Choice, 0,    1,    1,    "",   "Cut tails|Let tails ring", None) \
    P(PedalBitrotAt,  "pedal.bitrotAt",  "Bitrot joins at","pedal",  "Dirt blend (Grit)", Flip, 0,    0.9f, 0.25f,"%",  "",   None) \
    P(PedalVolFloor,  "pedal.volFloor",  "Heel volume",   "pedal",   "Volume (Groove, wah off)", Flip, -60, 0, -60, "dB", "", None) \
    P(BankStart,      "bank.start",      "Opens in",      "footswitches","This song",  Choice, 0,    2,    0,    "",   "Grit|Groove|Glow", None) \
    P(OutIron,        "output.iron",     "Iron",          "output",  "Output",         Flip,   0,    1,    0.3f, "%",  "",   None) \
    P(OutLevel,       "output.level",    "Level",         "output",  "Output",         Flip,   -24,  12,   0,    "dB", "",   None) \
    P(OutLimit,       "output.limit",    "Limiter",       "output",  "Output",         Toggle, 0,    1,    1,    "",   "",   None) \
    P(OutVoicing,     "output.voicing",  "Into",          "output",  "Output",         Choice, 0,    1,    0,    "",   "Amp|PA", None) \
    P(OutQuality,     "output.quality",  "Effects quality","output", "Setup",          Choice, 0,    2,    0,    "",   "Auto|Full|Light", Global) \
    P(OutBuffer,      "output.buffer",   "Audio buffer",  "output",  "Setup",          Choice, 0,    4,    0,    "",   "Auto|Smallest|4 ms|6 ms|10 ms", Global) \
    P(OutBufferStep,  "output.bufferAuto","Buffer learnt", "output", "Setup",          Choice, 0,    3,    0,    "",   "3 ms|4 ms|6 ms|10 ms", Global | Hidden) \
    P(OutMode,        "output.mode",     "Audio mode",    "output",  "Setup",          Choice, 0,    2,    0,    "",   "Exclusive|Shared|ASIO", Global) \
    P(OutInput,       "output.input",    "Bass input",    "output",  "Setup",          Choice, 0,    2,    0,    "",   "Auto|Input 1|Input 2", Global) \
    P(TunerMute,      "tuner.mute",      "Mute output while tuning","tuner","Tuner",   Toggle, 0,    1,    1,    "",   "",   Global | Hidden) \
    P(TunerRef,       "tuner.ref",       "Reference A",   "tuner",   "Tuner",          Flip,   430,  450,  440,  "Hz", "",   Global | Hidden) \
    /* ---- Kuddle, the home rig ---- */ \
    P(KMelt,          "kuddle.melt",     "Melt",          "k.melt",  "Melt",           Slider, 0,    1,    0,    "%",  "Open|Melted", Kuddle | Hidden) \
    P(KMeltWah,       "kuddle.meltWah",  "Pedal works the wah in manual", "k.melt", "Melt", Toggle, 0, 1, 1,   "",   "",   Kuddle) \
    P(KReachMute,     "kuddle.reachMute","Mute",          "k.melt",  "Reach",          Flip,   0,    1,    0.6f, "%",  "",   Kuddle) \
    P(KReachTape,     "kuddle.reachTape","Tape push",     "k.melt",  "Reach",          Flip,   0,    1,    0.5f, "%",  "",   Kuddle) \
    P(KReachTreble,   "kuddle.reachTreble","Treble roll-off","k.melt","Reach",         Flip,   0,    1,    0.7f, "%",  "",   Kuddle) \
    P(KReachRoom,     "kuddle.reachRoom","Room",          "k.melt",  "Reach",          Flip,   0,    1,    0,    "%",  "",   Kuddle) \
    P(KInGain,        "kuddle.inGain",   "Gain",          "k.input", "Input",          Flip,   -12,  12,   0,    "dB", "",   Kuddle) \
    P(KGate,          "kuddle.gate",     "Gate",          "k.input", "Input",          Flip,   -90,  -40,  -70,  "dB", "",   Kuddle) \
    P(KMuteOn,        "kuddle.muteOn",   "On",            "k.mute",  "",               Toggle, 0,    1,    1,    "",   "",   Kuddle) \
    P(KFoam,          "kuddle.foam",     "Foam",          "k.mute",  "Mute",           Flip,   0,    1,    0.4f, "%",  "",   Kuddle) \
    P(KThump,         "kuddle.thump",    "Thump",         "k.mute",  "Mute",           Flip,   0,    1,    0.55f,"%",  "",   Kuddle) \
    P(KStrings,       "kuddle.strings",  "Strings",       "k.mute",  "Mute",           Choice, 0,    2,    2,    "",   "New|Worn|Old", Kuddle) \
    P(KWahOn,         "kuddle.wahOn",    "On",            "k.wah",   "",               Toggle, 0,    1,    1,    "",   "",   Kuddle) \
    P(KWahMode,       "kuddle.wahMode",  "Mode",          "k.wah",   "Mode",           Choice, 0,    1,    0,    "",   "Auto|Manual", Kuddle) \
    P(KWahRange,      "kuddle.wahRange", "Range",         "k.wah",   "Mode",           Choice, 0,    1,    0,    "",   "Low|Mid", Kuddle) \
    P(KWahSense,      "kuddle.wahSense", "Sense",         "k.wah",   "Sweep",          Flip,   0,    1,    0.55f,"%",  "",   Kuddle) \
    P(KWahSpeed,      "kuddle.wahSpeed", "Speed",         "k.wah",   "Sweep",          Flip,   0,    1,    0.3f, "%",  "",   Kuddle) \
    P(KWahPeak,       "kuddle.wahPeak",  "Peak",          "k.wah",   "Sweep",          Flip,   0,    1,    0.4f, "%",  "",   Kuddle) \
    P(KWahPos,        "kuddle.wahPos",   "Position",      "k.wah",   "Sweep",          Flip,   0,    1,    0.5f, "%",  "",   Kuddle) \
    P(KDiDrive,       "kuddle.diDrive",  "Drive",         "k.di",    "Valve DI",       Flip,   0,    1,    0.3f, "%",  "",   Kuddle) \
    P(KDiAir,         "kuddle.diAir",    "Air",           "k.di",    "Valve DI",       Flip,   0,    1,    0.2f, "%",  "",   Kuddle) \
    P(KDiSag,         "kuddle.diSag",    "Sag",           "k.di",    "Valve DI",       Flip,   0,    1,    0.35f,"%",  "",   Kuddle) \
    P(KDiLowCut,      "kuddle.diLowCut", "Low cut",       "k.di",    "Valve DI",       Flip,   20,   120,  30,   "Hz", "",   Kuddle) \
    P(KOptoOn,        "kuddle.optoOn",   "On",            "k.opto",  "",               Toggle, 0,    1,    1,    "",   "",   Kuddle) \
    P(KOptoSqueeze,   "kuddle.optoSqueeze","Squeeze",     "k.opto",  "Opto",           Flip,   0,    1,    0.45f,"%",  "",   Kuddle) \
    P(KOptoSpeed,     "kuddle.optoSpeed","Speed",         "k.opto",  "Opto",           Choice, 0,    1,    0,    "",   "Slow|Fast", Kuddle) \
    P(KOptoBlend,     "kuddle.optoBlend","Blend",         "k.opto",  "Opto",           Flip,   0,    1,    0.8f, "%",  "",   Kuddle) \
    P(KOptoHold,      "kuddle.optoHold", "Hold D",        "k.opto",  "Opto",           Flip,   0,    12,   6,    "dB", "",   Kuddle) \
    P(KTapeOn,        "kuddle.tapeOn",   "On",            "k.tape",  "",               Toggle, 0,    1,    1,    "",   "",   Kuddle) \
    P(KTapePush,      "kuddle.tapePush", "Push",          "k.tape",  "Tape",           Flip,   0,    1,    0.35f,"%",  "",   Kuddle) \
    P(KTapeWow,       "kuddle.tapeWow",  "Wow",           "k.tape",  "Tape",           Flip,   0,    1,    0.1f, "%",  "",   Kuddle) \
    P(KTapeFlutter,   "kuddle.tapeFlutter","Flutter",     "k.tape",  "Tape",           Flip,   0,    1,    0.15f,"%",  "",   Kuddle) \
    P(KTapeBump,      "kuddle.tapeBump", "Bump",          "k.tape",  "Tape",           Flip,   0,    1,    0.4f, "%",  "",   Kuddle) \
    P(KTapeHiss,      "kuddle.tapeHiss", "Hiss",          "k.tape",  "Tape",           Flip,   0,    1,    0,    "%",  "",   Kuddle) \
    P(KToneOn,        "kuddle.toneOn",   "On",            "k.tone",  "",               Toggle, 0,    1,    1,    "",   "",   Kuddle) \
    P(KBass,          "kuddle.bass",     "Bass",          "k.tone",  "Tone",           Flip,   -12,  12,   2,    "dB", "",   Kuddle) \
    P(KBassHz,        "kuddle.bassHz",   "Bass at",       "k.tone",  "Tone",           Flip,   40,   200,  80,   "Hz", "",   Kuddle | Log) \
    P(KMids,          "kuddle.mids",     "Mids",          "k.tone",  "Tone",           Flip,   -12,  12,   1,    "dB", "",   Kuddle) \
    P(KMidsHz,        "kuddle.midsHz",   "Mids at",       "k.tone",  "Tone",           Flip,   200,  1500, 400,  "Hz", "",   Kuddle | Log) \
    P(KTreble,        "kuddle.treble",   "Treble",        "k.tone",  "Tone",           Flip,   1000, 12000,3200, "Hz", "",   Kuddle | Log) \
    P(KRoomOn,        "kuddle.roomOn",   "On",            "k.room",  "",               Toggle, 0,    1,    0,    "",   "",   Kuddle) \
    P(KRoomType,      "kuddle.roomType", "Type",          "k.room",  "Room",           Choice, 0,    1,    0,    "",   "Dark room|Spring", Kuddle) \
    P(KRoomSize,      "kuddle.roomSize", "Size",          "k.room",  "Room",           Flip,   0,    1,    0.3f, "%",  "",   Kuddle) \
    P(KRoomDecay,     "kuddle.roomDecay","Decay",         "k.room",  "Room",           Flip,   0.2f, 3,    0.8f, "s",  "",   Kuddle) \
    P(KRoomPre,       "kuddle.roomPre",  "Pre-delay",     "k.room",  "Room",           Flip,   0,    60,   10,   "ms", "",   Kuddle) \
    P(KRoomMix,       "kuddle.roomMix",  "Mix",           "k.room",  "Room",           Flip,   0,    1,    0.2f, "%",  "",   Kuddle) \
    P(KOutput,        "kuddle.output",   "Level",         "k.output","Output",         Flip,   -24,  12,   -3,   "dB", "",   Kuddle) \
    P(RecOn,          "rec.on",          "Record when I play","recorder","Recorder",   Toggle, 0,    1,    1,    "",   "",   Global) \
    P(RecRoom,        "rec.room",        "Room",          "recorder","Recorder",       Choice, 0,    1,    1,    "",   "Off|This phone's mic", Global) \
    P(WatchMotion,    "watch.motion",    "Movement",      "watch",   "Watch",          Choice, 0,    1,    0,    "",   "Reach|Twist", Global) \
    P(WatchRange,     "watch.range",     "Full sweep",    "watch",   "Watch",          Flip,   20,   120,  60,   "°",  "",   Global) \
    P(WatchGlide,     "watch.glide",     "Glide back",    "watch",   "Watch",          Flip,   0.05f, 3,   0.4f, "s",  "",   Global | Log) \
    P(WatchReverse,   "watch.reverse",   "Reverse twist", "watch",   "Watch",          Toggle, 0,    1,    0,    "",   "",   Global) \
    P(FsACycle,       "fs.aCycle",       "Tap A in Grit", "footswitches","This song",  Choice, 0,    3,    0,    "",   "Edge and Drive|Off, Edge, Drive|Off and Edge|Off and Drive", None)
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

// When a control only applies to some types: "delay.type=2", "grains.effect!=0",
// "delay.type=3|4". Empty when always shown.
const char* showIf (P p);

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
