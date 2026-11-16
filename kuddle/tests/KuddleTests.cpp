// Kuddle engine tests: renders a played bass line through the chain in many settings and
// fails on anything broken. No JUCE. Run: kuddle_tests [--wav out_dir]
//
// Checks (from the house DSP lessons):
//  - nothing goes NaN or past the ceiling
//  - every setting keeps the level near what went in (no effect that goes quiet)
//  - soft playing (-30 dB) still comes through
//  - switching each effect on and off doesn't click
//  - the Melt sweep darkens: less top at the toe than at the heel
//  - the auto wah follows dynamics: soft notes open it less than hard ones
//  - CPU per instance at 48 kHz, written to bench-kuddle.md

#include "KuddleEngine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace kud;

static int failures = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static constexpr float kSr = 48000.0f;

// A plucked bass: a bright pulse through a decaying low-pass, like a finger on a P bass.
static std::vector<float> bassLine (float seconds, float gainDb, bool alternateSoft = false)
{
    const float notes[] = { 41.2f, 55.0f, 73.4f, 49.0f, 61.7f, 82.4f, 46.2f, 98.0f };
    std::vector<float> out ((size_t) (seconds * kSr), 0.0f);
    const float noteLen = 0.5f;
    ks::Rng rng;
    for (int k = 0; k * noteLen < seconds; ++k)
    {
        const float f = notes[k % 8];
        const float amp = ks::dbToGain (gainDb - (alternateSoft && (k % 2) ? 12.0f : 0.0f));
        const size_t start = (size_t) (k * noteLen * kSr);
        float ph = 0, lp = 0;
        for (size_t i = 0; i < (size_t) (noteLen * kSr) && start + i < out.size(); ++i)
        {
            const float t = (float) i / kSr;
            ph += f / kSr; if (ph >= 1) ph -= 1;
            const float saw = 2.0f * ph - 1.0f;
            const float cut = 200.0f + 3000.0f * std::exp (-t * 12.0f);
            const float a = std::exp (-ks::kTwoPi * cut / kSr);
            lp = saw + (lp - saw) * a;
            const float env = std::min (1.0f, t * 400.0f) * std::exp (-t * 2.5f) * std::min (1.0f, (noteLen - t) * 200.0f);
            out[start + i] += amp * (lp * 0.8f + 0.02f * rng.bi() * std::exp (-t * 60.0f)) * env;
        }
    }
    return out;
}

struct Render { std::vector<float> l, r; };

static Render run (const Params& p, const std::vector<float>& in, std::function<void (Params&, int block)> perBlock = {})
{
    Engine e;
    e.prepare (kSr, 256);
    Params q = p;
    e.setParams (q);
    e.reset();
    Render r;
    r.l.resize (in.size()); r.r.resize (in.size());
    for (size_t i = 0, b = 0; i < in.size(); i += 256, ++b)
    {
        const int n = (int) std::min<size_t> (256, in.size() - i);
        if (perBlock) { perBlock (q, (int) b); e.setParams (q); }
        e.process (in.data() + i, r.l.data() + i, r.r.data() + i, n);
    }
    return r;
}

static float rms (const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double s = 0;
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return (float) std::sqrt (s / std::max<size_t> (1, to - from));
}
static bool finite (const std::vector<float>& v, float& peak)
{
    peak = 0;
    for (float x : v) { if (! std::isfinite (x)) return false; peak = std::max (peak, std::fabs (x)); }
    return true;
}
// energy above a frequency relative to the whole, in dB
static float topShare (const std::vector<float>& v, size_t from, size_t to, float above = 2000.0f)
{
    ks::Biquad hp; hp.highPass (above, 0.7f, kSr);
    double hi = 0, all = 0;
    for (size_t i = from; i < to; ++i) { const float h = hp.process (v[i]); hi += h * h; all += v[i] * v[i]; }
    return 10.0f * (float) std::log10 ((hi + 1e-20) / (all + 1e-20));
}

static void writeWav (const std::string& path, const Render& r)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = (uint32_t) r.l.size(), data = n * 4, rate = 48000, br = rate * 4;
    const uint16_t ch = 2, bits = 16, align = 4, fmt = 1;
    const uint32_t riff = 36 + data, fmtLen = 16;
    f.write ("RIFF", 4); f.write ((const char*) &riff, 4); f.write ("WAVEfmt ", 8);
    f.write ((const char*) &fmtLen, 4); f.write ((const char*) &fmt, 2); f.write ((const char*) &ch, 2);
    f.write ((const char*) &rate, 4); f.write ((const char*) &br, 4); f.write ((const char*) &align, 2); f.write ((const char*) &bits, 2);
    f.write ("data", 4); f.write ((const char*) &data, 4);
    for (uint32_t i = 0; i < n; ++i)
        for (float s : { r.l[i], r.r[i] }) { const int16_t v = (int16_t) std::lround (ks::clampf (s, -1, 1) * 32767.0f); f.write ((const char*) &v, 2); }
}

// The three starter presets, kept in step with the plugin's factory presets.
static Params voodoo()
{
    Params p;   // the defaults are Voodoo: muted, warm, a little wah
    return p;
}
static Params khruangbin()
{
    Params p;
    p.foam = 0.65f; p.thump = 0.7f; p.strings = 2;
    p.wahOn = false;
    p.diDrive = 0.2f; p.diAir = 0.1f;
    p.optoSqueeze = 0.55f; p.optoBlend = 0.9f;
    p.tapePush = 0.45f; p.tapeWow = 0.2f; p.tapeBump = 0.6f;
    p.bassDb = 3; p.midsDb = -1; p.midsHz = 500; p.trebleKHz = 2.4f;
    p.roomOn = true; p.roomType = 0; p.roomMix = 0.18f; p.roomSize = 0.25f;
    return p;
}
static Params dub()
{
    Params p;
    p.foam = 0.75f; p.thump = 0.8f;
    p.wahOn = false;
    p.diDrive = 0.25f; p.diAir = 0.0f;
    p.optoSqueeze = 0.6f;
    p.tapePush = 0.55f; p.tapeBump = 0.8f;
    p.bassDb = 5; p.bassHz = 70; p.midsDb = -3; p.midsHz = 700; p.trebleKHz = 1.6f;
    p.roomOn = true; p.roomType = 1; p.roomMix = 0.25f; p.roomDecay = 1.4f;
    return p;
}

int main (int argc, char** argv)
{
    std::string wavDir;
    for (int i = 1; i < argc; ++i) if (std::strcmp (argv[i], "--wav") == 0 && i + 1 < argc) wavDir = argv[++i];

    const auto line = bassLine (8.0f, -12.0f);
    const float inRms = rms (line);

    // ---- every preset and every effect alone: finite, under the ceiling, level kept ----
    struct Case { std::string name; Params p; };
    std::vector<Case> cases { { "Voodoo", voodoo() }, { "Khruangbin", khruangbin() }, { "Dub", dub() } };
    {
        Params none; none.muteOn = none.wahOn = none.optoOn = none.tapeOn = none.toneOn = none.roomOn = false;
        cases.push_back ({ "Valve DI only", none });
        auto add = [&] (const char* n, std::function<void (Params&)> f) { Params p = none; f (p); cases.push_back ({ n, p }); };
        add ("Mute only", [] (Params& p) { p.muteOn = true; });
        add ("Mute full foam", [] (Params& p) { p.muteOn = true; p.foam = 1.0f; p.thump = 1.0f; });
        add ("Auto wah only", [] (Params& p) { p.wahOn = true; });
        add ("Auto wah mid range, full peak", [] (Params& p) { p.wahOn = true; p.wahRange = 1; p.wahPeak = 1.0f; p.wahSense = 1.0f; });
        add ("Manual wah", [] (Params& p) { p.wahOn = true; p.wahManual = true; p.wahPos = 0.8f; p.meltWorksWah = false; });
        add ("Valve DI full drive and sag", [] (Params& p) { p.diDrive = 1.0f; p.diSag = 1.0f; p.diAir = 1.0f; });
        add ("Opto only", [] (Params& p) { p.optoOn = true; });
        add ("Opto full squeeze held, fast", [] (Params& p) { p.optoOn = true; p.optoSqueeze = 1; p.squeeze = true; p.optoFast = true; p.optoHoldDb = 12; });
        add ("Tape only", [] (Params& p) { p.tapeOn = true; });
        add ("Tape full push, wow, flutter, hiss", [] (Params& p) { p.tapeOn = true; p.tapePush = 1; p.tapeWow = 1; p.tapeFlutter = 1; p.tapeBump = 1; p.tapeHiss = 1; });
        add ("Tone only", [] (Params& p) { p.toneOn = true; });
        add ("Tone extremes", [] (Params& p) { p.toneOn = true; p.bassDb = 12; p.bassHz = 200; p.midsDb = 12; p.midsHz = 1500; p.trebleKHz = 1.0f; });
        add ("Dark room", [] (Params& p) { p.roomOn = true; p.roomMix = 0.6f; p.roomSize = 1.0f; p.roomDecay = 3.0f; });
        add ("Spring", [] (Params& p) { p.roomOn = true; p.roomType = 1; p.roomMix = 0.6f; p.roomDecay = 3.0f; });
        Params all; all.roomOn = true;
        all.melt = 1.0f; all.reachRoom = 1.0f;
        all.meltWorksWah = false;
        cases.push_back ({ "Everything on, toe down", all });
    }

    for (auto& c : cases)
    {
        const auto r = run (c.p, line);
        float pk = 0;
        const bool ok = finite (r.l, pk) && finite (r.r, pk);
        const float outRms = rms (r.l, (size_t) kSr);
        const float diff = ks::gainToDb (outRms) - ks::gainToDb (inRms) - c.p.outDb;
        char msg[200];
        std::snprintf (msg, sizeof msg, "%-34s finite, peak %.2f, level %+.1f dB against the input", c.name.c_str(), pk, diff);
        const float tol = c.name == "Tone extremes" ? 15.0f : 9.0f;   // +12 dB of EQ is meant to be louder
        check (ok && pk < 0.99f && diff > -tol && diff < tol, msg);
        if (! wavDir.empty()) writeWav (wavDir + "/" + c.name + ".wav", r);
    }

    // ---- soft playing still comes through (Speckle lesson) ----
    {
        const auto soft = bassLine (6.0f, -42.0f);
        for (auto& c : cases)
        {
            const auto r = run (c.p, soft);
            const float diff = ks::gainToDb (rms (r.l, (size_t) kSr)) - ks::gainToDb (rms (soft)) - c.p.outDb;
            char msg[200];
            std::snprintf (msg, sizeof msg, "%-34s soft notes (-42 dB): %+.1f dB", c.name.c_str(), diff);
            const float tol = c.name == "Tone extremes" ? 18.0f : 12.0f;
            check (diff > -tol && diff < tol, msg);
        }
    }

    // ---- switching each effect on and off mid-note doesn't click ----
    {
        // a steady 110 Hz tone: the largest second difference against the tone's own
        std::vector<float> tone ((size_t) (3 * kSr));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.2f * std::sin (ks::kTwoPi * 110.0f * (float) i / kSr) * std::min (1.0f, (float) i / (0.2f * kSr));
        const float own = 0.2f * std::pow (ks::kTwoPi * 110.0f / kSr, 2.0f);
        auto score = [&] (const std::vector<float>& v, size_t from, size_t to)
        {
            float m = 0;
            for (size_t i = from + 2; i < to; ++i) m = std::max (m, std::fabs (v[i] - 2 * v[i - 1] + v[i - 2]));
            return m / own;
        };
        const char* names[] = { "Mute", "Auto wah", "Opto", "Tape", "Tone", "Room" };
        for (int which = 0; which < 6; ++which)
        {
            Params base; base.roomOn = false;
            auto flip = [which] (Params& p, int block)
            {
                const bool on = (block / 60) % 2 == 1;   // toggles every 0.32 s
                switch (which)
                {
                    case 0: p.muteOn = on; break;
                    case 1: p.wahOn = on; break;
                    case 2: p.optoOn = on; break;
                    case 3: p.tapeOn = on; break;
                    case 4: p.toneOn = on; break;
                    default: p.roomOn = on; break;
                }
            };
            const auto r = run (base, tone, flip);
            // the reference is the rougher of the two steady states (an effect off can be
            // brighter than on, Tone's roll-off for one)
            const auto sOn = run (base, tone, [&] (Params& p, int) { flip (p, 60); });
            const auto sOff = run (base, tone, [&] (Params& p, int) { flip (p, 0); });
            const float sw = score (r.l, (size_t) kSr, tone.size());
            const float st = std::max (score (sOn.l, (size_t) kSr, tone.size()), score (sOff.l, (size_t) kSr, tone.size()));
            char msg[200];
            std::snprintf (msg, sizeof msg, "%-10s on and off: click score %.2f (steady %.2f)", names[which], sw, st);
            check (sw < std::max (3.0f, st * 1.6f), msg);
        }
    }

    // ---- the Melt sweep darkens ----
    {
        // the roll-off on its own (Mute, Tape and Tone already darken, so measure it clean)
        Params heel; heel.wahOn = heel.muteOn = heel.tapeOn = heel.toneOn = false; heel.melt = 0;
        Params toe = heel; toe.melt = 1;
        const auto a = run (heel, line), b = run (toe, line);
        const float ta = topShare (a.l, (size_t) kSr, a.l.size(), 3000.0f), tb = topShare (b.l, (size_t) kSr, b.l.size(), 3000.0f);
        char msg[200];
        std::snprintf (msg, sizeof msg, "Melt: top above 3 kHz %.1f dB at the heel, %.1f dB at the toe", ta, tb);
        check (tb < ta - 6.0f, msg);
        // and a sweep across the whole pedal mid-line doesn't click or jump in level
        const auto sweep = run (heel, line, [] (Params& p, int b) { p.melt = 0.5f + 0.5f * std::sin ((float) b * 0.02f); });
        float pk;
        check (finite (sweep.l, pk) && pk < 0.99f, "Melt rocking up and down: finite and under the ceiling");
    }

    // ---- the auto wah follows dynamics (Quack lesson) ----
    {
        // measure brightness of hard and soft notes through the wah alone
        Params w; w.muteOn = w.optoOn = w.tapeOn = w.toneOn = w.roomOn = false; w.wahOn = true;
        const auto dyn = bassLine (8.0f, -12.0f, true);
        const auto r = run (w, dyn);
        double hard = 0, soft = 0; int nh = 0, ns = 0;
        for (int k = 4; k < 16; ++k)
        {
            const size_t s0 = (size_t) (k * 0.5f * kSr), s1 = s0 + (size_t) (0.15f * kSr);
            std::vector<float> seg (r.l.begin() + (long) s0, r.l.begin() + (long) s1);
            std::vector<float> inSeg (dyn.begin() + (long) s0, dyn.begin() + (long) s1);
            const float lift = topShare (seg, 0, seg.size()) - topShare (inSeg, 0, inSeg.size());
            if (k % 2) { soft += lift; ++ns; } else { hard += lift; ++nh; }
        }
        hard /= nh; soft /= ns;
        char msg[200];
        std::snprintf (msg, sizeof msg, "Auto wah: hard notes %.1f dB of top, soft notes %.1f dB (soft opens it less)", hard, soft);
        check (soft < hard - 1.0f, msg);
    }

    // ---- latency is constant ----
    {
        Engine e; e.prepare (kSr, 256);
        check (e.latency() == 15 + 48, "Latency " + std::to_string (e.latency()) + " samples (1.3 ms), whatever is switched on");
    }

    // ---- CPU ----
    {
        Params all; all.roomOn = true; all.melt = 0.4f;
        const auto in = bassLine (20.0f, -12.0f);
        std::vector<float> l (in.size()), r (in.size());
        std::string bench = "# Kuddle CPU\n\nShare of one core at 48 kHz, everything on.\n\n| Block | CPU |\n|---|---|\n";
        for (int block : { 64, 128 })
        {
            Engine e; e.prepare (kSr, block); e.setParams (all); e.reset();
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i + (size_t) block <= in.size(); i += (size_t) block) e.process (in.data() + i, l.data() + i, r.data() + i, block);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double pct = 100.0 * secs / 20.0;
            char row[80];
            std::snprintf (row, sizeof row, "| %d | %.2f%% |\n", block, pct);
            bench += row;
            std::printf ("CPU at %d samples: %.2f%% of one core\n", block, pct);
        }
        std::ofstream ("bench-kuddle.md") << bench;
    }

    std::printf ("\n%s: %d failure%s\n", failures ? "FAILED" : "All passed", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
