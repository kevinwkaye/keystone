#pragma once
// The drive stage: Edge and Drive voices, each with its own gain, tone, level, low cut,
// presence, gate and clipping mode. Source is a NAM capture (Kevin's own) or a modelled
// fallback for anyone without the capture. Level matched, click-free switching.
//
// Latency is fixed at Oversampler2x::kLatency whichever source runs, so switching never
// moves the high band against the low band.

#include "Modules.h"

#include <atomic>
#include <memory>
#include <string>

#ifdef KEYSTONE_WITH_NAM
#include "NAM/dsp.h"
#endif

namespace ks
{
// Hands a loaded capture to the audio thread and the old one back for freeing.
class CaptureSlot
{
public:
#ifdef KEYSTONE_WITH_NAM
    using Model = nam::DSP;
#else
    struct Model {};
#endif
    ~CaptureSlot()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }
    // housekeeping thread
    bool offer (Model* m)
    {
        if (pending.load() != nullptr || retired.load() != nullptr) return false;
        pending.store (m);
        return true;
    }
    void collect() { delete retired.exchange (nullptr); }
    // audio thread, once per block
    Model* take()
    {
        if (retired.load() == nullptr)
        {
            if (Model* p = pending.exchange (nullptr))
            {
                retired.store (current);   // may be nullptr, which is fine
                current = p;
                if (p == clearMarker()) { current = nullptr; }
            }
        }
        return current;
    }
    static Model* clearMarker() { static char c; return reinterpret_cast<Model*> (&c); }

private:
    std::atomic<Model*> pending { nullptr }, retired { nullptr };
    Model* current = nullptr;
};

class Drive
{
public:
    static constexpr int kLatency = Oversampler2x::kLatency;

    void prepare (float sr_, int maxBlock)
    {
        sr = sr_;
        for (auto& vc : voices) vc.prepare (sr);
        dryDelay.prepare (kLatency + 4);
        capDelay.prepare (kLatency + 4);
        namIn.assign ((size_t) maxBlock, 0.0f);
        namOut.assign ((size_t) maxBlock, 0.0f);
        onS.setTime (0.015f, sr);
        lowIntoHpMix.setTime (0.05f, sr);
        lowIntoHp1.highPass (100, 0.707f, sr);
        lowIntoHp2 = lowIntoHp1;
        trimCoef = 0.02f;
        reset();
    }
    void reset()
    {
        for (auto& vc : voices) vc.reset();
        dryDelay.reset(); capDelay.reset();
        onS.reset (0); xf = 0; xfTarget = 0;
        trim[0] = trim[1] = 1.0f;
        lowIntoHp1.reset(); lowIntoHp2.reset();
    }

    void update (const Vals& v, bool bloomActive, float bloom, float splitFreq, bool haveCapture)
    {
        on = Vb (v, P::DriveOn);
        onS.set (on ? 1.0f : 0.0f);
        useCapture = Vi (v, P::DriveSource) == 0 && haveCapture;
        float target = (float) Vi (v, P::DriveVoice);
        if (Vi (v, P::DriveMorph) == 1 && bloomActive) target = bloom;
        xfTarget = target;
        xfStep = 1.0f / std::max (1.0f, V (v, P::DriveFade) * 0.001f * sr);
        autoMatch = Vi (v, P::DriveMatch) == 1;
        voices[0].update (sr, V (v, P::EdgeGain), V (v, P::EdgeTone), V (v, P::EdgeLevel), V (v, P::EdgeLowCut),
                          V (v, P::EdgePresence), V (v, P::EdgeGate), Vi (v, P::EdgeMode), useCapture);
        voices[1].update (sr, V (v, P::DriveGain), V (v, P::DriveTone), V (v, P::DriveLevel), V (v, P::DriveLowCut),
                          V (v, P::DrivePresence), V (v, P::DriveGate), Vi (v, P::DriveMode), useCapture);
        lowInto = V (v, P::DriveLowInto);
        lowIntoHpMix.set (lowInto > 0.01f ? 1.0f : 0.0f);
        if (std::fabs (splitFreq - lastSplit) > 0.5f)
        {
            lowIntoHp1.highPass (splitFreq, 0.707f, sr);
            Biquad t = lowIntoHp1; lowIntoHp2.b0 = t.b0; lowIntoHp2.b1 = t.b1; lowIntoHp2.b2 = t.b2; lowIntoHp2.a1 = t.a1; lowIntoHp2.a2 = t.a2;
            lowIntoHp1.reset();
            lastSplit = splitFreq;
        }
    }

    bool active() const { return on || ! onS.settled() || onS.value > 0.0001f; }

    // high: the high band (in/out). low: the low band, for "low end into drive".
    void process (float* high, const float* low, int n, CaptureSlot::Model* capture)
    {
        if (! active())
        {
            for (int i = 0; i < n; ++i) { dryDelay.push (high[i]); high[i] = dryDelay.at (kLatency); }
            return;
        }
        // input with some of the low band, if asked
        for (int i = 0; i < n; ++i) namIn[(size_t) i] = high[i] + lowInto * low[i];

#ifdef KEYSTONE_WITH_NAM
        const bool cap = useCapture && capture != nullptr;
#else
        const bool cap = false;
        (void) capture;
#endif
        if (cap)
        {
#ifdef KEYSTONE_WITH_NAM
            // one capture: the voices' input stages are interpolated into it, their
            // output shaping crossfaded after it
            float xfs = xf;
            for (int i = 0; i < n; ++i)
            {
                xfs = stepXf (xfs);
                const float w = smoothstep (xfs);
                const float x = namIn[(size_t) i];
                const float a = voices[0].preCapture (x), b = voices[1].preCapture (x);
                namOut[(size_t) i] = a + (b - a) * w;
                voices[0].measureIn (x); voices[1].measureIn (x);
            }
            float* ins[1] = { namOut.data() };
            float* outs[1] = { namIn.data() };   // reuse as output
            capture->process (ins, outs, n);
            for (int i = 0; i < n; ++i)
            {
                xf = stepXf (xf);
                const float w = smoothstep (xf);
                const float y = namIn[(size_t) i];
                float a = voices[0].postCapture (y) * trim[0];
                float b = voices[1].postCapture (y) * trim[1];
                if (w < 0.999f) voices[0].measureOut (a);
                if (w > 0.001f) voices[1].measureOut (b);
                capDelay.push (a + (b - a) * w);
                namOut[(size_t) i] = capDelay.at (kLatency);
            }
#endif
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                xf = stepXf (xf);
                const float w = smoothstep (xf);
                const float x = namIn[(size_t) i];
                float a = 0, b = 0;
                if (w < 0.999f) { a = voices[0].model (x) * trim[0]; voices[0].measureOut (a); }
                else voices[0].idleModel (x);
                if (w > 0.001f) { b = voices[1].model (x) * trim[1]; voices[1].measureOut (b); }
                else voices[1].idleModel (x);
                voices[0].measureIn (x); voices[1].measureIn (x);
                namOut[(size_t) i] = a + (b - a) * w;
            }
        }
        // high-pass the wet when low end was fed in, so the lows are never doubled
        for (int i = 0; i < n; ++i)
        {
            const float hm = lowIntoHpMix.next();
            float wet = namOut[(size_t) i];
            if (hm > 0.0001f) { const float h = lowIntoHp2.process (lowIntoHp1.process (wet)); wet = wet + (h - wet) * hm; }
            dryDelay.push (high[i]);
            const float g = onS.next();
            high[i] = dryDelay.at (kLatency) * (1.0f - g) + wet * g;
        }
        // level match: steer each voice's trim towards the level of the clean high band
        for (int k = 0; k < 2; ++k)
        {
            auto& vc = voices[k];
            if (! autoMatch) { trim[k] += (1.0f - trim[k]) * 0.05f; vc.clearMeasure(); continue; }
            if (vc.measuredSamples > (int) (0.1f * sr))
            {
                const float in = std::sqrt (vc.sumIn / vc.measuredSamples), out = std::sqrt (vc.sumOut / std::max (1, vc.measuredOutSamples));
                if (in > 3.0e-3f && out > 1.0e-5f && vc.measuredOutSamples > 0)
                {
                    const float want = clampf (trim[k] * in / out * vc.levelGain, 0.06f, 16.0f);
                    trim[k] += (want - trim[k]) * 0.15f;
                }
                vc.clearMeasure();
            }
        }
    }

private:
    float stepXf (float x) const
    {
        if (x < xfTarget) return std::min (xfTarget, x + xfStep);
        if (x > xfTarget) return std::max (xfTarget, x - xfStep);
        return x;
    }

    struct Voice
    {
        float sr = 48000;
        OnePole lowCut;
        Biquad tiltLo, tiltHi, presence, fizz;
        Oversampler2x os;
        AdaaTanh sat;
        OnePole osSmooth;
        DcBlock dc;
        Follower gateEnv;
        float gateGain = 1, gateThr = 0, gateAtt = 0, gateRel = 0;
        float gain = 1, capGain = 1, levelGain = 1, thr = 1, bias = 0;
        int mode = 1;
        float lastTone = -99, lastPres = -99, lastCut = -1;
        double sumIn = 0, sumOut = 0;
        int measuredSamples = 0, measuredOutSamples = 0;

        void prepare (float sr_)
        {
            sr = sr_;
            fizz.lowPass (7500.0f, 0.707f, sr);
            dc.prepare (sr);
            gateEnv.set (0.001f, 0.08f, sr);
            gateAtt = onePoleCoef (0.002f, sr);
            gateRel = onePoleCoef (0.06f, sr);
            osSmooth.setLowPass (14000.0f, sr * 2);
        }
        void reset()
        {
            lowCut.reset(); tiltLo.reset(); tiltHi.reset(); presence.reset(); fizz.reset();
            os.reset(); sat.reset(); osSmooth.reset(); dc.reset(); gateEnv.reset();
            gateGain = 1;
            clearMeasure();
        }
        void clearMeasure() { sumIn = sumOut = 0; measuredSamples = measuredOutSamples = 0; }
        void update (float sr_, float g, float tone, float level, float cut, float pres, float gate, int m, bool capture)
        {
            sr = sr_;
            gain = dbToGain (g * 4.0f);             // model: 0 to 40 dB
            capGain = dbToGain ((g - 5.0f) * 3.0f);  // capture: -15 to +15 dB around the capture's own gain
            levelGain = dbToGain (level);
            mode = m;
            thr = m == 1 ? 3.0f : (m == 0 ? 1.2f : 0.55f);
            bias = m == 0 ? 0.12f : (m == 2 ? 0.04f : 0.0f);
            if (cut != lastCut) { lowCut.setLowPass (cut, sr); lastCut = cut; }
            if (tone != lastTone)
            {
                const float t = (tone - 5.0f) * 1.4f;   // +/- 7 dB tilt round 800 Hz
                tiltLo.lowShelf (800.0f, -t, sr, 0.6f);
                tiltHi.highShelf (800.0f, t, sr, 0.6f);
                lastTone = tone;
            }
            if (pres != lastPres) { presence.highShelf (3200.0f, pres, sr, 0.8f); lastPres = pres; }
            gateThr = gate <= -89.0f ? 0.0f : dbToGain (gate);
            (void) capture;
        }
        float gate (float x)
        {
            if (gateThr <= 0) return x;
            const float e = gateEnv.process (x);
            const float want = e >= gateThr ? 1.0f : std::pow (e / gateThr, 3.0f);
            gateGain = want < gateGain ? want + (gateGain - want) * gateAtt : want + (gateGain - want) * gateRel;
            return x * gateGain;
        }
        float shape (float y) { return presence.process (tiltHi.process (tiltLo.process (y))) * levelGain; }
        float preCapture (float x) { return gate (lowCut.hp (x)) * capGain; }
        float postCapture (float y) { return shape (dc.process (y)); }
        void measureIn (float x) { sumIn += (double) x * x; ++measuredSamples; }
        void measureOut (float y) { sumOut += (double) y * y; ++measuredOutSamples; }
        float clip (float x)
        {
            return sat.process ((x + bias) / thr) * thr - std::tanh (bias / thr) * thr;
        }
        float model (float x)
        {
            x = gate (lowCut.hp (x)) * gain;
            float a, b;
            os.up (x, a, b);
            a = clip (osSmooth.lp (a));
            b = clip (osSmooth.lp (b));
            const float y = os.down (a, b);
            return shape (fizz.process (dc.process (y)) / std::sqrt (gain));
        }
        // keep the oversampler's history fresh when the voice is silent, so a switch
        // doesn't play stale samples
        void idleModel (float x) { float a, b; os.up (x * gain, a, b); os.down (clip (a), clip (b)); }
    };

    float sr = 48000;
    Voice voices[2];
    float trim[2] { 1, 1 };
    float trimCoef = 0.02f;
    bool on = true, useCapture = false, autoMatch = true;
    float xf = 0, xfTarget = 0, xfStep = 0.001f;
    float lowInto = 0.35f, lastSplit = -1;
    Biquad lowIntoHp1, lowIntoHp2;
    Smoothed onS, lowIntoHpMix;
    DelayLine dryDelay, capDelay;
    std::vector<float> namIn, namOut;
};
} // namespace ks
