#pragma once

// Kevy does Devy's building blocks that Keystone's delay uses: the pitch shifter, the
// reverse reader and the crusher. Plain C++17, no JUCE. (The rest of the pedal's blocks
// were trimmed in 0.4.1, since nothing here used them.)

#include "../spk/DspUtil.h"   // the same helpers as Speckle's

namespace kdd
{
using namespace spk;

// Two-grain pitch shifter with Hann crossfades. Grain length sets the character:
// long grains are smooth (MK II), short grains are grainy and warbly (MK I).
struct PitchShifter
{
    DelayLine line;
    float phase = 0.0f, grain = 2400.0f, fs = 48000.0f;

    void prepare (float sampleRate, float maxGrainSeconds = 0.12f)
    {
        fs = sampleRate;
        line.init (nextPow2 ((int) (maxGrainSeconds * fs * 2.0f) + 8));
        phase = 0.0f;
    }
    void clear() { line.clear(); phase = 0.0f; }
    void setGrain (float seconds) { grain = clampf (seconds * fs, 64.0f, (float) line.size() * 0.45f); }

    float process (float x, float ratio)
    {
        line.push (x);
        // the read point drifts at (1 - ratio) samples per sample, wrapped inside one grain
        phase += (1.0f - ratio) / grain;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float d1 = 4.0f + phase * grain, d2 = 4.0f + p2 * grain;
        const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
        return line.tap (d1) * w1 * w1 + line.tap (d2) * w2 * w2;
    }
};

// Plays the last "length" samples backwards, in overlapping windows.
// Average delay is about one window.
struct ReverseReader
{
    DelayLine line;
    float phase = 0.0f;

    void prepare (int maxSamples) { line.init (nextPow2 (maxSamples * 2 + 16)); phase = 0.0f; }
    void clear() { line.clear(); phase = 0.0f; }

    float process (float x, float length)
    {
        line.push (x);
        length = clampf (length, 256.0f, (float) line.size() * 0.45f);
        phase += 1.0f / length;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        // distance grows two samples per sample, so the read runs backwards at normal speed
        const float a = line.tap (4.0f + 2.0f * phase * length);
        const float b = line.tap (4.0f + 2.0f * p2 * length);
        const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
        return a * w1 * w1 + b * w2 * w2;
    }
};

// Sample-rate and bit-depth reduction. Used hard by Low-Bit, gently by MK I voicing.
struct Crusher
{
    float held = 0.0f, acc = 0.0f;
    float process (float x, float rateRatio, float bits)
    {
        acc += rateRatio;
        if (acc >= 1.0f) { acc -= std::floor (acc); held = x; }
        const float q = std::pow (2.0f, bits - 1.0f);
        return std::round (held * q) / q;
    }
    void reset() { held = acc = 0.0f; }
};

} // namespace kdd
