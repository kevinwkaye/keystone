#pragma once
// Gloss (0.6.7): a lush pad under the bass, on hold B in Grit.
//
// 0.6.1's Gloss was a synth that followed every cycle and fired on every pluck: too close
// to Bitrot. Now it's a pad that only follows whole notes. It listens to the bass's own
// crossings, waits for a note to hold steady (Settle), snaps it to the semitone and only
// then moves, fading across (or gliding) to the new note. Passing notes, slides, bends and
// replucks of the same note leave it where it is.
//
//  - Voice: five detuned oscillators per layer, saw (Warm), square (Hollow) or triangle
//    (Soft), each drifting a few cents on its own, so it shimmers like an analogue string
//    machine. Plays: octave up, octave and fifth, unison, fifth up or two octaves.
//  - Weight: a sine at the note you play, for body.
//  - Swell and Fade: it blooms in when you start and trails off when you stop (or Rings on).
//  - A soft 24 dB low-pass that opens as it swells and slowly breathes.
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
        onset.prepare (sr);
        playFall = onePoleCoef (4.0f, sr);
        lvlCoef = onePoleCoef (0.6f, sr / kCtl);
        hpCoef = std::exp (-kTwoPi * 45.0f / sr);
        easeCoef = onePoleCoef (0.5f, sr / kCtl);
        reset();
    }

    void reset()
    {
        det1.reset(); det2.reset(); dc.reset();
        detEnv.reset(); inEnv.reset(); onset.reset();
        f1.reset(); f2.reset();
        high = false; dPrev = 0; sincePrev = 0; sinceAny = 0; lastRaw = 0; smoothPeriod = 0;
        playLvl = 0.05f; padLvl = 0; gateOpen = false;
        cand = kNone; stable = 0; cur = kNone; tuneOff = 0; tuned = false; devSum = 0; devCount = 0;
        amp = 0; ampTarget = 0; ctl = 0; breathPh = 0; hpX = hpY = 0;
        for (auto& vc : voices) vc.gain = vc.target = 0, vc.note = kNone;
    }

    void update (const Vals& v)
    {
        voiceType = std::clamp (Vi (v, P::GlossVoice), 0, 2);
        plays = std::clamp (Vi (v, P::GlossPlays), 0, 4);
        spread = clampf (V (v, P::GlossSpread), 0.0f, 1.0f);
        weight = V (v, P::GlossWeight);
        settle = V (v, P::GlossSettle) * 0.001f * sr;
        glide = Vi (v, P::GlossMove) == 1;
        changeSamples = std::max (1.0f, V (v, P::GlossChange) * 0.001f * sr);
        ringOn = Vi (v, P::GlossRing) == 1;
        swellCoef = onePoleCoef (V (v, P::GlossSwell) * 0.001f / 3.0f, sr / kCtl);
        fadeCoef = onePoleCoef (V (v, P::GlossFade) * 0.001f / 4.0f, sr / kCtl);
        sens = V (v, P::GlossSens);
        bright = V (v, P::GlossBright);
        breathe = V (v, P::GlossBreathe);
        level = V (v, P::GlossLevel);
    }

    // in: the clean bass, out: the pad alone (the caller blends the clean bass)
    void process (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i) out[i] = tick (in[i]);
    }

    bool sounding() const { return amp > 1.0e-5f || ampTarget > 0; }
    // the note the pad is on (MIDI number), or -1
    int note() const { return cur == kNone ? -1 : cur; }

private:
    static constexpr int kNone = -1000;
    static constexpr int kCtl = 16;    // control rate: pitch, drift, envelopes, filter
    static constexpr int kOsc = 5;     // detuned oscillators per layer
    static constexpr int kVoices = 3;  // one sounding, up to two fading out

    struct Voice
    {
        int note = -1000;
        float gain = 0, target = 0, step = 0;
        float hz = 0, wantHz = 0;   // the played note (glide moves hz to wantHz)
        float ph[2][kOsc] {}, inc[2][kOsc] {};
        float subPh = 0, subInc = 0;
    };

    static float blep (float t, float dt)
    {
        if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
        if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
        return 0.0f;
    }

    // one detuned oscillator: saw (Warm), square (Hollow) or triangle (Soft)
    float wave (float t, float dt) const
    {
        switch (voiceType)
        {
            case 0: return 2.0f * t - 1.0f - blep (t, dt);
            case 1:
            {
                float y = t < 0.5f ? 1.0f : -1.0f;
                y += blep (t, dt);
                float t2 = t + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                return y - blep (t2, dt);
            }
            default: return 1.0f - 4.0f * std::fabs (t - 0.5f);
        }
    }

    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (1.0f / 16777216.0f); }

    static float noteHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

    void startVoice (int note)
    {
        const float hz = noteHz ((float) note + tuneOff);
        if (glide && amp > 0.02f && voices[0].gain > 0)
        {
            // one voice that slides to the new note
            for (int k = 1; k < kVoices; ++k) voices[k].target = 0, voices[k].step = 1.0f / changeSamples;
            voices[0].note = note; voices[0].wantHz = hz; voices[0].target = 1;
            return;
        }
        // the quietest slot takes the new note and fades in, the rest fade out
        int pick = 0;
        for (int k = 1; k < kVoices; ++k) if (voices[k].gain < voices[pick].gain) pick = k;
        if (glide) pick = 0;
        const bool fresh = amp < 0.02f;   // coming in from silence: no need to fade across
        for (int k = 0; k < kVoices; ++k)
        {
            auto& vc = voices[k];
            if (k == pick) continue;
            vc.target = 0;
            vc.step = fresh ? 1.0f : 1.0f / changeSamples;
        }
        auto& vc = voices[pick];
        vc.note = note; vc.hz = vc.wantHz = hz;
        vc.target = 1;
        vc.step = 1.0f / changeSamples;
        if (fresh) vc.gain = 1;
        for (auto& row : vc.ph) for (auto& p : row) p = rnd();   // free phases: no hard attack
        vc.subPh = 0;
    }

    // the layers each "Plays" choice sounds, as ratios of the played note, and their levels
    void layers (float (&r)[2], float (&g)[2]) const
    {
        switch (plays)
        {
            case 0: r[0] = 2.0f; g[0] = 1.0f; r[1] = 0; g[1] = 0; break;     // Octave up
            case 1: r[0] = 2.0f; g[0] = 1.0f; r[1] = 3.0f; g[1] = 0.6f; break;  // Octave and fifth
            case 2: r[0] = 1.0f; g[0] = 1.0f; r[1] = 0; g[1] = 0; break;     // Unison
            case 3: r[0] = 1.5f; g[0] = 1.0f; r[1] = 0; g[1] = 0; break;     // Fifth up
            default: r[0] = 2.0f; g[0] = 1.0f; r[1] = 4.0f; g[1] = 0.55f; break; // Two octaves
        }
    }

    void control()
    {
        // pitch, detune and drift for every voice that's sounding
        float r[2], g[2];
        layers (r, g);
        static const float spreadCents[kOsc] = { 0.0f, -11.0f, 11.0f, -23.0f, 23.0f };
        breathPh += 0.11f * kCtl / sr;
        if (breathPh >= 1.0f) breathPh -= 1.0f;
        for (int o = 0; o < kOsc; ++o)
        {
            // each oscillator wanders a few cents on its own slow path
            driftPh[o] += driftRate[o] * kCtl / sr;
            if (driftPh[o] >= 1.0f) driftPh[o] -= 1.0f;
            drift[o] = std::sin (kTwoPi * driftPh[o]) * 3.5f * spread;
        }
        for (auto& vc : voices)
        {
            if (vc.gain <= 0 && vc.target <= 0) continue;
            // the note, at your bass's tuning: a glide moves at the change time, otherwise
            // the pad eases into tune over half a second
            if (vc.note != kNone) vc.wantHz = noteHz ((float) vc.note + tuneOff);
            vc.hz = vc.wantHz + (vc.hz - vc.wantHz) * (glide ? glideCoef() : easeCoef);
            for (int l = 0; l < 2; ++l)
                for (int o = 0; o < kOsc; ++o)
                {
                    const float cents = spreadCents[o] * spread + drift[o];
                    vc.inc[l][o] = clampf (vc.hz * r[l] * std::pow (2.0f, cents / 1200.0f) / sr, 0.0f, 0.45f);
                }
            vc.subInc = vc.hz / sr;
        }
        layerGain[0] = g[0]; layerGain[1] = g[1];

        // the swell and the fade
        amp = ampTarget + (amp - ampTarget) * (ampTarget > amp ? swellCoef : fadeCoef);
        if (amp < 1.0e-5f && ampTarget <= 0) amp = 0;
        padLvl = playLvl + (padLvl - playLvl) * lvlCoef;

        // the filter blooms open as the pad swells in, and breathes
        const float open = std::pow (2.0f, -1.3f * (1.0f - amp));
        const float br = std::pow (2.0f, 0.7f * breathe * std::sin (kTwoPi * breathPh));
        const float fc = clampf (bright * open * br, 60.0f, sr * 0.45f);
        f1.set (fc, 0.15f, sr);
        f2.set (fc, 0.0f, sr);
    }

    float glideCoef() const { return std::exp (-3.0f * kCtl / changeSamples); }

    void listen (float x)
    {
        const float e = inEnv.process (x);
        playLvl = std::max (playLvl * playFall, e);
        playLvl = std::max (playLvl, 1.0e-3f);
        const float thr = playLvl * (0.25f - 0.22f * sens);
        if (! gateOpen && e > thr) gateOpen = true;
        else if (gateOpen && e < thr * 0.6f)
        {
            gateOpen = false; cand = kNone; stable = 0;
            // you've stopped: the pad fades (unless it rings on) and waits for the next note
            if (! ringOn) ampTarget = 0;
        }
        if (onset.process (x, playLvl)) smoothPeriod = 0;   // a new pluck may be any pitch

        // ---- the bass's own crossings give each cycle's length ----
        const float d = det2.process (det1.process (dc.process (x)));
        const float h = std::max (detEnv.process (d) * 0.2f, 1.0e-5f);
        sincePrev += 1.0f;
        sinceAny += 1.0f;
        if (! high && d > h)
        {
            const float frac = (d - dPrev) == 0.0f ? 1.0f : clampf ((h - dPrev) / (d - dPrev), 0.0f, 1.0f);
            const float t = 1.0f - frac;
            const float len = sincePrev - t, raw = sinceAny - t;
            const bool even = std::fabs (raw - lastRaw) < 0.15f * raw;
            lastRaw = raw;
            sinceAny = t;
            if (smoothPeriod > 0 && len <= smoothPeriod * 0.6f && gateOpen && even) { smoothPeriod = 0; sincePrev = raw + t; }
            const float len2 = sincePrev - t;
            if (smoothPeriod <= 0 || len2 > smoothPeriod * 0.6f || ! gateOpen)
            {
                const float l = std::min (len2, raw > 0 && smoothPeriod <= 0 ? raw : len2);
                if (l > sr / 1200.0f && l < sr / 25.0f)
                {
                    smoothPeriod = smoothPeriod <= 0 ? l : smoothPeriod + (l - smoothPeriod) * 0.35f;
                    cycle (l, e);
                }
                sincePrev = t;
            }
            high = true;
        }
        else if (high && d < -h) high = false;
        dPrev = d;
    }

    // Whole notes only: a cycle counts towards a note when it's within 40 cents of it. The
    // pad moves once the same note has held for the settle time, so quick passing notes,
    // slides, bends, ghost notes and the wobble of a fresh pluck never move it, and plucking
    // the same note again never restarts it. Notes snap to the semitone, offset by how your
    // bass is tuned (learnt slowly from settled notes), so the pad sits in tune with you.
    void cycle (float len, float e)
    {
        if (! gateOpen) return;
        const float m = 69.0f + 12.0f * std::log2 (sr / (len * 440.0f));
        const float rel = m - tuneOff;
        const int nn = (int) std::lround (rel);
        const float dev = rel - (float) nn;
        if (std::fabs (dev) > 0.4f) { cand = kNone; stable = 0; return; }
        if (nn == cand) { stable += len; devSum += dev; ++devCount; }
        else { cand = nn; stable = len; devSum = dev; devCount = 1; }
        if (stable < settle) return;
        // the first settled note sets how your bass is tuned, every one after nudges it
        if (! tuned) { tuneOff = clampf (tuneOff + devSum / (float) devCount, -0.45f, 0.45f); tuned = true; }
        else tuneOff = clampf (tuneOff + dev * 0.01f, -0.45f, 0.45f);
        // a note dying away can slip an octave in the detector: only a solid note moves the pad
        if (e < playLvl * 0.15f) return;
        if (cand != cur) { cur = cand; startVoice (cur); }
        ampTarget = 1;   // the same note again after a gap swells back in
    }

    float tick (float x)
    {
        listen (x);
        if (ctl-- <= 0) { ctl = kCtl - 1; control(); }
        if (amp <= 0 && ampTarget <= 0)
        {
            if (quiet < (int) (0.25f * sr)) ++quiet; else return 0.0f;
        }
        else quiet = 0;

        float sum = 0;
        for (auto& vc : voices)
        {
            if (vc.gain != vc.target)
                vc.gain = vc.target > vc.gain ? std::min (vc.target, vc.gain + vc.step) : std::max (vc.target, vc.gain - vc.step);
            if (vc.gain <= 0) continue;
            float v = 0;
            for (int l = 0; l < 2; ++l)
            {
                if (layerGain[l] <= 0) continue;
                float lv = 0;
                for (int o = 0; o < kOsc; ++o)
                {
                    float& p = vc.ph[l][o];
                    const float dt = vc.inc[l][o];
                    p += dt;
                    if (p >= 1.0f) p -= 1.0f;
                    lv += wave (p, dt);
                }
                v += lv * layerGain[l];
            }
            vc.subPh += vc.subInc;
            if (vc.subPh >= 1.0f) vc.subPh -= 1.0f;
            v = v * 0.28f + std::sin (kTwoPi * vc.subPh) * weight * 0.9f;
            // equal power across a fade: the voices are unrelated, so their powers add
            sum += v * std::sin (vc.gain * kPi * 0.5f);
        }

        // gentle 24 dB low-pass, then a little low cut so the pad stays off the bass's toes
        float lp, bp, y;
        f1.process (sum, lp, bp);
        f2.process (lp, y, bp);
        hpY = undenorm (hpCoef * (hpY + y - hpX));
        hpX = y;
        return undenorm (hpY * amp * padLvl * level * 2.6f);
    }

    float sr = 48000;
    Biquad det1, det2;
    DcBlock dc;
    Follower detEnv, inEnv;
    OnsetDetector onset;
    Svf f1, f2;
    Voice voices[kVoices];
    bool high = false, gateOpen = false, glide = false, ringOn = false;
    float sinceAny = 0, lastRaw = 0, dPrev = 0, sincePrev = 0, smoothPeriod = 0;
    float playLvl = 0.05f, playFall = 0.99999f, padLvl = 0, lvlCoef = 0.99f;
    int cand = kNone, cur = kNone, ctl = 0, quiet = 0;
    float stable = 0, tuneOff = 0, devSum = 0, easeCoef = 0.99f;
    int devCount = 0;
    bool tuned = false;
    float amp = 0, ampTarget = 0, swellCoef = 0.99f, fadeCoef = 0.99f;
    float breathPh = 0, hpCoef = 0.99f, hpX = 0, hpY = 0;
    float drift[kOsc] {}, driftPh[kOsc] { 0.0f, 0.31f, 0.57f, 0.12f, 0.84f };
    const float driftRate[kOsc] { 0.13f, 0.21f, 0.17f, 0.27f, 0.09f };
    float layerGain[2] { 1.0f, 0.0f };
    uint32_t seed = 0x6c6f7373u;
    int voiceType = 0, plays = 1;
    float spread = 0.5f, weight = 0.2f, settle = 8000, changeSamples = 20000;
    float sens = 0.6f, bright = 1800, breathe = 0.35f, level = 0.7f;
};
} // namespace ks
