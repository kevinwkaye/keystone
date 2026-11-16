#pragma once
// Keystone delay: twelve types in one mono engine.
//
// Digital, Bucket, Tape, Dual, Pattern, Reverse, Ice, Swell, Trem, Filter, Lo-fi, Galaxy.
// Tape is a three-machine tape echo: Fixed heads (Time changes the tape speed, so it
// pitches as it moves), Multi heads (pairs of heads together) and Single (the record head
// slides, crossfading so it never pitches), each with modes A to C, plus tape age, wow and
// flutter, crinkle, bias, a spring tank on the output and input boost.
//
// Every type shares one feedback loop: filter, low cut, grit, smear, modulation and a loop
// limiter, so the Bloom sweep can push any of them close to self-oscillation safely.
// Lessons from Kevy does Devy: soft clip in the loop, filters inside the loop so repeats
// darken and thin as they go, duck from the dry signal, per-type trims so each type sits at
// the same level.

#include "Ambient.h"
#include "kdd/Blocks.h"

namespace ks
{
class Delay
{
public:
    enum Type { Digital = 0, Bucket, Tape, Dual, Pattern, Reverse, Ice, Swell, Trem, Filter, Lofi, Galaxy, kTypes };

    void prepare (float sr_)
    {
        sr = sr_;
        line.prepare ((int) (1.7f * sr) + 16);
        line2.prepare ((int) (1.7f * sr) + 16);
        for (auto& a : smear) a.prepare ((int) (0.04f * sr));
        for (auto& a : spring) a.prepare ((int) (0.01f * sr));
        springComb.prepare ((int) (0.06f * sr));
        rev.prepare ((int) (1.6f * sr));
        ice.prepare (sr, 0.08f);
        ice.setGrain (0.035f);
        timeS.setTime (0.25f, sr);
        time2S.setTime (0.25f, sr);
        mixS.setTime (0.03f, sr);
        fbS.setTime (0.08f, sr);
        gateS.setTime (0.15f, sr);
        trimS.setTime (0.05f, sr);
        inS.setTime (0.03f, sr);
        crinkleS.setTime (0.004f, sr);
        crinkleShiftS.setTime (0.006f, sr);
        extraModS.setTime (0.2f, sr);
        smearS.setTime (0.05f, sr);
        tapeS.setTime (0.15f, sr);
        lim.prepare (sr);
        lim2.prepare (sr);
        springLp.setLowPass (3800.0f, sr);
        duckEnv.set (0.005f, 0.25f, sr);
        fastEnv.set (0.001f, 0.03f, sr);
        slowEnv.set (0.08f, 0.3f, sr);
        swellFall = onePoleCoef (0.003f, sr);
        reset();
    }

    void reset()
    {
        line.reset(); line2.reset();
        for (auto& a : smear) a.reset();
        for (auto& a : spring) a.reset();
        springComb.reset();
        rev.clear(); ice.clear();
        lp.reset(); hp.reset(); lp2.reset(); hp2.reset(); springLp.reset();
        svf.reset(); crush.reset();
        duckEnv.reset(); fastEnv.reset(); slowEnv.reset();
        lfo = lfo2 = wowPhase = flutterPhase = tremPhase = sweepPhase = 0;
        mixS.reset (0); fbS.reset (0); gateS.reset (1); trimS.reset (1); inS.reset (1); crinkleS.reset (1);
        timeS.reset (0.42f * sr); time2S.reset (0.21f * sr);
        headNow = headNext = 0.42f * sr; xf = 1;
        swellPos = 1; swellG = 1; tail = 0; drift = 0;
    }

    void update (const Vals& v, float zone3, bool bloomActive, bool heelBack, bool cutTails)
    {
        const bool on = Vb (v, P::DlOn);
        pendingType = std::max (0, std::min ((int) kTypes - 1, Vi (v, P::DlType)));
        float mix = on ? V (v, P::DlMix) : 0.0f;
        float fb = V (v, P::DlFeedback);
        hold = on && Vb (v, P::DlHold);
        if (bloomActive && zone3 > 0)
        {
            const float reach = V (v, P::BloomWash);
            fb = fb + (1.02f * reach - fb) * zone3;   // past 1 only with the loop limiter
            mix = std::max (mix, zone3 * 0.45f * reach);
        }
        if (hold) fb = 1.0f;
        if (! on && ! hold && ! (bloomActive && zone3 > 0)) fb = std::min (fb, 0.6f);   // tails die away naturally
        mixS.set (mix);
        fbS.set (fb);

        static const float syncMul[4] = { 1.0f, 1.0f, 0.75f, 0.5f };
        const float t = clampf (V (v, P::DlTime) * syncMul[std::max (0, std::min (3, Vi (v, P::DlSync)))] * 0.001f * sr,
                                16.0f, (float) line.size() - 0.02f * sr);
        timeTarget = t;
        machine = Vi (v, P::DlTapeMachine);
        mode = Vi (v, P::DlTapeMode);
        // Fixed heads: Time is the tape speed, slow and pitchy. Everything else glides quickly.
        timeS.setTime (pendingType == Tape && machine == 0 ? 0.6f : 0.25f, sr);
        timeS.set (t);
        static const float dualRatio[4] = { 0.5f, 2.0f / 3.0f, 0.75f, 0.75f };
        const int dr = Vi (v, P::DlDualRatio);
        time2S.set (t * dualRatio[std::max (0, std::min (3, dr))]);
        dualSeries = Vi (v, P::DlDualSeries) == 1;
        pattern = Vi (v, P::DlPattern);

        tone = V (v, P::DlTone);
        grit = V (v, P::DlGrit);
        smearAmt = V (v, P::DlSmear);
        lowCut = V (v, P::DlLowCut);
        modSpeed = V (v, P::DlModSpeed);
        modDepth = V (v, P::DlMod);
        duck = V (v, P::DlDuck);
        tapeAge = V (v, P::DlTapeAge);
        wow = V (v, P::DlWow);
        crinkle = V (v, P::DlCrinkle);
        bias = V (v, P::DlBias);
        springAmt = V (v, P::DlSpring);
        inS.set (pendingType == Tape ? dbToGain (V (v, P::DlBoost)) : 1.0f);
        static const float iceRatio[3] = { 2.0f, 1.4983f, 2.9966f };
        iceR = iceRatio[std::max (0, std::min (2, Vi (v, P::DlIceInterval)))];
        iceBlend = V (v, P::DlIceBlend);
        riseSamples = V (v, P::DlSwellRise) * 0.001f * sr;
        tremSpeed = V (v, P::DlTremSpeed);
        tremDepth = V (v, P::DlTremDepth);
        tremChoppy = Vi (v, P::DlTremShape) == 1;
        sweepSpeed = V (v, P::DlFiltSpeed);
        filtRes = V (v, P::DlFiltRes);
        bits = V (v, P::DlLofiBits);
        lofiRate = V (v, P::DlLofiRate);


        // level matched: every type sits about where Digital does
        static const float trims[kTypes] = { 1.0f, 1.12f, 1.1f, 0.95f, 1.0f, 1.0f, 1.05f, 1.1f, 1.15f, 1.2f, 1.05f, 1.0f };
        trimS.set (trims[pendingType]);

        gateS.set (heelBack && cutTails ? 0.0f : 1.0f);
        feeding = (on && ! hold) || (bloomActive && zone3 > 0);
        anyOn = on || (bloomActive && zone3 > 0);
        updateFilters();
    }

    bool active() const { return mixS.value > 1.0e-4f || mixS.target > 0 || tail > 1.0e-5f; }

    // returns the wet signal scaled by mix
    float process (float x)
    {
        // a new type: the repeats dip for 10 ms while the read heads change over
        if (pendingType != type)
        {
            swG -= 1.0f / (0.01f * sr);
            if (swG <= 0) { swG = 0; type = pendingType; svf.reset(); crush.reset(); xf = 1; headNow = timeTarget; }
        }
        else if (swG < 1) swG = std::min (1.0f, swG + 1.0f / (0.01f * sr));
        const float fb = fbS.next();
        if ((count++ & 15) == 0) updateLoopFilters (fb);

        // ---- input ----
        float in = feeding ? x * inS.next() : 0.0f;
        const float dEnv = duckEnv.process (x);
        const float sw = swell (x);   // always listening, so switching to Swell is smooth
        if (type == Swell || type == Galaxy) in *= sw;

        // ---- modulation ----
        lfo += modSpeed / sr;
        if (lfo >= 1) lfo -= 1;
        lfo2 += 0.37f / sr;
        if (lfo2 >= 1) lfo2 -= 1;
        float modSamples = modDepth * 0.003f * sr * std::sin (kTwoPi * lfo);
        // built-in chorus for the bucket brigade and Galaxy, faded in and out with the type
        extraModS.set (type == Bucket ? 0.0006f * sr : (type == Galaxy ? 0.0009f * sr : 0.0f));
        modSamples += extraModS.next() * std::sin (kTwoPi * lfo2);
        tapeS.set (type == Tape ? 1.0f : 0.0f);
        if (type == Tape || tapeS.value > 1.0e-4f) modSamples += tapeWobble() * tapeS.next();

        // ---- read ----
        const float t = timeS.next();
        float y = 0, fbSig = 0;
        switch (type)
        {
            case Tape: readTape (t, modSamples, y, fbSig); break;
            case Dual: readDual (t, modSamples, y, fbSig, in, fb); break;
            case Pattern: readPattern (t, modSamples, y, fbSig); break;
            default: y = fbSig = line.hermite (std::max (2.0f, t + modSamples)); break;
        }
        if (type != Dual)
        {
            // keep the second line and the reverse reader listening, so switching type is smooth
            time2S.next();
            line2.push (in);
        }
        const float reversed = rev.process (y, t);

        // ---- the loop ----
        float loop = loopProcess (fbSig);
        const float up = ice.process (loop, iceR);   // always fed, so switching to Ice is smooth
        if (type == Ice) loop = loop + (up - loop) * iceBlend;
        loop = lim.process (loop * fb);
        const float g = gateS.next();
        line.push (in + loop * g * swG);

        // ---- output colour ----
        float wet = y;
        switch (type)
        {
            case Reverse: wet = reversed; break;
            case Trem: wet *= trem(); break;
            case Filter: wet = sweep (wet); break;
            default: break;
        }
        if (type == Tape && springAmt > 0.001f) wet += springTank (wet * swG) * springAmt;

        // duck from the dry signal, so repeats bloom in the gaps
        const float dk = 1.0f - duck * 0.9f * clampf (dEnv * 8.0f, 0.0f, 1.0f);
        tail = std::max (std::fabs (wet), tail * 0.9999f);
        return wet * dk * swG * trimS.next() * mixS.next() * g;
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

    struct PitchUp
    {
        kdd::PitchShifter ps;
        float lastOut = 0;
        void prepare (float sr, float maxGrain) { ps.prepare (sr, maxGrain); }
        void setGrain (float s) { ps.setGrain (s); }
        void clear() { ps.clear(); lastOut = 0; }
        float process (float x, float r) { lastOut = ps.process (x, r); return lastOut; }
    };

    void updateFilters()
    {
        svfRes = 0.2f + 0.75f * filtRes;
    }

    void updateLoopFilters (float fb)
    {
        const float f = std::max (fb, 0.0f);
        float cut = 12000.0f * std::pow (0.1f, tone);
        float hpf = lowCut + (hold ? 0.0f : 200.0f * std::min (1.0f, f));
        if (type == Bucket)
        {
            // a bucket brigade gets darker the longer the delay
            const float ms = timeTarget * 1000.0f / sr;
            cut = std::min (cut, 5200.0f * std::pow (300.0f / std::max (60.0f, ms), 0.35f));
        }
        if (type == Tape)
        {
            cut = std::min (cut, 9000.0f * std::pow (0.3f, tapeAge));
            hpf += 120.0f * tapeAge;
        }
        if (type == Lofi) cut = std::min (cut, 0.45f * lofiRate);
        if (! hold) cut *= 1.0f - 0.35f * std::min (1.0f, f);   // darker as repeats rise
        lp.setLowPass (cut, sr);
        lp2.setLowPass (cut, sr);
        hp.setLowPass (hpf, sr);
        hp2.setLowPass (hpf, sr);
    }

    float loopProcess (float s)
    {
        s = hp.hp (lp.lp (s));
        // grit: tape bias, bucket clipping or plain warmth
        float drive = 1.0f + 5.0f * grit;
        if (type == Bucket) drive += 1.5f;
        if (type == Tape)
        {
            drive *= 1.0f + 2.5f * std::max (0.0f, 0.5f - bias);   // under-biased tape crunches
            s += (bias - 0.5f) * 0.25f * s * std::fabs (s);           // and leans one way
        }
        if (drive > 1.01f) s = std::tanh (s * drive) / drive;   // unity for small signals, rounds off the peaks
        if (type == Lofi) s = crush.process (s, lofiRate / sr, bits);
        smearS.set (type == Galaxy ? std::max (smearAmt, 0.6f) : smearAmt);
        const float sm = smearS.next();
        if (sm > 0.01f)
        {
            const float k = 0.7f * sm;
            static const float lens[3] = { 7.3f, 11.9f, 17.3f };
            const float scale = 1.0f;
            for (int i = 0; i < 3; ++i) s = smear[i].process (s, lens[i] * 0.001f * sr * scale, k);
        }
        return s;
    }

    float tapeWobble()
    {
        // wow: slow, drifting; flutter: fast and small; crinkle: odd blips of worn tape
        wowPhase += (0.45f + 0.15f * drift) / sr;
        if (wowPhase >= 1) wowPhase -= 1;
        flutterPhase += 6.3f / sr;
        if (flutterPhase >= 1) flutterPhase -= 1;
        drift += (rng.bi() - drift) * (1.0f / sr);
        float m = wow * (0.0018f * sr * std::sin (kTwoPi * wowPhase) + 0.00025f * sr * std::sin (kTwoPi * flutterPhase));
        if (crinkle > 0.001f)
        {
            if (crinkleLeft > 0) --crinkleLeft;
            else if (rng.uni() < crinkle * 18.0f / sr)
            {
                crinkleLeft = (int) ((0.004f + 0.03f * rng.uni()) * sr);
                crinkleDepth = 0.25f + 0.5f * rng.uni() * crinkle;
                crinkleShift = (rng.bi()) * crinkle * 0.0008f * sr;
            }
            crinkleShiftS.set (crinkleLeft > 0 ? crinkleShift : 0.0f);
            crinkleS.set (crinkleLeft > 0 ? 1.0f - crinkleDepth : 1.0f);
            m += crinkleShiftS.next();
        }
        return m;
    }

    // reads one tape head at a fraction of the time
    float head (float t, float frac, float mod) const { return line.hermite (std::max (2.0f, t * frac + mod)); }

    void readTape (float t, float mod, float& y, float& fbSig)
    {
        static const float heads[3][3][3] = {
            // Fixed: A one head, B two, C three (an echo machine with all heads up)
            { { 1.0f, 0, 0 }, { 0.5f, 1.0f, 0 }, { 1.0f / 3, 2.0f / 3, 1.0f } },
            // Multi: pairs of heads together
            { { 1.0f / 3, 2.0f / 3, 0 }, { 2.0f / 3, 1.0f, 0 }, { 1.0f / 3, 1.0f, 0 } },
            // Single: one sliding record head, B and C add the half and three-quarter heads
            { { 1.0f, 0, 0 }, { 0.5f, 1.0f, 0 }, { 0.75f, 1.0f, 0 } },
        };
        const int m = std::max (0, std::min (2, machine)), md = std::max (0, std::min (2, mode));
        float tt = t;
        if (m == 2)
        {
            // the record head slides: crossfade between positions, no pitch smear
            if (xf >= 1.0f && std::fabs (timeTarget - headNow) > 0.01f * headNow) { headNext = timeTarget; xf = 0; }
            if (xf < 1.0f) { xf = std::min (1.0f, xf + 1.0f / (0.06f * sr)); if (xf >= 1.0f) headNow = headNext; }
            tt = headNow;
        }
        float sum = 0;
        int n = 0;
        for (int k = 0; k < 3; ++k)
        {
            const float f = heads[m][md][k];
            if (f <= 0) continue;
            float s = head (tt, f, mod);
            if (m == 2 && xf < 1.0f) s = s * (1.0f - xf) + head (headNext, f, mod) * xf;
            sum += s * (k == 2 || heads[m][md][k + 1] <= 0 ? 1.0f : 0.8f);
            ++n;
        }
        const float c = crinkleS.next();
        y = sum * c / std::sqrt ((float) n);
        fbSig = sum * c / (float) n;
    }

    void readDual (float t, float mod, float& y, float& fbSig, float in, float fb)
    {
        const float t2 = time2S.next();
        const float a = line.hermite (std::max (2.0f, t + mod));
        const float b = line2.hermite (std::max (2.0f, t2 - mod));
        // second line: fed alongside the first, or by the first one's repeats
        float l2 = loop2 (b);
        l2 = lim2.process (l2 * fb);
        line2.push ((dualSeries ? a : in) + l2 * gateS.value * swG);
        y = (a + b) * 0.72f;
        fbSig = a;
    }
    float loop2 (float s)
    {
        s = hp2.hp (lp2.lp (s));
        return s;
    }

    void readPattern (float t, float mod, float& y, float& fbSig)
    {
        // taps across one beat of Time, the last tap feeds back
        static const float taps[4][4] = {
            { 0.5f, 0.75f, 1.0f, 0 },          // gallop
            { 1.0f / 3, 2.0f / 3, 1.0f, 0 },   // triplet
            { 2.0f / 3, 1.0f, 0, 0 },          // shuffle
            { 0.25f, 0.5f, 0.75f, 1.0f },      // cascade
        };
        static const float gains[4][4] = {
            { 0.8f, 0.6f, 1.0f, 0 }, { 0.7f, 0.85f, 1.0f, 0 }, { 0.7f, 1.0f, 0, 0 }, { 0.45f, 0.6f, 0.8f, 1.0f },
        };
        const int p = std::max (0, std::min (3, pattern));
        float sum = 0;
        for (int k = 0; k < 4; ++k)
        {
            const float f = taps[p][k];
            if (f <= 0) break;
            const float s = line.hermite (std::max (2.0f, t * f + mod));
            sum += s * gains[p][k];
            if (f >= 1.0f) fbSig = s;
        }
        y = sum * 0.62f;
    }

    // Swell: each new note fades in, so attacks vanish and the repeats bloom
    float swell (float x)
    {
        const float f = fastEnv.process (x), s = slowEnv.process (x);
        if (f > 2.0f * s + 1.0e-4f && swellPos > 0.25f) swellPos = 0;
        swellPos = std::min (1.0f, swellPos + 1.0f / std::max (32.0f, riseSamples));
        const float target = smoothstep (swellPos);
        // drop in 3 ms rather than at once, so the cut itself never clicks
        swellG = target < swellG ? target + (swellG - target) * swellFall : target;
        return swellG;
    }

    float trem()
    {
        tremPhase += tremSpeed / sr;
        if (tremPhase >= 1) tremPhase -= 1;
        float w = 0.5f + 0.5f * std::sin (kTwoPi * tremPhase);
        if (tremChoppy) w = smoothstep (clampf ((w - 0.5f) * 6.0f + 0.5f, 0.0f, 1.0f));
        return 1.0f - tremDepth * w;
    }

    float sweep (float x)
    {
        sweepPhase += sweepSpeed / sr;
        if (sweepPhase >= 1) sweepPhase -= 1;
        const float m = 0.5f - 0.5f * std::cos (kTwoPi * sweepPhase);
        if ((count & 7) == 0) svf.set (350.0f * std::pow (2.0f, m * 3.8f), svfRes, sr);
        float lo, bp;
        svf.process (x, lo, bp);
        return lo * 0.7f + bp * 0.6f * (0.5f + svfRes);
    }

    // A small spring tank: dispersive all-passes and a short metallic comb
    float springTank (float x)
    {
        float s = x;
        static const float lens[4] = { 1.3f, 2.1f, 3.4f, 4.7f };
        for (int i = 0; i < 4; ++i) s = spring[i].process (s, lens[i] * 0.001f * sr, 0.62f);
        const float c = springComb.frac (0.037f * sr);
        springComb.push (s + springLp.lp (c) * 0.72f);
        return c * 0.8f;
    }

    float sr = 48000;
    int type = 0, pendingType = 0, machine = 0, mode = 0, pattern = 0;
    bool feeding = false, anyOn = false, hold = false, dualSeries = false, tremChoppy = false;
    float tone = 0.3f, grit = 0.2f, smearAmt = 0.2f, lowCut = 140, modSpeed = 0.6f, modDepth = 0.2f, duck = 0.3f;
    float tapeAge = 0.4f, wow = 0.35f, crinkle = 0.1f, bias = 0.5f, springAmt = 0.2f;
    float iceR = 2.0f, iceBlend = 0.6f, riseSamples = 19200, tremSpeed = 4, tremDepth = 0.6f;
    float sweepSpeed = 0.4f, filtRes = 0.5f, svfRes = 0.5f, bits = 8, lofiRate = 8000;
    float timeTarget = 20160, headNow = 20160, headNext = 20160, xf = 1;
    float lfo = 0, lfo2 = 0, wowPhase = 0, flutterPhase = 0, tremPhase = 0, sweepPhase = 0, drift = 0;
    float swellPos = 1, swellG = 1, swellFall = 0.99f, tail = 0, swG = 1;
    int crinkleLeft = 0;
    float crinkleDepth = 0, crinkleShift = 0;
    unsigned count = 0;
    DelayLine line, line2, springComb;
    Allpass smear[3], spring[4];
    OnePole lp, hp, lp2, hp2, springLp;
    Svf svf;
    kdd::Crusher crush;
    kdd::ReverseReader rev;
    PitchUp ice;
    LoopLimiter lim, lim2;
    Follower duckEnv, fastEnv, slowEnv;
    Smoothed smearS, tapeS, extraModS, crinkleShiftS, timeS, time2S, mixS, fbS, gateS, trimS, inS, crinkleS;
    Rng rng;
};
} // namespace ks
