#pragma once
// Keystone DSP modules. Each one is plain C++ that runs on the audio thread:
// coefficients are updated once per block from the parameter snapshot, the work is
// per sample. Nothing allocates after prepare().

#include "bit/BitrotEngine.h"
#include "Dsp.h"
#include "keystone/Params.h"

#include <array>

namespace ks
{
using Vals = std::array<float, kNumParams>;
inline float V (const Vals& v, P p) { return v[(size_t) p]; }
inline int Vi (const Vals& v, P p) { return (int) std::lround (v[(size_t) p]); }
inline bool Vb (const Vals& v, P p) { return v[(size_t) p] > 0.5f; }

// ---------------------------------------------------------------------------------------
// How loud Kevin is playing, learnt from note peaks (the Quack and Speckle lessons):
// halfway towards a louder note, a third of the way towards a softer one, holding
// through silence. A swell's peak is where it levels off.
struct PlayLevel
{
    float level = 0.05f;           // linear peak of a typical note
    float riseMax = 0, env = 0, holdLeft = 0;
    float att = 0, rel = 0, holdSamples = 0, stallSamples = 0, sinceRise = 0;
    bool rising = false;

    void prepare (float sr, float holdSec = 0.045f)
    {
        att = onePoleCoef (0.002f, sr);
        rel = onePoleCoef (0.12f, sr);
        holdSamples = holdSec * sr;
        stallSamples = 0.25f * sr;
        reset();
    }
    void reset() { level = 0.05f; riseMax = env = holdLeft = sinceRise = 0; rising = false; }

    // returns the smoothed envelope
    float process (float x)
    {
        const float a = std::fabs (x);
        if (a >= env) { env = a + (env - a) * att; holdLeft = holdSamples; }
        else if (holdLeft > 0) holdLeft -= 1;
        else env = a + (env - a) * rel;
        env = undenorm (env);

        if (env > riseMax * 1.02f && env > 1.0e-4f)
        {
            riseMax = env;
            rising = true;
            sinceRise = 0;
        }
        else if (rising)
        {
            sinceRise += 1;
            if (env < riseMax * 0.7f || sinceRise > stallSamples)
            {
                learn (riseMax);
                rising = false;
                riseMax = env;
            }
        }
        else riseMax = std::min (riseMax, env * 1.0f + 1.0e-6f);
        return env;
    }
    void learn (float peak)
    {
        if (peak < 3.0e-4f) return;   // ignore noise floor bumps (about -70 dBFS)
        level += (peak - level) * (peak > level ? 0.5f : 0.333f);
    }
    // Quiet means 23 dB under the playing level, with a floor near -94 dBFS.
    float quietThreshold() const { return std::max (level * 0.0708f, 2.0e-5f); }
};

// ---------------------------------------------------------------------------------------
// Low band: tight shelf, weight, light compression with slap mode.
class LowBand
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        slapMix.setTime (0.05f, sr);
        onMix.setTime (0.02f, sr);
        detAtt = onePoleCoef (0.012f, sr);
        gainGlide = onePoleCoef (0.004f, sr);
        reset();
    }
    void reset() { shelf.reset(); weight.reset(); env = 0; gainDb = 0; slapMix.reset (0); onMix.reset (1); grSmooth = 0; }

    void update (const Vals& v)
    {
        const float tight = V (v, P::SplitTight), w = V (v, P::SplitWeight);
        if (tight != lastTight) { shelf.lowShelf (45.0f, tight, sr, 0.7f); lastTight = tight; }
        if (w != lastWeight) { weight.peak (80.0f, 0.8f, w, sr); lastWeight = w; }
        amount = V (v, P::CompAmount);
        slapAmount = V (v, P::CompSlapAmount);
        relSec = V (v, P::CompRelease) * 0.001f;
        slapMix.set (Vb (v, P::CompSlap) ? 1.0f : 0.0f);
        onMix.set (Vb (v, P::CompOn) ? 1.0f : 0.0f);
    }

    float process (float x)
    {
        x = weight.process (shelf.process (x));

        const float s = slapMix.next(), on = onMix.next();
        // normal: gentle; slap: firm and fast
        const float thr = lerp (-14.0f - 16.0f * amount, -16.0f - 18.0f * slapAmount, s);
        const float ratio = lerp (1.5f + 2.5f * amount, 3.0f + 5.0f * slapAmount, s);
        const float att = lerp (detAtt, onePoleCoef (0.002f, sr), s);
        const float rel = onePoleCoef (lerp (relSec, relSec * 0.6f, s), sr);

        const float p = x * x;
        env = p > env ? p + (env - p) * att : p + (env - p) * rel;
        env = undenorm (env);
        const float lvl = 10.0f * std::log10 (env + 1.0e-12f);
        // soft knee, 6 dB wide
        const float over = lvl - thr;
        float gr = 0;
        if (over > 3.0f) gr = over * (1.0f - 1.0f / ratio);
        else if (over > -3.0f) gr = (over + 3.0f) * (over + 3.0f) / 12.0f * (1.0f - 1.0f / ratio);
        const float makeup = lerp (0.5f, 0.62f, s) * std::max (0.0f, (-8.0f - thr)) * (1.0f - 1.0f / ratio);
        // glide the gain itself, so a change of settings (slap on) never steps it
        const float gDb = (makeup - gr) * on;
        gainDb = gDb + (gainDb - gDb) * gainGlide;
        grSmooth = gr * on;
        return x * dbToGain (gainDb);
    }
    float reduction() const { return grSmooth; }

private:
    float sr = 48000;
    Biquad shelf, weight;
    float lastTight = 99, lastWeight = 99;
    float amount = 0.35f, slapAmount = 0.7f, relSec = 0.15f;
    float env = 0, detAtt = 0, grSmooth = 0, gainDb = 0, gainGlide = 0;
    Smoothed slapMix, onMix;
};

// ---------------------------------------------------------------------------------------
// Mono-voiced dimension: two short modulated delays with LFOs a quarter cycle apart, so
// the movement survives in mono (the real one's movement comes from stereo and cancels).
class MonoDimension
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        line.prepare ((int) (0.03f * sr));
        bbd.setLowPass (7000.0f, sr);
        mixS.setTime (0.03f, sr);
        reset();
    }
    void reset() { line.reset(); bbd.reset(); phase = 0; mixS.reset (0); }

    void setMode (int mode)   // 0..3 = modes 1..4
    {
        static const float rates[4] = { 0.28f, 0.45f, 0.55f, 0.8f };
        static const float depths[4] = { 0.35f, 0.6f, 1.1f, 1.6f };
        mode = std::max (0, std::min (3, mode));
        rate = rates[mode];
        depth = depths[mode] * 0.001f * sr;
    }
    void setMix (float m) { mixS.set (m); }

    float process (float x)
    {
        line.push (x);
        phase += rate / sr;
        if (phase >= 1.0f) phase -= 1.0f;
        const float ta = tri (phase), tb = tri (phase + 0.25f);
        const float a = line.hermite (baseA * 0.001f * sr + depth * ta);
        const float b = line.hermite (baseB * 0.001f * sr + depth * tb);
        const float wet = bbd.lp (0.5f * (a + b));
        const float m = mixS.next();
        return x * (1.0f - 0.35f * m) + wet * 0.85f * m;
    }

private:
    static float tri (float p) { p -= std::floor (p); return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; }
    float sr = 48000, phase = 0, rate = 0.45f, depth = 20;
    const float baseA = 6.5f, baseB = 9.5f;
    DelayLine line;
    OnePole bbd;
    Smoothed mixS;
};

// ---------------------------------------------------------------------------------------
// Bitrot: Kevin's own Bitrot plugin engine (a modulated monophonic PLL, after the Data
// Corrupter), vendored in core/src/bit. It hears the whole bass and plays a square wave,
// a divided sub and a multiplied oscillator back from it. Its own gate follows how loud
// you play, so it stops dead between notes rather than tracking hiss.
class Bitrot
{
public:
    void prepare (double sampleRate)
    {
        eng.prepare (sampleRate);
        right.assign (kChunk, 0.0f);
    }
    void reset() { /* the engine clears itself in prepare */ }
    void update (const Vals& v)
    {
        p.square = V (v, P::BitrotSquare);
        p.sub = V (v, P::BitrotSub);
        p.osc = V (v, P::BitrotOsc);
        p.dry = 0.0f;   // Keystone blends the clean bass itself (Clean bass)
        p.level = V (v, P::BitrotVol);
        p.subInterval = Vi (v, P::BitrotSubInt);
        p.subRoot = Vi (v, P::BitrotSubRoot);
        p.subMix = Vi (v, P::BitrotSubMix);
        p.floor = Vb (v, P::BitrotFloor);
        p.floorHz = Vi (v, P::BitrotFloorHz);
        p.oscInterval = Vi (v, P::BitrotOscInt);
        p.oscRoot = Vi (v, P::BitrotOscRoot);
        p.bend = V (v, P::BitrotBend);
        p.fmMode = Vi (v, P::BitrotFm);
        p.rate = V (v, P::BitrotFmRate);
        p.depth = V (v, P::BitrotFmDepth);
        p.corrupt = V (v, P::BitrotCorrupt);
        p.gate = V (v, P::BitrotGate);
        p.input = 1;   // bass
        p.trackComp = true;
    }
    // in place, mono: buf holds the whole bass in, the synth out
    void process (float* buf, int n)
    {
        for (int at = 0; at < n; at += kChunk)
        {
            const int m = std::min (kChunk, n - at);
            std::copy (buf + at, buf + at + m, right.begin());
            eng.process (buf + at, right.data(), m, p);
        }
    }
    float lock() const { return eng.uiLock.load (std::memory_order_relaxed); }

private:
    static constexpr int kChunk = 256;
    bit::BitrotEngine eng;
    bit::EngineParams p;
    std::vector<float> right;
};

// ---------------------------------------------------------------------------------------
// Preamp: an SVT-style growl and a Darkglass-style cut side by side, time-aligned (no
// oversampling either side), blended with one control. Small signals pass at unity.
class Preamp
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        blendS.setTime (0.03f, sr);
        reset();
    }
    void reset()
    {
        for (auto* b : { &gPeak, &gDip, &gLp, &cHp, &cPeak, &cLp, &detail }) b->reset();
        gSat.reset(); cSat.reset(); blendS.reset (0.7f);
    }
    void update (const Vals& v)
    {
        const float gf = V (v, P::PreGrowlFreq), g = V (v, P::PreGrowl), mf = V (v, P::PreMidFreq), m = V (v, P::PreMid);
        const float d = V (v, P::PreDrive), det = V (v, P::PreDetail);
        if (gf != lgf || g != lg) { gPeak.peak (gf, 0.8f, 9.0f * g, sr); lgf = gf; lg = g; }
        if (mf != lmf || m != lm) { cPeak.peak (mf, 1.1f, 10.0f * m, sr); lmf = mf; lm = m; }
        if (det != ldet) { detail.peak (3000.0f, 0.7f, det, sr); ldet = det; }
        if (! init)
        {
            gDip.peak (400.0f, 1.0f, -3.0f, sr);   // keep out of the guitars' 250 to 500 Hz
            gLp.lowPass (4500.0f, 0.707f, sr);
            cHp.highPass (140.0f, 0.707f, sr);
            cLp.lowPass (7000.0f, 0.707f, sr);
            init = true;
        }
        gDrive = dbToGain (2.0f + 16.0f * d);
        cDrive = dbToGain (6.0f + 22.0f * d);
        blendS.set (V (v, P::PreBlend));
        level = dbToGain (V (v, P::PreLevel));
    }
    float process (float x)
    {
        // growl
        float g = gPeak.process (x);
        g = gSat.process (g * gDrive + 0.08f) - 0.0798f;
        g = gLp.process (gDip.process (g / gDrive * 1.12f));
        // cut
        float c = cPeak.process (cHp.process (x));
        c = cSat.process (c * cDrive) / cDrive * 1.25f;
        c = cLp.process (c);
        const float b = blendS.next();
        return detail.process ((1.0f - b) * g + b * c) * level;
    }

private:
    float sr = 48000, gDrive = 1, cDrive = 1, level = 1;
    float lgf = -1, lg = -1, lmf = -1, lm = -1, ldet = -99;
    bool init = false;
    Biquad gPeak, gDip, gLp, cHp, cPeak, cLp, detail;
    AdaaTanh gSat, cSat;
    Smoothed blendS;
};

// ---------------------------------------------------------------------------------------
// Envelope filter, from Quack's engine: the sweep follows each note against the playing
// level learnt from note peaks, glides through two one-pole stages, held 45 ms for bass.
class EnvFilter
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        det.prepare (sr, 0.045f);
        glideA = glideB = std::log (600.0f);
        glide = onePoleCoef (0.012f, sr);
        mixS.setTime (0.02f, sr);
        envAtt = onePoleCoef (0.002f, sr);
        envRel = onePoleCoef (0.6f, sr);
        makeupGlide = 1.0f - onePoleCoef (0.25f, sr);
        reset();
    }
    void reset() { svf.reset(); det.reset(); env = 0; mixS.reset (1); counter = 0; }
    void update (const Vals& v)
    {
        manual = Vi (v, P::FiltMode) == 1;
        bandPass = Vi (v, P::FiltType) == 1;
        down = Vi (v, P::FiltDir) == 1;
        sens = V (v, P::FiltSens);
        lo = V (v, P::FiltLow);
        hi = std::max (lo * 1.2f, V (v, P::FiltHigh));
        res = V (v, P::FiltQ);
        pos = V (v, P::FiltPos);
        relCoef = onePoleCoef (0.04f + 0.36f * (1.0f - V (v, P::FiltSpeed)), sr);
        mixS.set (V (v, P::FiltMix));
    }
    float process (float x)
    {
        const float e = det.process (x);
        env = e > env ? e : e + (env - e) * relCoef;   // det already holds and attacks
        float p;
        if (manual) p = pos;
        else
        {
            const float rel = env / std::max (det.level, 1.0e-4f);
            p = clampf (std::pow (rel, 0.6f) * (0.4f + 1.2f * sens), 0.0f, 1.0f);
            if (down) p = 1.0f - p;
        }
        const float target = std::log (lo) + (std::log (hi) - std::log (lo)) * p;
        glideA = target + (glideA - target) * glide;
        glideB = glideA + (glideB - glideA) * glide;
        if ((counter++ & 7) == 0) svf.set (std::exp (glideB), 0.15f + 0.8f * res, sr);
        float l, b;
        svf.process (x, l, b);
        float wet = bandPass ? b * (1.0f + 1.5f * res) : l;
        // a band-pass drops the fundamental, so bring the wet back to the level you played
        const float ax = std::fabs (x), aw = std::fabs (wet);
        inEnv = ax > inEnv ? ax + (inEnv - ax) * envAtt : ax + (inEnv - ax) * envRel;
        wetEnv = aw > wetEnv ? aw + (wetEnv - aw) * envAtt : aw + (wetEnv - aw) * envRel;
        const float want = inEnv > 1.0e-3f && wetEnv > 1.0e-6f ? clampf (1.0f * inEnv / wetEnv, 1.0f, 4.0f) : 1.0f;
        makeup += (want - makeup) * makeupGlide;
        wet *= makeup;
        wet = std::tanh (wet * 1.5f) / 1.5f;   // resonant peaks rounded off, not spiking the amp
        const float m = mixS.next();
        return x * (1.0f - m) + wet * m;
    }
    float position() const { return glideB; }   // log Hz
    float position01() const { return (glideB - std::log (lo)) / std::max (1.0e-3f, std::log (hi) - std::log (lo)); }

private:
    float sr = 48000;
    Svf svf;
    PlayLevel det;
    float env = 0, relCoef = 0, glide = 0, glideA = 0, glideB = 0;
    bool manual = false, bandPass = false, down = false;
    float sens = 0.5f, lo = 300, hi = 2200, res = 0.5f, pos = 0.5f;
    float inEnv = 0, wetEnv = 0, makeup = 2.5f, envAtt = 0.99f, envRel = 0.9999f, makeupGlide = 0.0001f;
    unsigned counter = 0;
    Smoothed mixS;
};

// ---------------------------------------------------------------------------------------
// Output limiter: fast peak limiter at -1 dBFS plus a soft ceiling for anything faster.
class Limiter
{
public:
    void prepare (float sr)
    {
        att = onePoleCoef (0.0003f, sr);
        rel = onePoleCoef (0.12f, sr);
        gain = 1;
    }
    float process (float x, bool on)
    {
        const float ceiling = 0.891f;   // -1 dBFS
        const float a = std::fabs (x);
        const float want = a > ceiling ? ceiling / a : 1.0f;
        gain = want < gain ? want + (gain - want) * att : want + (gain - want) * rel;
        float y = on ? x * gain : x;
        // soft ceiling: transparent below 0.85, never past 0.98
        const float ay = std::fabs (y);
        if (ay > 0.85f) y = std::copysign (0.85f + 0.13f * std::tanh ((ay - 0.85f) / 0.13f), y);
        return y;
    }
    float reductionDb() const { return -gainToDb (gain); }

private:
    float att = 0, rel = 0, gain = 1;
};
} // namespace ks
