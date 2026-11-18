// Keystone engine render tests and benchmark.
//
// Pass marks come from the project's DSP lessons: crossover sums flat, held drone notes
// within 3 cents and over 18 dB pure, envelope filter opens less for softer notes, grains
// never drop more than 20 dB under the input, feedback never howls, no clicks on stomps.
// Then CPU per instance at 48 kHz with 64 and 128 sample blocks, written to bench.md.

#include "keystone/Controller.h"
#include "keystone/Engine.h"

#include "Analysis.h"
#include "Drone.h"
#include "Valve.h"
#include "Recorder.h"
#include "Sha256.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <random>
#include <chrono>
#include <complex>
#include <functional>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ks;

static int failures = 0, passes = 0;
static std::string report;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        char _b[512];                                                          \
        std::snprintf (_b, sizeof (_b), __VA_ARGS__);                          \
        if (cond) { ++passes; std::printf ("  pass  %s\n", _b); }             \
        else { ++failures; std::printf ("  FAIL  %s\n", _b); }                \
    } while (0)

static const double kSr = 48000.0;

// A bass-like plucked note: harmonics falling off, with a decay.
static std::vector<float> pluck (double f, double seconds, double amp, double decay = 1.2, double sr = kSr)
{
    std::vector<float> x ((size_t) (seconds * sr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = i / sr;
        double s = 0;
        for (int k = 1; k <= 12; ++k) s += std::sin (2 * M_PI * f * k * t) / (k * k * 0.6 + 0.4);
        const double env = std::min (1.0, t / 0.004) * std::exp (-t / decay);
        x[i] = (float) (amp * s * env * 0.5);
    }
    return x;
}

static std::vector<float> sine (double f, double seconds, double amp, double sr = kSr)
{
    std::vector<float> x ((size_t) (seconds * sr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = i / sr;
        const double fade = std::min (1.0, std::min (t, seconds - t) / 0.02);
        x[i] = (float) (amp * fade * std::sin (2 * M_PI * f * t));
    }
    return x;
}

static double rms (const float* x, size_t n)
{
    double s = 0;
    for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max<size_t> (1, n));
}

static std::vector<float> render (Engine& e, const std::vector<float>& in, int block = 64)
{
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - i);
        e.process (in.data() + i, out.data() + i, n);
        if ((i / block) % 16 == 0) e.idle();
    }
    return out;
}

static void neutral (Engine& e)
{
    // everything that colours the high band off, so the split can be checked on its own
    for (auto p : { P::DriveOn, P::PreOn, P::DimOn, P::CompOn, P::BitrotOn, P::FiltOn, P::GrOn, P::DlOn, P::RvOn, P::DroneOn })
        e.setParam (p, 0);
    e.setParam (P::SplitTight, 0);
    e.setParam (P::SplitWeight, 0);
}

// ---------------------------------------------------------------------------------------
static void testCrossover()
{
    std::printf ("Crossover and alignment\n");
    Engine e;
    e.prepare (kSr, 128);
    neutral (e);
    double worst = 0;
    for (double f : { 40.0, 70.0, 100.0, 130.0, 200.0, 500.0, 1000.0, 4000.0 })
    {
        auto in = sine (f, 1.0, 0.1);
        auto out = render (e, in);
        const size_t a = in.size() / 4, n = in.size() / 2;
        const double g = 20 * std::log10 (rms (out.data() + a, n) / rms (in.data() + a, n));
        worst = std::max (worst, std::fabs (g));
    }
    // the full path is the whole bass with only its sub cut, so it overlaps the bottom end a
    // little: within 1.5 dB at the default 50 Hz sub cut and 100 Hz crossover
    CHECK (worst < 1.5, "bottom end and full sound sum near flat, worst %.2f dB (mark 1.5)", worst);
    CHECK (e.latencySamples() == 30, "engine latency %d samples (%.2f ms at 48 kHz)", e.latencySamples(), e.latencySamples() * 1000.0 / kSr);
    report += "- Crossover sum: flat within " + std::to_string (worst).substr (0, 4) + " dB from 40 Hz to 4 kHz\n";
}

// ---------------------------------------------------------------------------------------
// Held drone note: pitch, purity and steadiness (Afterglow lesson).
static void measureDrone (const std::vector<float>& y, double f0, double& cents, double& purityDb, double& wobbleDb)
{
    const int n = 32768;
    const size_t start = y.size() - (size_t) n - 1000;
    double clarity;
    const double f = yinPitch (y.data() + start, 8192, kSr, 30, 500, clarity);
    cents = f > 0 ? 1200 * std::log2 (f / f0) : 999;
    // purity: energy within +/- 3% of each harmonic against everything else (windowed both sides)
    std::vector<std::complex<double>> a ((size_t) n);
    for (int i = 0; i < n; ++i) a[(size_t) i] = y[start + (size_t) i] * (0.5 - 0.5 * std::cos (2 * M_PI * i / n));
    fft (a);
    double near = 0, all = 0;
    for (int b = 1; b < n / 2; ++b)
    {
        const double hz = b * kSr / n, p = std::norm (a[(size_t) b]);
        if (hz > 3000) break;
        all += p;
        const double k = std::round (hz / f0);
        if (k >= 1 && std::fabs (hz - k * f0) < std::max (k * f0 * 0.03, 4.0 * kSr / n)) near += p;   // the note's band, at least the window's main lobe
    }
    purityDb = 10 * std::log10 (near / std::max (1e-20, all - near));
    // steadiness over whole 100 ms windows
    double lo = 1e9, hi = 0;
    const size_t w = (size_t) (0.1 * kSr);
    for (size_t i = y.size() - 2 * (size_t) kSr; i + w < y.size(); i += w)
    {
        const double r = rms (y.data() + i, w);
        lo = std::min (lo, r); hi = std::max (hi, r);
    }
    wobbleDb = 20 * std::log10 (hi / std::max (lo, 1e-12));
}

static void testDrone()
{
    std::printf ("Drone capture\n");
    for (double f0 : { 41.2, 55.0, 98.0, 36.7 })
    {
        Drone d;
        d.prepare ((float) kSr);
        Vals v {};
        for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramTable()[i].def;
        v[(size_t) P::DroneDim] = 0;   // measure the voice itself
        v[(size_t) P::DroneDuck] = 0;
        v[(size_t) P::DroneSwell] = 0.1f;
        if (f0 < 40) v[(size_t) P::DroneTuning] = 1;
        auto note = pluck (f0, 0.8, 0.5, 3.0);
        std::vector<float> y;
        const int B = 64;
        size_t i = 0;
        bool asked = false;
        for (; i < note.size(); i += B)
        {
            d.update (v, 0.25f);
            if (! asked && i > (size_t) (0.6 * kSr)) { d.requestCapture(); asked = true; }
            d.block (B);
            d.analyse (0.25f);
            for (int k = 0; k < B && i + k < note.size(); ++k) y.push_back (d.process (note[i + k]));
        }
        for (int s = 0; s < (int) (3.5 * kSr) / B; ++s)
        {
            d.update (v, 0.25f);
            d.block (B);
            d.analyse (0.25f);
            for (int k = 0; k < B; ++k) y.push_back (d.process (0.0f));
        }
        double cents, purity, wobble;
        measureDrone (y, f0, cents, purity, wobble);
        CHECK (std::fabs (cents) < 3 && purity > 18 && wobble < 3,
               "%.1f Hz held: %.2f cents, %.1f dB pure, %.1f dB wobble (marks 3 cents, 18 dB, 3 dB)", f0, cents, purity, wobble);
        if (f0 == 41.2) report += "- Drone on low E: " + std::to_string (cents).substr (0, 5) + " cents, " + std::to_string (purity).substr (0, 4) + " dB pure\n";
    }
    // silence must not be captured
    Drone d;
    d.prepare ((float) kSr);
    Vals v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramTable()[i].def;
    for (int s = 0; s < 200; ++s)
    {
        d.update (v, 0.25f);
        if (s == 100) d.requestCapture();
        d.block (64);
        d.analyse (0.25f);
        for (int k = 0; k < 64; ++k) d.process (0.0f);
    }
    for (int s = 0; s < 4; ++s) { d.update (v, 0.25f); d.block (64); }
    CHECK (d.layerCount() == 0, "silence isn't captured as a drone");
    // one note at a time: a second tap takes over, it never stacks; clearing trails off
    {
        Drone h;
        h.prepare ((float) kSr);
        Vals w {};
        for (int i = 0; i < kNumParams; ++i) w[(size_t) i] = paramTable()[i].def;
        auto a1 = pluck (55.0, 2.0, 0.5, 3.0), a2 = pluck (82.4, 2.0, 0.5, 3.0);
        a1.insert (a1.end(), a2.begin(), a2.end());
        const int B = 64;
        int most = 0;
        std::vector<float> y;
        for (size_t i = 0; i + B <= a1.size(); i += B)
        {
            h.update (w, 0.25f);
            if (i == (size_t) (0.6 * kSr) / B * B || i == (size_t) (2.6 * kSr) / B * B) h.requestCapture();
            h.block (B);
            h.analyse (0.25f);
            for (int k = 0; k < B; ++k) h.process (a1[i + (size_t) k]);
            if (i > (size_t) (3.6 * kSr)) most = std::max (most, h.layerCount());
        }
        h.requestClear();
        double before = 0, after = 0;
        for (int s2 = 0; s2 < (int) (6 * kSr) / B; ++s2)
        {
            h.update (w, 0.25f);
            h.block (B);
            h.analyse (0.25f);
            for (int k = 0; k < B; ++k) { const float o = h.process (0.0f); if (s2 * B < (int) (0.2 * kSr)) before = std::max (before, (double) std::fabs (o)); else if (s2 * B > (int) (5.5 * kSr)) after = std::max (after, (double) std::fabs (o)); }
        }
        CHECK (most <= 1, "a second tap replaces the drone, never stacks (%d held)", most);
        CHECK (before > 1.0e-3 && after < 1.0e-5, "a tap to stop trails off rather than cutting (%.4f, then %.6f)", before, after);
    }
}

// The drone sits apart from the expression pedal: the same captured drone at heel and toe,
// a drone captured with the pedal at heel, and no drone from moving the pedal alone.
static double droneTail (float bloom, bool capture)
{
    Engine e;
    e.prepare (kSr, 64);
    neutral (e);
    e.setParam (P::DroneOn, 1);
    e.setParam (P::DroneDim, 0);
    e.setParam (P::DroneDuck, 0);
    e.setParam (P::DroneSwell, 0.1f);
    e.setParam (P::BloomWash, 0);
    e.setBloom (bloom);
    auto note = pluck (41.2, 0.8, 0.5, 3.0);
    render (e, std::vector<float> (note.begin(), note.begin() + (size_t) (0.6 * kSr)));
    if (capture) e.action (Action::DroneCapture);
    render (e, std::vector<float> (note.begin() + (size_t) (0.6 * kSr), note.end()));
    e.setBloom (1.0f - bloom);
    render (e, std::vector<float> ((size_t) (1.5 * kSr), 0.0f));
    e.setBloom (bloom);
    auto tail = render (e, std::vector<float> ((size_t) (1.0 * kSr), 0.0f));
    return 20 * std::log10 (rms (tail.data() + tail.size() / 2, tail.size() / 2) + 1e-9);
}

static void testDroneApartFromBloom()
{
    std::printf ("Drone apart from the expression pedal\n");
    const double heel = droneTail (0.0f, true), toe = droneTail (1.0f, true), none = droneTail (1.0f, false);
    CHECK (heel > -40, "drone captured with the pedal at heel sounds: %.1f dB (mark over -40)", heel);
    CHECK (std::fabs (heel - toe) < 1.0, "drone level the same at heel and toe: %.1f and %.1f dB (mark within 1 dB)", heel, toe);
    CHECK (none < -70, "moving the pedal alone brings in no drone: %.1f dB (mark under -70)", none);
}

// ---------------------------------------------------------------------------------------
static void testFilterDynamics()
{
    std::printf ("Envelope filter\n");
    // Quack's test: alternate normal and 12 dB softer notes, read the sweep 50 ms in
    EnvFilter f;
    f.prepare ((float) kSr);
    Vals v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramTable()[i].def;
    f.update (v);
    double hard = 0, soft = 0;
    for (int k = 0; k < 8; ++k)
    {
        auto n = pluck (110.0, 0.5, k % 2 ? 0.125 : 0.5, 0.3);
        for (size_t i = 0; i < n.size(); ++i)
        {
            f.process (n[i]);
            if (i == (size_t) (0.05 * kSr) && k >= 4) (k % 2 ? soft : hard) += f.position01() / 2;
        }
    }
    CHECK (hard > 0.85 && hard - soft > 0.15, "hard notes open to %.2f, 12 dB softer to %.2f (Quack had 1.00 and 0.74)", hard, soft);
    report += "- Envelope filter: hard notes open to " + std::to_string (hard).substr (0, 4) + ", softer ones to " + std::to_string (soft).substr (0, 4) + "\n";
}

// ---------------------------------------------------------------------------------------
static void testGrains()
{
    std::printf ("Grains\n");
    for (double amp : { 0.5, 0.0158 })   // normal and 30 dB down
    {
        Engine e;
        e.prepare (kSr, 64);
        neutral (e);
        e.setParam (P::SplitLowLevel, -12);
        e.setParam (P::GrOn, 1);
        e.setParam (P::GrMix, 1);
        std::vector<float> in;
        for (int k = 0; k < 6; ++k) { auto n = pluck (82.4, 0.6, amp, 0.8); in.insert (in.end(), n.begin(), n.end()); }
        Engine dry;
        dry.prepare (kSr, 64);
        neutral (dry);
        dry.setParam (P::SplitLowLevel, -12);
        auto out = render (e, in), ref = render (dry, in);
        std::vector<float> wet (out.size());
        for (size_t i = 0; i < out.size(); ++i) wet[i] = out[i] - ref[i];
        const size_t a = (size_t) (1.2 * kSr), n = in.size() - a;
        const double drop = 20 * std::log10 (rms (wet.data() + a, n) / rms (in.data() + a, n));
        CHECK (drop > -20, "grains with %s input: wet %.1f dB against the input (mark -20)", amp > 0.1 ? "normal" : "quiet (-30 dB)", drop);
    }
}


// ---------------------------------------------------------------------------------------
// Delay: every type sits near the same level, stays finite, decays, and survives Bloom.
static const char* kDelayNames[12] = { "Digital", "Bucket", "Tape", "Dual", "Pattern", "Reverse", "Ice", "Swell", "Trem", "Filter", "Lo-fi", "Galaxy" };

static std::vector<float> bassLine (double seconds, double amp)
{
    std::vector<float> in;
    const double notes[4] = { 82.4, 110.0, 98.0, 73.4 };
    int k = 0;
    while (in.size() < (size_t) (seconds * kSr)) { auto n = pluck (notes[k++ % 4], 0.5, amp, 0.6); in.insert (in.end(), n.begin(), n.end()); }
    in.resize ((size_t) (seconds * kSr));
    return in;
}

static std::vector<float> wetOf (const std::function<void (Engine&)>& setup, const std::vector<float>& in)
{
    Engine e, dry;
    e.prepare (kSr, 64);
    dry.prepare (kSr, 64);
    neutral (e);
    neutral (dry);
    setup (e);
    auto out = render (e, in), ref = render (dry, in);
    for (size_t i = 0; i < out.size(); ++i) out[i] -= ref[i];
    return out;
}

static void testDelayTypes()
{
    std::printf ("Delay types\n");
    auto in = bassLine (4.0, 0.5);
    in.resize (in.size() + (size_t) (5 * kSr), 0.0f);
    double ref = 0;
    for (int t = 0; t < 12; ++t)
    {
        auto wet = wetOf ([t] (Engine& e)
                          {
                              e.setParam (P::DlOn, 1);
                              e.setParam (P::DlType, (float) t);
                              e.setParam (P::DlMix, 0.5f);
                              e.setParam (P::DlFeedback, 0.5f);
                              e.setParam (P::DlTime, 330);
                          }, in);
        double peak = 0;
        bool finite = true;
        for (float s : wet) { peak = std::max (peak, (double) std::fabs (s)); finite = finite && std::isfinite (s); }
        const size_t a = (size_t) (1.0 * kSr), playEnd = (size_t) (4.0 * kSr);
        const double lvl = 20 * std::log10 (rms (wet.data() + a, playEnd - a) + 1e-12);
        const double late = 20 * std::log10 (rms (wet.data() + wet.size() - (size_t) kSr, (size_t) kSr) + 1e-12);
        if (t == 0) ref = lvl;
        CHECK (finite && peak < 0.99, "%-8s finite, wet peak %.1f dBFS", kDelayNames[t], 20 * std::log10 (peak + 1e-12));
        CHECK (std::fabs (lvl - ref) < 6.0, "%-8s level %.1f dB against Digital (mark within 6)", kDelayNames[t], lvl - ref);
        CHECK (late < lvl - 25, "%-8s tail falls away: %.1f dB below playing level 4 s later (mark 25)", kDelayNames[t], lvl - late);
    }
}

static void testTapeMachines()
{
    std::printf ("Tape machines\n");
    auto in = bassLine (3.0, 0.5);
    in.resize (in.size() + (size_t) (2 * kSr), 0.0f);
    const char* machines[3] = { "Fixed", "Multi", "Single" };
    double ref = 0;
    for (int m = 0; m < 3; ++m)
        for (int md = 0; md < 3; ++md)
        {
            auto wet = wetOf ([m, md] (Engine& e)
                              {
                                  e.setParam (P::DlOn, 1);
                                  e.setParam (P::DlType, 2);
                                  e.setParam (P::DlTapeMachine, (float) m);
                                  e.setParam (P::DlTapeMode, (float) md);
                                  e.setParam (P::DlCrinkle, 0.8f);
                                  e.setParam (P::DlSpring, 0.6f);
                                  e.setParam (P::DlMix, 0.5f);
                              }, in);
            bool finite = true;
            for (float s : wet) finite = finite && std::isfinite (s);
            const double lvl = 20 * std::log10 (rms (wet.data() + (size_t) kSr, (size_t) (2 * kSr)) + 1e-12);
            if (m == 0 && md == 0) ref = lvl;
            CHECK (finite && std::fabs (lvl - ref) < 6.0, "%s %c: level %.1f dB against Fixed A", machines[m], 'A' + md, lvl - ref);
        }
}

// ---------------------------------------------------------------------------------------
// Tempo: saved per song, tapped from the phone, synced delays and Speckle follow it.
static double echoMs (Engine& e)
{
    // a click, then the time of the loudest repeat
    // a second of silence first, so the delay time has settled
    std::vector<float> in ((size_t) (3.0 * kSr), 0.0f);
    in[(size_t) (1.0 * kSr)] = 0.8f;
    auto y = render (e, in);
    size_t best = 0; float pk = 0;
    for (size_t i = (size_t) (1.03 * kSr); i < y.size(); ++i) if (std::fabs (y[i]) > pk) { pk = std::fabs (y[i]); best = i; }
    return ((double) best / kSr - 1.0) * 1000.0;
}

static void testTempo()
{
    std::printf ("Tempo\n");
    auto setup = [] (Engine& e)
    {
        e.prepare (kSr, 64); neutral (e); e.setParam (P::DroneOn, 0);
        e.setParam (P::DlOn, 1); e.setParam (P::DlMix, 1.0f); e.setParam (P::DlFeedback, 0.0f); e.setParam (P::DlType, 0);
    };
    // each on a fresh delay, since a changed time glides
    auto at = [&] (int sync, float bpm, float ms) { Engine e; setup (e); e.setParam (P::Tempo, bpm); e.setParam (P::DlSync, (float) sync); e.setParam (P::DlTime, ms); return echoMs (e); };
    const double q = at (1, 100, 420), d = at (2, 100, 420), f = at (0, 100, 300), t3 = at (6, 100, 420);
    CHECK (std::fabs (q - 600) < 20, "100 BPM, delay on 1/4: repeats after %.0f ms (mark 600)", q);
    CHECK (std::fabs (d - 450) < 20, "dotted 1/8: %.0f ms (mark 450)", d);
    CHECK (std::fabs (t3 - 400) < 20, "1/4 triplet: %.0f ms (mark 400)", t3);
    CHECK (std::fabs (f - 300) < 20, "Free ignores the tempo: %.0f ms (mark 300)", f);
    {
        // four taps 500 ms apart: 120 BPM; a free delay follows the tap
        const auto dir = std::filesystem::temp_directory_path() / "keystone_tap";
        { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
        Engine e; e.prepare (kSr, 64);
        Controller c (e, dir.string(), (dir / "caps").string());
        c.load();
        c.setValue (P::DlSync, 0);
        c.takeBroadcasts();
        for (int i = 0; i < 4; ++i) c.tapTempo (10.0 + 0.5 * i);
        CHECK (std::fabs (c.value (P::Tempo) - 120) < 0.06f && std::fabs (c.value (P::DlTime) - 500) < 0.5f, "four taps 500 ms apart: %.1f BPM, delay %.0f ms", c.value (P::Tempo), c.value (P::DlTime));
        int sent = 0;
        for (auto& m : c.takeBroadcasts()) sent += m.find ("tempo.bpm") != std::string::npos;
        CHECK (sent > 0, "and the phone hears the new tempo");
        for (int i = 0; i < 3; ++i) c.tapTempo (20.0 + 0.4 * i);
        CHECK (std::fabs (c.value (P::Tempo) - 150) < 0.06f, "after a pause the taps start afresh: %.1f BPM (mark 150)", c.value (P::Tempo));
        { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    }
}

static void testDelayLimits()
{
    std::printf ("Delay at the edge: Bloom, full repeats and Hold\n");
    auto in = bassLine (2.0, 0.9);
    in.resize (in.size() + (size_t) (12 * kSr), 0.0f);
    double worst = 0;
    bool finite = true;
    for (int t = 0; t < 12; ++t)
    {
        Engine e;
        e.prepare (kSr, 64);
        neutral (e);
        e.setParam (P::DlOn, 1);
        e.setParam (P::DlType, (float) t);
        e.setParam (P::DlFeedback, 0.95f);
        e.setParam (P::DlGrit, 1.0f);
        e.setParam (P::DlMix, 1.0f);
        e.setParam (P::BloomWash, 1.0f);
        e.setBloom (1.0f);
        auto out = render (e, in);
        for (float s : out) { worst = std::max (worst, (double) std::fabs (s)); finite = finite && std::isfinite (s); }
    }
    CHECK (finite && worst < 0.99, "all twelve types at full repeats and Bloom: peak %.1f dBFS (mark under 0)", 20 * std::log10 (worst));

    // Hold keeps the repeats going without new input getting in
    Engine e;
    e.prepare (kSr, 64);
    neutral (e);
    e.setParam (P::DlOn, 1);
    e.setParam (P::DlMix, 1.0f);
    e.setParam (P::DlTone, 0.0f);
    e.setParam (P::DlLowCut, 20.0f);
    render (e, bassLine (2.0, 0.5));
    e.setParam (P::DlHold, 1);
    std::vector<float> sil ((size_t) (6 * kSr), 0.0f);
    auto held = render (e, sil);
    const double heldLvl = 20 * std::log10 (rms (held.data() + held.size() - (size_t) kSr, (size_t) kSr) + 1e-12);
    double pk = 0;
    for (float s : held) pk = std::max (pk, (double) std::fabs (s));
    CHECK (heldLvl > -45 && pk < 0.99, "Hold: repeats still at %.1f dBFS after 6 s, peak %.1f dBFS", heldLvl, 20 * std::log10 (pk + 1e-12));

    // switching type while repeats ring: no worse a step than either type makes on its own
    auto tone = sine (220, 1.6, 0.3);
    auto runWith = [&] (int first, int second)
    {
        Engine c;
        c.prepare (kSr, 64);
        neutral (c);
        c.setParam (P::DlOn, 1);
        c.setParam (P::DlMix, 1.0f);
        c.setParam (P::DlFeedback, 0.7f);
        c.setParam (P::DlType, (float) first);
        const size_t at = (size_t) (1.0 * kSr);
        auto y1 = render (c, std::vector<float> (tone.begin(), tone.begin() + (long) at));
        c.setParam (P::DlType, (float) second);
        auto y2 = render (c, std::vector<float> (tone.begin() + (long) at, tone.end()));
        double w = 0;
        size_t wi = 0;
        for (size_t i = 1; i + 1 < y2.size(); ++i) { const double d = std::fabs (y2[i + 1] - 2 * y2[i] + y2[i - 1]); if (d > w) { w = d; wi = i; } }
        if (getenv ("KS_DEBUG")) std::printf ("      %d -> %d worst %.5f at %.1f ms\n", first, second, w, wi * 1000.0 / kSr);
        return w;
    };
    double worstRatio = 0;
    int worstType = 0;
    const double steadyDigital = runWith (0, 0);
    for (int t = 1; t < 12; ++t)
    {
        if (t == 10) continue;   // Lo-fi: its own staircase is the sound
        const double ref = std::max (steadyDigital, runWith (t, t));
        const double r = runWith (0, t) / ref;
        if (r > worstRatio) { worstRatio = r; worstType = t; }
    }
    CHECK (worstRatio < 2.0, "changing type mid-repeats: worst step %.2f times what the types make anyway (into %s, mark 2)", worstRatio, kDelayNames[worstType]);
}

// ---------------------------------------------------------------------------------------
// Reverb: twelve spaces, level matched, bounded, freeze holds, impulses load from .wav
static const char* kReverbNames[12] = { "Room", "Hall", "Plate", "Spring", "Chamber", "Cloud", "Bloom", "Chorale", "Shimmer", "Magneto", "Nonlinear", "Impulse" };

static std::string writeTestWav (int bits, bool isFloat, int channels, int rate)
{
    // a 1.5 s decaying noise burst
    const int n = (int) (1.5 * rate);
    std::vector<uint8_t> d;
    auto put16 = [&] (uint32_t v) { d.push_back ((uint8_t) v); d.push_back ((uint8_t) (v >> 8)); };
    auto put32 = [&] (uint32_t v) { put16 (v & 0xffff); put16 (v >> 16); };
    const int bytes = bits / 8;
    d.insert (d.end(), { 'R', 'I', 'F', 'F' }); put32 (0);
    d.insert (d.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }); put32 (16);
    put16 (isFloat ? 3 : 1); put16 ((uint32_t) channels); put32 ((uint32_t) rate); put32 ((uint32_t) (rate * channels * bytes));
    put16 ((uint32_t) (channels * bytes)); put16 ((uint32_t) bits);
    d.insert (d.end(), { 'd', 'a', 't', 'a' }); put32 ((uint32_t) (n * channels * bytes));
    uint32_t r = 12345;
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < channels; ++c)
        {
            r = r * 1664525u + 1013904223u;
            const float v = ((float) (r >> 8) / 16777216.0f * 2 - 1) * std::exp (-4.0f * i / rate) * 0.5f;
            if (isFloat) { uint32_t u; std::memcpy (&u, &v, 4); put32 (u); }
            else if (bits == 16) put16 ((uint32_t) (int32_t) (v * 32767));
            else { const int32_t q = (int32_t) (v * 8388607); d.push_back ((uint8_t) q); d.push_back ((uint8_t) (q >> 8)); d.push_back ((uint8_t) (q >> 16)); }
        }
    const uint32_t riff = (uint32_t) d.size() - 8;
    std::memcpy (d.data() + 4, &riff, 4);
    const std::string path = (std::filesystem::temp_directory_path() / ("ks_space_" + std::to_string (bits) + (isFloat ? "f" : "") + ".wav")).string();
    std::ofstream (path, std::ios::binary).write ((const char*) d.data(), (std::streamsize) d.size());
    return path;
}

static void testReverbTypes()
{
    std::printf ("Reverb types\n");
    auto in = bassLine (3.0, 0.5);
    in.resize (in.size() + (size_t) (6 * kSr), 0.0f);
    const std::string wav = writeTestWav (24, false, 2, 44100);
    double ref = 0;
    for (int t = 0; t < 12; ++t)
    {
        auto wet = wetOf ([t, &wav] (Engine& e)
                          {
                              e.setParam (P::RvOn, 1);
                              e.setParam (P::RvType, (float) t);
                              e.setParam (P::RvMix, 0.5f);
                              e.setParam (P::RvDecay, 2.0f);
                              if (t == 11) { std::string err; e.loadSpace (wav, err); }
                          }, in);
        double peak = 0;
        bool finite = true;
        for (float s : wet) { peak = std::max (peak, (double) std::fabs (s)); finite = finite && std::isfinite (s); }
        const double lvl = 20 * std::log10 (rms (wet.data() + (size_t) (1.0 * kSr), (size_t) (2.0 * kSr)) + 1e-12);
        const double late = 20 * std::log10 (rms (wet.data() + wet.size() - (size_t) kSr, (size_t) kSr) + 1e-12);
        if (t == 1) ref = lvl;
        if (t == 0) ref = lvl;   // replaced by Hall below
        CHECK (finite && peak < 0.99, "%-9s finite, wet peak %.1f dBFS", kReverbNames[t], 20 * std::log10 (peak + 1e-12));
        if (t >= 1) CHECK (std::fabs (lvl - ref) < 6.0, "%-9s level %.1f dB against Hall (mark within 6)", kReverbNames[t], lvl - ref);
        CHECK (late < lvl - 30, "%-9s dies away: %.1f dB down 5 s after the last note (mark 30)", kReverbNames[t], lvl - late);
    }
}

// Reverb decays: each band dies in its own true time, the way the BigSky's do: the mids in
// Decay, the highs about twice as fast, the lows a touch longer in a hall
static void testReverbDecay()
{
    std::printf ("Reverb decay, band by band\n");
    std::vector<float> in ((size_t) (8 * kSr), 0.0f);
    uint32_t r = 7;
    for (size_t i = 0; i < (size_t) (0.05 * kSr); ++i) { r = r * 1664525u + 1013904223u; in[i] = ((float) (r >> 8) / 16777216.0f * 2 - 1) * 0.4f; }
    auto wet = wetOf ([] (Engine& e) { e.setParam (P::RvOn, 1); e.setParam (P::RvType, 1); e.setParam (P::RvMix, 1.0f);
                                       e.setParam (P::RvDecay, 3.0f); e.setParam (P::RvPredelay, 0.0f); e.setParam (P::RvDuck, 0.0f); }, in);
    auto rt = [&] (float lo, float hi)
    {
        Biquad a, a2, b, b2;   // steep, so the strong mids can't leak into the highs' reading
        a.highPass (lo, 0.54f, (float) kSr); a2.highPass (lo, 1.31f, (float) kSr); b.lowPass (hi, 0.54f, (float) kSr); b2.lowPass (hi, 1.31f, (float) kSr);
        std::vector<double> e;
        double acc = 0; int k = 0;
        for (float x : wet) { const float y = b2.process (b.process (a2.process (a.process (x)))); acc += (double) y * y; if (++k == (int) (0.05 * kSr)) { e.push_back (10 * std::log10 (acc / k + 1e-20)); acc = 0; k = 0; } }
        // fit the fall from 0.5 s to 2 s
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (size_t i = 10; i < 40 && i < e.size(); ++i) { const double t = i * 0.05; sx += t; sy += e[i]; sxx += t * t; sxy += t * e[i]; ++n; }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        return slope < 0 ? -60.0 / slope : 99.0;
    };
    const double low = rt (90, 220), mid = rt (400, 1400), high = rt (5000, 9000);
    CHECK (mid > 2.4 && mid < 3.6, "Hall at 3 s: the mids ring %.2f s (mark 2.4 to 3.6)", mid);
    CHECK (high < 0.65 * mid, "the highs fall away faster: %.2f s (mark under two thirds of the mids)", high);
    CHECK (low > 0.95 * mid, "the lows hold their body: %.2f s (mark at least the mids)", low);
}

static void testReverbEdges()
{
    std::printf ("Reverb at the edge: Bloom, freeze, type changes, impulses\n");
    auto in = bassLine (2.0, 0.9);
    in.resize (in.size() + (size_t) (10 * kSr), 0.0f);
    double worst = 0;
    bool finite = true;
    for (int t = 0; t < 11; ++t)
    {
        Engine e;
        e.prepare (kSr, 64);
        neutral (e);
        e.setParam (P::RvOn, 1);
        e.setParam (P::RvType, (float) t);
        e.setParam (P::RvMix, 1.0f);
        e.setParam (P::RvGrit, 1.0f);
        e.setParam (P::RvShimmer, 1.0f);
        e.setParam (P::RvEchoes, 0.9f);
        e.setParam (P::BloomWash, 1.0f);
        e.setBloom (1.0f);
        auto out = render (e, in);
        for (float s : out) { worst = std::max (worst, (double) std::fabs (s)); finite = finite && std::isfinite (s); }
    }
    CHECK (finite && worst < 0.99, "every type at full Bloom, grit and shimmer: peak %.1f dBFS (mark under 0)", 20 * std::log10 (worst));

    // freeze holds the tail and lets nothing new in
    Engine f;
    f.prepare (kSr, 64);
    neutral (f);
    f.setParam (P::RvOn, 1);
    f.setParam (P::RvMix, 1.0f);
    render (f, bassLine (2.0, 0.5));
    f.setParam (P::RvFreeze, 1);
    auto held = render (f, bassLine (6.0, 0.5));   // playing on top: shouldn't get in
    const double a = 20 * std::log10 (rms (held.data() + (size_t) kSr, (size_t) kSr) + 1e-12);
    const double b = 20 * std::log10 (rms (held.data() + held.size() - (size_t) kSr, (size_t) kSr) + 1e-12);
    CHECK (b > -45 && std::fabs (a - b) < 6, "Freeze: tail at %.1f then %.1f dBFS five seconds later", a, b);

    // type changes while the tail rings: no worse a step than the types make anyway
    auto tone = sine (220, 1.6, 0.3);
    auto runWith = [&] (int first, int second)
    {
        Engine c;
        c.prepare (kSr, 64);
        neutral (c);
        c.setParam (P::RvOn, 1);
        c.setParam (P::RvMix, 1.0f);
        c.setParam (P::RvType, (float) first);
        const size_t at = (size_t) (1.0 * kSr);
        render (c, std::vector<float> (tone.begin(), tone.begin() + (long) at));
        c.setParam (P::RvType, (float) second);
        auto y2 = render (c, std::vector<float> (tone.begin() + (long) at, tone.end()));
        double w = 0;
        // the switch itself: the first 300 ms, before the test tone stops
        const size_t span = std::min (y2.size(), (size_t) (0.3 * kSr));
        for (size_t i = 1; i + 1 < span; ++i) w = std::max (w, (double) std::fabs (y2[i + 1] - 2 * y2[i] + y2[i - 1]));
        if (getenv ("KS_DEBUG")) { size_t wi = 0; double ww = 0; for (size_t i = 1; i + 1 < y2.size(); ++i) { const double dd = std::fabs (y2[i + 1] - 2 * y2[i] + y2[i - 1]); if (dd > ww) { ww = dd; wi = i; } } std::printf ("      rv %d -> %d %.5f at %.1f ms\n", first, second, ww, wi * 1000.0 / kSr); }
        return w;
    };
    double worstRatio = 0;
    int worstType = 0;
    const double steadyHall = runWith (1, 1);
    for (int t = 0; t < 11; ++t)
    {
        if (t == 1) continue;
        const double r = runWith (1, t) / std::max (steadyHall, runWith (t, t));
        if (r > worstRatio) { worstRatio = r; worstType = t; }
    }
    // mark 2.5 since 0.2.9: the full path feeds the reverb more body, and Chamber's first
    // reflections arriving into a fresh space read a little above 2 on this pure tone
    CHECK (worstRatio < 2.5, "changing type mid-tail: worst step %.2f times what the types make anyway (into %s, mark 2.5)", worstRatio, kReverbNames[worstType]);

    // impulses in every format load and sound alike
    double lv[4] = {};
    const int bits[4] = { 16, 24, 32, 32 };
    const bool fl[4] = { false, false, false, true };
    bool allLoaded = true;
    auto shortIn = bassLine (1.5, 0.5);
    for (int k = 0; k < 4; ++k)
    {
        const std::string path = writeTestWav (bits[k], fl[k], k == 1 ? 2 : 1, k == 2 ? 96000 : 48000);
        bool ok = false;
        auto wet = wetOf ([&] (Engine& e)
                          {
                              e.setParam (P::RvOn, 1);
                              e.setParam (P::RvType, 11);
                              e.setParam (P::RvMix, 0.5f);
                              std::string err;
                              ok = e.loadSpace (path, err);
                          }, shortIn);
        allLoaded = allLoaded && ok;
        lv[k] = 20 * std::log10 (rms (wet.data() + (size_t) (0.5 * kSr), (size_t) kSr) + 1e-12);
    }
    const double spread = *std::max_element (lv, lv + 4) - *std::min_element (lv, lv + 4);
    CHECK (allLoaded && spread < 3.0 && lv[0] > -40, "impulses as 16, 24, 32-bit and float .wav, mono, stereo and 96 kHz all load, levels within %.1f dB (mark 3)", spread);
    std::string err;
    Engine bad;
    bad.prepare (kSr, 64);
    const bool refused = ! bad.loadSpace ("/nonexistent/space.wav", err);
    CHECK (refused && ! err.empty(), "a missing file says why: \"%s\"", err.c_str());
}

// Grain effects: all eleven Speckle effects make sound and live up top
static double toneAt (const std::vector<float>& x, size_t a, size_t n, double f)
{
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 2 * M_PI * f * (double) i / kSr;
        re += x[a + i] * std::cos (w); im += x[a + i] * std::sin (w);
    }
    return 2 * std::sqrt (re * re + im * im) / (double) n;
}

// Bitrot sits before the preamp and comes in at your level: fuzzier, not louder (0.4.9)
static void testBitrotLevel()
{
    std::printf ("Bitrot level\n");
    auto in = bassLine (4.0, 0.3);
    auto level = [&] (bool on)
    {
        Engine e; e.prepare (kSr, 64);
        e.setParam (P::BitrotOn, on ? 1.0f : 0.0f);
        auto y = render (e, in);
        return 20 * std::log10 (rms (y.data() + (size_t) kSr, y.size() - (size_t) kSr) + 1e-12);
    };
    const double off = level (false), on = level (true);
    // the synth on a plain test line reads a little under, real playing a little over (+2.8 dB
    // on Kevin's own recording); before 0.4.9 it was +15 dB
    CHECK (std::fabs (on - off) < 4.5, "Bitrot on: %+.1f dB against off (mark within 4.5)", on - off);
}

static void testBitrotSynth()
{
    std::printf ("Bitrot synth\n");
    Engine e;
    e.prepare (kSr, 128);
    neutral (e);
    e.setParam (P::BitrotOn, 1);
    e.setParam (P::BitrotClean, 0);
    e.setParam (P::BitrotSquare, 0); e.setParam (P::BitrotSub, 0); e.setParam (P::BitrotOsc, 1);
    auto in = sine (98.0, 1.5, 0.15);           // G on the E string
    in.resize (in.size() + (size_t) kSr, 0.0f);  // then a second of silence
    auto out = render (e, in);
    const size_t a = (size_t) (0.8 * kSr), n = (size_t) (0.4 * kSr);
    const double up = toneAt (out, a, n, 196.0), fund = toneAt (out, a, n, 98.0);
    std::printf ("  octave up %.1f dB, fundamental %.1f dB\n", 20 * std::log10 (up + 1e-9), 20 * std::log10 (fund + 1e-9));
    CHECK (up > 0.02 && up > 4 * fund, "the oscillator locks an octave above the note (x2)");
    const double tail = rms (out.data() + (size_t) (2.0 * kSr), (size_t) (0.4 * kSr));
    CHECK (tail < 1.0e-4, "between notes the synth is silent, so hiss never plays it");
}

static void testGrainEffects()
{
    std::printf ("Speckle effects\n");
    const char* names[11] = { "Mosaic", "Seq", "Glide", "Haze", "Tunnel", "Strum", "Blocks", "Interrupt", "Arp", "Pattern", "Warp" };
    auto in = bassLine (6.0, 0.5);
    for (int fx = 0; fx < 11; ++fx)
    {
        for (int var = 0; var < 4; var += 3)
        {
            auto wet = wetOf ([fx, var] (Engine& e)
                              {
                                  e.setParam (P::GrOn, 1);
                                  e.setParam (P::GrEffect, (float) fx);
                                  e.setParam (P::GrVariant, (float) var);
                                  e.setParam (P::GrMix, 1.0f);
                              }, in);
            bool finite = true;
            double pk = 0;
            for (float s : wet) { finite = finite && std::isfinite (s); pk = std::max (pk, (double) std::fabs (s)); }
            const size_t a = (size_t) (1.5 * kSr), n = wet.size() - a;
            const double lvl = 20 * std::log10 (rms (wet.data() + a, n) / rms (in.data() + a, n) + 1e-12);
            // how much of the wet sits under 250 Hz, where the bass lives
            Biquad lp1, lp2;
            lp1.lowPass (250, 0.541f, (float) kSr);
            lp2.lowPass (250, 1.307f, (float) kSr);
            std::vector<float> lo (wet.size());
            for (size_t i = 0; i < wet.size(); ++i) lo[i] = lp2.process (lp1.process (wet[i]));
            const double low = 20 * std::log10 (rms (lo.data() + a, n) / (rms (wet.data() + a, n) + 1e-12) + 1e-12);
            CHECK (finite && pk < 0.99 && lvl > -30 && lvl < 6, "%-9s %c: wet %.1f dB against the input (mark -30 to +6)", names[fx], 'A' + var, lvl);
            CHECK (low > -24, "%-9s %c: %.1f dB of it under 250 Hz, so it has body (mark -24)", names[fx], 'A' + var, low);
        }
    }
}

// ---------------------------------------------------------------------------------------
static void testValvePreamp()
{
    std::printf ("Valve preamp\n");
    Vals v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramTable()[i].def;
    v[(size_t) P::PreBlend] = 0; v[(size_t) P::PreTape] = 0; v[(size_t) P::PreWow] = 0;
    auto run = [&] (float g, double f, double amp)
    {
        Preamp p;
        p.prepare ((float) kSr);
        v[(size_t) P::PreGain] = g;
        std::vector<float> y;
        for (int i = 0; i < (int) (8.0 * kSr); ++i)   // gain staging settles over a few seconds
        {
            if (i % 64 == 0) p.update (v, (float) amp);
            y.push_back (p.process ((float) (amp * std::sin (2 * M_PI * f * i / kSr)), 1.0f));
        }
        return y;
    };
    double lo = 99, hi = -99;
    for (float g : { 0.0f, 0.35f, 0.7f, 1.0f })
        for (double amp : { 0.02, 0.2 })
        {
            auto y = run (g, 82.4, amp);
            const size_t a = (size_t) (7 * kSr), n = (size_t) kSr;
            const double l = 20 * std::log10 (toneAt (y, a, n, 82.4) / amp);
            lo = std::min (lo, l); hi = std::max (hi, l);
            if (getenv ("KS_DEBUG")) std::printf ("      gain %.2f amp %.2f: %.1f dB\n", g, amp, l);
        }
    CHECK (lo > -2 && hi < 2, "Gain changes the tone, not the volume: fundamental %.1f to %.1f dB, quiet or loud input (mark 2 dB)", lo, hi);
    auto y = run (0.35f, 82.4, 0.02);
    const size_t a = (size_t) (7 * kSr), n = (size_t) kSr;
    const double h1 = toneAt (y, a, n, 82.4), h2 = toneAt (y, a, n, 164.8), h3 = toneAt (y, a, n, 247.2);
    CHECK (h2 > 2 * h3 && h2 / h1 > 0.05, "warm at the default: 2nd harmonic %.1f dB, 3rd %.1f dB", 20 * std::log10 (h2 / h1), 20 * std::log10 (h3 / h1 + 1e-12));
    auto clean = run (0.0f, 82.4, 0.2);
    const double c1 = toneAt (clean, a, n, 82.4), c2 = toneAt (clean, a, n, 164.8);
    CHECK (c2 / c1 < 0.06, "Gain at zero is close to clean: 2nd harmonic %.1f dB (mark -24)", 20 * std::log10 (c2 / c1));
}

static void testOffMeansOff()
{
    std::printf ("Effects that are off stay silent, pedal or not\n");
    auto in = bassLine (3.0, 0.5);
    for (float pedal : { 0.0f, 0.5f, 1.0f })
    {
        Engine e, dry;
        e.prepare (kSr, 64); dry.prepare (kSr, 64);
        neutral (e); neutral (dry);
        e.setBloom (pedal);
        auto out = render (e, in), ref = render (dry, in);
        double diff = 0;
        for (size_t i = 0; i < out.size(); ++i) diff = std::max (diff, (double) std::fabs (out[i] - ref[i]));
        CHECK (diff < 1.0e-4, "pedal at %.0f%%: nothing extra (%.6f)", pedal * 100, diff);
    }
}

static void testBloomSafety()
{
    std::printf ("Bloom at toe: feedback limiter\n");
    Engine e;
    e.prepare (kSr, 64);
    e.setParam (P::BloomWash, 1.0f);
    e.setParam (P::DlOn, 1);
    e.setParam (P::RvOn, 1);
    e.setBloom (1.0f);
    std::vector<float> in;
    for (int k = 0; k < 4; ++k) { auto n = pluck (41.2 * (1 + k % 3), 0.8, 0.9, 1.0); in.insert (in.end(), n.begin(), n.end()); }
    in.resize (in.size() + (size_t) (20 * kSr), 0.0f);
    auto out = render (e, in);
    double peak = 0;
    bool finite = true;
    for (float s : out) { peak = std::max (peak, (double) std::fabs (s)); finite = finite && std::isfinite (s); }
    const double lateRms = 20 * std::log10 (rms (out.data() + out.size() - (size_t) kSr, (size_t) kSr) + 1e-12);
    CHECK (finite && peak < 0.99, "peak %.2f dBFS with feedback near self-oscillation (mark under 0 dBFS)", 20 * std::log10 (peak));
    CHECK (lateRms < -6, "20 s after the last note the wash sits at %.1f dBFS RMS, bounded (mark under -6)", lateRms);
    // heel fully back with Cut: tails go
    e.setParam (P::BloomTails, 0);
    e.setBloom (0.0f);
    std::vector<float> sil ((size_t) (1.0 * kSr), 0.0f);
    auto t = render (e, sil);
    const double after = 20 * std::log10 (rms (t.data() + t.size() / 2, t.size() / 2) + 1e-12);
    if (getenv ("KS_DEBUG")) for (int k = 0; k < 10; ++k) std::printf ("      cut %d00 ms: %.1f dB\n", k, 20 * std::log10 (rms (t.data() + (size_t) (k * 0.1 * kSr), (size_t) (0.1 * kSr)) + 1e-12));
    // the wash is gone; what's left is the reverb's own short tail of it, which rings on
    // because the reverb is switched on (the delay's repeats no longer die off by themselves).
    // Since 0.4.9 the reverb decays in exactly its Decay time (3 s) instead of faster, so
    // half a second on it's that tail, about 25 dB under the wash: mark -35.
    CHECK (after < -35, "heel back with Cut: %.1f dBFS half a second later (mark -35)", after);
}

// ---------------------------------------------------------------------------------------
// Clicks: largest second difference against the steady tone's own (Backtrack lesson).
static double clickScore (const std::vector<float>& y, size_t from, size_t to, double f, double amp)
{
    double worst = 0;
    for (size_t i = from + 1; i + 1 < to; ++i) worst = std::max (worst, (double) std::fabs (y[i + 1] - 2 * y[i] + y[i - 1]));
    const double w = 2 * M_PI * f / kSr;
    return worst / (amp * w * w);
}

static void testClicks()
{
    std::printf ("Click-free switching\n");
    struct Case { const char* name; P p; float a, b; };
    for (const auto& c : { Case { "Edge to Drive", P::DriveVoice, 0, 1 }, Case { "Bitrot on", P::BitrotOn, 0, 1 },
                           Case { "Filter on", P::FiltOn, 0, 1 }, Case { "Slap on", P::CompSlap, 0, 1 },
                           Case { "Drive on", P::DriveOn, 0, 1 }, Case { "Wah on", P::WahOn, 0, 1 },
                           Case { "Growl/Cut flip", P::PreFlip, 0, 1 } })
    {
        Engine e;
        e.prepare (kSr, 64);
        e.setParam (P::DriveSource, 1);
        e.setParam (c.p, c.a);
        const double f = 220, amp = 0.05;   // quiet enough that drives stay near linear
        auto in = sine (f, 1.0, amp);
        std::vector<float> out (in.size());
        for (size_t i = 0; i < in.size(); i += 64)
        {
            if (i == (size_t) (0.5 * kSr)) e.setParam (c.p, c.b);
            e.process (in.data() + i, out.data() + i, 64);
        }
        // compare the switch region against an equal stretch of steady output before it
        const size_t s0 = (size_t) (0.5 * kSr);
        // steady output on both sides: the new state may be brighter, which isn't a click
        const double steady = std::max (clickScore (out, s0 - 4800, s0, f, amp), clickScore (out, s0 + 9600, s0 + 14400, f, amp));
        const double sw = clickScore (out, s0, s0 + 4800, f, amp);
        CHECK (sw < std::max (3.0, steady * 1.6), "%s: switch %.2f against steady %.2f (mark 1.6x or 3)", c.name, sw, steady);
    }
}

// A held tone through the drive must stay smooth: level matching once stepped its trim
// ten times a second, a faint click each time.
static void testSteadyDrive()
{
    std::printf ("Steady drive\n");
    double worst[2] {};
    for (int fixedTrim = 0; fixedTrim < 2; ++fixedTrim)
    {
        Engine e;
        e.prepare (kSr, 64);
        e.setParam (P::DriveSource, 1);
        e.setParam (P::DimOn, 0);                        // its chorus moves the level on purpose
        e.setParam (P::DriveMatch, fixedTrim ? 0 : 1);   // reference: no level matching at all
        const double f = 220, amp = 0.05;
        auto in = sine (f, 3.0, amp);
        auto out = render (e, in);
        std::vector<double> sc;
        for (size_t a = (size_t) (0.15 * kSr); a + 2400 < (size_t) (1.6 * kSr); a += 2400)   // while the level match is still settling
            sc.push_back (clickScore (out, a, a + 2400, f, amp));
        auto sorted = sc;
        std::sort (sorted.begin(), sorted.end());
        worst[fixedTrim] = sorted.back() / sorted[sorted.size() / 2];
    }
    CHECK (worst[0] < std::max (1.3, worst[1] * 1.15), "level matching adds no steps: worst window %.2fx the median, %.2fx without matching", worst[0], worst[1]);
}

// ---------------------------------------------------------------------------------------
static void testTuner()
{
    std::printf ("Tuner\n");
    for (double f : { 41.203, 36.708, 55.0, 98.0, 146.83 })
    {
        Engine e;
        e.prepare (kSr, 64);
        e.action (Action::TunerOn);
        auto in = pluck (f * std::pow (2.0, 3.0 / 1200), 1.0, 0.4, 2.0);
        std::vector<float> out (in.size());
        Meters m {};
        for (size_t i = 0; i < in.size(); i += 64)
        {
            e.process (in.data() + i, out.data() + i, 64);
            if (i % 2400 == 0) { e.idle(); std::this_thread::sleep_for (std::chrono::milliseconds (0)); }
        }
        // the tuner runs at most every 45 ms of wall time: give it a few goes
        for (int k = 0; k < 4; ++k) { std::this_thread::sleep_for (std::chrono::milliseconds (50)); e.idle(); }
        m = e.meters();
        const auto want = nameNote (f);
        CHECK (m.tunerMidi == want.midi && std::fabs (m.tunerCents - 3.0) < 1.5, "%.1f Hz played 3 cents sharp reads %s%d %+.1f cents",
               f, noteName (m.tunerMidi), m.tunerMidi / 12 - 1, m.tunerCents);
        double outPeak = 0;
        for (size_t i = in.size() / 2; i < in.size(); ++i) outPeak = std::max (outPeak, (double) std::fabs (out[i]));
        if (f == 41.203) CHECK (outPeak < 1e-3, "output muted while tuning");
    }
}

// ---------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------
// The pedal's jobs: dirt blend, wah, volume, Bloom, with soft takeover between banks.
static double bandDb (const std::vector<float>& y, double f, size_t from)
{
    // Goertzel power at one frequency over the steady part
    const double w = 2 * M_PI * f / kSr, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < y.size(); ++i) { const double s0 = y[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return 10 * std::log10 (s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-20);
}

static double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double m = 0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) m = std::max (m, (double) std::fabs (a[i] - b[i]));
    return m;
}

static void testPedalJobs()
{
    std::printf ("Pedal jobs: dirt blend, wah, volume, takeover\n");
    auto in = bassLine (2.0, 0.5);
    auto setup = [] (Engine& e) { e.prepare (kSr, 64); neutral (e); e.setParam (P::DroneOn, 0); };

    // dirt blend: heel is clean even with the drive and Bitrot switched on
    {
        Engine dirty, clean;
        setup (dirty); setup (clean);
        dirty.setParam (P::DriveOn, 1); dirty.setParam (P::DriveVoice, 1); dirty.setParam (P::BitrotOn, 1);
        dirty.setPedalBank (0); dirty.setPedal (0.0f);
        const double d = maxDiff (render (dirty, in), render (clean, in));
        CHECK (d < 1.0e-4, "Grit, heel: drive and Bitrot on but blended out, same as clean (%.6f)", d);
        Engine toe; setup (toe);
        toe.setParam (P::DriveOn, 1); toe.setParam (P::DriveVoice, 1); toe.setParam (P::BitrotOn, 1);
        toe.setPedalBank (0); toe.setPedal (1.0f);
        Engine clean2; setup (clean2);
        const double d2 = maxDiff (render (toe, in), render (clean2, in));
        CHECK (d2 > 0.05, "Grit, toe: fully dirty (differs from clean by %.3f)", d2);
        Engine half, driveOnly;
        setup (half); setup (driveOnly);
        for (Engine* e : { &half, &driveOnly }) { e->setParam (P::DriveOn, 1); e->setParam (P::DriveVoice, 1); }
        half.setParam (P::BitrotOn, 1); half.setParam (P::PedalBitrotAt, 0.5f);
        half.setPedalBank (0); half.setPedal (0.5f);
        const double d3 = maxDiff (render (half, in), render (driveOnly, in));
        CHECK (d3 < 1.0e-4, "Grit, halfway with Bitrot joining at 50%%: the drive is fully in, Bitrot not yet (%.6f)", d3);
    }

    // soft takeover between banks
    {
        Engine e; setup (e);
        e.setPedalBank (0); e.setPedal (0.6f);
        e.setPedalBank (2); e.setPedal (0.2f);
        auto m = e.meters();
        CHECK (std::fabs (m.dirt - 0.6f) < 1e-6 && std::fabs (m.bloom - 0.2f) < 1e-6, "Glow: Bloom takes the pedal on its first move, the dirt stays at 60%%");
        e.setPedalBank (0); e.setPedal (0.3f);
        m = e.meters();
        CHECK (std::fabs (m.dirt - 0.6f) < 1e-6 && m.waiting, "back in Grit: the dirt waits at 60%% while the pedal is at 30%%");
        e.setPedal (0.5f);
        CHECK (std::fabs (e.meters().dirt - 0.6f) < 1e-6, "still waiting at 50%%");
        e.setPedal (0.7f);
        m = e.meters();
        CHECK (std::fabs (m.dirt - 0.7f) < 1e-6 && ! m.waiting, "the pedal passes 60%%: the dirt follows it again");
        e.resetPedalJobs();
        m = e.meters();
        CHECK (! m.dirtActive && ! m.bloomActive && m.volume == 1.0f, "a new song: every job back to its saved sound");
    }

    // volume: starts full and waits, heel silent, or the song's floor
    {
        Engine e; setup (e);
        e.setParam (P::DriveOn, 0);
        e.setPedalBank (1); e.setPedal (0.0f);
        auto m = e.meters();
        CHECK (m.pedalTarget == (int) PedalJob::Volume && m.volume == 1.0f && m.waiting, "Groove with the heel down: volume stays full until the pedal reaches it");
        const double full = rms (render (e, in).data(), in.size());
        e.setPedal (0.5f); e.setPedal (1.0f); e.setPedal (0.0f);
        auto heel = render (e, in);
        const double heelDb = 20 * std::log10 (rms (heel.data() + heel.size() / 4, heel.size() * 3 / 4) / full + 1e-12);
        CHECK (heelDb < -80, "once caught, heel down is silent (%.1f dB)", heelDb);
        e.setParam (P::PedalVolFloor, -18);
        auto fl = render (e, in);
        const double floorDb = 20 * std::log10 (rms (fl.data() + fl.size() / 4, fl.size() * 3 / 4) / full + 1e-12);
        CHECK (std::fabs (floorDb + 18) < 1.5, "with a -18 dB floor the heel sits at %.1f dB", floorDb);
        e.setPedal (0.5f);
        auto hv = render (e, in);
        const double halfDb = 20 * std::log10 (rms (hv.data() + hv.size() / 4, hv.size() * 3 / 4) / full + 1e-12);
        CHECK (halfDb < -10 && halfDb > -14, "halfway sits at %.1f dB, a volume pedal's taper (mark -10 to -14)", halfDb);
    }

    // wah: the peak sweeps up from heel to toe, only when switched on
    {
        std::vector<float> saw ((size_t) (2 * kSr));
        for (size_t i = 0; i < saw.size(); ++i) { double t = i / kSr, x = 0; for (int k = 1; k <= 40; ++k) x += std::sin (2 * M_PI * 55 * k * t) / k; saw[i] = (float) (0.3 * x); }
        Engine heel, toe;
        for (Engine* e : { &heel, &toe }) { setup (*e); e->setParam (P::WahOn, 1); e->setPedalBank (1); }
        heel.setPedal (0.0f); toe.setPedal (1.0f);
        auto h = render (heel, saw), t = render (toe, saw);
        const size_t from = saw.size() / 2;
        const double up = bandDb (t, 2200, from) - bandDb (h, 2200, from), down = bandDb (h, 330, from) - bandDb (t, 330, from);
        CHECK (up > 10 && down > 10, "wah: toe lifts 2.2 kHz by %.1f dB, heel lifts 330 Hz by %.1f dB (marks 10 dB)", up, down);
        Engine off; setup (off); off.setPedalBank (1); off.setPedal (0.5f); off.setPedal (1.0f);
        Engine ref; setup (ref);
        const double d = maxDiff (render (off, in), render (ref, in));
        CHECK (d < 1.0e-4, "wah off and volume at the toe: no change to the sound (%.6f)", d);
    }

    // the Growl/Cut flip plays the second blend
    {
        Engine a, b;
        setup (a); setup (b);
        for (Engine* e : { &a, &b }) e->setParam (P::PreOn, 1);
        a.setParam (P::PreFlip, 1); a.setParam (P::PreBlendB, 0.9f);
        b.setParam (P::PreBlend, 0.9f);
        const double d = maxDiff (render (a, in), render (b, in));
        CHECK (d < 1.0e-5, "tap C in Grit flips the preamp to its second blend (%.7f)", d);
    }
}

// Tails ring over a part change; a plain switch-off still fades them.
static void testPartTails()
{
    std::printf ("Tails over a part change\n");
    for (int which = 0; which < 2; ++which)
    {
        const char* name = which == 0 ? "delay" : "reverb";
        double after[2] = { 0, 0 };
        for (int spill = 0; spill < 2; ++spill)
        {
            Engine e;
            e.prepare (kSr, 64);
            neutral (e);
            e.setParam (P::DroneOn, 0);
            if (which == 0) { e.setParam (P::DlOn, 1); e.setParam (P::DlMix, 0.5f); e.setParam (P::DlFeedback, 0.5f); e.setParam (P::DlTime, 300); }
            else { e.setParam (P::RvOn, 1); e.setParam (P::RvMix, 0.5f); e.setParam (P::RvDecay, 4); }
            auto note = pluck (82.4, 0.5, 0.8, 0.3);
            render (e, note);
            if (spill) e.action (Action::Spill);
            e.setParam (which == 0 ? P::DlOn : P::RvOn, 0);
            std::vector<float> sil ((size_t) (0.6 * kSr), 0.0f);
            auto t = render (e, sil);
            after[spill] = 20 * std::log10 (rms (t.data() + (size_t) (0.3 * kSr), (size_t) (0.3 * kSr)) + 1e-12);
        }
        CHECK (after[1] > -50 && after[1] > after[0] + 30, "%s: over a part change the tail still rings (%.1f dBFS, against %.1f switched off)", name, after[1], after[0]);
    }
}

static void testController()
{
    std::printf ("Controller: footswitches and setlist\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_test";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e;
    e.prepare (kSr, 64);
    Controller c (e, dir.string(), (dir / "caps").string());
    int keeps = 0;
    c.onKeep = [&] (const std::string& j) { keeps += j.find ("\"plugin\":\"Keystone\"") != std::string::npos; };
    // four plain songs, so the footswitch checks below have a known list
    std::filesystem::create_directories (dir);
    std::ofstream (dir / "setlist.json") << R"({"version":6,"current":0,"songs":[{"name":"New choon","values":{}},{"name":"Robot","values":{}},{"name":"Circus","values":{}},{"name":"Comedian","values":{}}]})";
    c.load();
    CHECK (c.songCount() == 4 && c.songName (0) == "New choon", "a setlist of your own loads as it is");
    double t = 0;
    auto tap = [&] (int sw) { c.footswitchPress (sw, t); t += 0.1; c.footswitchRelease (sw, t); t += 0.5; };
    auto hold = [&] (int sw) { c.footswitchPress (sw, t); for (int i = 0; i < 10; ++i) { t += 0.06; c.tick (t); } c.footswitchRelease (sw, t); t += 0.5; };
    CHECK (c.bank() == 0, "songs open in Grit by default");
    tap (0);
    CHECK (c.value (P::DriveVoice) == 1, "Grit, tap A: Edge to Drive");
    tap (0);
    CHECK (c.value (P::DriveVoice) == 0, "Grit, tap A again: back to Edge");
    tap (1);
    CHECK (c.value (P::BitrotOn) == 1, "Grit, tap B: Bitrot on");
    tap (2);
    CHECK (c.value (P::PreFlip) == 1, "Grit, tap C: preamp flipped to the second blend");
    CHECK (c.edited(), "stomps mark the song edited");
    {
        const float fBefore = c.value (P::FiltOn), dBefore = c.value (P::DimOn);
        hold (0);
        CHECK (c.bank() == 1 && c.value (P::FiltOn) == fBefore && c.value (P::DimOn) == dBefore && c.value (P::BitrotOn) == 1,
               "hold A: Groove, and nothing about the sound changes");
    }
    tap (0);
    CHECK (c.value (P::FiltOn) == 1, "Groove, tap A: auto filter on");
    tap (1);
    CHECK (c.value (P::DimOn) == 0, "Groove, tap B: Dimension off");
    tap (2);
    CHECK (c.value (P::WahOn) == 1 && e.meters().pedalTarget == (int) PedalJob::Wah, "Groove, tap C: wah on, the pedal sweeps it");
    tap (2);
    CHECK (c.value (P::WahOn) == 0 && e.meters().pedalTarget == (int) PedalJob::Volume, "Groove, tap C again: wah off, the pedal is volume");
    hold (0);
    CHECK (c.bank() == 2 && e.meters().pedalTarget == (int) PedalJob::Bloom, "hold A again: Glow, the pedal is Bloom");
    tap (0); tap (1); tap (2);
    CHECK (c.value (P::GrOn) == 1 && c.value (P::DlOn) == 1 && c.value (P::RvOn) == 1, "Glow: taps A, B, C switch Speckle, delay and reverb");
    hold (0);
    CHECK (c.bank() == 0 && e.meters().pedalTarget == (int) PedalJob::Dirt, "hold A a third time: back to Grit, the pedal is the dirt blend");
    hold (1);
    CHECK (c.currentSong() == 1 && c.value (P::BitrotOn) == 0 && c.value (P::PreFlip) == 0, "hold B: next song, back to its saved state");
    hold (1); hold (1);
    CHECK (c.currentSong() == 3, "hold B steps on to the last song");
    hold (1);
    CHECK (c.currentSong() == 3 && c.lastToast() == "LAST SONG", "hold B at the last song stays put, no wrap to song one");
    c.selectSong (0);
    hold (3);
    CHECK (c.value (P::CompSlap) == 1, "hold D: slap on");
    hold (2);
    for (int i = 0; i < 4; ++i) { e.process (std::vector<float> (64).data(), std::vector<float> (64).data(), 64); }
    CHECK (c.tunerOn(), "hold C: tuner");
    hold (0);
    CHECK (c.bank() == 0, "hold A does nothing while tuning");
    tap (1);
    CHECK (! c.tunerOn() && c.value (P::BitrotOn) == 0, "any tap leaves the tuner without doing its job");
    // hold B counts from 0.3 s
    {
        const int before = c.currentSong();
        c.footswitchPress (1, t); t += 0.32; c.tick (t); c.footswitchRelease (1, t); t += 0.5;
        CHECK (c.currentSong() == before + 1, "hold B counts from 0.3 s now");
        c.selectSong (before);
        // A and B pressed together in Stomp mode: nothing, and neither tap fires
        const float dv = c.value (P::DriveVoice), br = c.value (P::BitrotOn);
        c.footswitchPress (0, t); c.footswitchPress (1, t + 0.02); c.footswitchRelease (0, t + 0.1); c.footswitchRelease (1, t + 0.12); t += 0.5;
        CHECK (c.currentSong() == before && c.value (P::DriveVoice) == dv && c.value (P::BitrotOn) == br, "A and B together: no song change, no taps");
    }
    // C and D pressed together: Song mode, the moment both are down; Keep is off the feet
    const float slapBefore = c.value (P::CompSlap);
    c.footswitchPress (2, t); c.footswitchPress (3, t + 0.03);
    CHECK (c.footMode() == 1 && c.lastToast() == "SONG MODE", "C and D together: Song mode straight away");
    for (int i = 0; i < 25; ++i) { t += 0.05; c.tick (t); }
    c.footswitchRelease (2, t); c.footswitchRelease (3, t); t += 0.5;
    CHECK (keeps == 0 && ! c.tunerOn() && c.value (P::CompSlap) == slapBefore, "holding them on doesn't Keep, tune or slap");
    {
        c.selectSong (0);
        auto press = [&] (int sw) { c.footswitchPress (sw, t); t += 0.05; c.tick (t); };
        auto lift = [&] (int sw) { c.footswitchRelease (sw, t); t += 0.5; };
        press (2);
        CHECK (c.currentSong() == 1, "Song mode, C: next song on the press");
        t += 0.5; c.tick (t); lift (2);
        CHECK (c.currentSong() == 1 && ! c.tunerOn(), "holding C neither tunes nor steps again");
        press (0); lift (0);
        CHECK (c.currentSong() == 0, "Song mode, A: previous song");
        const float bit = c.value (P::BitrotOn);
        press (1); lift (1);
        CHECK (c.lastToast() == "LAST PART" && c.currentSong() == 0 && c.value (P::BitrotOn) == bit, "Song mode, B: no part to go to, and no Bitrot tap");
        press (0); press (1); lift (0); lift (1);
        CHECK (c.currentSong() == 0 && c.rig() == 0, "A then B straight after: two presses, not a combo");
        // C first then D straight after: back to Stomp mode, and the C press is undone
        c.footswitchPress (2, t); c.footswitchPress (3, t + 0.04);
        CHECK (c.footMode() == 0 && c.currentSong() == 0, "C+D with C a touch first: Stomp mode, still on the same song");
        c.footswitchRelease (2, t + 0.2); c.footswitchRelease (3, t + 0.2); t += 1;
    }
    // hold A and B for a second in Stomp mode: Kuddle, and back
    c.footswitchPress (0, t); c.footswitchPress (1, t + 0.02);
    for (int i = 0; i < 25; ++i) { t += 0.05; c.tick (t); }
    c.footswitchRelease (0, t); c.footswitchRelease (1, t); t += 0.5;
    CHECK (c.rig() == 1, "hold A and B: Kuddle");
    c.footswitchPress (2, t); c.footswitchPress (3, t + 0.02);
    for (int i = 0; i < 25; ++i) { t += 0.05; c.tick (t); }
    c.footswitchRelease (2, t); c.footswitchRelease (3, t); t += 0.5;
    CHECK (keeps == 1, "in Kuddle, hold C and D still Keeps");
    c.footswitchPress (0, t); c.footswitchPress (1, t + 0.02);
    for (int i = 0; i < 25; ++i) { t += 0.05; c.tick (t); }
    c.footswitchRelease (0, t); c.footswitchRelease (1, t); t += 0.5;
    CHECK (c.rig() == 0 && c.footMode() == 0, "hold A and B again: back to Keystone in Stomp mode");

    // setlist
    auto msg = [&] (const std::string& m) { c.handleMessage (m, [] (const std::string&) {}); };
    msg (R"({"t":"setlist","op":"save"})");
    CHECK (! c.edited(), "save clears the edited mark");
    msg (R"({"t":"setlist","op":"add","name":"Big song"})");
    CHECK (c.songCount() == 5 && c.currentSong() == 4, "add a song");
    msg (R"({"t":"setlist","op":"rename","i":4,"name":"Encore"})");
    msg (R"({"t":"setlist","op":"move","i":4,"to":0})");
    CHECK (c.songName (0) == "Encore" && c.currentSong() == 0, "rename and move to the top, still playing");
    msg (R"({"t":"setlist","op":"copy","i":1})");
    CHECK (c.songCount() == 6 && c.songName (2) == "New choon copy", "copy");
    for (int i = 0; i < 12; ++i) msg (R"({"t":"setlist","op":"add"})");
    CHECK (c.songCount() == 15, "fifteen songs at most");
    msg (R"({"t":"setlist","op":"delete","i":2})");
    CHECK (c.songCount() == 14, "delete");

    // parts: a song split in two, one press of B between them
    c.selectSong (0);
    c.setValue (P::BankStart, 2); c.setValue (P::DlOn, 1); c.setValue (P::RvOn, 1);
    msg (R"({"t":"setlist","op":"save"})");
    msg (R"({"t":"setlist","op":"addPart","i":0,"name":"Drop"})");
    CHECK (c.songCount() == 15 && c.isPart (1) && c.songName (1) == "Drop" && c.songNumber (1) == 1 && c.songNumber (2) == 2 && c.songsOnly() == 14,
           "add a part: it shares its song's number and doesn't count as a song");
    c.selectSong (1);
    c.setValue (P::BankStart, 0); c.setValue (P::DlOn, 0); c.setValue (P::RvOn, 0); c.setValue (P::DriveOn, 1); c.setValue (P::DriveVoice, 1);
    msg (R"({"t":"setlist","op":"save"})");
    c.selectSong (0);
    CHECK (c.bank() == 2 && c.value (P::DlOn) == 1, "the song opens in Glow with the delay on");
    hold (1);
    CHECK (c.currentSong() == 1 && c.bank() == 0 && c.value (P::DlOn) == 0 && c.value (P::DriveVoice) == 1,
           "hold B: one press to the part, in Grit with Drive on and the space off");
    msg (R"({"t":"setlist","op":"add"})");
    CHECK (c.songCount() == 16 && c.songsOnly() == 15, "fifteen songs plus a part");
    msg (R"({"t":"setlist","op":"add"})");
    CHECK (c.songCount() == 16 && c.lastToast() == "15 songs is the limit", "still fifteen songs at most");
    msg (R"({"t":"setlist","op":"addPart","i":15})");
    CHECK (c.songCount() == 17 && c.isPart (16) && c.songName (16) == "Part 2", "parts can still be added");
    msg (R"({"t":"setlist","op":"part","i":16})");
    CHECK (c.isPart (16) && c.songCount() == 17 && c.lastToast() == "15 songs is the limit", "a part can't become a sixteenth song");
    msg (R"({"t":"setlist","op":"delete","i":16})");
    msg (R"({"t":"setlist","op":"delete","i":15})");
    // reload from disk
    Engine e2;
    e2.prepare (kSr, 64);
    Controller c2 (e2, dir.string(), (dir / "caps").string());
    c2.load();
    CHECK (c2.songCount() == 15 && c2.songName (0) == "Encore" && c2.isPart (1) && c2.songName (1) == "Drop" && c2.songName (2) == "New choon",
           "setlist and its parts survive a restart");
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

static void testKuddle()
{
    std::printf ("Kuddle, the home rig\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_kuddle";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    auto in = bassLine (2.0, 0.5);
    {
        Engine e; e.prepare (kSr, 64);
        Controller c (e, dir.string(), (dir / "caps").string());
        c.load();
        CHECK (c.rig() == 0, "first run starts in Keystone");
        auto ks = render (e, in);
        c.setRig (1);
        CHECK (c.rig() == 1 && c.songCount() == 3 && c.songName (0) == "Voodoo" && c.songName (2) == "Dub", "switch: Kuddle with Voodoo, Khruangbin and Dub");
        auto kd = render (e, in);
        const double kdDb = 20 * std::log10 (rms (kd.data() + kd.size() / 4, kd.size() * 3 / 4) + 1e-12);
        CHECK (kdDb > -40 && maxDiff (ks, kd) > 0.02, "Kuddle plays, and sounds different (%.1f dBFS)", kdDb);
        double t = 0;
        auto tap = [&] (int sw) { c.footswitchPress (sw, t); t += 0.1; c.footswitchRelease (sw, t); t += 0.5; };
        auto hold = [&] (int sw) { c.footswitchPress (sw, t); for (int i = 0; i < 10; ++i) { t += 0.06; c.tick (t); } c.footswitchRelease (sw, t); t += 0.5; };
        tap (0); tap (1); tap (2); tap (3);
        CHECK (c.value (P::KMuteOn) == 0 && c.value (P::KWahOn) == 0 && c.value (P::KRoomOn) == 1 && c.value (P::KTapeOn) == 0,
               "Kuddle taps: A Mute, B Auto wah, C Room, D Tape");
        hold (1);
        CHECK (c.currentSong() == 1 && c.value (P::KRoomOn) == 1 && c.value (P::KWahOn) == 0, "hold B: Khruangbin (room on, wah off)");
        hold (0);
        CHECK (c.currentSong() == 0, "hold A: back to Voodoo");
        c.footswitchPress (3, t); for (int i = 0; i < 10; ++i) { t += 0.06; c.tick (t); }
        render (e, in);
        CHECK (e.meters().kuddleSqueezeDb > 0.5f, "hold D: the Opto squeezes (%.1f dB)", e.meters().kuddleSqueezeDb);
        c.footswitchRelease (3, t); t += 0.5;
        e.setPedal (0.8f);
        CHECK (std::fabs (e.meters().melt - 0.8f) < 1e-6 && ! e.meters().bloomActive, "in Kuddle the pedal is Melt, Bloom left alone");
        c.setRig (0);
        CHECK (c.rig() == 0 && c.songName (0) == "The Comedian and the Boxer", "back to Keystone and its own setlist");
    }
    {
        Engine e; e.prepare (kSr, 64);
        Controller c (e, dir.string(), (dir / "caps").string());
        c.setRig (0);
        c.load();
        CHECK (c.rig() == 0, "the last rig used comes back at start");
        c.setRig (1);
        Engine e2; e2.prepare (kSr, 64);
        Controller c2 (e2, dir.string(), (dir / "caps").string());
        c2.load();
        CHECK (c2.rig() == 1 && c2.songName (0) == "Voodoo", "Kuddle comes back too, with its presets");
    }
    // Keystone's tails ring out over a switch to Kuddle
    {
        Engine e; e.prepare (kSr, 64);
        neutral (e); e.setParam (P::DroneOn, 0);
        e.setParam (P::DlOn, 1); e.setParam (P::DlMix, 0.5f); e.setParam (P::DlFeedback, 0.6f); e.setParam (P::DlTime, 300);
        e.setParam (P::KOutput, -60);   // Kuddle near silent, so only the tail is measured
        render (e, pluck (82.4, 0.5, 0.8, 0.3));
        e.setRig (1);
        std::vector<float> sil ((size_t) (0.8 * kSr), 0.0f);
        auto tl = render (e, sil);
        const double tail = 20 * std::log10 (rms (tl.data() + (size_t) (0.2 * kSr), (size_t) (0.4 * kSr)) + 1e-12);
        CHECK (tail > -50, "the delay tail rings on after switching to Kuddle (%.1f dBFS)", tail);
        e.setParam (P::KOutput, -3);
        auto kd = render (e, in);
        Engine ref; ref.prepare (kSr, 64); ref.setRig (1); neutral (ref);
        // the bass no longer reaches Keystone: after the tail, only Kuddle plays
        CHECK (e.meters().rig == 1, "Kuddle is the rig playing");
    }
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

static void testNewDefaults()
{
    std::printf ("0.4 defaults, and old songs moved across\n");
    CHECK (info (P::DroneRelease).def == 2.0f && info (P::BitrotVol).def == 0.4f, "drone trails off over 2 s, Bitrot at 40%%");
    CHECK (info (P::DrivePresence).def == 4.0f && info (P::DriveLowCut).def == 110.0f && info (P::DriveLevel).def == 1.5f,
           "the Drive voice cuts through: presence +4 dB, low cut 110 Hz, level +1.5 dB");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_v7";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    std::filesystem::create_directories (dir);
    std::ofstream (dir / "setlist.json") << R"({"version":6,"current":0,"songs":[{"name":"Old","values":{"bitrot.vol":0.7,"drone.release":4,"drive.drive.presence":2,"drive.drive.lowcut":70,"drive.drive.level":0}},{"name":"Tweaked","values":{"bitrot.vol":0.55,"drone.release":6}}]})";
    Engine e; e.prepare (kSr, 64);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    CHECK (std::fabs (c.value (P::BitrotVol) - 0.4f) < 1e-4 && c.value (P::DroneRelease) == 2.0f && c.value (P::DrivePresence) == 4.0f,
           "a song on the old defaults moves to the new ones");
    CHECK (std::fabs (c.value (P::Tempo) - 142.9f) < 0.05f, "and gets a tempo from its delay time (%.1f BPM from 420 ms)", c.value (P::Tempo));
    c.selectSong (1);
    CHECK (std::fabs (c.value (P::BitrotVol) - 0.55f) < 1e-4 && c.value (P::DroneRelease) == 6.0f, "a song you'd changed keeps your settings");
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

static void testRecorder()
{
    std::printf ("The rehearsal recorder\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_rec";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e; e.prepare (kSr, 128);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    auto& r = c.recorder();
    std::vector<float> quiet ((size_t) (3 * kSr), 0.0f);
    render (e, quiet);
    r.flushForTests();
    CHECK (! r.recording() && r.takes().empty(), "silence: no take");
    render (e, pluck (55.0, 2.0, 0.6, 0.6));
    r.flushForTests();
    CHECK (r.recording(), "the first note starts a take");
    c.selectSong (1);
    c.mark ("Keep");
    std::vector<float> minute ((size_t) (65 * kSr), 0.0f);
    render (e, minute);
    r.flushForTests();
    std::this_thread::sleep_for (std::chrono::milliseconds (150));
    const auto takes = r.takes();
    CHECK (! r.recording() && takes.size() == 1, "a quiet minute ends it");
    if (takes.size() == 1)
    {
        const double secs = takes[0].seconds;
        CHECK (secs > 6.5 && secs < 7.5, "two seconds before the note, the note, three after: %.1f s (mark 7)", secs);
        std::ifstream f (dir / "Recordings" / (takes[0].name + ".wav"), std::ios::binary);
        char h[44] = {};
        f.read (h, 44);
        uint32_t dataBytes = 0; std::memcpy (&dataBytes, h + 40, 4);
        CHECK (std::string (h, 4) == "RIFF" && std::string (h + 8, 4) == "WAVE" && (h[22] == 2) && (uint8_t) h[34] == 24
               && dataBytes == (uint32_t) (std::filesystem::file_size (dir / "Recordings" / (takes[0].name + ".wav")) - 44), "a proper 24-bit stereo WAV");
        std::ifstream js (dir / "Recordings" / (takes[0].name + ".json"));
        const std::string side ((std::istreambuf_iterator<char> (js)), std::istreambuf_iterator<char>());
        CHECK (side.find ("Song: The Comedian and the Boxer") != std::string::npos && side.find ("Song: When the Circus") != std::string::npos
               && side.find ("Keep") != std::string::npos, "marked: the song at the start, the song change, the Keep (%s)", side.substr (0, 300).c_str());
        // the phone's room recording, in two pieces of base64
        auto msg = [&] (const std::string& m) { std::string got; c.handleMessage (m, [&] (const std::string& x) { got = x; }); return got; };
        const auto a1 = msg (R"({"t":"room","take":")" + takes[0].name + R"(","seq":0,"last":false,"ext":"m4a","data":"aGVsbG8g"})");
        const auto a2 = msg (R"({"t":"room","take":")" + takes[0].name + R"(","seq":1,"last":true,"ext":"m4a","data":"cm9vbQ=="})");
        std::ifstream room (dir / "Recordings" / (takes[0].name + " room.m4a"), std::ios::binary);
        const std::string got ((std::istreambuf_iterator<char> (room)), std::istreambuf_iterator<char>());
        CHECK (got == "hello room" && a2.find ("\"ok\":true") != std::string::npos, "the phone's room file lands beside the take");
        const auto bad = msg (R"({"t":"room","take":"../evil","seq":0,"last":true,"ext":"m4a","data":"aGk="})");
        CHECK (bad.find ("\"ok\":false") != std::string::npos, "a room file can't be written anywhere else");
    }
    // the listening pack: that take's bass through every song and part
    if (takes.size() == 1)
    {
        std::string why;
        auto t0 = std::chrono::steady_clock::now();
        CHECK (c.startListening (takes[0].name, 0, 30, why) && c.listening(), "a listening pack starts (%s)", why.c_str());
        std::string other;
        CHECK (! c.startListening (takes[0].name, 0, 30, other), "only one at a time");
        c.waitForListening();
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        c.tick (100);
        const auto out = c.takeBroadcasts();
        bool done = false;
        for (auto& b : out) if (b.find ("listenDone") != std::string::npos && b.find ("\"ok\":true") != std::string::npos) done = true;
        const auto folder = dir / "Recordings" / "Listening" / takes[0].name;
        int files = 0; bool loud = true;
        for (auto& f : std::filesystem::directory_iterator (folder))
        {
            if (f.path().extension() != ".wav") continue;
            ++files;
            // each song's file has sound in it
            std::ifstream w (f.path(), std::ios::binary);
            std::vector<char> d ((std::istreambuf_iterator<char> (w)), std::istreambuf_iterator<char>());
            int peak = 0;
            for (size_t i = 44; i + 1 < d.size(); i += 2) peak = std::max (peak, std::abs ((int) (int16_t) ((uint8_t) d[i] | ((uint8_t) d[i + 1] << 8))));
            if (peak < 300) { loud = false; std::printf ("    quiet: %s (%d)\n", f.path().filename().string().c_str(), peak); }
        }
        CHECK (done && files == c.songCount() + 2 && loud, "a file for the dry bass, the take and each of %d songs and parts, all with sound (%d files, %.1f s)", c.songCount(), files, secs);
        CHECK (std::filesystem::exists (folder / "02 The Comedian and the Boxer.wav"), "named by song");
        // the phone's pages
        auto ask = [&] (const std::string& m) { std::string got; c.handleMessage (m, [&] (const std::string& x) { got = x; }); return got; };
        const auto info = ask (R"({"t":"takeInfo","take":")" + takes[0].name + R"("})");
        CHECK (info.find ("The Comedian and the Boxer") != std::string::npos && info.find ("\"pack\"") != std::string::npos, "the take's songs and its listening pack");
        const auto list = ask (R"({"t":"listenList","take":")" + takes[0].name + R"("})");
        CHECK (list.find ("\"played\":\"The Comedian and the Boxer\"") != std::string::npos && list.find ("01 As you played it.wav") != std::string::npos,
               "the pack knows what you were playing");
        // fetch a whole file in pieces, as the phone does
        std::vector<uint8_t> got;
        int64_t total = -1;
        while (total < 0 || (int64_t) got.size() < total)
        {
            const auto j = nlohmann::json::parse (ask (R"({"t":"listenGet","take":")" + takes[0].name + R"(","file":"03 When the Circus Comes to Town.wav","offset":)" + std::to_string (got.size()) + "}"));
            if (! j.value ("ok", false)) break;
            total = j.value ("total", (int64_t) 0);
            const auto part = Controller::fromBase64 (j.value ("data", std::string()));
            if (part.empty()) break;
            got.insert (got.end(), part.begin(), part.end());
        }
        CHECK (total > 0 && (int64_t) got.size() == total && total == (int64_t) std::filesystem::file_size (folder / "03 When the Circus Comes to Town.wav"), "a file comes across whole in pieces");
        CHECK (ask (R"({"t":"listenGet","take":")" + takes[0].name + R"(","file":"../../setlist.json","offset":0})").find ("\"ok\":false") != std::string::npos, "only the pack's own files");
        // USE: Circus's sound onto the Comedian, unsaved until you save
        c.selectSong (0);
        const auto use = ask (R"({"t":"listenUse","take":")" + takes[0].name + R"(","from":"When the Circus Comes to Town","onto":"The Comedian and the Boxer"})");
        CHECK (use.find ("\"ok\":true") != std::string::npos && c.currentSong() == 0 && c.edited(), "USE puts that sound on the song you played, as an edit");
        ask (R"({"t":"setlist","op":"revert"})");
        CHECK (! c.edited(), "Revert brings the old sound back");

    }
    c.setValue (P::RecOn, 0);
    render (e, pluck (55.0, 2.0, 0.6, 0.6));
    r.flushForTests();
    CHECK (! r.recording(), "switched off: playing doesn't record");
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

static void testFirmware()
{
    std::printf ("Update firmware from the phone\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_fw";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e; e.prepare (kSr, 128);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    auto msg = [&] (const std::string& m) { std::string got; c.handleMessage (m, [&] (const std::string& x) { got = x; }); return got; };
    const std::string begin = R"({"t":"fw","op":"begin","version":"0.4.9","file":"Keystone-0.4.9-Setup.exe","size":10,"sha256":")";
    CHECK (msg (begin + std::string (64, '0') + "\"}").find ("can't update itself") != std::string::npos, "a device with no updater says so");
    std::string gotFile, gotVersion;
    c.onFirmware = [&] (const std::string& f, const std::string& v) { gotFile = f; gotVersion = v; return std::string(); };
    CHECK (msg (R"({"t":"fw","op":"begin","version":"0.4.9","file":"..\\evil.exe","size":10,"sha256":"x"})").find ("\"ok\":false") != std::string::npos, "only a Keystone release file name");
    Sha256 h; h.add ((const uint8_t*) "hello room", 10);
    const std::string real = h.hex();
    Sha256 abc; abc.add ((const uint8_t*) "abc", 3);
    CHECK (abc.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc matches the standard");
    // a wrong checksum is refused
    msg (begin + std::string (64, '0') + "\"}");
    msg (R"({"t":"fw","op":"chunk","seq":0,"data":"aGVsbG8gcm9vbQ=="})");
    CHECK (msg (R"({"t":"fw","op":"end"})").find ("didn't arrive whole") != std::string::npos && gotFile.empty(), "a damaged update is never installed");
    // a missing piece is refused
    msg (begin + real + "\"}");
    CHECK (msg (R"({"t":"fw","op":"chunk","seq":1,"data":"aGVsbG8gcm9vbQ=="})").find ("missing") != std::string::npos, "a missing piece stops it");
    // the real thing
    msg (begin + real + "\"}");
    msg (R"({"t":"fw","op":"chunk","seq":0,"data":"aGVsbG8g"})");
    msg (R"({"t":"fw","op":"chunk","seq":1,"data":"cm9vbQ=="})");
    const auto end = msg (R"({"t":"fw","op":"end"})");
    CHECK (end.find ("\"ok\":true") != std::string::npos && gotVersion == "0.4.9" && std::filesystem::file_size (gotFile) == 10, "a whole update goes to the updater");
    // not while playing (recording off: it still notices playing, and leaves no take open)
    c.setValue (P::RecOn, 0);
    render (e, pluck (55.0, 1.0, 0.6, 0.6));
    c.recorder().flushForTests();
    CHECK (msg (begin + real + "\"}").find ("playing") != std::string::npos, "never while you're playing");
    std::ifstream st (dir / "started.json");
    const std::string started ((std::istreambuf_iterator<char> (st)), std::istreambuf_iterator<char>());
    CHECK (started.find (Controller::kVersion) != std::string::npos, "started.json tells the updater which version is running");
    st.close();
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}

static void testBackup()
{
    std::printf ("Daily backup\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_backup";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e; e.prepare (kSr, 128);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    c.handleMessage (R"({"t":"setlist","op":"save"})", [] (const std::string&) {});
    CHECK (c.backup ("2026-01-01") && std::filesystem::exists (dir / "Backups" / "2026-01-01" / "setlist.json"), "the first backup of the day copies the setlist");
    CHECK (! c.backup ("2026-01-01"), "only once a day");
    for (int d = 2; d <= 40; ++d) { char n[16]; std::snprintf (n, sizeof (n), "2026-01-%02d", d); c.backup (n); }
    int count = 0;
    for (auto& x : std::filesystem::directory_iterator (dir / "Backups")) { (void) x; ++count; }
    CHECK (count == 30 && ! std::filesystem::exists (dir / "Backups" / "2026-01-01") && std::filesystem::exists (dir / "Backups" / "2026-01-40"),
           "thirty days kept, the oldest go (%d)", count);
    // a broken setlist comes back from the newest backup
    c.handleMessage (R"({"t":"setlist","op":"rename","i":0,"name":"Backed up"})", [] (const std::string&) {});
    c.handleMessage (R"({"t":"setlist","op":"save"})", [] (const std::string&) {});
    std::filesystem::remove_all (dir / "Backups" / "2026-01-40");
    CHECK (c.backup ("2026-01-41"), "a new day's backup");
    std::ofstream (dir / "setlist.json") << "{ \"songs\": [ half a fi";
    Engine e2; e2.prepare (kSr, 128);
    Controller c2 (e2, dir.string(), (dir / "caps").string());
    c2.load();
    CHECK (c2.songName (0) == "Backed up" && std::filesystem::exists (dir / "setlist.json.broken"), "a broken setlist comes back from the backup (%s)", c2.songName (0).c_str());
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

static void testMyDefaults()
{
    std::printf ("Your own defaults\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_defaults";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e; e.prepare (kSr, 128);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    auto ask = [&] (const std::string& m) { std::string got; c.handleMessage (m, [&] (const std::string& x) { got = x; }); return got; };
    // song 2 has its own Bitrot already
    c.selectSong (2);
    c.setValue (P::BitrotSquare, 0.9f);
    ask (R"({"t":"setlist","op":"save"})");
    c.selectSong (0);
    const bool wasOn = c.value (P::BitrotOn) > 0.5f;
    // square and oscillator off, sub only
    c.setValue (P::BitrotSquare, 0); c.setValue (P::BitrotOsc, 0);
    const auto saved = nlohmann::json::parse (ask (R"({"t":"default","op":"save","module":"bitrot"})"));
    const int stale = saved.value ("stale", -1);
    CHECK (saved.value ("ok", false) && stale == c.songCount() - 1, "saving counts the songs still on the old default (%d of %d)", stale, c.songCount());
    const auto applied = nlohmann::json::parse (ask (R"({"t":"default","op":"apply","module":"bitrot"})"));
    c.selectSong (1);
    CHECK (applied.value ("moved", 0) == stale && c.value (P::BitrotSquare) < 0.01f && c.value (P::BitrotOsc) < 0.01f, "Update those: songs on the old default take the new one");
    c.selectSong (2);
    CHECK (std::fabs (c.value (P::BitrotSquare) - 0.9f) < 0.01f, "a song with its own Bitrot keeps it");
    c.setValue (P::BitrotOsc, 0.7f);
    ask (R"({"t":"default","op":"reset","module":"bitrot"})");
    CHECK (c.value (P::BitrotOsc) < 0.01f && c.edited(), "Reset goes back to your default, as an edit");
    ask (R"({"t":"setlist","op":"revert"})");
    ask (R"({"t":"setlist","op":"add","name":"New one"})");
    c.selectSong (c.songCount() - 1);
    CHECK (c.value (P::BitrotSquare) < 0.01f && c.value (P::BitrotSub) > 0.5f && (c.value (P::BitrotOn) > 0.5f) == false, "a new song starts with your default, Bitrot still off");
    (void) wasOn;
    Engine e2; e2.prepare (kSr, 128);
    Controller c2 (e2, dir.string(), (dir / "caps").string());
    c2.load();
    CHECK (std::fabs (c2.myDefault ((int) P::BitrotSquare)) < 0.01f, "your defaults are kept when Keystone starts again");
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

static double rmsOf (const std::vector<float>& x, size_t a, size_t b) { return rms (x.data() + a, b - a); }
// energy below a frequency: a simple 2-pole low-pass, then rms
static double lowBandRms (const std::vector<float>& x, double hz)
{
    Biquad lp; lp.lowPass ((float) hz, 0.707f, (float) kSr);
    double acc = 0; for (float v : x) { const float y = lp.process (v); acc += (double) y * y; }
    return std::sqrt (acc / std::max<size_t> (1, x.size()));
}

static void testRunaway()
{
    std::printf ("Runaway (Glow, hold C)\n");
    Engine e; e.prepare (kSr, 128);
    e.setParam (P::DlOn, 1); e.setParam (P::DlType, 0); e.setParam (P::DlFeedback, 0.3f); e.setParam (P::DlMix, 0.3f);
    auto note = pluck (55.0, 0.8, 0.6, 0.6);
    auto run = [&] (bool held, double secs) { e.setRunaway (held); std::vector<float> q ((size_t) (secs * kSr), 0.0f); return render (e, q); };
    render (e, note);
    auto quiet = run (false, 4.0);
    const double before = rmsOf (quiet, quiet.size() - (size_t) kSr, quiet.size());
    e.setRunaway (true);
    render (e, note);   // play into it, then stop and let it sing
    auto sing = run (true, 5.0);
    if (getenv ("KS_DEBUG")) for (int k = 0; k < 10; ++k) std::printf ("      run %.1f s: %.5f (amount %.2f)\n", k * 0.5, rmsOf (sing, (size_t) (k * 0.5 * kSr), (size_t) ((k + 1) * 0.5 * kSr)), e.runawayAmount());
    const double during = rmsOf (sing, sing.size() - (size_t) kSr, sing.size());
    auto after = run (false, 3.0);
    const double later = rmsOf (after, after.size() - (size_t) kSr, after.size());
    double peak = 0; for (float x : sing) peak = std::max (peak, (double) std::fabs (x));
    CHECK (during > 0.01 && during > before * 20, "held, the repeats sing on their own (%.4f, %.6f without)", during, before);
    CHECK (peak < 1.0, "the loop limiter holds it (peak %.2f)", peak);
    CHECK (later < during * 0.05, "let go, it falls back (%.5f)", later);
    // no sub build-up on bass
    const std::vector<float> lastTwo (sing.end() - (long) (2 * kSr), sing.end());
    const double sub = lowBandRms (lastTwo, 60.0), all = rmsOf (lastTwo, 0, lastTwo.size());
    CHECK (sub < all * 0.35, "a runaway doesn't fill up with sub (%.2f)", sub / all);
}

static void testBrake()
{
    std::printf ("Vinyl brake (Groove, hold C)\n");
    Engine e; e.prepare (kSr, 128);
    auto tone = [] (double secs) { std::vector<float> v ((size_t) (secs * kSr)); for (size_t i = 0; i < v.size(); ++i) v[i] = 0.3f * (float) std::sin (2 * M_PI * 110.0 * i / kSr); return v; };
    render (e, tone (1.0));
    e.setBrake (true);
    auto held = render (e, tone (2.0));
    e.setBrake (false);
    auto back = render (e, tone (2.0));
    // the pitch falls: count zero crossings early and late in the hold
    auto crossings = [] (const std::vector<float>& v, size_t a, size_t b) { int c = 0; for (size_t i = a + 1; i < b; ++i) if ((v[i - 1] < 0) != (v[i] < 0)) ++c; return c; };
    const int early = crossings (held, 0, (size_t) (0.25 * kSr)), late = crossings (held, (size_t) (0.75 * kSr), (size_t) (1.0 * kSr));
    CHECK (late < early * 0.7, "holding, the pitch falls (%d crossings, then %d)", early, late);
    CHECK (rmsOf (held, (size_t) (1.6 * kSr), held.size()) < 0.01, "then it stops");
    const double r = rmsOf (back, (size_t) (1.0 * kSr), back.size()), want = 0.3 / std::sqrt (2.0);
    CHECK (r > want * 0.5, "let go, it spins back up and you're live again (%.3f)", r);
    // live again: no lag left behind
    auto later = render (e, tone (0.5));
    CHECK (crossings (later, 0, later.size()) > 105 && crossings (later, 0, later.size()) < 115, "back at the right pitch");
}

static void testBandSetlist()
{
    std::printf ("The band's setlist\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_band";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    {
        Engine e; e.prepare (kSr, 64);
        Controller c (e, dir.string(), (dir / "caps").string());
        c.load();
        CHECK (c.songCount() == 20 && c.songsOnly() == 10 && c.songName (0) == "The Comedian and the Boxer", "first run: ten songs, twenty songs and parts");
        CHECK (c.bank() == 1 && c.value (P::DriveOn) == 0, "The Comedian and the Boxer: clean, opens in Groove for the filter");
        double t = 0;
        auto hold = [&] (int sw) { c.footswitchPress (sw, t); for (int i = 0; i < 10; ++i) { t += 0.06; c.tick (t); } c.footswitchRelease (sw, t); t += 0.5; };
        auto tap = [&] (int sw) { c.footswitchPress (sw, t); t += 0.1; c.footswitchRelease (sw, t); t += 0.5; };
        hold (1);
        CHECK (c.songName (1) == "When the Circus Comes to Town" && c.value (P::DriveVoice) == 1 && c.value (P::PreGain) > 0.55f, "Circus starts heavy: the Blumes and the preamp pushed");
        hold (1);
        CHECK (c.isPart (2) && c.currentSong() == 2 && c.value (P::DriveOn) == 0 && c.value (P::RvOn) == 1 && c.bank() == 2, "Circus verse: clean with reverb, in Glow");
        hold (1); hold (1); hold (1);
        CHECK (c.songName (5) == "Bridge" && c.value (P::BitrotOn) == 1 && c.value (P::GrOn) == 1, "Circus bridge: Bitrot and Speckle sparkle");
        hold (1);
        CHECK (c.songName (6) == "Ending" && c.value (P::PreGain) > 0.55f && c.value (P::DriveOn) == 1, "Circus ending: Blumes and preamp");
        hold (1);
        CHECK (c.songName (7) == "DWHALTS" && c.value (P::DriveOn) == 0, "DWHALTS opens clean");
        tap (0);
        CHECK (c.value (P::DriveOn) == 1 && c.value (P::DriveVoice) == 1, "DWHALTS, tap A: straight to Drive for the chorus");
        tap (0);
        CHECK (c.value (P::DriveOn) == 0, "tap A again: clean");
        std::filesystem::path f;
        int songNo = 0;
        for (int i = 0; i < c.songCount(); ++i) if (c.songName (i) == "Middle 32") { c.selectSong (i); songNo = c.songNumber (i); }
        CHECK (songNo == 9 && c.value (P::DriveOn) == 0 && c.value (P::RvOn) == 1 && c.value (P::DlOn) == 1 && c.bank() == 2, "Sluts middle 32: ambient, in Glow");
    }
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
}

// ---------------------------------------------------------------------------------------
static void testCapture (const char* path)
{
    std::printf ("NAM capture\n");
    if (! Engine::namAvailable() || path == nullptr || ! std::filesystem::exists (path))
    {
        std::printf ("  skip  no capture to test with\n");
        return;
    }
    Engine e;
    e.prepare (kSr, 64);
    std::string err;
    const bool ok = e.loadCapture (path, err);
    CHECK (ok, "loads %s %s", std::filesystem::path (path).filename().string().c_str(), err.c_str());
    auto in = pluck (55, 2.0, 0.4);
    auto out = render (e, in);
    bool finite = true;
    for (float s : out) finite = finite && std::isfinite (s);
    CHECK (finite && rms (out.data(), out.size()) > 1e-4, "capture plays through the drive stage");

    // Edge on the model, Drive on the capture: tap to Drive and back, no clicks, both heard
    Engine c;
    c.prepare (kSr, 64);
    neutral (c);
    c.setParam (P::DriveOn, 1);
    c.setParam (P::SplitLowLevel, -40);   // listen to the high band only
    c.loadCapture (path, err);
    auto tone = sine (220, 9.0, 0.3);
    const size_t third = tone.size() / 3;
    auto y1 = render (c, std::vector<float> (tone.begin(), tone.begin() + (long) third));
    c.setParam (P::DriveVoice, 1);
    auto y2 = render (c, std::vector<float> (tone.begin() + (long) third, tone.begin() + (long) (2 * third)));
    c.setParam (P::DriveVoice, 0);
    auto y3 = render (c, std::vector<float> (tone.begin() + (long) (2 * third), tone.end()));
    auto worstStep = [] (const std::vector<float>& y, size_t from, size_t to)
    {
        double w = 0;
        for (size_t i = from + 1; i + 1 < to; ++i) w = std::max (w, (double) std::fabs (y[i + 1] - 2 * y[i] + y[i - 1]));
        return w;
    };
    const double steadyEdge = worstStep (y1, y1.size() / 2, y1.size());
    const double steadyDrive = worstStep (y2, y2.size() / 2, y2.size());
    const double sw1 = worstStep (y2, 0, (size_t) (0.3 * kSr)), sw2 = worstStep (y3, 0, (size_t) (0.3 * kSr));
    const double ref = std::max (steadyEdge, steadyDrive);
    CHECK (sw1 < 1.6 * ref && sw2 < 1.6 * ref, "Edge (model) to Drive (capture) and back: switch steps %.2f and %.2f times the steady (mark 1.6)", sw1 / ref, sw2 / ref);
    const double l1 = rms (y1.data() + y1.size() / 2, y1.size() / 2), l2 = rms (y2.data() + y2.size() / 2, y2.size() / 2);
    CHECK (std::fabs (20 * std::log10 (l2 / l1)) < 3.0, "after a few seconds, Drive on the capture sits %.1f dB from Edge (mark within 3)", 20 * std::log10 (l2 / l1));
}

// ---------------------------------------------------------------------------------------
static double benchCase (const char* name, int block, const std::function<void (Engine&)>& setup, const char* capture)
{
    Engine e;
    e.prepare (kSr, block);
    setup (e);
    std::string err;
    if (capture) e.loadCapture (capture, err);
    // realistic input: a bass line of plucked notes
    std::vector<float> in;
    const double notes[] = { 41.2, 55.0, 61.7, 73.4, 49.0, 82.4 };
    for (int k = 0; k < 12; ++k) { auto n = pluck (notes[k % 6], 0.5, 0.4, 0.6); in.insert (in.end(), n.begin(), n.end()); }
    std::vector<float> out (in.size());
    // warm up, then time 6 s of audio
    for (size_t i = 0; i + block <= in.size() / 3; i += (size_t) block) e.process (in.data() + i, out.data() + i, block);
    const auto t0 = std::chrono::steady_clock::now();
    size_t samples = 0;
    for (size_t i = 0; i + block <= in.size(); i += (size_t) block) { e.process (in.data() + i, out.data() + i, block); samples += (size_t) block; }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double pct = sec / (samples / kSr) * 100.0;
    std::printf ("  %-44s block %3d: %5.1f%% of one core\n", name, block, pct);
    return pct;
}

static void bench (const char* capture)
{
    std::printf ("CPU at 48 kHz\n");
    auto defaults = [] (Engine&) {};
    auto everything = [] (Engine& e)
    {
        for (auto p : { P::DriveOn, P::BitrotOn, P::PreOn, P::FiltOn, P::DimOn, P::GrOn, P::DlOn, P::RvOn, P::DroneOn }) e.setParam (p, 1);
        e.setParam (P::DriveSource, 1);
        e.setBloom (0.8f);
    };
    auto everythingLight = [&] (Engine& e) { everything (e); e.setParam (P::OutQuality, 2); };
    report += "\n| Case | 64 samples | 128 samples |\n|---|---|---|\n";
    struct C { const char* name; std::function<void (Engine&)> f; const char* cap; };
    std::vector<C> cases = { { "Song default, modelled drive", [] (Engine& e) { e.setParam (P::DriveSource, 1); }, nullptr },
                             { "Everything on, Bloom at 80%", everything, nullptr },
                             { "Everything on, light effects", everythingLight, nullptr },
                             { "Everything on, tape delay and Haze grains", [&] (Engine& e) { everything (e); e.setParam (P::DlType, 2); e.setParam (P::DlTapeMode, 2); e.setParam (P::GrEffect, 4); }, nullptr },
                             { "Everything on, Cloud reverb", [&] (Engine& e) { everything (e); e.setParam (P::RvType, 5); }, nullptr },
                             { "Everything on, Impulse reverb (2 s)", [&] (Engine& e) { everything (e); e.setParam (P::RvType, 11); std::string err; e.loadSpace (writeTestWav (24, false, 1, 48000), err); }, nullptr },
                             { "Everything on, Galaxy delay and Arp grains", [&] (Engine& e) { everything (e); e.setParam (P::DlType, 11); e.setParam (P::GrEffect, 9); e.setParam (P::GrPitch, 3); }, nullptr },
                             { "Groove: wah, auto filter and Dimension", [] (Engine& e) { e.setParam (P::DriveSource, 1); e.setParam (P::WahOn, 1); e.setParam (P::FiltOn, 1); e.setParam (P::DimOn, 1); e.setPedalBank (1); e.setPedal (0.6f); }, nullptr },
                             { "Kuddle, Voodoo (Keystone asleep)", [] (Engine& e) { e.setRig (1); }, nullptr },
                             { "Kuddle with the room on", [] (Engine& e) { e.setRig (1); e.setParam (P::KRoomOn, 1); }, nullptr } };
    if (capture && Engine::namAvailable() && std::filesystem::exists (capture))
    {
        cases.insert (cases.begin(), { "Song default (Edge on the model), capture loaded", defaults, capture });
        cases.insert (cases.begin() + 1, { "Drive voice on the NAM capture (A1 standard)", [] (Engine& e) { e.setParam (P::DriveVoice, 1); }, capture });
        cases.push_back ({ "Everything on with the NAM capture", [&] (Engine& e) { everything (e); e.setParam (P::DriveSource, 0); }, capture });
    }
    for (auto& c : cases)
    {
        const double a = benchCase (c.name, 64, c.f, c.cap), b = benchCase (c.name, 128, c.f, c.cap);
        char line[200];
        std::snprintf (line, sizeof (line), "| %s | %.1f%% | %.1f%% |\n", c.name, a, b);
        report += line;
    }
}

// Writes the phone app's built-in defaults (parameter list and a fresh state), so its
// menus work before it has found Keystone. CI checks the committed copy is current.
static int writeDefaults (const char* path)
{
    const auto dir = std::filesystem::temp_directory_path() / "keystone_defaults";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e;
    e.prepare (kSr, 64);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    std::string state = c.stateJson();
    std::ofstream (path) << "// Generated by keystone_tests --defaults. Do not edit.\nwindow.KS_DEFAULT = { schema: " << c.schemaJson()
                         << ", state: " << state << " };\n";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    return 0;
}


// ---------------------------------------------------------------------------------------
// Soak: the audio runs on its own thread, flat out, while another thread does what a gig
// does to it: stomps, holds, both modes, banks, song and part changes, rig swaps, the phone
// moving controls and editing the setlist, the pedal rocking. The bass comes and goes,
// with silence, loud digs and the odd spike. It fails on any non-finite sample, output
// past the limiter's ceiling, a stuck output, or memory that keeps growing.
static long rssKb()
{
    std::ifstream f ("/proc/self/status");
    std::string line;
    while (std::getline (f, line))
        if (line.rfind ("VmRSS:", 0) == 0) return std::atol (line.c_str() + 6);
    return 0;
}

static int soak (double minutes)
{
    std::printf ("Soak: %.0f minutes of audio with a gig's worth of footwork\n", minutes);
    const auto dir = std::filesystem::temp_directory_path() / "keystone_soak";
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    Engine e; e.prepare (kSr, 128);
    Controller c (e, dir.string(), (dir / "caps").string());
    c.load();
    c.setValue (P::RecOn, 0);   // an hour of takes would only fill the build machine's disk
    const int64_t total = (int64_t) (minutes * 60 * kSr);
    std::atomic<int64_t> done { 0 };
    std::atomic<bool> stop { false };
    std::atomic<int> bad { 0 };
    std::atomic<float> worst { 0 };
    std::atomic<double> slowestMs { 0 };
    std::atomic<int> lastEvent { -1 }, overruns { 0 };
    std::array<std::atomic<int>, 16> slowAfter {};     // blocks over half their time, by the event before them
    std::string firstBad;
    std::mutex badLock;
    auto fail = [&] (const std::string& why) { std::lock_guard<std::mutex> l (badLock); if (bad.fetch_add (1) == 0) firstBad = why; };

    std::thread audio ([&]
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        std::vector<float> in (128), out (128);
        double ph = 0, freq = 55, amp = 0;
        int64_t quietRun = 0, still = 0;
        float last = 0;
        while (done.load() < total && ! stop.load())
        {
            // a new note, a rest, or something rude, now and then
            if (u (rng) < 0.01f)
            {
                const float r = u (rng);
                if (r < 0.6f) { freq = 41.2 * std::pow (2.0, std::floor (u (rng) * 24) / 12.0); amp = 0.1 + 0.6 * u (rng); }
                else if (r < 0.9f) amp = 0;
                else amp = 1.4;   // past full scale: a hot input or a cable knock
            }
            for (int i = 0; i < 128; ++i)
            {
                ph += freq / kSr;
                amp *= 0.99995;
                in[(size_t) i] = (float) (amp * (std::sin (2 * M_PI * ph) + 0.3 * std::sin (4 * M_PI * ph)));
            }
            if (u (rng) < 0.0005f) in[(size_t) (rng() % 128)] = 4.0f;   // a spike
            const auto t0 = std::chrono::steady_clock::now();
            e.process (in.data(), out.data(), 128);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            if (ms > slowestMs.load()) slowestMs.store (ms);
            if (ms > 128 / kSr * 1000) overruns.fetch_add (1);
            if (ms > 0.5 * 128 / kSr * 1000) { const int le = lastEvent.load(); if (le >= 0) slowAfter[(size_t) le].fetch_add (1); }
            float pk = 0;
            for (float y : out)
            {
                if (! std::isfinite (y)) { fail ("a non-finite sample"); y = 0; }
                pk = std::max (pk, std::fabs (y));
            }
            if (pk > worst.load()) worst.store (pk);
            if (pk > 1.01f) fail ("output past full scale: " + std::to_string (pk));
            // stuck: the same non-zero value for ten seconds
            if (pk > 1.0e-3f && std::fabs (out[127] - last) < 1.0e-9f && std::fabs (out[0] - last) < 1.0e-9f) still += 128; else still = 0;
            if (still > (int64_t) (10 * kSr)) fail ("output stuck at " + std::to_string (last));
            last = out[127];
            quietRun = pk < 1.0e-6f ? quietRun + 128 : 0;
            done.fetch_add (128);
        }
    });

    // the gig, on the control thread
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    double t = 0;
    const long rssStart = rssKb();
    long rssAtHalf = 0;
    int events = 0;
    auto press = [&] (int sw) { c.footswitchPress (sw, t); };
    auto release = [&] (int sw) { c.footswitchRelease (sw, t); };
    auto msg = [&] (const std::string& m) { c.handleMessage (m, [] (const std::string&) {}); };
    while (done.load() < total && bad.load() == 0)
    {
        const double audioT = (double) done.load() / kSr;
        if (t > audioT) { std::this_thread::sleep_for (std::chrono::microseconds (200)); c.tick (t); continue; }
        t += 0.05 + 2.0 * u (rng);
        ++events;
        const float r = u (rng);
        const int sw = (int) (u (rng) * 4) % 4;
        static const float edges[] = { 0.35f, 0.50f, 0.55f, 0.75f, 0.85f, 0.90f, 0.93f, 0.96f, 0.97f, 0.98f, 0.99f, 1.01f };
        int kind = 0; while (r >= edges[kind]) ++kind;
        lastEvent.store (kind);
        if (r < 0.35f) { press (sw); t += 0.05 + 0.1 * u (rng); c.tick (t); release (sw); }        // tap
        else if (r < 0.50f) { press (sw); for (int i = 0; i < 12; ++i) { t += 0.05; c.tick (t); } release (sw); }   // hold
        else if (r < 0.55f) { const int a = u (rng) < 0.5f ? 0 : 2; press (a); press (a + 1); for (int i = 0; i < (int) (u (rng) * 25); ++i) { t += 0.05; c.tick (t); } release (a); release (a + 1); }
        else if (r < 0.75f) e.setPedal (u (rng));
        else if (r < 0.85f)
        {
            // the phone moves a random control somewhere in its range (not the globals:
            // they'd write settings files flat out)
            const int p = (int) (u (rng) * kNumParams) % kNumParams;
            const auto& in = paramTable()[p];
            if (! (in.flags & Global)) c.setValue ((P) p, in.min + (in.max - in.min) * u (rng));
        }
        else if (r < 0.90f) msg (R"({"t":"songStep","d":)" + std::string (u (rng) < 0.5f ? "1" : "-1") + "}");
        else if (r < 0.93f) msg (R"({"t":"rig","r":)" + std::to_string ((int) (u (rng) * 2)) + "}");
        else if (r < 0.96f) msg (R"({"t":"bank","i":)" + std::to_string ((int) (u (rng) * 3) % 3) + "}");
        else if (r < 0.97f) msg (R"({"t":"setlist","op":"revert"})");
        else if (r < 0.98f) msg (R"({"t":"tap"})");
        else if (r < 0.99f) msg (R"({"t":"footMode"})");
        else msg (R"({"t":"setlist","op":"save"})");
        c.takeBroadcasts();   // the app sends these to the phones every few milliseconds
        if (! rssAtHalf && done.load() > total / 2) rssAtHalf = rssKb();
    }
    stop.store (true);
    audio.join();
    const long rssEnd = rssKb();
    std::printf ("  %d events, loudest sample %.3f, slowest block %.2f ms (a block lasts %.2f ms)\n", events, worst.load(), slowestMs.load(), 128 / kSr * 1000);
    static const char* kinds[] = { "tap", "hold", "pair", "pedal", "phone control", "song step", "rig", "bank", "revert", "tap tempo", "mode", "save" };
    std::printf ("  blocks over their time: %d. Slow blocks (over half their time) after: ", overruns.load());
    for (int k = 0; k < 12; ++k) if (slowAfter[(size_t) k].load()) std::printf ("%s %d, ", kinds[k], slowAfter[(size_t) k].load());
    std::printf ("\n");
    std::printf ("  memory %ld KB at the start, %ld KB halfway, %ld KB at the end\n", rssStart, rssAtHalf, rssEnd);
    if (rssAtHalf > 0 && rssEnd - rssAtHalf > 8192) fail ("memory grew by " + std::to_string ((rssEnd - rssAtHalf) / 1024) + " MB in the second half");
    { std::error_code wipeEc; std::filesystem::remove_all (dir, wipeEc); }
    if (bad.load()) { std::printf ("  FAIL  %s (%d problems)\n", firstBad.c_str(), bad.load()); return 1; }
    std::printf ("  Soak passed\n");
    return 0;
}

// --render in.wav out.wav "id=value,id=value": a recording's first channel through a fresh
// engine with those settings, plus 5 s of tails. For before-and-after listening.
static int renderFile (const char* inPath, const char* outPath, const std::string& settings)
{
    std::ifstream f (inPath, std::ios::binary);
    std::vector<char> d ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (d.size() < 44) { std::printf ("can't read %s\n", inPath); return 1; }
    auto u16 = [&] (size_t o) { return (uint32_t) (uint8_t) d[o] | ((uint32_t) (uint8_t) d[o + 1] << 8); };
    auto u32 = [&] (size_t o) { return u16 (o) | (u16 (o + 2) << 16); };
    // find fmt and data chunks
    size_t pos = 12, dataAt = 0, dataLen = 0; int ch = 1, bits = 16; double rate = 48000;
    while (pos + 8 <= d.size())
    {
        const std::string id (d.data() + pos, 4); const uint32_t len = u32 (pos + 4);
        if (id == "fmt ") { ch = (int) u16 (pos + 10); rate = u32 (pos + 12); bits = (int) u16 (pos + 22); }
        if (id == "data") { dataAt = pos + 8; dataLen = std::min<size_t> (len, d.size() - pos - 8); break; }
        pos += 8 + len + (len & 1);
    }
    const int bps = bits / 8;
    const size_t frames = dataLen / (size_t) (bps * ch);
    std::vector<float> in (frames + (size_t) (5 * rate), 0.0f);
    for (size_t i = 0; i < frames; ++i)
    {
        const char* b = d.data() + dataAt + i * (size_t) (bps * ch);
        int32_t v = 0;
        if (bps == 3) v = (int32_t) (((uint32_t) (uint8_t) b[0] << 8) | ((uint32_t) (uint8_t) b[1] << 16) | ((uint32_t) (uint8_t) b[2] << 24)) >> 8, in[i] = v / 8388608.0f;
        else in[i] = (int16_t) ((uint8_t) b[0] | ((uint8_t) b[1] << 8)) / 32768.0f;
    }
    Engine e;
    e.prepare (rate, 128);
    // everything at its default, then the settings asked for
    std::stringstream ss (settings);
    std::string kv;
    while (std::getline (ss, kv, ','))
    {
        const auto eq = kv.find ('=');
        if (eq == std::string::npos) continue;
        const int i = findParam (kv.substr (0, eq).c_str());
        if (i < 0) { std::printf ("no such setting %s\n", kv.substr (0, eq).c_str()); return 1; }
        e.setParam ((P) i, (float) std::atof (kv.substr (eq + 1).c_str()));
    }
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); i += 128)
    {
        const int n = (int) std::min<size_t> (128, in.size() - i);
        e.process (in.data() + i, out.data() + i, n);
        e.idle();
    }
    std::ofstream o (outPath, std::ios::binary);
    const uint32_t r = (uint32_t) rate, bytes = (uint32_t) out.size() * 3;
    auto w32 = [&] (uint32_t v) { o.put ((char) v); o.put ((char) (v >> 8)); o.put ((char) (v >> 16)); o.put ((char) (v >> 24)); };
    auto w16 = [&] (uint16_t v) { o.put ((char) v); o.put ((char) (v >> 8)); };
    o.write ("RIFF", 4); w32 (36 + bytes); o.write ("WAVEfmt ", 8); w32 (16); w16 (1); w16 (1); w32 (r); w32 (r * 3); w16 (3); w16 (24); o.write ("data", 4); w32 (bytes);
    for (float v : out) { const int32_t q = (int32_t) std::lrint (std::clamp (v, -1.0f, 1.0f) * 8388607.0f); o.put ((char) q); o.put ((char) (q >> 8)); o.put ((char) (q >> 16)); }
    return 0;
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // unbuffered (Windows has no line buffering), so a crash shows where it was
    if (argc > 2 && std::string (argv[1]) == "--soak") return soak (std::atof (argv[2]));
    if (argc > 2 && std::string (argv[1]) == "--defaults") return writeDefaults (argv[2]);
    if (argc > 3 && std::string (argv[1]) == "--render") return renderFile (argv[2], argv[3], argc > 4 ? argv[4] : "");
    const char* capture = argc > 1 ? argv[1] : nullptr;
    const bool benchOnly = argc > 2 && std::string (argv[2]) == "--bench";
    std::printf ("Keystone %s engine tests\n", Controller::kVersion);
    if (! benchOnly)
    {
        testCrossover();
        testDrone();
        testDroneApartFromBloom();
        testFilterDynamics();
        testGrains();
        testDelayTypes();
        testTapeMachines();
        testDelayLimits();
        testTempo();
        testBitrotSynth();
        testBitrotLevel();
    testOffMeansOff();
    testValvePreamp();
    testGrainEffects();
        testReverbTypes();
        testReverbDecay();
        testReverbEdges();
        testBloomSafety();
        testClicks();
        testSteadyDrive();
        testTuner();
        testPedalJobs();
        testPartTails();
        testController();
        testBandSetlist();
        testKuddle();
        testNewDefaults();
        testRecorder();
        testFirmware();
        testBackup();
        testMyDefaults();
        testRunaway();
        testBrake();
        testCapture (capture);
    }
    bench (capture);
    std::printf ("\n%d passed, %d failed\n", passes, failures);
    std::ofstream ("bench.md") << "## Engine measurements\n\n" << report << "\nCPU is the share of one core for one Keystone at 48 kHz, measured on the build machine.\n";
    return failures == 0 ? 0 : 1;
}
