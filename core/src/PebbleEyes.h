#pragma once
// Pebble's eyes: two RGB LEDs behind the lid sticker, one per eye. Eye 0 is the left eye
// and eye 1 the right, as the player sees them standing at the board.
//
// What they show:
//   Stomp mode   both eyes in the bank's colour: Grit orange, Groove purple, Glow blue
//   Song mode    the Successpit colours, green and purple, swapping eyes on every beat
//   Tuner        left eye red when flat, right eye red when sharp (brighter the further
//                out), both green when in tune (within 2 cents, as the phone). Dark
//                until a note is heard. The tuner wins over every mode.
//   Kuddle       a warm amber, like its phone screens
//
// Pure logic, no hardware: the host reads the controller and meters, asks for the colours
// and hands them to whatever drives the LEDs (EyeLights on the Pi).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ks::eyes
{
struct Rgb
{
    uint8_t r = 0, g = 0, b = 0;
    bool operator== (const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!= (const Rgb& o) const { return ! (*this == o); }
};
using Pair = std::array<Rgb, 2>;

// LED colours, saturated so they read through the vinyl (the phone's softer shades of
// the same colours wash out to white on an LED)
constexpr Rgb kGrit       { 255, 70, 0 };
constexpr Rgb kGroove     { 140, 0, 255 };
constexpr Rgb kGlow       { 0, 60, 255 };
constexpr Rgb kSongGreen  { 0, 255, 40 };
constexpr Rgb kSongPurple { 140, 0, 255 };
constexpr Rgb kOutOfTune  { 255, 0, 0 };
constexpr Rgb kInTune     { 0, 255, 0 };
constexpr Rgb kKuddle     { 255, 110, 15 };

constexpr float kInTuneCents = 2.0f;    // the phone's tuner goes green at the same point
constexpr float kFullRedCents = 25.0f;  // this far out or more, the red eye is at full
constexpr float kSongFade = 0.15f;      // Song mode: the swap fades over this much of a beat

struct Input
{
    int rig = 0;              // 0 Keystone, 1 Kuddle
    int footMode = 0;         // 0 Stomp, 1 Song
    int bank = 0;             // 0 Grit, 1 Groove, 2 Glow
    bool tuner = false;
    bool noteHeard = false;   // the tuner has a note
    float cents = 0;          // negative is flat
    float bpm = 120;          // the song's tempo, for Song mode's swap
    double now = 0;           // seconds, any steady clock
    float brightness = 0.4f;  // 0..1 over everything; pixels are fierce on a dark stage
};

inline Rgb scale (Rgb c, float k)
{
    k = std::clamp (k, 0.0f, 1.0f);
    auto ch = [k] (uint8_t v) { return (uint8_t) std::lround (v * k); };
    return { ch (c.r), ch (c.g), ch (c.b) };
}

inline Rgb mix (Rgb a, Rgb b, float t)
{
    t = std::clamp (t, 0.0f, 1.0f);
    auto ch = [t] (uint8_t x, uint8_t y) { return (uint8_t) std::lround (x + (y - x) * t); };
    return { ch (a.r, b.r), ch (a.g, b.g), ch (a.b, b.b) };
}

inline Pair colours (const Input& in)
{
    Pair p {};
    if (in.tuner)
    {
        if (! in.noteHeard) return p;
        const float off = std::fabs (in.cents);
        if (off <= kInTuneCents)
            p[0] = p[1] = kInTune;
        else
        {
            const float k = 0.25f + 0.75f * std::min (1.0f, (off - kInTuneCents) / (kFullRedCents - kInTuneCents));
            p[in.cents < 0 ? 0 : 1] = scale (kOutOfTune, k);
        }
    }
    else if (in.rig == 1)
        p[0] = p[1] = kKuddle;
    else if (in.footMode == 1)
    {
        const double beat = 60.0 / std::clamp ((double) in.bpm, 40.0, 240.0);
        const double phase = std::max (0.0, in.now) / beat;
        const long n = (long) std::floor (phase);
        const float t = std::min (1.0f, (float) (phase - (double) n) / kSongFade);
        const bool even = n % 2 == 0;
        const Rgb left = even ? kSongGreen : kSongPurple, right = even ? kSongPurple : kSongGreen;
        p[0] = mix (right, left, t);   // each eye fades from last beat's colour to this one's
        p[1] = mix (left, right, t);
    }
    else
        p[0] = p[1] = in.bank == 1 ? kGroove : in.bank == 2 ? kGlow : kGrit;

    for (auto& c : p) c = scale (c, in.brightness);
    return p;
}
} // namespace ks::eyes
