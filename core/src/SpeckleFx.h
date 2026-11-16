#pragma once
// Speckle's eleven effects (Mosaic, Seq, Glide, Haze, Tunnel, Strum, Blocks, Interrupt,
// Arp, Pattern, Warp, each with variations A to D) running inside Keystone.
//
// Kept up top in the mix: the audio is pitched up (an octave, an octave and a fifth or two
// octaves, always in key) before Speckle hears it, so every effect works on material that
// already sits above the band. The caller high-passes and caps the output afterwards.
// Mono in, mono out: Speckle's stereo pair is fed the same signal and folded back down.

#include "Modules.h"
#include "spk/Detector.h"
#include "spk/Effects.h"
#include "spk/MultiDelay.h"

namespace ks
{
// Pitches a signal up by a fixed ratio with two crossfading read heads. The input is
// band-limited first, so nothing folds back above the new Nyquist, and the lows are cut
// so octaves of bass notes never turn to mud (the Kevy does Devy lesson).
class UpShifter
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        line.prepare ((int) (0.25f * sr));
        hp.highPass (150.0f, 0.707f, sr);
        setRatio (2.0f);
        phase = 0;
    }
    void reset() { line.reset(); hp.reset(); lp1.reset(); lp2.reset(); phase = 0; }
    void setRatio (float r)
    {
        if (std::fabs (r - ratio) < 1.0e-4f) return;
        ratio = r;
        const float fc = std::min (0.45f * sr / ratio, 16000.0f);
        lp1.lowPass (fc, 0.541f, sr);
        lp2.lowPass (fc, 1.307f, sr);
    }
    float process (float x)
    {
        line.push (lp2.process (lp1.process (hp.process (x))));
        const float win = 0.05f * sr;   // 50 ms windows: smooth, no audible flutter
        phase += (ratio - 1.0f) / win;
        phase -= std::floor (phase);
        float s = 0;
        for (int k = 0; k < 2; ++k)
        {
            float p = phase + 0.5f * k;
            p -= std::floor (p);
            const float w = std::sin (kPi * p);
            s += line.hermite ((1.0f - p) * win + 4.0f) * w * w;
        }
        return s;
    }

private:
    float sr = 48000, ratio = 0, phase = 0;
    DelayLine line;
    Biquad hp, lp1, lp2;
};

class SpeckleFx
{
public:
    static constexpr int kBlock = spk::EffectBank::block;

    void prepare (float sr_)
    {
        sr = sr_;
        detector.prepare (sr);
        hist.prepare (sr, 8.0f);
        fx.prepare (sr);
        delay.prepare (sr);
        shifter.prepare (sr);
        onsets.clear();
        env = envAtOnset = playLevel = 0;
        sinceOnset = 1 << 29;
        envAtt = 1.0f - std::exp (-1.0f / (0.005f * sr));
        envRel = 1.0f - std::exp (-1.0f / (0.25f * sr));
        levelRel = std::exp (-1.0f / (3.0f * sr));
        trimS.setTime (0.03f, sr);
        passS.setTime (0.03f, sr);
        dOutS.setTime (0.03f, sr);
        fbBuf.assign (kBlock, 0.0f);
        lastEffect = -1;
    }

    void reset()
    {
        hist.clear();
        fx.reset();
        fx.clearGrains();
        shifter.reset();
        onsets.clear();
        std::fill (fbBuf.begin(), fbBuf.end(), 0.0f);
    }

    struct Settings
    {
        int effect = spk::Haze, variant = 0;
        float activity = 0.5f, repeats = 0.45f, shape = 0.5f;
        bool reverse = false;
        float ratio = 2.0f;
        float beatSamples = 24000;
        bool scatter = false;
        float scatterAmount = 0.4f;
    };

    // Wet signal, mono, n <= kBlock at a time is handled inside.
    void process (const float* src, float* out, int n, const Settings& s)
    {
        for (int start = 0; start < n; start += kBlock)
            processBlock (src + start, out + start, std::min (kBlock, n - start), s);
    }

    int grainCount() const { return fx.grainCount(); }

private:
    void processBlock (const float* src, float* out, int n, const Settings& s)
    {
        // Scatter: a new in-key interval every beat or so, picked at random
        if (s.scatter)
        {
            scatterLeft -= n;
            if (scatterLeft <= 0)
            {
                static const float ratios[3] = { 2.0f, 3.0f, 4.0f };
                currentRatio = ratios[(int) (rng.uni() * 3.0f) % 3];
                scatterLeft = (int) (s.beatSamples * (0.5f + rng.uni() * (0.5f + s.scatterAmount)));
            }
        }
        else currentRatio = s.ratio;
        shifter.setRatio (currentRatio);

        float in[kBlock];
        const float fb = fx.feedback();
        for (int i = 0; i < n; ++i)
        {
            const float x = shifter.process (src[i]);
            in[i] = x;
            const float on = detector.process (x);
            if (on > 0.0f) addOnset (on);
            else sustainedNote (x, s.beatSamples);
            const float h = spk::softClip (x + fbBuf[(size_t) i] * fb);
            hist.push (h, h);
        }

        spk::FxParams fp;
        fp.effect = s.effect;
        fp.variant = s.variant;
        fp.activity = s.activity;
        fp.repeats = s.repeats;
        fp.shape = s.shape;
        fp.reverse = s.reverse;
        fp.beatSamples = s.beatSamples;
        fp.stepSamples = s.beatSamples * 0.5f;
        fp.level = playLevel;
        const bool delayFx = spk::isDelayEffect (s.effect);
        if (! delayFx) fx.schedule (hist, onsets, fp);
        float wl[kBlock] = {}, wr[kBlock] = {};
        fx.render (wl, wr, n);

        spk::MultiDelay::Settings ds;
        ds.warp = s.effect == spk::Warp;
        ds.variant = s.variant;
        ds.activity = s.activity;
        ds.repeats = s.repeats;
        ds.stepSamples = fp.stepSamples;
        ds.reverse = s.reverse;
        // Speckle's own level trims, so every effect sits where the input does
        static constexpr float trims[spk::kNumEffects] = { 1.7f, 1.0f, 1.7f, 1.15f, 1.5f, 1.1f, 1.15f, 1.0f, 0.8f, 0.72f, 1.0f };
        for (int i = 0; i < n; ++i)
        {
            const float dOut = (dOutS.set (delayFx ? 1.0f : 0.0f), dOutS.next());
            if (dOut > 1.0e-4f || delayActive)
            {
                const float since = onsets.latest < 0 ? 1.0f : clampf ((float) (hist.writePos() - n + i - onsets.latest) / (2.0f * s.beatSamples), 0.0f, 1.0f);
                const float c = delayFx ? spk::contour (since, s.shape) * spk::contourMakeup (s.shape) : 0.0f;
                float dl, dr;
                delay.process (in[i] * c, in[i] * c, dl, dr, ds);
                wl[i] += dl * dOut;
                wr[i] += dr * dOut;
                delayActive = dOut > 1.0e-4f || std::fabs (dl) + std::fabs (dr) > 1.0e-5f;
            }
            trimS.set (trims[std::max (0, std::min (spk::kNumEffects - 1, s.effect))]);
            passS.set (fx.passTarget());
            const float y = 0.5f * (wl[i] + wr[i]) * trimS.next() + in[i] * passS.next();
            fbBuf[(size_t) i] = y;
            out[i] = y;
        }
    }

    void addOnset (float strength)
    {
        onsets.add (hist.writePos(), strength);
        sinceOnset = 0;
        envAtOnset = std::max (env, 1.0e-6f);
    }
    void sustainedNote (float x, float beatSamples)
    {
        const float a = std::fabs (x);
        env += (a - env) * (a > env ? envAtt : envRel);
        playLevel = env > playLevel ? playLevel + (env - playLevel) * envAtt : playLevel * levelRel;
        ++sinceOnset;
        const float gap = std::max (beatSamples, 0.35f * sr);
        // a held note (pads, swells) counts as a new note every beat (the Speckle lesson)
        if (sinceOnset > (int) gap && env > std::max (3.0e-5f, 0.1f * playLevel) && env > 0.5f * envAtOnset)
            addOnset (0.4f);
    }

    float sr = 48000;
    spk::Detector detector;
    spk::History hist;
    spk::EffectBank fx;
    spk::MultiDelay delay;
    spk::OnsetRing onsets;
    UpShifter shifter;
    Smoothed trimS, passS, dOutS;
    std::vector<float> fbBuf;
    float env = 0, envAtOnset = 0, playLevel = 0, envAtt = 0, envRel = 0, levelRel = 0;
    int sinceOnset = 0, lastEffect = -1, scatterLeft = 0;
    float currentRatio = 2.0f;
    bool delayActive = false;
    Rng rng;
};
} // namespace ks
