#pragma once
// Delay, reverb and shimmer for the ambient section. Mono. Both climb towards near
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

class Delay
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        line.prepare ((int) (1.6f * sr) + 8);
        timeS.setTime (0.25f, sr);
        mixS.setTime (0.03f, sr);
        fbS.setTime (0.08f, sr);
        gateS.setTime (0.15f, sr);
        lim.prepare (sr);
        reset();
    }
    void reset() { line.reset(); lp.reset(); hp.reset(); lfo = 0; mixS.reset (0); fbS.reset (0); gateS.reset (1); timeS.reset (0.42f * sr); }

    void update (const Vals& v, float zone3, bool bloomActive, bool heelBack, bool cutTails)
    {
        const bool on = Vb (v, P::DlOn);
        float mix = on ? V (v, P::DlMix) : 0.0f;
        float fb = V (v, P::DlFeedback);
        if (bloomActive && zone3 > 0)
        {
            const float reach = V (v, P::BloomWash);
            fb = fb + (1.02f * reach - fb) * zone3;   // past 1 only with the loop limiter
            mix = std::max (mix, zone3 * 0.45f * reach);
        }
        if (! on && ! (bloomActive && zone3 > 0)) fb = std::min (fb, 0.6f);   // tails die away naturally
        mixS.set (mix);
        fbS.set (fb);
        timeS.set (clampf (V (v, P::DlTime) * 0.001f * sr, 16.0f, (float) line.size() - 8));
        tone = V (v, P::DlTone);
        mod = V (v, P::DlMod);
        gateS.set (heelBack && cutTails ? 0.0f : 1.0f);
        feeding = on || (bloomActive && zone3 > 0);
    }
    bool active() const { return mixS.value > 1.0e-4f || mixS.target > 0 || tail > 1.0e-5f; }

    // returns the wet signal scaled by mix
    float process (float x)
    {
        const float fb = fbS.next();
        // darker and thinner as feedback rises
        if ((count++ & 15) == 0)
        {
            const float f = std::max (fb, 0.0f);
            lp.setLowPass (7000.0f * (1.0f - 0.75f * tone) * (1.0f - 0.45f * std::min (1.0f, f)), sr);
            hp.setLowPass (60.0f + 340.0f * std::min (1.0f, f), sr);
        }
        lfo += 0.55f / sr;
        if (lfo >= 1) lfo -= 1;
        const float d = timeS.next() + mod * 0.002f * sr * std::sin (kTwoPi * lfo);
        const float y = line.hermite (d);
        float loop = hp.hp (lp.lp (y));
        loop = lim.process (loop * fb);
        const float g = gateS.next();
        line.push ((feeding ? x : 0.0f) + loop * g);
        tail = std::max (std::fabs (y), tail * 0.9999f);
        return y * mixS.next() * g;
    }

private:
    float sr = 48000, tone = 0.5f, mod = 0.2f, lfo = 0, tail = 0;
    unsigned count = 0;
    bool feeding = false;
    DelayLine line;
    OnePole lp, hp;
    LoopLimiter lim;
    Smoothed timeS, mixS, fbS, gateS;
};

// Feedback delay network reverb (8 lines, 4 in light mode), with a shimmer pitch shifter
// in the loop.
class Reverb
{
public:
    static constexpr int kLines = 8;

    void prepare (float sr_)
    {
        sr = sr_;
        for (auto& l : lines) l.prepare ((int) (0.09f * sr) + 8);
        pre.prepare ((int) (0.16f * sr) + 8);
        for (auto& a : diff) a.prepare ((int) (0.02f * sr));
        shift.prepare ((int) (0.2f * sr));
        mixS.setTime (0.03f, sr);
        gateS.setTime (0.15f, sr);
        shimS.setTime (0.05f, sr);
        lim.prepare (sr);
        shimHp.highPass (300.0f, 0.707f, sr);
        reset();
    }
    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& d : damp) d.reset();
        pre.reset();
        for (auto& a : diff) a.reset();
        shift.reset();
        shimHp.reset();
        shimPhase = 0;
        mixS.reset (0); gateS.reset (1); shimS.reset (0);
        tail = 0;
    }

    void update (const Vals& v, float zone3, bool bloomActive, bool heelBack, bool cutTails, bool light)
    {
        const bool on = Vb (v, P::RvOn);
        float mix = on ? V (v, P::RvMix) : 0.0f;
        float decay = V (v, P::RvDecay);
        if (bloomActive && zone3 > 0)
        {
            const float reach = V (v, P::BloomWash);
            decay = decay + (40.0f * reach - decay) * zone3;
            mix = std::max (mix, zone3 * 0.35f * reach);
        }
        mixS.set (mix);
        const float size = 0.35f + 0.65f * V (v, P::RvSize);
        static const float base[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        nLines = light ? 4 : kLines;
        for (int i = 0; i < kLines; ++i)
        {
            const int idx = light ? i * 2 % kLines : i;
            len[i] = base[idx] * 0.001f * sr * size;
            const float g = std::pow (10.0f, -3.0f * len[i] / (decay * sr));
            gains[i] = std::min (g, 0.9995f);
        }
        const float dampHz = 12000.0f * std::pow (0.12f, V (v, P::RvDamp));
        for (auto& d : damp) d.setLowPass (dampHz, sr);
        predelay = V (v, P::RvPredelay) * 0.001f * sr;
        shimS.set (V (v, P::RvShimmer));
        ratio = Vi (v, P::RvInterval) == 0 ? 2.0f : 1.5f;
        gateS.set (heelBack && cutTails ? 0.0f : 1.0f);
        feeding = on || (bloomActive && zone3 > 0);
    }
    bool active() const { return mixS.value > 1.0e-4f || mixS.target > 0 || tail > 1.0e-5f; }

    float process (float x)
    {
        pre.push (feeding ? x : 0.0f);
        float in = pre.frac (std::max (1.0f, predelay));
        // shimmer: the reverb an interval up, fed back in
        const float sh = shimS.next();
        if (sh > 0.001f) in += shimHp.process (pitchShift (lastOut)) * sh * 0.55f;
        in = lim.process (in);
        for (int i = 0; i < 4; ++i) in = diff[i].process (in, diffLen[i] * sr * 0.001f);

        // read lines
        float o[kLines];
        for (int i = 0; i < nLines; ++i) o[i] = damp[i].lp (lines[i].frac (len[i]));
        // Hadamard mix
        hadamard (o, nLines);
        float out = 0;
        const float g = gateS.next();
        for (int i = 0; i < nLines; ++i)
        {
            lines[i].push ((in + o[i] * gains[i]) * g);
            out += (i & 1) ? -o[i] : o[i];
        }
        out *= 0.35f;
        lastOut = out;
        tail = std::max (std::fabs (out), tail * 0.9999f);
        return out * mixS.next() * g;
    }

private:
    struct Allpass
    {
        DelayLine d;
        void prepare (int n) { d.prepare (n); }
        void reset() { d.reset(); }
        float process (float x, float len)
        {
            const float del = d.frac (len);
            const float y = -0.6f * x + del;
            d.push (x + 0.6f * y);
            return y;
        }
    };

    static void hadamard (float* o, int n)
    {
        for (int h = 1; h < n; h <<= 1)
            for (int i = 0; i < n; i += h * 2)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = o[j], b = o[j + h];
                    o[j] = a + b; o[j + h] = a - b;
                }
        const float s = 1.0f / std::sqrt ((float) n);
        for (int i = 0; i < n; ++i) o[i] *= s;
    }

    // two crossfading read heads, 60 ms window
    float pitchShift (float x)
    {
        shift.push (x);
        const float win = 0.06f * sr;
        shimPhase += (ratio - 1.0f) / win;
        if (shimPhase >= 1) shimPhase -= 1;
        float s = 0;
        for (int k = 0; k < 2; ++k)
        {
            float p = shimPhase + 0.5f * k;
            if (p >= 1) p -= 1;
            const float d = (1.0f - p) * win + 4.0f;
            const float w = std::sin (kPi * p);
            s += shift.frac (d) * w * w;
        }
        return s;
    }

    float sr = 48000;
    DelayLine lines[kLines], pre, shift;
    OnePole damp[kLines];
    Allpass diff[4];
    const float diffLen[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
    float len[kLines] {}, gains[kLines] {};
    int nLines = kLines;
    float predelay = 0, ratio = 2, shimPhase = 0, lastOut = 0, tail = 0;
    bool feeding = false;
    Biquad shimHp;
    LoopLimiter lim;
    Smoothed mixS, gateS, shimS;
};
} // namespace ks
