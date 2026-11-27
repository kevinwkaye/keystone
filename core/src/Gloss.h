#pragma once
// Gloss (0.6.7): a lush pad under the bass, on hold B in Grit.
//
// 0.6.1's Gloss was a synth that followed every cycle and fired on every pluck: too close
// to Bitrot. Now it's a pad that only follows whole notes. It listens to the bass's own
// crossings, waits for a note to hold steady (Settle), snaps it to the semitone and only
// then moves, fading across (or gliding) to the new note. Passing notes, slides, bends and
// replucks of the same note leave it where it is.
//
// Built the way high-end pads are:
//  - Thick oscillators: five detuned per layer, saw (Warm), pulse with moving width
//    (Hollow) or triangle (Soft). Plays: octave up, octave and fifth, open sus2 (octave,
//    fifth, ninth), unison, fifth up or two octaves.
//  - Layers with a job each: body (the oscillators), shine (a sine an octave over the top),
//    air (breathy band-passed noise) and weight (a sine at your note).
//  - Slow shapes: it swells in, trails off (or rings on), and each new note sweeps the
//    filter up over the Bloom time.
//  - Constant movement: slow LFOs on cutoff, pulse width and layer balance, each at its own
//    rate between 0.1 and 0.5 Hz, plus a pitch drift of its own for every oscillator.
//  - Wander, the unpredictable bit: slow random walks on brightness, level, layer balance,
//    shine and every oscillator's pitch, and each note comes out a little different.
//  - A warm low-pass with a little resonance that opens when you dig in (Touch).
//  - Saturation for glue (Warmth), then a Juno-style chorus. Space comes from Keystone's
//    own reverb: To reverb sends the pad in wetter than the bass.
// Keystone is mono into one amp, so the chorus and drift give the width a stereo spread
// would.
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
        pickEnv.set (0.0005f, 0.3f, sr);
        onset.prepare (sr);
        playFall = onePoleCoef (4.0f, sr);
        const float cr = sr / kCtl;
        lvlCoef = onePoleCoef (0.6f, cr);
        easeCoef = onePoleCoef (0.5f, cr);
        touchCoef = onePoleCoef (1.2f, cr);
        wanderCoef = onePoleCoef (1.8f, cr);    // the random walks: a couple of seconds to turn
        hpCoef = std::exp (-kTwoPi * 45.0f / sr);
        bbdCoef = std::exp (-kTwoPi * 7000.0f / sr);
        chorusLen = (int) chorusBuf.size();
        // every slow movement gets its own rate between 0.1 and 0.5 Hz, so they never line up
        for (auto& r : lfoRate) r = 0.1f + 0.4f * rnd();
        for (int o = 0; o < kOsc; ++o) { driftRate[o] = 0.1f + 0.4f * rnd(); driftPh[o] = rnd(); }
        reset();
    }

    void reset()
    {
        det1.reset(); det2.reset(); dc.reset();
        detEnv.reset(); inEnv.reset(); pickEnv.reset(); onset.reset();
        f1.reset(); f2.reset(); airF.reset();
        high = false; dPrev = 0; sincePrev = 0; sinceAny = 0; lastRaw = 0; smoothPeriod = 0;
        playLvl = 0.05f; padLvl = 0; gateOpen = false; touch = 1;
        cand = kNone; stable = 0; cur = kNone; tuneOff = 0; tuned = false; devSum = 0; devCount = 0;
        amp = 0; ampTarget = 0; bloomLvl = 1; ctl = 0; hpX = hpY = 0; bbd = 0;
        chorusBuf.fill (0.0f); chorusPos = 0; chorusPh = 0;
        for (auto& w : walk) w = 0;
        for (auto& w : walkTarget) w = 0;
        walkCount = 0;
        for (auto& vc : voices) vc.gain = vc.target = 0, vc.note = kNone;
    }

    void update (const Vals& v)
    {
        voiceType = std::clamp (Vi (v, P::GlossVoice), 0, 2);
        plays = std::clamp (Vi (v, P::GlossPlays), 0, 5);
        spread = clampf (V (v, P::GlossSpread), 0.0f, 1.0f);
        shine = V (v, P::GlossShine);
        air = V (v, P::GlossAir);
        weight = V (v, P::GlossWeight);
        settle = V (v, P::GlossSettle) * 0.001f * sr;
        glide = Vi (v, P::GlossMove) == 1;
        changeSamples = std::max (1.0f, V (v, P::GlossChange) * 0.001f * sr);
        ringOn = Vi (v, P::GlossRing) == 1;
        swellCoef = onePoleCoef (V (v, P::GlossSwell) * 0.001f / 3.0f, sr / kCtl);
        fadeCoef = onePoleCoef (V (v, P::GlossFade) * 0.001f / 4.0f, sr / kCtl);
        bloomCoef = onePoleCoef (V (v, P::GlossBloom) * 0.001f / 3.0f, sr / kCtl);
        sens = V (v, P::GlossSens);
        bright = V (v, P::GlossBright);
        breathe = V (v, P::GlossBreathe);
        wander = V (v, P::GlossWander);
        touchAmt = V (v, P::GlossTouch);
        warmth = V (v, P::GlossWarmth);
        chorus = V (v, P::GlossChorus);
        level = V (v, P::GlossLevel);
    }

    // in: the clean bass, out: the pad alone (the caller blends the clean bass)
    void process (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i) out[i] = tick (in[i]);
    }

    bool sounding() const { return amp > 1.0e-5f || ampTarget > 0; }
    // Switched off, the pad stops taking new notes and fades out over its Fade time, so it
    // leaves a tail instead of cutting (0.6.10). Switched back on mid-tail, it carries on.
    void setEnabled (bool on) { enabled = on; if (! on) ampTarget = 0; }
    // the note the pad is on (MIDI number), or -1
    int note() const { return cur == kNone ? -1 : cur; }
    // where the filter sits (Hz), for the tests
    float cutoff() const { return fcNow; }

private:
    static constexpr int kNone = -1000;
    static constexpr int kCtl = 16;    // control rate: pitch, drift, envelopes, filter
    static constexpr int kOsc = 5;     // detuned oscillators per layer
    static constexpr int kLayers = 3;
    static constexpr int kVoices = 3;  // one sounding, up to two fading out
    static constexpr int kWalks = 4;   // Wander: brightness, level, layer balance, shine

    struct Voice
    {
        int note = -1000;
        float gain = 0, target = 0, step = 0;
        float hz = 0, wantHz = 0;   // the played note (glide moves hz to wantHz)
        float ph[kLayers][kOsc] {}, inc[kLayers][kOsc] {};
        float subPh = 0, subInc = 0, shinePh = 0, shineInc = 0;
        // Wander: each note comes out a little different
        float detuneScale = 1, oscCents[kOsc] {}, oscWalk[kOsc] {}, oscWalkTarget[kOsc] {};
    };

    static float blep (float t, float dt)
    {
        if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
        if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
        return 0.0f;
    }

    // one detuned oscillator: saw (Warm), pulse with slow width movement (Hollow) or
    // triangle (Soft)
    float wave (float t, float dt) const
    {
        switch (voiceType)
        {
            case 0: return 2.0f * t - 1.0f - blep (t, dt);
            case 1:
            {
                float y = t < pw ? 1.0f : -1.0f;
                y += blep (t, dt);
                float t2 = t + 1.0f - pw; if (t2 >= 1.0f) t2 -= 1.0f;
                return y - blep (t2, dt);
            }
            default: return 1.0f - 4.0f * std::fabs (t - 0.5f);
        }
    }

    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) (seed >> 8) * (1.0f / 16777216.0f); }
    float rnd2() { return 2.0f * rnd() - 1.0f; }

    static float noteHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

    void startVoice (int note)
    {
        const float hz = noteHz ((float) note + tuneOff);
        bloomLvl = 0;   // each new note sweeps the filter up again
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
        vc.subPh = 0; vc.shinePh = rnd();
        // Wander: a slightly different spread and a few cents of its own for each oscillator
        vc.detuneScale = 1.0f + 0.3f * wander * rnd2();
        for (int o = 0; o < kOsc; ++o) { vc.oscCents[o] = 4.0f * wander * rnd2(); vc.oscWalk[o] = vc.oscWalkTarget[o] = 0; }
    }

    // the layers each "Plays" choice sounds, as ratios of the played note, and their levels
    void layers (float (&r)[kLayers], float (&g)[kLayers]) const
    {
        for (int l = 0; l < kLayers; ++l) r[l] = 1.0f, g[l] = 0.0f;
        switch (plays)
        {
            case 0: r[0] = 2.0f; g[0] = 1.0f; break;                                             // Octave up
            case 1: r[0] = 2.0f; g[0] = 1.0f; r[1] = 3.0f; g[1] = 0.6f; break;                     // Octave and fifth
            case 2: r[0] = 2.0f; g[0] = 1.0f; r[1] = 3.0f; g[1] = 0.55f; r[2] = 4.5f; g[2] = 0.4f; break; // Open sus2: octave, fifth, ninth
            case 3: r[0] = 1.0f; g[0] = 1.0f; break;                                             // Unison
            case 4: r[0] = 1.5f; g[0] = 1.0f; break;                                             // Fifth up
            default: r[0] = 2.0f; g[0] = 1.0f; r[1] = 4.0f; g[1] = 0.55f; break;                   // Two octaves
        }
    }

    void control()
    {
        float r[kLayers], g[kLayers];
        layers (r, g);
        float top = 1.0f;
        for (int l = 0; l < kLayers; ++l) if (g[l] > 0) top = std::max (top, r[l]);
        const float dtc = (float) kCtl / sr;
        static const float spreadCents[kOsc] = { 0.0f, -11.0f, 11.0f, -23.0f, 23.0f };

        // the slow LFOs: filter, pulse width, layer balance, each at its own rate
        for (int k = 0; k < 3; ++k) { lfoPh[k] += lfoRate[k] * dtc; if (lfoPh[k] >= 1.0f) lfoPh[k] -= 1.0f; }
        const float lfoCut = std::sin (kTwoPi * lfoPh[0]);
        pw = 0.5f - 0.2f * (0.5f + 0.5f * breathe) * (0.5f + 0.5f * std::sin (kTwoPi * lfoPh[1]));
        const float lfoBal = std::sin (kTwoPi * lfoPh[2]);

        // Wander: slow random walks that pick a new place every few seconds and drift to it
        if (--walkCount <= 0)
        {
            walkCount = (int) ((2.5f + 4.0f * rnd()) / dtc);
            for (auto& t : walkTarget) t = rnd2();
        }
        for (int k = 0; k < kWalks; ++k) walk[k] = walkTarget[k] + (walk[k] - walkTarget[k]) * wanderCoef;

        for (int o = 0; o < kOsc; ++o)
        {
            driftPh[o] += driftRate[o] * dtc;
            if (driftPh[o] >= 1.0f) driftPh[o] -= 1.0f;
            drift[o] = std::sin (kTwoPi * driftPh[o]) * 3.0f * spread;
        }
        for (auto& vc : voices)
        {
            if (vc.gain <= 0 && vc.target <= 0) continue;
            // the note, at your bass's tuning: a glide moves at the change time, otherwise
            // the pad eases into tune over half a second
            if (vc.note != kNone) vc.wantHz = noteHz ((float) vc.note + tuneOff);
            vc.hz = vc.wantHz + (vc.hz - vc.wantHz) * (glide ? glideCoef() : easeCoef);
            for (int o = 0; o < kOsc; ++o)
            {
                // each oscillator's own random pitch walk, like old analogue gear
                if (rnd() < dtc * 0.4f) vc.oscWalkTarget[o] = rnd2();
                vc.oscWalk[o] = vc.oscWalkTarget[o] + (vc.oscWalk[o] - vc.oscWalkTarget[o]) * wanderCoef;
            }
            for (int l = 0; l < kLayers; ++l)
            {
                if (g[l] <= 0) continue;
                for (int o = 0; o < kOsc; ++o)
                {
                    const float cents = spreadCents[o] * spread * vc.detuneScale + drift[o]
                                      + (vc.oscCents[o] + 5.0f * vc.oscWalk[o]) * wander;
                    vc.inc[l][o] = clampf (vc.hz * r[l] * std::pow (2.0f, cents / 1200.0f) / sr, 0.0f, 0.45f);
                }
            }
            vc.subInc = vc.hz / sr;
            vc.shineInc = clampf (vc.hz * top * 2.0f / sr, 0.0f, 0.45f);
        }
        // the layers above the first breathe against each other a little
        for (int l = 0; l < kLayers; ++l)
            layerGain[l] = g[l] * (l == 0 ? 1.0f : 1.0f + 0.25f * breathe * lfoBal + 0.3f * wander * walk[2]);
        shineNow = shine * (1.0f + 0.6f * wander * walk[3]);

        // the swell, the fade and the bloom
        amp = ampTarget + (amp - ampTarget) * (ampTarget > amp ? swellCoef : fadeCoef);
        if (amp < 1.0e-5f && ampTarget <= 0) amp = 0;
        bloomLvl = 1.0f + (bloomLvl - 1.0f) * bloomCoef;
        padLvl = playLvl + (padLvl - playLvl) * lvlCoef;
        levelWander = 1.0f + 0.12f * wander * walk[1];   // about 1 dB at full Wander

        // dig in and it opens up: how hard you're playing against your usual level
        const float hard = clampf (pickEnv.env / std::max (playLvl, 1.0e-4f), 0.3f, 1.5f);
        touch = hard + (touch - hard) * touchCoef;

        // the filter: opens as the pad swells, sweeps up on each new note, breathes,
        // wanders and follows how hard you play
        const float oct = -1.3f * (1.0f - amp) - 1.0f * (1.0f - bloomLvl)
                        + 0.7f * breathe * lfoCut + 0.45f * wander * walk[0]
                        + 1.2f * touchAmt * (touch - 0.8f);
        const float fc = clampf (bright * std::pow (2.0f, oct), 60.0f, sr * 0.45f);
        fcNow = fc;
        f1.set (fc, 0.3f, sr);
        f2.set (fc, 0.0f, sr);
        airF.set (clampf (fc * 1.6f, 300.0f, sr * 0.4f), 0.25f, sr);
    }

    float glideCoef() const { return std::exp (-3.0f * kCtl / changeSamples); }

    void listen (float x)
    {
        const float e = inEnv.process (x);
        pickEnv.process (x);
        playLvl = std::max (playLvl * playFall, e);
        playLvl = std::max (playLvl, 1.0e-3f);
        const float thr = playLvl * (0.25f - 0.22f * sens);
        if (! gateOpen && e > thr) gateOpen = true;
        else if (gateOpen && e < thr * 0.6f)
        {
            gateOpen = false; cand = kNone; stable = 0;
            // you've stopped: the pad fades (unless it rings on) and waits for the next note
            if (! ringOn || ! enabled) ampTarget = 0;
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
    // bass is tuned (learnt from settled notes), so the pad sits in tune with you.
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
        if (stable < settle || ! enabled) return;
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
            if (quiet < (int) (0.5f * sr)) ++quiet; else return 0.0f;   // lets the chorus empty
        }
        else quiet = 0;

        // ---- the layers: body (the detuned oscillators), shine (a sine an octave over the
        // top) and weight (a sine at your note) ----
        float sum = 0;
        for (auto& vc : voices)
        {
            if (vc.gain != vc.target)
                vc.gain = vc.target > vc.gain ? std::min (vc.target, vc.gain + vc.step) : std::max (vc.target, vc.gain - vc.step);
            if (vc.gain <= 0) continue;
            float v = 0;
            for (int l = 0; l < kLayers; ++l)
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
            vc.shinePh += vc.shineInc;
            if (vc.shinePh >= 1.0f) vc.shinePh -= 1.0f;
            v = v * 0.28f + std::sin (kTwoPi * vc.subPh) * weight * 0.9f + std::sin (kTwoPi * vc.shinePh) * shineNow * 0.5f;
            // equal power across a fade: the voices are unrelated, so their powers add
            sum += v * std::sin (vc.gain * kPi * 0.5f);
        }

        // warm 24 dB low-pass with a little resonance
        float lp, bp, y;
        f1.process (sum, lp, bp);
        f2.process (lp, y, bp);

        // air: breathy noise, band-passed just above the filter
        if (air > 0)
        {
            float alp, abp;
            airF.process (rnd2(), alp, abp);
            y += abp * air * 0.7f;
        }

        // saturation for glue: a soft tape-like curve, a touch lopsided for warmth
        if (warmth > 0)
        {
            const float k = 0.5f + 1.5f * warmth;
            const float sat = std::tanh (k * (y + 0.08f * warmth * y * y)) / k;
            y += (sat - y) * warmth;
        }

        // Juno-style chorus: a short delay swept by a slow triangle, two taps in opposite
        // directions, a little darker like a bucket brigade
        if (chorus > 0)
        {
            chorusBuf[(size_t) chorusPos] = y;
            chorusPh += 0.48f / sr;
            if (chorusPh >= 1.0f) chorusPh -= 1.0f;
            const float tri = 1.0f - 4.0f * std::fabs (chorusPh - 0.5f);   // -1..1
            const float base = 0.0055f * sr, depth = 0.0022f * sr;
            const float wet = 0.5f * (readChorus (base + depth * tri) + readChorus (base - depth * tri));
            bbd = undenorm (wet + (bbd - wet) * bbdCoef);
            y = y * (1.0f - 0.35f * chorus) + bbd * 0.75f * chorus;
            chorusPos = (chorusPos + 1) % chorusLen;
        }

        // a little low cut so the pad stays off the bass's toes
        hpY = undenorm (hpCoef * (hpY + y - hpX));
        hpX = y;
        return undenorm (hpY * amp * padLvl * level * levelWander * 2.6f);
    }

    float readChorus (float delay) const
    {
        float pos = (float) chorusPos - delay;
        while (pos < 0) pos += (float) chorusLen;
        const int i0 = (int) pos;
        const float f = pos - (float) i0;
        const float a = chorusBuf[(size_t) i0], b = chorusBuf[(size_t) ((i0 + 1) % chorusLen)];
        return a + (b - a) * f;
    }

    float sr = 48000;
    Biquad det1, det2;
    DcBlock dc;
    Follower detEnv, inEnv, pickEnv;
    OnsetDetector onset;
    Svf f1, f2, airF;
    Voice voices[kVoices];
    bool high = false, gateOpen = false, glide = false, ringOn = false, enabled = true;
    float sinceAny = 0, lastRaw = 0, dPrev = 0, sincePrev = 0, smoothPeriod = 0;
    float playLvl = 0.05f, playFall = 0.99999f, padLvl = 0, lvlCoef = 0.99f;
    int cand = kNone, cur = kNone, ctl = 0, quiet = 0;
    float stable = 0, tuneOff = 0, devSum = 0, easeCoef = 0.99f;
    int devCount = 0;
    bool tuned = false;
    float amp = 0, ampTarget = 0, swellCoef = 0.99f, fadeCoef = 0.99f, bloomLvl = 1, bloomCoef = 0.99f;
    float hpCoef = 0.99f, hpX = 0, hpY = 0, fcNow = 0;
    float touch = 1, touchCoef = 0.99f, wanderCoef = 0.99f, levelWander = 1;
    float lfoPh[3] { 0.0f, 0.33f, 0.66f }, lfoRate[3] { 0.11f, 0.23f, 0.37f };
    float drift[kOsc] {}, driftPh[kOsc] {}, driftRate[kOsc] {};
    float walk[kWalks] {}, walkTarget[kWalks] {};
    int walkCount = 0;
    float layerGain[kLayers] { 1.0f, 0.0f, 0.0f }, shineNow = 0, pw = 0.5f;
    std::array<float, 1024> chorusBuf {};
    int chorusPos = 0, chorusLen = 1024;
    float chorusPh = 0, bbd = 0, bbdCoef = 0.5f;
    uint32_t seed = 0x6c6f7373u;
    int voiceType = 0, plays = 1;
    float spread = 0.5f, shine = 0.2f, air = 0.15f, weight = 0.2f, settle = 8000, changeSamples = 20000;
    float sens = 0.6f, bright = 1800, breathe = 0.35f, wander = 0.3f, touchAmt = 0.4f, warmth = 0.3f, chorus = 0.5f, level = 0.7f;
};
} // namespace ks
