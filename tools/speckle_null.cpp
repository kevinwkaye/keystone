// Speckle null test: the same bass through Keystone's Speckle and through the Speckle
// plugin as Kaleidoscope hosts it, with the same settings. Prints how far apart the two
// wet signals are for each of the eleven effects, and fails if any is closer than 60 dB
// to the plugin's own level (they null at about -83 dB, float rounding). Columns:
//   plug/plug  the plugin against itself (proves it is repeatable)
//   wrapper    Keystone's Speckle wrapper (Grains.h) fed the raw bass
//   +inputs    the same, fed the old input (the full sound with its 50 Hz sub cut): 0.6.3
//   engine     Keystone's whole engine, Speckle's part of the output
// Usage: speckle_null [effect index]
#include "keystone/Engine.h"
#include "Grains.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>

using namespace ks;
static constexpr double kSr = 48000;
static constexpr int kBlk = 128;

static std::vector<float> bassLine()
{
    // a bass riff: plucked notes with harmonics, real-ish attack, a little hiss
    const double notes[] = { 41.2, 41.2, 55.0, 61.7, 73.4, 55.0, 49.0, 41.2 };
    std::vector<float> x;
    std::mt19937 rng (7);
    std::normal_distribution<float> n (0.0f, 1.0e-4f);
    for (int rep = 0; rep < 2; ++rep)
        for (double f : notes)
        {
            const double len = 0.5;
            for (int i = 0; i < (int) (len * kSr); ++i)
            {
                const double t = i / kSr;
                double s = 0;
                for (int k = 1; k <= 16; ++k) s += std::sin (2 * M_PI * f * k * t + k) / (k * k * 0.5 + 0.5);
                const double env = std::min (1.0, t / 0.003) * std::exp (-t / 0.9);
                x.push_back ((float) (0.25 * s * env) + n (rng));
            }
        }
    x.resize (x.size() + (size_t) (3 * kSr), 0.0f);   // tails
    return x;
}

struct Match { int effect; int variant; };

// Kaleidoscope: PedalRack feeds the clean bass to both inputs, sums L and R by half
static std::vector<float> pluginWet (const std::vector<float>& in, int effect)
{
    spk::SpeckleEngine e;
    e.prepare ((float) kSr);
    spk::EngineParams p;   // the plugin's defaults (Params.h in the speckle repo)
    p.effect = effect; p.variant = 1; p.activity = 0.55f; p.repeats = 0.5f; p.shape = 0.5f;
    p.beatSamples = (float) (60.0 / 120.0 * kSr); p.stepSamples = p.beatSamples * 0.5f;
    p.filter = 1.0f; p.resonance = 0.2f; p.mix = 0.5f; p.volumeDb = 0; p.space = 0.4f; p.reverbMode = 3;
    p.modDepth = 0.12f; p.modRateHz = 0.05f * std::pow (2.0f, 0.45f * 7.3f);
    p.inputMode = spk::InputGuitar; p.hold = false; p.bypass = false; p.trails = true;
    p.loopLevel = 0.8f;
    std::vector<float> out (in.size());
    std::vector<float> L (kBlk), R (kBlk);
    for (size_t i = 0; i < in.size(); i += kBlk)
    {
        const int n = (int) std::min<size_t> (kBlk, in.size() - i);
        for (int k = 0; k < n; ++k) L[k] = R[k] = in[i + k];
        e.process (L.data(), R.data(), n, p);
        // the plugin passes its dry at full level (Mix 50%), so wet = out - in
        for (int k = 0; k < n; ++k) out[i + k] = 0.5f * (L[k] + R[k]) - in[i + k];
    }
    return out;
}

static std::vector<float> runKeystone (const std::vector<float>& in, int effect, bool grOn, bool soloSpeckle)
{
    Engine e;
    e.prepare (kSr, kBlk);
    e.setParam (P::OutQuality, 1);   // Full
    e.setParam (P::DlOn, 0); e.setParam (P::RvOn, 0);
    e.setParam (P::GrOn, grOn ? 1 : 0);
    e.setParam (P::GrEffect, (float) effect);
    e.setParam (P::GrVariant, 1);
    e.setParam (P::GrModDepth, 0.12f); e.setParam (P::GrModRate, 0.45f);
    e.setParam (P::GrSpaceType, 3);
    e.setParam (P::GrMix, 0.5f);
    e.setParam (P::Tempo, 120);
    (void) soloSpeckle;
    // Everything else stays at its default (drive, preamp, Iron and the rest all on). Only
    // the safety limiter is off: it acts on dry and Speckle together, so taking Speckle
    // away changes what it does and the subtraction below would not be Speckle alone.
    e.setParam (P::OutLimit, 0);
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += kBlk)
    {
        const int n = (int) std::min<size_t> (kBlk, in.size() - i);
        e.process (in.data() + i, out.data() + i, n);
        e.idle();
    }
    return out;
}

// Keystone's Speckle wrapper on its own (Grains.h), fed whatever input we give it
static std::vector<float> grainsWet (const std::vector<float>& in, int effect)
{
    Grains g;
    g.prepare ((float) kSr);
    Vals v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = info ((P) i).def;
    v[(size_t) P::GrOn] = 1; v[(size_t) P::GrEffect] = (float) effect; v[(size_t) P::GrVariant] = 1;
    v[(size_t) P::GrModDepth] = 0.12f; v[(size_t) P::GrModRate] = 0.45f; v[(size_t) P::GrSpaceType] = 3;
    v[(size_t) P::GrMix] = 0.5f; v[(size_t) P::Tempo] = 120;
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += kBlk)
    {
        g.update (v, 0.0f, false, false);
        const int n = (int) std::min<size_t> (kBlk, in.size() - i);
        for (int k = 0; k < n; ++k) out[i + k] = g.process (in[i + k], 0.0f);
    }
    return out;
}

static double rms (const std::vector<float>& x) { double s = 0; for (float v : x) s += (double) v * v; return std::sqrt (s / x.size()); }
static double db (double r) { return 20 * std::log10 (r + 1e-12); }

static double compare (const std::vector<float>& ref, const std::vector<float>& x, int& lagOut)
{
    int bestLag = 0; double bestErr = 1e30;
    for (int lag = -1024; lag <= 1024; ++lag)
    {
        double err = 0;
        for (size_t i = 24000; i + 4096 < ref.size(); i += 3) { const double d = x[(size_t) ((long) i + lag)] - ref[i]; err += d * d; }
        if (err < bestErr) { bestErr = err; bestLag = lag; }
    }
    std::vector<float> d (ref.size(), 0.0f), r2 (ref.size(), 0.0f);
    for (size_t i = 24000; i + 4096 < ref.size(); ++i) { d[i] = x[(size_t) ((long) i + bestLag)] - ref[i]; r2[i] = ref[i]; }
    lagOut = bestLag;
    return db (rms (d)) - db (rms (r2));
}

int main (int argc, char** argv)
{
    const auto in = bassLine();
    static const char* names[] = { "Mosaic", "Seq", "Glide", "Haze", "Tunnel", "Strum", "Blocks", "Interrupt", "Arp", "Pattern", "Warp" };
    const int only = argc > 1 ? std::atoi (argv[1]) : -1;
    std::printf ("difference from Kaleidoscope's wet, dB (lower is closer; -60 or below is a null)\n");
    std::printf ("%-10s %9s %9s %9s %9s\n", "effect", "plug/plug", "wrapper", "+inputs", "engine");
    double worst = -200;
    for (int fx = 0; fx < 11; ++fx)
    {
        if (only >= 0 && fx != only) continue;
        const auto pw = pluginWet (in, fx);
        const auto pw2 = pluginWet (in, fx);
        const auto gw = grainsWet (in, fx);
        const auto a = runKeystone (in, fx, true, true);
        const auto b = runKeystone (in, fx, false, true);
        std::vector<float> kw (in.size());
        for (size_t i = 0; i < in.size(); ++i) kw[i] = a[i] - b[i];
        // the input as Keystone's engine hands it to Speckle
        std::vector<float> kin (in.size());
        {
            Biquad c1, c2; c1.highPass (50.0f, 0.541f, (float) kSr); c2.highPass (50.0f, 1.307f, (float) kSr);
            for (size_t i = 0; i < in.size(); ++i) kin[i] = c2.process (c1.process (in[i]));
        }
        int l0, l1, l2, l3;
        const auto gw2 = grainsWet (kin, fx);
        const double s0 = compare (pw, pw2, l0), s1 = compare (pw, gw, l1), s2 = compare (pw, gw2, l2), s3 = compare (pw, kw, l3);
        worst = std::max (worst, s3);
        std::printf ("%-10s %9.1f %9.1f %9.1f %9.1f   (lags %d %d %d)\n", names[fx], s0, s1, s2, s3, l1, l2, l3);
    }
    std::printf ("worst: %.1f dB\n", worst);
    if (worst > -60.0) { std::printf ("FAIL: Keystone's Speckle no longer sounds like the plugin\n"); return 1; }
    return 0;
}
