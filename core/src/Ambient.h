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

} // namespace ks
