#pragma once
// Keystone reverb: twelve spaces in one mono engine.
//
// Room, Hall, Plate, Chamber, Cloud, Bloom, Chorale, Shimmer and Magneto share an 8-line
// feedback delay network with per-type voicing (size, diffusion, early reflections,
// modulation). Spring is its own dispersive tank, Nonlinear a shaped burst of dense taps,
// Impulse plays your own .wav spaces (Convolver.h).
//
// Shared: Decay, Pre-delay, Mix, Darkness, Low cut, Duck, Mod, Swell, Grit and Freeze.
// Like the delay, the loop has a limiter so the Bloom sweep can push it towards
// self-oscillation safely, and it darkens and loses its lows as it climbs.

#include "Ambient.h"
#include "Convolver.h"

namespace ks
{
class Reverb
{
public:
    enum Type { Room = 0, Hall, Plate, Spring, Chamber, Cloud, Bloom, Chorale, Shimmer, Magneto, Nonlinear, Impulse, kTypes };
    static constexpr int kLines = 8, kDiff = 6, kTaps = 24, kSprings = 3, kSpringAp = 8;

    void prepare (float sr_)
    {
        sr = sr_;
        for (auto& l : lines) l.prepare ((int) (0.2f * sr) + 64);
        pre.prepare ((int) (0.27f * sr) + 8);
        for (auto& a : diff) a.prepare ((int) (0.06f * sr));
        for (auto& a : bloomAp) a.prepare ((int) (0.2f * sr));
        shift.prepare ((int) (0.2f * sr));
        echo.prepare ((int) (0.85f * sr));
        burst.prepare ((int) (0.85f * sr));
        for (auto& s : springLine) s.prepare ((int) (0.06f * sr));
        for (auto& ap : springAp) for (auto& a : ap) a.prepare ((int) (0.004f * sr));
        conv.prepare (sr);
        mixS.setTime (0.03f, sr);
        gateS.setTime (0.15f, sr);
        shimS.setTime (0.05f, sr);
        lim.prepare (sr); limEcho.prepare (sr); limBloom.prepare (sr);
        for (auto& l : limSpring) l.prepare (sr);
        shimHp.highPass (300.0f, 0.707f, sr);
        duckEnv.set (0.005f, 0.3f, sr);
        fastEnv.set (0.001f, 0.03f, sr);
        slowEnv.set (0.08f, 0.3f, sr);
        swellFall = onePoleCoef (0.003f, sr);
        lenGlide = 1.0f - onePoleCoef (0.15f, sr);
        echoLp.setLowPass (4200.0f, sr);
        echoHp.setLowPass (120.0f, sr);
        for (auto& f : springLp) f.setLowPass (4500.0f, sr);
        for (auto& f : springHp) f.setLowPass (180.0f, sr);
        reset();
    }

    void clearSpace()
    {
        for (auto& l : lines) l.reset();
        for (auto& d : damp) d.reset();
        for (auto& h : hpf) h.reset();
        for (auto& a : diff) a.reset();
        for (auto& a : bloomAp) a.reset();
        shift.reset(); echo.reset(); burst.reset();
        for (auto& sl : springLine) sl.reset();
        for (auto& ap : springAp) for (auto& a : ap) a.reset();
        for (auto& b : formant) b.reset();
        conv.reset();
        shimPhase = 0; lastOut = 0; bloomFb = 0; echoFb = 0;
    }

    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& d : damp) d.reset();
        for (auto& h : hpf) h.reset();
        pre.reset();
        for (auto& a : diff) a.reset();
        for (auto& a : bloomAp) a.reset();
        shift.reset();
        echo.reset(); burst.reset();
        for (auto& s : springLine) s.reset();
        for (auto& ap : springAp) for (auto& a : ap) a.reset();
        for (auto& f : springLp) f.reset();
        for (auto& f : springHp) f.reset();
        for (auto& b : formant) b.reset();
        conv.reset();
        outLp.reset(); outHp.reset();
        shimPhase = 0; lastOut = 0; tail = 0; lfo = 0; bloomFb = 0; echoFb = 0;
        mixS.reset (0); gateS.reset (1); shimS.reset (0); sw = 1;
        swellPos = 1; swellG = 1;
    }

    // ---- impulses: the housekeeping thread loads, the audio thread picks up ----
    Convolver& convolver() { return conv; }

    void update (const Vals& v, float zone3, bool bloomActive, bool heelBack, bool cutTails, bool light)
    {
        const bool on = Vb (v, P::RvOn);
        pendingType = std::max (0, std::min ((int) kTypes - 1, Vi (v, P::RvType)));
        float mix = on ? V (v, P::RvMix) : 0.0f;
        float decay = V (v, P::RvDecay);
        freeze = on && Vb (v, P::RvFreeze) && pendingType != Spring && pendingType != Nonlinear && pendingType != Impulse;
        if (bloomActive && on && zone3 > 0)
        {
            const float reach = V (v, P::BloomWash);
            decay = decay + (40.0f * reach - decay) * zone3;
            mix = std::max (mix, zone3 * 0.35f * reach);
        }
        mixS.set (mix);
        decaySec = decay;
        vals = &v;
        lightMode = light;
        voice (v);
        gateS.set (heelBack && cutTails && ! on ? 0.0f : 1.0f);
        feeding = (on && ! freeze) || (bloomActive && on && zone3 > 0);
        updateGains();
    }

    // everything that depends on the type, for the type now playing
    void voice (const Vals& v)
    {

        // per type voicing: line size, input diffusion, early reflections, built-in movement
        struct Voice { float size, diff, er, mod, bright, trim; };
        static const Voice voices[kTypes] = {
            { 0.38f, 0.60f, 0.9f, 0.15f, 1.00f, 1.25f },   // Room
            { 1.00f, 0.70f, 0.3f, 0.45f, 0.90f, 1.00f },   // Hall
            { 0.60f, 0.85f, 0.0f, 0.30f, 1.40f, 1.05f },   // Plate
            { 0.30f, 0.50f, 0.0f, 0.10f, 1.00f, 1.00f },   // Spring (own tank)
            { 0.62f, 0.72f, 1.0f, 0.20f, 0.75f, 1.0f },    // Chamber
            { 1.45f, 0.85f, 0.0f, 0.65f, 0.85f, 1.00f },   // Cloud
            { 1.10f, 0.75f, 0.0f, 0.50f, 0.90f, 1.05f },   // Bloom
            { 1.00f, 0.72f, 0.0f, 0.55f, 1.00f, 1.70f },   // Chorale
            { 1.10f, 0.75f, 0.0f, 0.50f, 1.00f, 1.00f },   // Shimmer
            { 0.90f, 0.70f, 0.0f, 0.40f, 0.80f, 1.00f },   // Magneto
            { 0.50f, 0.70f, 0.0f, 0.00f, 1.00f, 1.00f },   // Nonlinear (own taps)
            { 1.00f, 0.00f, 0.0f, 0.00f, 1.00f, 1.00f },   // Impulse (convolution)
        };
        const Voice& vc = voices[type];
        const float userSize = V (v, P::RvSize), userDiff = V (v, P::RvDiffusion);
        const float size = vc.size * (0.45f + 1.1f * userSize);
        diffK = clampf (vc.diff * (0.55f + 0.65f * userDiff), 0.1f, 0.82f);
        erAmt = vc.er;
        modAmt = vc.mod + V (v, P::RvMod);
        trim = vc.trim;
        static const float base[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        nLines = lightMode ? 4 : kLines;
        for (int i = 0; i < kLines; ++i)
        {
            const int idx = lightMode ? i * 2 % kLines : i;
            len[i] = std::min (base[idx] * 0.001f * sr * size, (float) lines[i].size() - 0.004f * sr);
        }
        diffScale = 0.5f + 0.6f * size;

        const float dark = V (v, P::RvDamp);
        dampHz = std::min (0.45f * sr, 12000.0f * vc.bright * std::pow (0.12f, dark));
        lowCut = V (v, P::RvLowCut);
        predelay = V (v, P::RvPredelay) * 0.001f * sr;
        duck = V (v, P::RvDuck);
        swellSamples = V (v, P::RvSwell) * sr;
        grit = V (v, P::RvGrit);

        const bool shimmerShown = type <= Plate || type == Chamber || type == Cloud || type == Bloom || type == Shimmer;
        float sh = shimmerShown ? V (v, P::RvShimmer) : 0.0f;
        if (type == Shimmer) sh = std::max (sh, 0.2f);
        shimS.set (sh);
        static const float ratios[3] = { 2.0f, 1.5f, 3.0f };
        ratio = ratios[std::max (0, std::min (2, Vi (v, P::RvInterval)))];

        springs = 1 + std::max (0, std::min (2, Vi (v, P::RvSprings)));
        tension = V (v, P::RvTension);
        vowel = std::max (0, std::min (4, Vi (v, P::RvVowel)));
        voicesAmt = V (v, P::RvVoices);
        echoTime = V (v, P::RvEchoTime) * 0.001f * sr;
        echoes = V (v, P::RvEchoes);
        shape = std::max (0, std::min (3, Vi (v, P::RvShape)));
        burstLen = V (v, P::RvLength) * 0.001f * sr;

        outLp.setLowPass (dampHz, sr);
        outHp.setLowPass (lowCut, sr);
    }

    bool active() const { return mixS.value > 1.0e-4f || mixS.target > 0 || tail > 1.0e-5f; }

    // once per audio block, before process()
    void block() { conv.block(); }

    float process (float x)
    {
        // a new type: dip for 10 ms while the engine changes over
        if (pendingType != type)
        {
            sw -= 1.0f / (0.01f * sr);
            if (sw <= 0)
            {
                // silent for a moment: start the new space fresh
                sw = 0;
                type = pendingType;
                clearSpace();
                if (vals != nullptr) voice (*vals);
                for (int i = 0; i < kLines; ++i) lenNow[i] = len[i];
                updateGains();
            }
        }
        else if (sw < 1) sw = std::min (1.0f, sw + 1.0f / (0.025f * sr));
        const float swS = sw * sw * sw * (sw * (6.0f * sw - 15.0f) + 10.0f);   // a smooth S-curve, no corners to click on
        if ((count++ & 15) == 0) updateLoopFilters();

        const float dEnv = duckEnv.process (x);
        float in = feeding ? x : 0.0f;
        if (swellSamples > 1.0f) in *= swell (x);
        pre.push (in);
        float s = pre.frac (std::max (1.0f, predelay)) * swS;   // ramped, so a new type's own lines start smoothly

        float wet = 0;
        switch (type)
        {
            case Spring: wet = springTank (s); break;
            case Nonlinear: wet = nonlinear (s); break;
            case Impulse: wet = conv.process (s); break;
            default: wet = fdn (s); break;
        }
        if (type == Spring || type == Nonlinear || type == Impulse) wet = outHp.hp (outLp.lp (wet));

        const float g = gateS.next();
        const float dk = 1.0f - duck * 0.9f * clampf (dEnv * 8.0f, 0.0f, 1.0f);
        tail = std::max (std::fabs (wet), tail * 0.9999f);
        return wet * trim * dk * swS * mixS.next() * g;
    }

private:
    struct Allpass
    {
        DelayLine d;
        void prepare (int n) { d.prepare (n); }
        void reset() { d.reset(); }
        float process (float x, float len, float k)
        {
            const float del = d.frac (len);
            const float y = -k * x + del;
            d.push (x + k * y);
            return y;
        }
    };

    void updateGains()
    {
        for (int i = 0; i < kLines; ++i)
        {
            const float g = std::pow (10.0f, -3.0f * len[i] / (std::max (0.2f, decaySec) * sr));
            gains[i] = freeze ? 1.0f : std::min (g, 0.9995f);
        }
    }

    void updateLoopFilters()
    {
        for (int i = 0; i < kLines; ++i)
        {
            damp[i].setLowPass (freeze ? 0.45f * sr : dampHz, sr);
            hpf[i].setLowPass (freeze ? 10.0f : lowCut * 0.5f, sr);
        }
    }

    // ---------------------------------------------------------------- the FDN types
    float fdn (float s)
    {
        // Magneto: a multi-head tape echo in front of the room
        float echoOut = 0;
        if (type == Magneto)
        {
            lfo2 += 0.5f / sr;
            if (lfo2 >= 1) lfo2 -= 1;
            const float wob = 0.0012f * sr * std::sin (kTwoPi * lfo2);
            float sum = 0;
            for (int k = 1; k <= 4; ++k) sum += echo.frac (std::max (2.0f, echoTime * 0.25f * (float) k + wob)) * (0.55f + 0.15f * (float) k);
            echoOut = sum * 0.35f;
            const float last = echo.frac (std::max (2.0f, echoTime + wob));
            echoFb = limEcho.process (echoHp.hp (echoLp.lp (last)) * echoes);
            echo.push (s + echoFb);
            s = s * 0.4f + echoOut;
        }

        // shimmer: the reverb an interval up, fed back in
        const float sh = shimS.next();
        if (sh > 0.001f) s += shimHp.process (pitchShift (lastOut)) * sh * 0.6f;

        // input diffusion; Cloud gets all six, the rest four
        const int nd = type == Cloud ? kDiff : 4;
        static const float dl[kDiff] = { 4.7f, 3.6f, 12.7f, 9.3f, 23.1f, 31.7f };
        for (int i = 0; i < nd; ++i) s = diff[i].process (s, dl[i] * diffScale * 0.001f * sr, diffK);

        // Bloom: a slow-building ring of all-passes before the tank, so the space swells
        if (type == Bloom)
        {
            static const float bl[3] = { 61.0f, 89.0f, 127.0f };
            float b = s + bloomFb * 0.55f;
            for (int i = 0; i < 3; ++i) b = bloomAp[i].process (b, bl[i] * 0.001f * sr, 0.6f);
            bloomFb = limBloom.process (b);
            s = s * 0.3f + b * 0.8f;
        }
        s = lim.process (s);

        // read lines, with slow movement on each
        lfo += 0.31f / sr;
        if (lfo >= 1) lfo -= 1;
        const float modDepth = modAmt * 0.0016f * sr;
        float o[kLines];
        for (int i = 0; i < nLines; ++i)
        {
            const float m = modDepth > 0.5f ? modDepth * std::sin (kTwoPi * (lfo + 0.125f * (float) i) + (float) i) : 0.0f;
            lenNow[i] += (len[i] - lenNow[i]) * lenGlide;   // Size moves glide, never jump
            o[i] = damp[i].lp (lines[i].frac (std::max (4.0f, lenNow[i] + m)));
            o[i] = o[i] - hpf[i].lp (o[i]);
            if (grit > 0.01f) { const float dr = 1.0f + 4.0f * grit; o[i] = std::tanh (o[i] * dr) / dr; }
        }
        hadamard (o, nLines);
        float out = 0;
        const float g = gateS.value;
        for (int i = 0; i < nLines; ++i)
        {
            lines[i].push ((s + o[i] * gains[i]) * g);
            out += (i & 1) ? -o[i] : o[i];
        }
        out *= 0.35f;

        // early reflections from the pre-delay line (Room, Chamber, a touch on Hall)
        if (erAmt > 0.01f)
        {
            static const float er[6] = { 7.1f, 11.3f, 17.9f, 23.3f, 31.9f, 41.3f };
            static const float eg[6] = { 0.5f, -0.42f, 0.36f, -0.3f, 0.24f, -0.2f };
            float e = 0;
            for (int k = 0; k < 6; ++k) e += pre.frac (std::max (1.0f, predelay + er[k] * diffScale * 0.001f * sr)) * eg[k];
            out += e * erAmt * 0.6f;
        }
        lastOut = out;

        if (type == Chorale) out = chorale (out);
        if (type == Magneto) out += echoOut * 0.5f;
        return out;
    }

    // Chorale: the tail sung through vowel formants
    float chorale (float x)
    {
        static const float f[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        if ((count & 31) == 0)
        {
            lfo3 += 32.0f * 0.13f / sr;
            if (lfo3 >= 1) lfo3 -= 1;
            const float wob = 1.0f + 0.06f * std::sin (kTwoPi * lfo3);
            for (int k = 0; k < 3; ++k) formant[k].bandPass (f[vowel][k] * wob, 6.0f, sr);
        }
        const float y = formant[0].process (x) * 1.0f + formant[1].process (x) * 0.7f + formant[2].process (x) * 0.4f;
        return x * (1.0f - voicesAmt) + y * 2.2f * voicesAmt;
    }

    // ---------------------------------------------------------------- Spring
    float springTank (float x)
    {
        static const float lensMs[kSprings] = { 33.7f, 41.3f, 47.9f };
        const float k = 0.45f + 0.35f * tension;   // more drip, more chirp
        float out = 0;
        for (int sp = 0; sp < springs; ++sp)
        {
            const float L = lensMs[sp] * 0.001f * sr;
            const float fb = std::pow (10.0f, -3.0f * L / (std::max (0.3f, std::min (decaySec, 6.0f)) * sr));
            float y = springLine[sp].frac (L);
            float c = springHp[sp].hp (springLp[sp].lp (y));
            float z = x + c * fb * gateS.value;
            for (int a = 0; a < kSpringAp; ++a) z = springAp[sp][a].process (z, (0.7f + 0.13f * (float) a + 0.05f * (float) sp) * 0.001f * sr, k);
            springLine[sp].push (limSpring[sp].process (z));
            out += y;
        }
        return out * 0.9f / std::sqrt ((float) springs);
    }

    // ---------------------------------------------------------------- Nonlinear
    // A burst of dense taps across Length, shaped: gated, reverse, swoosh or ramp.
    float nonlinear (float x)
    {
        float s = x;
        for (int i = 0; i < 2; ++i) s = diff[i].process (s, (i ? 3.1f : 5.3f) * 0.001f * sr, 0.6f);
        burst.push (s);
        float out = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            const float t = ((float) k + 0.5f) / kTaps;
            float g;
            switch (shape)
            {
                case 0: g = t < 0.85f ? 1.0f : (1.0f - t) / 0.15f; break;     // gate
                case 1: g = t * t; break;                                      // reverse
                case 2: g = std::sin (kPi * t); break;                         // swoosh
                default: g = std::exp (-4.0f * t); break;                      // ramp
            }
            static const float jit[kTaps] = { 0.3f, -0.2f, 0.1f, 0.4f, -0.35f, 0.2f, -0.1f, 0.25f, -0.3f, 0.15f, 0.05f, -0.4f,
                                              0.35f, -0.15f, 0.2f, -0.25f, 0.1f, 0.3f, -0.05f, -0.3f, 0.4f, -0.2f, 0.15f, 0.0f };
            const float pos = (t + jit[k] * 0.5f / kTaps) * burstLen;
            out += burst.frac (std::max (2.0f, pos)) * g * ((k & 1) ? -1.0f : 1.0f);
        }
        return out * 0.42f;
    }

    // ---------------------------------------------------------------- helpers
    float swell (float x)
    {
        const float f = fastEnv.process (x), s = slowEnv.process (x);
        if (f > 2.0f * s + 1.0e-4f && swellPos > 0.25f) swellPos = 0;
        swellPos = std::min (1.0f, swellPos + 1.0f / swellSamples);
        const float target = smoothstep (swellPos);
        swellG = target < swellG ? target + (swellG - target) * swellFall : target;
        return swellG;
    }

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
        shimPhase -= std::floor (shimPhase);
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
    int type = Hall, pendingType = Hall, nLines = kLines, springs = 2, vowel = 0, shape = 0;
    bool feeding = false, freeze = false, lightMode = false;
    float len[kLines] {}, lenNow[kLines] {}, gains[kLines] {}, lenGlide = 0.0002f;
    const Vals* vals = nullptr;
    float decaySec = 3, diffK = 0.6f, diffScale = 1, erAmt = 0, modAmt = 0.3f, trim = 1, dampHz = 6000, lowCut = 180;
    float predelay = 0, duck = 0.2f, swellSamples = 0, grit = 0, ratio = 2, tension = 0.5f, voicesAmt = 0.7f;
    float echoTime = 14400, echoes = 0.4f, burstLen = 14400;
    float shimPhase = 0, lastOut = 0, tail = 0, lfo = 0, lfo2 = 0, lfo3 = 0, bloomFb = 0, echoFb = 0;
    float swellPos = 1, swellG = 1, swellFall = 0.99f, sw = 1;
    unsigned count = 0;
    DelayLine lines[kLines], pre, shift, echo, burst, springLine[kSprings];
    OnePole damp[kLines], hpf[kLines], echoLp, echoHp, springLp[kSprings], springHp[kSprings], outLp, outHp;
    Allpass diff[kDiff], bloomAp[3], springAp[kSprings][kSpringAp];
    Biquad shimHp, formant[3];
    Convolver conv;
    LoopLimiter lim, limEcho, limBloom, limSpring[kSprings];
    Follower duckEnv, fastEnv, slowEnv;
    Smoothed mixS, gateS, shimS;
};
} // namespace ks
