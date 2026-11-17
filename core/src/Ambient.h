#pragma once
// Shared pieces for the ambient section (Delay.h, Reverb.h). Mono. Both climb towards near
// self-oscillation on the top third of the Bloom sweep, with a limiter in every feedback
// loop so they never howl or spike the amp, and repeats that darken and lose their lows
// as feedback rises.

#include "Modules.h"

namespace ks
{
// Soft limiter for feedback loops: unity below the knee, slow gain riding above it.
struct LoopLimiter
{
    float env = 0, att = 0, rel = 0;
    void prepare (float sr) { att = onePoleCoef (0.005f, sr); rel = onePoleCoef (0.3f, sr); env = 0; }
    float process (float x)
    {
        const float a = std::fabs (x);
        env = a > env ? a + (env - a) * att : a + (env - a) * rel;
        const float knee = 0.35f;
        const float g = env > knee ? knee / env : 1.0f;
        return std::tanh (x * g * 1.4f) / 1.4f;
    }
};


// Vinyl brake (Groove: hold C), from Lathe: hold and the whole record slows to a stop, its
// pitch falling away; let go and it spins back up to speed, then hands back to your live
// playing so nothing is left running late.
struct VinylBrake
{
    void prepare (float sr_)
    {
        sr = sr_;
        line.prepare ((int) (1.8f * sr) + 8);
        maxDelay = 1.6f * sr;
        reset();
    }
    void reset() { line.reset(); k = 1; delay = 0; mix = 0; }
    bool active() const { return mix > 0.0f || held; }

    float process (float x, bool hold)
    {
        held = hold;
        line.push (x);
        if (! hold && mix <= 0.0f) { k = 1; delay = 0; return x; }
        // the platter: stops over 1.4 s, comes back up to speed in 0.4 s
        k = hold ? std::max (0.0f, k - 1.0f / (1.4f * sr)) : std::min (1.0f, k + 1.0f / (0.4f * sr));
        delay = std::min (maxDelay, delay + (1.0f - k));   // the record falls behind as it slows
        // in at once (in 3 ms); out once it's back up to speed, over 60 ms
        if (hold || k < 0.98f) mix = std::min (1.0f, mix + 1.0f / (0.003f * sr));
        else mix = std::max (0.0f, mix - 1.0f / (0.06f * sr));
        const float w = line.hermite (std::max (1.0f, delay)) * clampf (k * 4.0f, 0.0f, 1.0f);
        return x + (w - x) * mix;
    }

    DelayLine line;
    float sr = 48000, k = 1, delay = 0, mix = 0, maxDelay = 76800;
    bool held = false;
};
} // namespace ks
