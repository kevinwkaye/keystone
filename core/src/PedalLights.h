#pragma once
// Every light on the pedal's top plate, from one Input: Pebble's two eyes (PebbleEyes.h),
// four status lights, a meter of seven and one light above each footswitch. The lights
// are one strand of WS2812 dots, so the result is a frame in the order the wire visits
// them under the lid:
//
//   0 left eye, 1 right eye, 2 Phone, 3 Watch, 4 Projector, 5 Audio,
//   6 meter 7, 7 footswitch D, 8 meter 6, 9 meter 5, 10 footswitch C, 11 meter 4,
//   12 meter 3, 13 meter 2, 14 meter 1, 15 footswitch B, 16 hidden spacer (always off),
//   17 footswitch A, 18 and 19 spares coiled underneath (always off)
//
// Meter 1 is the left of the seven. The spacer bridges A and B, which are further apart
// than one length of the strand's wire.
//
// What they show:
//   Phone      on while a phone is connected
//   Watch      dim while a watch is connected, bright while it's live (C held in Grit)
//   Projector  on while the projector's HDMI is connected
//   Audio      green fine, amber busy or hot, red a dropout or no audio
//   Meter      the expression pedal's (or the watch's) amount, filling from the left in
//              the bank's colour. In Song mode otherwise a four-bar phrase counter from
//              the moment the part was picked: green filling over the first three bars,
//              purple flashing on every beat of the fourth. Off in the tuner.
//   Footswitch Stomp mode: A to C in the bank's colour, bright when the effect is on and
//              faint when it's off. When it's off here but on in another bank, a faint
//              glow in that bank's colour instead (both other banks: they take turns).
//              Glow's A (Speckle) pulses slowly while Speckle's Hold is on.
//              D warm white when the drone is held. Song mode: A and C faint green, B
//              green while there's a next part, D the drone. Tuner: all faint white, as
//              any tap leaves it. Kuddle: amber, bright for each switch's effect when on.
//
// Pure logic, no hardware: PixelLink sends the frame to the Pixel Trinkey.

#include "PebbleEyes.h"

#include <array>
#include <cmath>

namespace ks::lights
{
using eyes::Rgb;
constexpr int kPixels = 20;
using Frame = std::array<Rgb, kPixels>;

// where each light sits on the strand
namespace at
{
    constexpr int leftEye = 0, rightEye = 1, phone = 2, watch = 3, projector = 4, audio = 5, spacer = 16;
    constexpr int meter[7] = { 14, 13, 12, 11, 9, 8, 6 };   // meter 1 (left) to meter 7
    constexpr int foot[4] = { 17, 15, 10, 7 };              // A, B, C, D
}

constexpr Rgb kStatus  { 0, 200, 120 };
constexpr Rgb kOk      { 0, 255, 0 };
constexpr Rgb kBusy    { 255, 120, 0 };
constexpr Rgb kBad     { 255, 0, 0 };
constexpr Rgb kWhite   { 255, 200, 140 };   // warm, like the screen's cream
constexpr Rgb kBank[3] = { eyes::kGrit, eyes::kGroove, eyes::kGlow };

constexpr float kFaint = 0.06f;      // an effect that's off: just enough to see the job
constexpr float kElsewhere = 0.22f;  // on in another bank
constexpr float kWatchReady = 0.25f;

struct Input
{
    eyes::Input eyes;                 // rig, foot mode, bank, tuner, tempo, now, brightness
    bool phone = false, watch = false, watchLive = false, projector = false;
    int audio = 0;                    // 0 fine, 1 busy or hot, 2 a dropout or no audio
    bool pedalShow = false;           // the pedal (or the watch) is working its job
    float pedal = 0;                  // and its amount, 0..1
    double partStartedAt = 0;         // Song mode's phrase counter starts here (same clock as now)
    bool fx[3][3] {};                 // [bank][A to C] effect on
    bool drone = false;
    bool speckleHold = false;         // Speckle's Hold: Glow's A pulses while Speckle is on
    bool nextPart = false;            // Song mode: B has a part to go to
    bool kuddle[4] {};                // Kuddle: Mute, Auto wah, Room, Tape on
};

inline Frame frame (const Input& in)
{
    using eyes::scale;
    Frame f {};
    const auto pair = eyes::colours (in.eyes);   // already at the overall brightness
    f[at::leftEye] = pair[0];
    f[at::rightEye] = pair[1];

    const float k = in.eyes.brightness;
    auto put = [&] (int i, Rgb c, float level) { f[(size_t) i] = scale (c, level * k); };

    // status
    if (in.phone) put (at::phone, kStatus, 1.0f);
    if (in.watch || in.watchLive) put (at::watch, kStatus, in.watchLive ? 1.0f : kWatchReady);
    if (in.projector) put (at::projector, kStatus, 1.0f);
    put (at::audio, in.audio >= 2 ? kBad : in.audio == 1 ? kBusy : kOk, 1.0f);

    const bool tuner = in.eyes.tuner;
    const bool kuddle = in.eyes.rig == 1;
    const bool song = ! kuddle && in.eyes.footMode == 1;
    const int bank = std::clamp (in.eyes.bank, 0, 2);

    // meter
    if (! tuner)
    {
        if (in.pedalShow)
        {
            const int lit = (int) std::lround (std::clamp (in.pedal, 0.0f, 1.0f) * 7.0f);
            const Rgb c = kuddle ? eyes::kKuddle : kBank[bank];
            for (int i = 0; i < 7; ++i) if (i < lit) put (at::meter[i], c, 1.0f);
        }
        else if (song)
        {
            const double beatLen = 60.0 / std::clamp ((double) in.eyes.bpm, 40.0, 240.0);
            const double beats = std::max (0.0, in.eyes.now - in.partStartedAt) / beatLen;
            const int inPhrase = (int) std::fmod (std::floor (beats), 16.0);   // 0..15
            if (inPhrase >= 12)
            {
                // the fourth bar: every light flashes purple on the beat
                const float t = (float) (beats - std::floor (beats));
                const float pulse = 0.25f + 0.75f * std::exp (-t * 4.0f);
                for (int i = 0; i < 7; ++i) put (at::meter[i], eyes::kSongPurple, pulse);
            }
            else
            {
                const int lit = 1 + inPhrase * 7 / 12;   // 1 on the first beat, 7 by the end of bar three
                for (int i = 0; i < 7; ++i) if (i < lit) put (at::meter[i], eyes::kSongGreen, 0.8f);
            }
        }
    }

    // footswitches
    if (tuner)
    {
        for (int sw : at::foot) put (sw, kWhite, kFaint);
    }
    else if (kuddle)
    {
        for (int i = 0; i < 4; ++i) put (at::foot[i], eyes::kKuddle, in.kuddle[i] ? 1.0f : kFaint);
    }
    else if (song)
    {
        put (at::foot[0], eyes::kSongGreen, kFaint);
        put (at::foot[1], eyes::kSongGreen, in.nextPart ? 0.5f : kFaint);
        put (at::foot[2], eyes::kSongGreen, kFaint);
        put (at::foot[3], kWhite, in.drone ? 1.0f : kFaint);
    }
    else
    {
        for (int sw = 0; sw < 3; ++sw)
        {
            if (in.fx[bank][sw])
            {
                // Speckle frozen on Hold: a slow pulse, once a second, so it reads as not following you
                const bool pulse = bank == 2 && sw == 0 && in.speckleHold;
                const float level = pulse ? 0.35f + 0.65f * (0.5f + 0.5f * (float) std::cos (in.eyes.now * 2.0 * 3.14159265)) : 1.0f;
                put (at::foot[sw], kBank[bank], level);
                continue;
            }
            int others[2], n = 0;
            for (int b = 0; b < 3; ++b) if (b != bank && in.fx[b][sw]) others[n++] = b;
            if (n == 0) put (at::foot[sw], kBank[bank], kFaint);
            else
            {
                // both other banks: they take turns, a second each
                const int which = n == 2 ? (int) std::fmod (std::floor (std::max (0.0, in.eyes.now)), 2.0) : 0;
                put (at::foot[sw], kBank[others[which]], kElsewhere);
            }
        }
        put (at::foot[3], kWhite, in.drone ? 1.0f : kFaint);
    }
    // the spacer and the two spares stay dark
    return f;
}
} // namespace ks::lights
