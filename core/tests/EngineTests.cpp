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

#include <chrono>
#include <complex>
#include <functional>
#include <thread>
#include <cmath>
#include <cstdio>
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
    CHECK (worst < 0.3, "low and high bands sum flat, worst %.2f dB (mark 0.3)", worst);
    CHECK (e.latencySamples() == 15, "engine latency %d samples (%.2f ms at 48 kHz)", e.latencySamples(), e.latencySamples() * 1000.0 / kSr);
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
            d.update (v, 0, false, 0.25f);
            if (! asked && i > (size_t) (0.6 * kSr)) { d.requestCapture(); asked = true; }
            d.block (B);
            d.analyse (0.25f);
            for (int k = 0; k < B && i + k < note.size(); ++k) y.push_back (d.process (note[i + k]));
        }
        for (int s = 0; s < (int) (3.5 * kSr) / B; ++s)
        {
            d.update (v, 0, false, 0.25f);
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
        d.update (v, 0, false, 0.25f);
        if (s == 100) d.requestCapture();
        d.block (64);
        d.analyse (0.25f);
        for (int k = 0; k < 64; ++k) d.process (0.0f);
    }
    for (int s = 0; s < 4; ++s) { d.update (v, 0, false, 0.25f); d.block (64); }
    CHECK (d.layerCount() == 0, "silence isn't captured as a drone");
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
        e.setParam (P::GrHighPass, 300);
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
static void testBloomSafety()
{
    std::printf ("Bloom at toe: feedback limiter\n");
    Engine e;
    e.prepare (kSr, 64);
    e.setParam (P::BloomWash, 1.0f);
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
    CHECK (after < -60, "heel back with Cut: %.1f dBFS half a second later (mark -60)", after);
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
                           Case { "Drive on", P::DriveOn, 0, 1 } })
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
static void testController()
{
    std::printf ("Controller: footswitches and setlist\n");
    const auto dir = std::filesystem::temp_directory_path() / "keystone_test";
    std::filesystem::remove_all (dir);
    Engine e;
    e.prepare (kSr, 64);
    Controller c (e, dir.string(), (dir / "caps").string());
    int keeps = 0;
    c.onKeep = [&] (const std::string& j) { keeps += j.find ("\"plugin\":\"Keystone\"") != std::string::npos; };
    c.load();
    CHECK (c.songCount() == 4 && c.songName (0) == "New choon", "first run seeds the four songs");
    double t = 0;
    auto tap = [&] (int sw) { c.footswitchPress (sw, t); t += 0.1; c.footswitchRelease (sw, t); t += 0.5; };
    auto hold = [&] (int sw) { c.footswitchPress (sw, t); for (int i = 0; i < 10; ++i) { t += 0.06; c.tick (t); } c.footswitchRelease (sw, t); t += 0.5; };
    tap (0);
    CHECK (c.value (P::DriveVoice) == 1, "tap A: Edge to Drive");
    tap (0);
    CHECK (c.value (P::DriveVoice) == 0, "tap A again: back to Edge");
    tap (1);
    CHECK (c.value (P::BitrotOn) == 1, "tap B: Bitrot on");
    tap (2);
    CHECK (c.value (P::FiltOn) == 1, "tap C: filter on");
    CHECK (c.edited(), "stomps mark the song edited");
    hold (1);
    CHECK (c.currentSong() == 1 && c.value (P::BitrotOn) == 0, "hold B: next song, back to its saved state");
    hold (0);
    CHECK (c.currentSong() == 0, "hold A: previous song");
    hold (3);
    CHECK (c.value (P::CompSlap) == 1, "hold D: slap on");
    hold (2);
    for (int i = 0; i < 4; ++i) { e.process (std::vector<float> (64).data(), std::vector<float> (64).data(), 64); }
    CHECK (c.tunerOn(), "hold C: tuner");
    tap (1);
    CHECK (! c.tunerOn() && c.value (P::BitrotOn) == 0, "any tap leaves the tuner without doing its job");
    // C and D together, held a second: Keep
    c.footswitchPress (2, t); c.footswitchPress (3, t + 0.03);
    for (int i = 0; i < 25; ++i) { t += 0.05; c.tick (t); }
    c.footswitchRelease (2, t); c.footswitchRelease (3, t);
    CHECK (keeps == 1 && ! c.tunerOn() && c.value (P::CompSlap) == 1, "hold C and D: Keep, nothing else fires");
    // A then B quickly is two presses, not a combo
    t += 1;
    c.footswitchPress (0, t); c.footswitchPress (1, t + 0.02); c.footswitchRelease (0, t + 0.1); c.footswitchRelease (1, t + 0.12);
    CHECK (c.value (P::DriveVoice) == 1 && c.value (P::BitrotOn) == 1, "A and B together stay two taps");

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
    // reload from disk
    Engine e2;
    e2.prepare (kSr, 64);
    Controller c2 (e2, dir.string(), (dir / "caps").string());
    c2.load();
    CHECK (c2.songCount() == 14 && c2.songName (0) == "Encore" && std::fabs (c2.value (P::CompSlap) - 0) < 1e-6 && c2.songName (1) == "New choon",
           "setlist survives a restart");
    std::filesystem::remove_all (dir);
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
                             { "Everything on, light effects", everythingLight, nullptr } };
    if (capture && Engine::namAvailable() && std::filesystem::exists (capture))
    {
        cases.insert (cases.begin(), { "Song default, NAM capture (A1 standard)", defaults, capture });
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

int main (int argc, char** argv)
{
    const char* capture = argc > 1 ? argv[1] : nullptr;
    const bool benchOnly = argc > 2 && std::string (argv[2]) == "--bench";
    std::printf ("Keystone %s engine tests\n", Controller::kVersion);
    if (! benchOnly)
    {
        testCrossover();
        testDrone();
        testFilterDynamics();
        testGrains();
        testBloomSafety();
        testClicks();
        testTuner();
        testController();
        testCapture (capture);
    }
    bench (capture);
    std::printf ("\n%d passed, %d failed\n", passes, failures);
    std::ofstream ("bench.md") << "## Engine measurements\n\n" << report << "\nCPU is the share of one core for one Keystone at 48 kHz, measured on the build machine.\n";
    return failures == 0 ? 0 : 1;
}
