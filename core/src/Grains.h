#pragma once
// Speckle-style grains as ear candy: pitched up an octave, an octave and a fifth, two
// octaves, or a scatter of those, high-passed so the sparkle lands in the open 1 to 4 kHz
// space. Mono-friendly movement from timing and pitch variation, not stereo spread.
//
// Lessons applied: band-limited polyphase read head for pitch-ups (Backtrack), grains
// only spawn from audio that has sound in it, judged against the playing level (Speckle).

#include "Modules.h"

namespace ks
{
class Grains
{
public:
    static constexpr int kMaxVoices = 8, kPhases = 128, kMaxTaps = 32;
    static constexpr int kEnvTable = 1024, kLevelDecim = 64;

    void prepare (float sr_)
    {
        sr = sr_;
        int n = 1;
        while (n < (int) (2.8f * sr)) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        level.assign ((size_t) (n / kLevelDecim), 0.0f);
        levelMask = (int) level.size() - 1;
        buildTables (kMaxTaps);
        for (int i = 0; i < kEnvTable; ++i)
        {
            const float t = (float) i / (kEnvTable - 1);
            // bell: quick raised-cosine attack, then a ringing decay to zero
            const float a = 0.1f;
            bell[i] = t < a ? 0.5f - 0.5f * std::cos (kPi * t / a)
                            : std::exp (-3.5f * (t - a) / (1 - a)) * (0.5f + 0.5f * std::cos (kPi * (t - a) / (1 - a)));
            smooth[i] = 0.5f - 0.5f * std::cos (kTwoPi * t);
        }
        hp1.reset(); hp2.reset(); shelf.reset();
        mixS.setTime (0.05f, sr);
        reset();
    }
    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        std::fill (level.begin(), level.end(), 0.0f);
        for (auto& v : voices) v.on = false;
        wp = 0; levelAcc = 0; levelCount = 0; untilNext = 0; fbSample = 0;
        mixS.reset (0);
        activity = 0;
    }

    void update (const Vals& v, float bloomZone2, bool bloomActive, bool light)
    {
        const bool on = Vb (v, P::GrOn);
        const float mix = V (v, P::GrMix);
        float m = on ? mix : 0.0f;
        densityScale = 1.0f;
        if (bloomActive)
        {
            // a third to two thirds: grains join, mix and density rising over the drone
            const float reach = V (v, P::BloomGrains);
            const float bm = bloomZone2 * reach * std::max (mix, 0.35f);
            if (bm > m) { m = bm; densityScale = 0.3f + 0.7f * bloomZone2; }
        }
        mixS.set (m);
        pitchMode = Vi (v, P::GrPitch);
        scatter = V (v, P::GrScatter);
        sizeS = V (v, P::GrSize) * 0.001f * sr;
        density = V (v, P::GrDensity);
        spray = V (v, P::GrSpray);
        reverseProb = V (v, P::GrReverse);
        feedback = V (v, P::GrFeedback);
        bellShape = Vi (v, P::GrShape) == 0;
        dirty = Vi (v, P::GrSource) == 1;
        const float hp = V (v, P::GrHighPass), br = V (v, P::GrBright);
        if (hp != lastHp) { hp1.highPass (hp, 0.5412f, sr); hp2.highPass (hp, 1.3066f, sr); lastHp = hp; }
        if (br != lastBright) { shelf.highShelf (3000.0f, br, sr, 0.8f); lastBright = br; }
        const int taps = light ? 16 : kMaxTaps;
        if (taps != tapsNow) buildTables (taps);
        maxVoices = light ? 3 : kMaxVoices;
    }

    bool sourceDirty() const { return dirty; }
    bool active() const { return mixS.value > 1.0e-4f || mixS.target > 0 || anyVoice; }

    // source: the clean or dirty signal (always recorded, so grains have history)
    // returns the wet grain signal, already scaled by mix
    float process (float source, float playLevel)
    {
        wp = (wp + 1) & mask;
        buf[(size_t) wp] = source + fbSample;
        levelAcc = std::max (levelAcc, std::fabs (source));
        if (++levelCount >= kLevelDecim)
        {
            level[(size_t) ((wp / kLevelDecim) & levelMask)] = levelAcc;
            levelAcc = 0; levelCount = 0;
        }

        const float m = mixS.next();
        if (m < 1.0e-4f && ! anyVoice) { fbSample = 0; activity *= 0.999f; return 0.0f; }

        if (--untilNext <= 0 && m > 1.0e-4f) spawn (playLevel);

        float sum = 0;
        anyVoice = false;
        int count = 0;
        for (auto& g : voices)
        {
            if (! g.on) continue;
            anyVoice = true;
            ++count;
            const float t = (float) g.age / (float) g.len;
            const float* env = g.bell ? bell : smooth;
            const float e = env[std::min (kEnvTable - 1, (int) (t * (kEnvTable - 1)))];
            sum += read (g.pos, g.table) * e * g.gain;
            g.pos += g.inc;
            if (++g.age >= g.len) g.on = false;
        }
        activity += ((float) count / (float) maxVoices - activity) * 0.001f;
        float y = sum * norm;
        y = shelf.process (hp2.process (hp1.process (y)));
        // feedback back into the buffer, gently limited so cascades never run away
        fbSample = std::tanh (y * feedback * 1.2f) * 0.8f;
        return y * m;
    }
    float activityLevel() const { return activity; }

private:
    struct Voice
    {
        bool on = false, bell = true;
        double pos = 0;
        double inc = 2;
        int len = 1, age = 0, table = 0;
        float gain = 1;
    };

    void buildTables (int taps)
    {
        tapsNow = taps;
        // windowed sinc per pitch ratio (2, 3, 4): cutoff below the new Nyquist
        const float ratios[3] = { 2.0f, 3.0f, 4.0f };
        for (int r = 0; r < 3; ++r)
        {
            const double fc = 0.47 / ratios[r];
            for (int ph = 0; ph < kPhases; ++ph)
            {
                const double frac = (double) ph / kPhases;
                double sum = 0;
                for (int k = 0; k < taps; ++k)
                {
                    const double x = (double) (k - taps / 2 + 1) - frac;
                    const double s = std::fabs (x) < 1e-9 ? 2 * fc : std::sin (2 * M_PI * fc * x) / (M_PI * x);
                    const double w = 0.42 + 0.5 * std::cos (M_PI * x / (taps / 2.0)) + 0.08 * std::cos (2 * M_PI * x / (taps / 2.0));
                    const double h = std::fabs (x) >= taps / 2.0 ? 0.0 : s * w;
                    table[r][ph][k] = (float) h;
                    sum += h;
                }
                if (sum != 0) for (int k = 0; k < taps; ++k) table[r][ph][k] = (float) (table[r][ph][k] / sum);
            }
        }
    }

    float read (double pos, int t) const
    {
        const int i = (int) std::floor (pos);
        const float frac = (float) (pos - i);
        const int ph = std::min (kPhases - 1, (int) (frac * kPhases));
        const float* h = table[t][ph];
        float s = 0;
        const int base = i - tapsNow / 2 + 1;
        for (int k = 0; k < tapsNow; ++k) s += h[k] * buf[(size_t) ((base + k) & mask)];
        return s;
    }

    float levelAt (int pos) const { return level[(size_t) (((pos & mask) / kLevelDecim) & levelMask)]; }

    void spawn (float playLevel)
    {
        const float rate = std::max (0.5f, density * densityScale);
        const float jitter = 1.0f + scatter * 0.8f * rng.bi();
        untilNext = std::max (16, (int) (sr / rate * jitter));

        Voice* g = nullptr;
        int busy = 0;
        for (auto& v : voices) if (v.on) ++busy;
        if (busy >= maxVoices) return;
        for (auto& v : voices) if (! v.on) { g = &v; break; }
        if (g == nullptr) return;

        int choice = pitchMode;
        if (pitchMode == 3) choice = (int) (rng.uni() * 3.0f) % 3;   // scatter, always in key
        const float ratios[3] = { 2.0f, 3.0f, 4.0f };
        const float detuneCents = scatter * 10.0f * rng.bi();
        const float ratio = ratios[choice] * std::pow (2.0f, detuneCents / 1200.0f);
        const int len = std::max (64, (int) (sizeS * (1.0f + scatter * 0.3f * rng.bi())));
        const bool rev = rng.uni() < reverseProb;
        const int offset = (int) (0.015f * sr + spray * 0.35f * sr * rng.uni()) + tapsNow;
        const int travel = (int) (ratio * len) + tapsNow;
        int start = rev ? wp - offset : wp - offset - travel;
        if ((offset + travel) * 1 >= mask - 64) return;

        // only spawn from audio that actually has a note in it
        const int centre = rev ? start - travel / 2 : start + travel / 2;
        const float lv = std::max (levelAt (centre), levelAt (start));
        if (lv < std::max (playLevel * 0.0708f, 2.0e-5f)) { untilNext = (int) (0.01f * sr); return; }

        g->on = true;
        g->bell = bellShape;
        g->pos = start;
        g->inc = rev ? -ratio : ratio;
        g->len = len;
        g->age = 0;
        g->table = choice;
        g->gain = 1.0f + 0.15f * scatter * rng.bi();
        const float overlap = std::max (1.0f, rate * len / sr);
        norm = 1.6f / std::sqrt (overlap);
    }

    float sr = 48000;
    std::vector<float> buf, level;
    int mask = 0, levelMask = 0, wp = 0, levelCount = 0;
    float levelAcc = 0;
    float table[3][kPhases][kMaxTaps] {};
    int tapsNow = kMaxTaps;
    float bell[kEnvTable] {}, smooth[kEnvTable] {};
    Voice voices[kMaxVoices];
    int maxVoices = kMaxVoices, untilNext = 0;
    bool anyVoice = false, bellShape = true, dirty = false;
    int pitchMode = 1;
    float scatter = 0.4f, sizeS = 3840, density = 12, densityScale = 1, spray = 0.3f, reverseProb = 0.2f, feedback = 0.25f;
    float norm = 1, fbSample = 0, activity = 0;
    float lastHp = -1, lastBright = -99;
    Biquad hp1, hp2, shelf;
    Smoothed mixS;
    Rng rng;
};
} // namespace ks
