#pragma once
// The projector's reactive layer (0.6.1): what the bass and the feet are doing, turned into
// the handful of numbers keystone-react.glsl draws with. Portable, so the tests can check it;
// the Pi's projector thread runs it about 30 times a second and sends the result to mpv.
//
// Scenes: the Successpit loop (8 min 1 s) is 41 scenes. Each belongs to a family that says
// how the picture reacts (calm scenes take the full pulse and brightness, movement scenes a
// push, colour fields the note colours, busy and logo scenes barely anything). Kevin can set
// any scene to Off, Subtle or Full on the phone. Effect visuals (Bitrot, Speckle Hold,
// Afterglow, the delay and reverb haze) work in every scene, whatever its strength.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ks::visuals
{
enum Family : int { Calm = 0, Movement = 1, Colour = 2, Busy = 3, Live = 4 };

struct Scene
{
    double start;      // seconds into the loop
    const char* name;
    Family family;
};

constexpr double kLoopLength = 481.0;

// The Successpit loop, from the projection brief (the visuals branch's scene list)
inline const std::vector<Scene>& loopScenes()
{
    static const std::vector<Scene> s {
        { 0, "TV static", Busy }, { 3, "Test card", Busy }, { 17, "Square tunnel", Movement }, { 33, "Blocky plasma", Colour },
        { 47, "Copper bars", Colour }, { 61, "Hypnotic rings", Movement }, { 75, "Checker floor", Calm }, { 91, "Static", Busy },
        { 93, "Logo wave", Busy }, { 103, "Pixel kaleidoscope", Movement }, { 117, "Checker tunnel", Movement }, { 133, "Oscilloscope", Live },
        { 145, "Starfield", Calm }, { 159, "Synthwave sun", Calm }, { 173, "Moiré ripples", Colour }, { 187, "Pixel rain", Busy },
        { 199, "Channel flicking", Busy }, { 209, "Equaliser", Live }, { 221, "Logo rotozoomer", Busy }, { 233, "Double spiral", Movement },
        { 247, "Plasma 2", Colour }, { 261, "Synthwave mountains", Calm }, { 277, "Broken test card", Busy }, { 287, "Reverse tunnel", Movement },
        { 301, "Wobbling copper bars", Colour }, { 313, "Checkerboard floor", Calm }, { 325, "Static", Busy }, { 327, "Logo wave 2", Busy },
        { 335, "Checker tunnel 2", Movement }, { 347, "Moiré 2", Colour }, { 359, "Kaleidoscope 2", Movement }, { 371, "Starfield 2", Calm },
        { 383, "Plasma 3", Colour }, { 397, "Equaliser 2", Live }, { 407, "Rings 2", Movement }, { 419, "Mountains 2", Calm },
        { 433, "Synthwave grid", Calm }, { 447, "Spiral 2", Movement }, { 459, "Channel flicking 2", Busy }, { 467, "Logo rotozoomer 2", Busy },
        { 477, "Static", Busy } };
    return s;
}

// the scene playing at t seconds into the loop (it wraps)
inline int sceneAt (double t)
{
    const auto& s = loopScenes();
    t = std::fmod (std::fmax (t, 0.0), kLoopLength);
    int i = 0;
    while (i + 1 < (int) s.size() && s[(size_t) i + 1].start <= t) ++i;
    return i;
}

// A video counts as the Successpit loop when it's within a few seconds of its length
inline bool isLoop (double duration) { return std::fabs (duration - kLoopLength) < 4.0; }

inline const char* familyName (int f)
{
    static const char* n[] = { "Calm", "Movement", "Colour", "Busy", "Live" };
    return f >= 0 && f <= 4 ? n[f] : "Calm";
}

// how hard a family reacts when its scene is on Auto, before Off, Subtle or Full
inline float autoStrength (int family) { return family == Busy ? 0.15f : 1.0f; }

// Off 0, Subtle 0.5, Full 1, Auto the family's own
inline float strengthFor (int family, const std::string& setting)
{
    if (setting == "off") return 0.0f;
    if (setting == "subtle") return 0.5f;
    if (setting == "full") return 1.0f;
    return autoStrength (family);
}

struct Feed
{
    float level = 0, high = 0;        // against your playing level (about 1 on a normal note)
    int onsets = 0;                   // goes up on every note start
    float onsetStrength = 0;
    int notePc = -1;                  // 0 = C, -1 none
    float noteClarity = 0;
    int key = -1;                     // the song's key (0 = C), -1 none set
    bool bitrot = false;
    float bitrotAmount = 0.5f;        // its pedal job, 0..1
    bool hold = false;                // Speckle on and its Hold on
    bool drone = false;               // a held drone (Afterglow)
    bool space = false;               // delay or reverb on
    float runaway = 0;
};

struct Out
{
    float on = 1, bright = 0, pulse = 0, tr = 1, tg = 1, tb = 1, tint = 0;
    float glitch = 0, seed = 0, smear = 0, drift = 0, bloom = 0, haze = 0;
    bool pause = false;               // Speckle Hold freezes the picture
};

class Reactor
{
public:
    // dt in seconds; family and strength for the scene playing (strength from strengthFor)
    Out step (const Feed& f, float dt, int family, float strength)
    {
        Out o;
        dt = std::fmin (std::fmax (dt, 0.001f), 0.25f);
        auto glide = [dt] (float& v, float target, float up, float down)
        {
            const float tau = target > v ? up : down;
            v += (target - v) * (1.0f - std::exp (-dt / std::fmax (tau, 1.0e-3f)));
        };
        // family shape: brightness, push, colour
        static const float shape[5][3] = { { 1.0f, 1.0f, 1.0f }, { 0.3f, 1.0f, 0.8f }, { 0.4f, 0.4f, 1.2f }, { 1.0f, 1.0f, 1.0f }, { 1.3f, 1.2f, 0.6f } };
        const int fam = family >= 0 && family <= 4 ? family : 0;
        // the strength changes over half a second at a cut, so nothing jumps
        glide (str, strength, 0.5f, 0.5f);
        for (int k = 0; k < 3; ++k) glide (shapeNow[k], shape[fam][k], 0.5f, 0.5f);

        // how hard you play
        glide (lvl, std::fmin (f.level, 1.5f), 0.03f, 0.25f);
        glide (slowLvl, std::fmin (f.level, 1.5f), 0.4f, 1.5f);
        o.bright = shapeNow[0] * str * 0.35f * std::fmax (0.0f, std::fmin (lvl - 0.2f, 1.3f));

        // a push on every note start
        if (! started) { started = true; lastOnsets = f.onsets; }
        const bool fresh = f.onsets != lastOnsets;
        if (fresh) pulseEnv = std::fmax (pulseEnv, std::fmin (std::fmax (f.onsetStrength, 0.3f), 1.2f));
        lastOnsets = f.onsets;
        pulseEnv *= std::exp (-dt / 0.15f);
        o.pulse = std::fmin (1.0f, shapeNow[1] * str * pulseEnv);

        // the note's colour, and a flash on the song's root, green then purple
        if (f.notePc >= 0 && f.noteClarity > 0.6f)
        {
            float r, g, b;
            noteColour (f.notePc, r, g, b);
            target[0] = r; target[1] = g; target[2] = b;
            glide (tintBase, 0.25f, 0.1f, 0.1f);
            const bool newRoot = f.key >= 0 && f.notePc == f.key && (fresh || lastPc != f.notePc);
            if (newRoot)
            {
                rootFlash = 1.0f;
                rootGreen = ! rootGreen;
            }
            lastPc = f.notePc;
        }
        else { glide (tintBase, 0.0f, 0.1f, 0.6f); lastPc = -1; }
        rootFlash *= std::exp (-dt / 0.4f);
        if (rootFlash > 0.05f)
        {
            const float rc[3] = { rootGreen ? 0.18f : 0.48f, rootGreen ? 0.72f : 0.31f, rootGreen ? 0.42f : 0.84f };
            for (int k = 0; k < 3; ++k) target[k] = target[k] + (rc[k] - target[k]) * std::fmin (1.0f, rootFlash * 1.5f);
        }
        for (int k = 0; k < 3; ++k) glide (tintCol[k], target[k], 0.12f, 0.12f);
        o.tr = tintCol[0]; o.tg = tintCol[1]; o.tb = tintCol[2];
        o.tint = std::fmin (1.0f, shapeNow[2] * str * std::fmax (tintBase, 0.8f * rootFlash));

        // effects: in every scene, whatever its strength
        const float gWant = f.bitrot ? (0.25f + 0.75f * f.bitrotAmount) * (0.35f + 0.65f * std::fmin (lvl, 1.0f)) : 0.0f;
        glide (glitch, gWant, 0.06f, 0.2f);
        o.glitch = glitch;
        seedTime += dt;
        if (glitch > 0.01f && seedTime > 1.0f / 12.0f) { seedTime = 0; seed = std::fmod (seed * 1.618f + 13.37f, 997.0f); }
        o.seed = seed;

        o.pause = f.hold;
        glide (smear, f.hold ? 0.6f : 0.0f, 1.5f, 0.3f);
        holdTime = f.hold ? holdTime + dt : 0.0f;
        glide (drift, f.hold ? 0.03f * std::sin (holdTime * 0.4f) : 0.0f, 0.8f, 0.3f);
        o.smear = smear; o.drift = drift;

        glide (bloom, f.drone ? 0.75f : 0.0f, 1.2f, 2.0f);
        o.bloom = bloom;
        glide (haze, std::fmin (1.0f, (f.space ? 0.15f + 0.35f * std::fmin (slowLvl, 1.0f) : 0.0f) + 0.5f * f.runaway), 0.5f, 0.8f);
        o.haze = haze;
        return o;
    }

    // a hue round the colour wheel for each note, softened so the picture keeps its own colours
    static void noteColour (int pc, float& r, float& g, float& b)
    {
        const float h = (float) ((pc % 12 + 12) % 12) / 12.0f * 6.0f;
        const float x = 1.0f - std::fabs (std::fmod (h, 2.0f) - 1.0f);
        float c[3] = { 0, 0, 0 };
        switch ((int) h)
        {
            case 0: c[0] = 1; c[1] = x; break;
            case 1: c[0] = x; c[1] = 1; break;
            case 2: c[1] = 1; c[2] = x; break;
            case 3: c[1] = x; c[2] = 1; break;
            case 4: c[0] = x; c[2] = 1; break;
            default: c[0] = 1; c[2] = x; break;
        }
        r = 0.3f + 0.7f * c[0]; g = 0.3f + 0.7f * c[1]; b = 0.3f + 0.7f * c[2];
    }

private:
    float str = 0, shapeNow[3] = { 1, 1, 1 }, lvl = 0, slowLvl = 0, pulseEnv = 0;
    int lastOnsets = 0, lastPc = -1;
    float tintBase = 0, rootFlash = 0, target[3] = { 1, 1, 1 }, tintCol[3] = { 1, 1, 1 };
    bool rootGreen = false, started = false;
    float glitch = 0, seed = 1, seedTime = 0, smear = 0, drift = 0, holdTime = 0, bloom = 0, haze = 0;
};
} // namespace ks::visuals
