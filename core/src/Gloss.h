#pragma once
// Gloss (0.6.1): the clean synth voice under the bass, on hold B in Grit.
//
// Bitrot is the glitchy one: a tracking synth that hunts and corrupts. Gloss is the
// playable one. It never has to work out the note: it rides the bass's own zero crossings,
// so it can't lag or pick the wrong note, the way the old analogue bass synths did.
//
//  - Instant: each wave restarts on every rising crossing of the bass, so its period is the
//    bass's period, cycle by cycle. Square (with pulse width), saw or triangle, plus a sub
//    from a flip-flop on the crossings (an octave or two down).
//  - Tracked: a free-running oscillator at the measured pitch times an interval, with
//    glide. Any interval, best for pads and leads.
//  - Follow: the voice's level is your playing's level. ADSR: every new note fires the
//    envelope, pick strength sets how loud it comes in.
//  - A 24 dB resonant low-pass with its own envelope, then Tone.
//
// Every jump in the waves is smoothed with a two-sample BLEP at its exact fractional
// position, so the waves stay clean up top (sound bench). The output runs one sample late
// for that.
#include "Modules.h"
#include <array>

namespace ks
{
// Note starts: the peak of the last 4 ms jumping at least 2.3 dB over the peak of the 26 ms
// before the last 6 ms, above a floor set against your playing level. Both windows are longer than a
// cycle of low E, so the wave's own ripple never fires it. Catches a repluck of a ringing
// string (the jump) and a note after a gap, and never fires on a decaying note.
struct OnsetDetector
{
    // jump: how far over the earlier peak counts as a new note (1.3 is 2.3 dB)
    void prepare (float sr, float jump = 1.3f)
    {
        ratio = jump;
        every = std::max (1, (int) (sr / 1500.0f));   // a slot every ~0.67 ms
        refractoryLen = (int) (0.07f * sr);
        reset();
    }
    void reset() { slots.fill (0.0f); pos = 0; count = 0; refractory = 0; strength = 0; cur = 0; }
    // play: your playing level (linear peak of a typical note)
    bool process (float x, float play)
    {
        cur = std::max (cur, std::fabs (x));
        if (refractory > 0) --refractory;
        if (++count < every) return false;
        count = 0;
        slots[(size_t) pos] = cur;
        cur = 0;
        const int n = (int) slots.size();
        float recent = 0, before = 0;
        for (int k = 0; k < 6; ++k) recent = std::max (recent, slots[(size_t) ((pos - k + n) % n)]);
        for (int k = 9; k < n; ++k) before = std::max (before, slots[(size_t) ((pos - k + n) % n)]);
        pos = (pos + 1) % n;
        if (refractory > 0 || recent < play * 0.08f) return false;
        if (recent > before * ratio + play * 0.02f)
        {
            refractory = refractoryLen;
            strength = recent / std::max (play, 1.0e-4f);
            return true;
        }
        return false;
    }
    float strength = 0;
private:
    std::array<float, 48> slots {};
    float cur = 0, ratio = 1.3f;
    int pos = 0, count = 0, every = 32, refractory = 0, refractoryLen = 3000;
};

class Gloss
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        det1.lowPass (320.0f, 0.54f, sr);
        det2.lowPass (320.0f, 1.31f, sr);
        dc.prepare (sr);
        detEnv.set (0.001f, 0.03f, sr);
        inEnv.set (0.002f, 0.08f, sr);
        fastEnv.set (0.0007f, 0.02f, sr);
        onset.prepare (sr);
        playFall = onePoleCoef (4.0f, sr);
        toneCoef = 0.0f;
        reset();
    }

    void reset()
    {
        det1.reset(); det2.reset(); dc.reset();
        detEnv.reset(); inEnv.reset(); fastEnv.reset(); onset.reset();
        f1.reset(); f2.reset();
        high = false; dPrev = 0; sincePrev = 0; sinceAny = 0; lastRaw = 0; period = 0; smoothPeriod = 0; ph = 0; tPh = 0; subPh = 0;
        subState = 1.0f; subCount = 0; prevOut = 0; prevWave = 0; amp.stage = Stage::Idle; amp.level = 0;
        fenv.stage = Stage::Idle; fenv.level = 0; playLvl = 0.05f; gateOpen = false;
        velocity = 1; velWindow = 0; tone = 0; freq = 0; filterCount = 0; quiet = 0;
    }

    void update (const Vals& v)
    {
        wave = Vi (v, P::GlossWave);
        pw = clampf (V (v, P::GlossPw), 0.05f, 0.5f);
        waveLvl = V (v, P::GlossWaveLvl);
        subLvl = V (v, P::GlossSub);
        subDiv = Vi (v, P::GlossSubOct) == 1 ? 4 : 2;
        level = V (v, P::GlossLevel);
        const float t = clampf (V (v, P::GlossTone), 0.0f, 10.0f);
        toneCoef = std::exp (-kTwoPi * std::min (400.0f * std::pow (40.0f, t / 10.0f), sr * 0.45f) / sr);
        tracked = Vi (v, P::GlossTrack) == 1;
        static const float ratios[] = { 1.0f, 2.0f, 1.5f, 0.5f, 1.33484f };
        ratio = ratios[std::clamp (Vi (v, P::GlossInterval), 0, 4)];
        adsr = Vi (v, P::GlossEnv) == 1;
        amp.set (V (v, P::GlossA), V (v, P::GlossD), V (v, P::GlossS), V (v, P::GlossR), sr);
        fenv.set (V (v, P::GlossFA), V (v, P::GlossFD), V (v, P::GlossFS), V (v, P::GlossFR), sr);
        sens = V (v, P::GlossSens);
        velAmt = V (v, P::GlossVel);
        cutoff = V (v, P::GlossCutoff);
        res = V (v, P::GlossRes);
        envAmt = V (v, P::GlossEnvAmt);
        glideCoef = onePoleCoef (V (v, P::GlossGlide) * 0.001f * 0.35f, sr);
    }

    // in: the clean bass, out: the synth alone (the caller blends the clean bass)
    void process (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i) out[i] = tick (in[i]);
    }

    bool sounding() const { return gateOpen || amp.stage != Stage::Idle; }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    struct Adsr
    {
        Stage stage = Stage::Idle;
        float level = 0, aInc = 0, dCoef = 0, rCoef = 0, sus = 0.6f;
        void set (float aMs, float dMs, float s, float rMs, float sr)
        {
            aInc = 1.0f / std::max (1.0f, aMs * 0.001f * sr);
            dCoef = onePoleCoef (dMs * 0.001f / 3.0f, sr);   // within ~5% of sustain by the decay time
            rCoef = onePoleCoef (rMs * 0.001f / 3.0f, sr);
            sus = clampf (s, 0.0f, 1.0f);
        }
        void trigger() { stage = Stage::Attack; }   // from where it is: no click on a retrigger
        void release() { if (stage != Stage::Idle) stage = Stage::Release; }
        float next()
        {
            switch (stage)
            {
                case Stage::Idle: level = 0; break;
                case Stage::Attack: level += aInc; if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; } break;
                case Stage::Decay: level = sus + (level - sus) * dCoef; if (std::fabs (level - sus) < 1.0e-3f) stage = Stage::Sustain; break;
                case Stage::Sustain: level = sus; break;
                case Stage::Release: level *= rCoef; if (level < 1.0e-5f) { level = 0; stage = Stage::Idle; } break;
            }
            return level;
        }
    };

    // a step of height h, t samples before this sample (0..1): the two BLEP corrections
    void step (float h, float t, float& now)
    {
        prevOut += h * t * t * 0.5f;
        now -= h * (1.0f - t) * (1.0f - t) * 0.5f;
    }

    float naive (float p) const
    {
        switch (wave)
        {
            case 0: return p < pw ? 1.0f : -1.0f;
            case 1: return 2.0f * p - 1.0f;
            default: return 1.0f - 4.0f * std::fabs (p - 0.5f);
        }
    }

    float tick (float x)
    {
        // ---- listen: envelopes and the playing level ----
        const float e = inEnv.process (x);
        const float fe = fastEnv.process (x);
        playLvl = std::max (playLvl * playFall, e);
        playLvl = std::max (playLvl, 1.0e-3f);
        const float thr = playLvl * (0.25f - 0.22f * sens);

        // gate and note starts: a fresh rise over the slow envelope, or the gate opening
        bool trig = false;
        const bool started = onset.process (x, playLvl);
        if (! gateOpen && e > thr) { gateOpen = true; trig = true; }
        else if (gateOpen && e < thr * 0.6f) { gateOpen = false; amp.release(); fenv.release(); }
        if (gateOpen && started) trig = true;
        if (trig)
        {
            amp.trigger(); fenv.trigger();
            smoothPeriod = 0;   // a new note may be any pitch: take the next cycles as they come
            velPeak = fe; velWindow = (int) (0.012f * sr);
        }
        if (velWindow > 0)
        {
            velPeak = std::max (velPeak, fe);
            if (--velWindow == 0) velocity = (1.0f - velAmt) + velAmt * clampf (velPeak / playLvl, 0.15f, 1.4f);
        }

        // ---- the bass's own crossings ----
        float d = det2.process (det1.process (dc.process (x)));
        const float de = detEnv.process (d);
        const float h = std::max (de * 0.2f, 1.0e-5f);
        sincePrev += 1.0f;
        sinceAny += 1.0f;
        bool rose = false;
        float t = 0;   // how long before this sample the crossing was
        if (! high && d > h)
        {
            const float frac = (dPrev - h) == (d - h) ? 1.0f : clampf ((h - dPrev) / (d - dPrev), 0.0f, 1.0f);
            t = 1.0f - frac;
            const float len = sincePrev - t, raw = sinceAny - t;
            // a second crossing far too soon is a strong harmonic, not a new cycle, unless the
            // crossings are evenly spaced: then it's a higher note played legato
            const bool even = std::fabs (raw - lastRaw) < 0.15f * raw;
            lastRaw = raw;
            sinceAny = t;
            if (smoothPeriod > 0 && len <= smoothPeriod * 0.6f && gateOpen && even) { smoothPeriod = 0; sincePrev = raw + t; }
            const float len2 = sincePrev - t;
            if (smoothPeriod <= 0 || len2 > smoothPeriod * 0.6f || ! gateOpen)
            {
                rose = true;
                const float l = std::min (len2, raw > 0 && smoothPeriod <= 0 ? raw : len2);
                if (l > sr / 1200.0f && l < sr / 20.0f)
                {
                    period = l;
                    smoothPeriod = smoothPeriod <= 0 ? l : smoothPeriod + (l - smoothPeriod) * 0.35f;
                }
                sincePrev = t;
            }
            high = true;
        }
        else if (high && d < -h) high = false;
        dPrev = d;

        // ---- the voice ----
        // resting: once the envelope has finished, a quarter of a second lets the filter ring
        // down, then the voice costs next to nothing until the next note
        const bool resting = adsr ? amp.stage == Stage::Idle : (! gateOpen && e < 1.0e-5f);
        if (resting) { if (++quiet > (int) (0.25f * sr)) { prevOut = subPrev = 0; return 0.0f; } }
        else quiet = 0;

        float now = 0, subNow = 0;
        const float inc = period > 0 ? 1.0f / period : 0.0f;
        if (! tracked)
        {
            if (inc > 0)
            {
                // every jump this sample: the pulse edge, the wrap and the restart at the
                // crossing, each at its own fractional time (samples before this one)
                float hs[4] = {}, ts[4] = {};
                int k = 0;
                const float before = ph, reach = rose ? before + inc * (1.0f - t) : before + inc;
                const float tAt = rose ? t : 0.0f;   // how long before this sample "reach" was
                if (wave == 0 && before < pw && reach >= pw) { hs[k] = -2.0f; ts[k++] = (reach - pw) / inc + tAt; }
                float pre = reach;
                if (reach >= 1.0f)
                {
                    if (wave != 2) { hs[k] = wave == 0 ? 2.0f : -2.0f; ts[k++] = (reach - 1.0f) / inc + tAt; }
                    pre = reach - 1.0f;
                    if (wave == 0 && pre >= pw) { hs[k] = -2.0f; ts[k++] = (pre - pw) / inc + tAt; }
                }
                if (rose)
                {
                    ph = t * inc;
                    hs[k] = naive (0.0f) - naive (pre); ts[k++] = t;
                    if (wave == 0 && ph >= pw) { hs[k] = -2.0f; ts[k++] = (ph - pw) / inc; }
                }
                else ph = pre;
                now = naive (ph);
                for (int j = 0; j < k; ++j) step (hs[j], clampf (ts[j], 0.0f, 1.0f), now);
            }
            // the sub: a flip-flop on the crossings
            if (rose && ++subCount >= subDiv / 2) { subCount = 0; subState = -subState; subNow = 0; subStep = 2.0f * subState; subT = t; }
            else subStep = 0;
        }
        else
        {
            // a free oscillator at the measured pitch times the interval, gliding between notes
            if (smoothPeriod > 0)
            {
                const float want = sr / smoothPeriod * ratio;
                freq = freq <= 0 ? want : want + (freq - want) * glideCoef;
            }
            const float tInc = clampf (freq / sr, 0.0f, 0.45f);
            const float before = tPh;
            tPh += tInc;
            if (tPh >= 1.0f)
            {
                tPh -= 1.0f;
                now = naive (tPh);
                if (wave != 2 && tInc > 0) step (wave == 0 ? 2.0f : -2.0f, tPh / tInc, now);
                if (++subCount >= subDiv / 2) { subCount = 0; subState = -subState; subStep = 2.0f * subState; subT = tInc > 0 ? tPh / tInc : 0.0f; }
                else subStep = 0;
            }
            else { now = naive (tPh); subStep = 0; }
            if (wave == 0 && before < pw && tPh >= pw && tInc > 0) step (-2.0f, (tPh - pw) / tInc, now);
        }

        // sub BLEP (it keeps its own one-sample history)
        float sub = subState;
        if (subStep != 0)
        {
            subPrev += subStep * subT * subT * 0.5f;
            sub -= subStep * (1.0f - subT) * (1.0f - subT) * 0.5f;
        }
        const float subOut = subPrev;
        subPrev = sub;

        const float osc = prevOut * waveLvl + subOut * subLvl * 0.8f;
        prevOut = now;

        // ---- level ----
        float a;
        if (adsr) a = amp.next() * velocity * playLvl * 0.9f;
        else { amp.next(); a = e * 1.15f; }
        const float fe2 = fenv.next();

        // ---- filter, every 16 samples ----
        if (filterCount-- <= 0)
        {
            filterCount = 15;
            const float fc = clampf (cutoff * std::pow (2.0f, envAmt * 6.0f * fe2), 30.0f, sr * 0.45f);
            f1.set (fc, 0.0f, sr);
            f2.set (fc, res, sr);
        }
        float lp, bp;
        f1.process (osc * a, lp, bp);
        float y;
        f2.process (lp, y, bp);
        tone = y + (tone - y) * toneCoef;
        return undenorm (tone * level * 1.4f);
    }

    float sr = 48000;
    Biquad det1, det2;
    DcBlock dc;
    Follower detEnv, inEnv, fastEnv;
    OnsetDetector onset;
    Svf f1, f2;
    Adsr amp, fenv;
    bool high = false, gateOpen = false, tracked = false, adsr = true;
    float sinceAny = 0, lastRaw = 0;
    float dPrev = 0, sincePrev = 0, period = 0, smoothPeriod = 0, ph = 0, tPh = 0, subPh = 0, freq = 0;
    float subState = 1, subPrev = 0, subStep = 0, subT = 0;
    int subCount = 0, subDiv = 2, velWindow = 0, filterCount = 0, quiet = 0;
    float prevOut = 0, prevWave = 0, playLvl = 0.05f, playFall = 0.99999f, velocity = 1, velPeak = 0;
    int wave = 0;
    float pw = 0.35f, waveLvl = 0.8f, subLvl = 0.6f, level = 0.7f, toneCoef = 0, tone = 0, ratio = 1;
    float sens = 0.6f, velAmt = 0.7f, cutoff = 900, res = 0.45f, envAmt = 0.65f, glideCoef = 0;
};
} // namespace ks
