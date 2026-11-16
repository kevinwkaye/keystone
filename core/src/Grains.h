#pragma once
// Speckle in Keystone: the whole Speckle engine from Kevin's Speckle plugin (core/src/spk),
// eleven effects with its filter, pitch mod and space, on the full sound path. Mono in,
// mono out (the engine's stereo is summed). An octave pitch-up before it is optional.
//
// It only runs while it's switched on (plus a short fade), so it is never heard when off.

#include "SpeckleFx.h"
#include "spk/SpeckleEngine.h"

namespace ks
{
class Grains
{
public:
    static constexpr int kBlock = spk::EffectBank::block;

    void prepare (float sr_)
    {
        sr = sr_;
        eng.prepare (sr);
        shifter.prepare (sr);
        mixS.setTime (0.05f, sr);
        reset();
    }
    void reset()
    {
        mixS.reset (0);
        std::fill (std::begin (inL), std::end (inL), 0.0f);
        std::fill (std::begin (outM), std::end (outM), 0.0f);
        idx = 0; activity = 0; running = false;
    }

    void update (const Vals& v, float bloomZone2, bool bloomActive, bool /*light*/)
    {
        const bool on = Vb (v, P::GrOn);
        const float mix = V (v, P::GrMix);
        float m = on ? mix : 0.0f;
        // the expression pedal pushes Speckle further in when it's on; it never brings it in
        if (on && bloomActive) m = mix + (1.0f - mix) * bloomZone2 * V (v, P::BloomGrains);
        mixS.set (m);
        dirty = Vi (v, P::GrSource) == 1;

        p.effect = std::max (0, std::min ((int) spk::kNumEffects - 1, Vi (v, P::GrEffect)));
        p.variant = Vi (v, P::GrVariant);
        p.reverse = Vb (v, P::GrRev);
        p.activity = V (v, P::GrActivity);
        p.repeats = V (v, P::GrRepeats);
        p.shape = V (v, P::GrContour);
        // the delay's time (tap tempo sets it) is the beat
        static constexpr float subdiv[9] = { 4.0f, 2.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f };
        p.beatSamples = clampf (V (v, P::DlTime), 150.0f, 1500.0f) * 0.001f * sr;
        p.stepSamples = p.beatSamples * subdiv[std::max (0, std::min (8, Vi (v, P::GrSubdiv)))];
        p.filter = V (v, P::GrFilter);
        p.resonance = V (v, P::GrRes);
        p.modDepth = V (v, P::GrModDepth);
        p.modRateHz = 0.05f * std::pow (2.0f, V (v, P::GrModRate) * 7.3f);
        p.space = V (v, P::GrSpace);
        p.reverbMode = std::max (0, std::min (3, Vi (v, P::GrSpaceType)));
        p.volumeDb = V (v, P::GrVolume);
        p.hold = Vb (v, P::GrHold);
        p.mix = 1.0f;   // all wet: Keystone keeps the dry itself
        p.inputMode = spk::InputGuitar;   // the full path already has its sub cut
        p.detectorMode = spk::InputBass;
        p.bypass = false; p.trails = true;
        p.loopPre = false; p.loopOnly = false;
        p.loopLevel = 0.0f;

        pitchMode = Vi (v, P::GrPitch);
        static const float ratios[3] = { 2.0f, 3.0f, 4.0f };
        if (pitchMode >= 1 && pitchMode <= 3) shifter.setRatio (ratios[pitchMode - 1]);
        if (pitchMode == 4)
        {
            // scatter: wander between the three intervals, more often with more movement
            if (++scatterTick > (int) (40 - 35 * V (v, P::GrScatter))) { scatterTick = 0; shifter.setRatio (ratios[(rng.next() >> 8) % 3]); }
        }
    }

    bool sourceDirty() const { return dirty; }
    bool active() const { return mixS.value > 1.0e-4f || mixS.target > 0; }
    float activityLevel() const { return activity; }

    // source: the clean or dirty full sound. Returns the wet signal, already scaled by mix.
    // A block of 32 samples at a time, under a millisecond late.
    float process (float source, float /*playLevel*/)
    {
        const float m = mixS.next();
        if (m < 1.0e-4f && mixS.target <= 0.0f)
        {
            running = false;
            activity *= 0.999f;
            return 0.0f;
        }
        running = true;
        const float y = outM[idx];
        inL[idx] = pitchMode > 0 ? shifter.process (source) : source;
        if (++idx >= kBlock)
        {
            float l[kBlock], r[kBlock];
            for (int i = 0; i < kBlock; ++i) { l[i] = inL[i]; r[i] = inL[i]; }
            eng.process (l, r, kBlock, p);
            for (int i = 0; i < kBlock; ++i) outM[i] = 0.5f * (l[i] + r[i]);
            idx = 0;
            activity += (std::min (1.0f, (float) eng.uiGrains.load (std::memory_order_relaxed) / 24.0f) - activity) * 0.05f;
        }
        return y * m;
    }

private:
    float sr = 48000;
    spk::SpeckleEngine eng;
    spk::EngineParams p;
    UpShifter shifter;
    Smoothed mixS;
    Rng rng;
    float inL[kBlock] = {}, outM[kBlock] = {};
    int idx = 0, pitchMode = 0, scatterTick = 0;
    float activity = 0;
    bool dirty = false, running = false;
};
} // namespace ks
