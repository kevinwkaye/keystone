// Sound quality bench (0.5.5): the measurements boutique pedal makers tune by ear, turned
// into numbers so they can't quietly slip. Included by EngineTests.cpp, which owns the
// helpers (sine, pluck, rms, render, neutral, clickScore, CHECK) and writes bench.md.
//
//  1. Noise floor: what comes out with nothing played.
//  2. Aliasing: energy that isn't a harmonic of the note, through every stage that distorts.
//  3. Level jumps: how far the level moves when each effect comes on.
//  4. Clicks: every switch the feet and phone can make, against steady playing.
//  5. Tails and the CPU: blocks while delay and reverb die away cost no more than playing
//     (denormals show up here as a slow tail).
// Marks sit a little past what the engine measured when the bench went in, so a change that
// makes any of these worse fails CI.

#pragma once

namespace soundbench
{
// Everything at its song default except the drive on the model (CI has the capture, a
// laptop build may not, and the model is what every build can measure the same way).
static void songDefault (Engine& e) { e.setParam (P::DriveSource, 1); }

static void everythingOn (Engine& e)
{
    songDefault (e);
    for (auto p : { P::DriveOn, P::PreOn, P::DimOn, P::GrOn, P::DlOn, P::RvOn }) e.setParam (p, 1);
    e.setParam (P::DroneOn, 0);   // the drone holds a note on its own; it would mask the tails
}

// ---------------------------------------------------------------------------------------
// Power spectrum of the last n samples (n a power of two), Blackman-Harris windowed.
static std::vector<double> powerSpectrum (const std::vector<float>& y, int n)
{
    std::vector<std::complex<double>> a ((size_t) n);
    const size_t start = y.size() - (size_t) n;
    for (int i = 0; i < n; ++i)
    {
        const double t = 2 * M_PI * i / n;
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[(size_t) i] = y[start + (size_t) i] * w;
    }
    fft (a);
    std::vector<double> p ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k) p[(size_t) k] = std::norm (a[(size_t) k]);
    return p;
}

// dB of everything that isn't a harmonic of f0 below the total. Below half the note is left
// out: valves and coupling filters settle slowly after the tone starts, and that sub-bass
// drift isn't aliasing (every test tone here is 110 Hz or higher).
// Blackman-Harris spreads each line over about 4 bins, so 6 either side is the line.
static double nonHarmonicDb (const std::vector<float>& y, double f0)
{
    const int n = 65536;
    auto p = powerSpectrum (y, n);
    const double binHz = kSr / n;
    double total = 0, harm = 0;
    const int lowest = (int) (std::max (20.0, 0.5 * f0) / binHz);
    for (int k = lowest; k < n / 2; ++k)
    {
        total += p[(size_t) k];
        const double h = k * binHz / f0;
        const double near = std::fabs (h - std::round (h)) * f0 / binHz;
        if (std::round (h) >= 1 && near <= 6) harm += p[(size_t) k];
    }
    const double rest = std::max (total - harm, 1e-30);
    return 10 * std::log10 (rest / std::max (total, 1e-30));
}

// A tone on an exact FFT bin, so its harmonics land on bins too.
static double binTone (double want) { const double binHz = kSr / 65536; return std::round (want / binHz) * binHz; }

static std::vector<float> steadySine (double f, double seconds, double amp)
{
    std::vector<float> x ((size_t) (seconds * kSr));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = i / kSr;
        x[i] = (float) (amp * std::min (1.0, t / 0.05) * std::sin (2 * M_PI * f * t));
    }
    return x;
}

// ---------------------------------------------------------------------------------------
static void noiseFloor()
{
    std::printf ("Sound bench: noise floor\n");
    report += "\n### Sound bench\n\n| Measure | Result | Mark |\n|---|---|---|\n";
    struct C { const char* name; std::function<void (Engine&)> f; double mark; };
    // Keystone adds no noise of its own (0.5.5 measures digital silence), so anything that
    // does would show at once
    for (const auto& c : { C { "Noise floor, song default", songDefault, -120.0 },
                           C { "Noise floor, everything on", everythingOn, -120.0 } })
    {
        Engine e;
        e.prepare (kSr, 128);
        c.f (e);
        // a note first, so anything that wakes on playing is awake, then 20 s of silence:
        // the last 4 s is what's left once every tail has gone
        auto in = pluck (55.0, 1.0, 0.5, 0.3);
        in.resize ((size_t) (21 * kSr), 0.0f);
        auto out = render (e, in, 128);
        const size_t n = (size_t) (4 * kSr);
        const double db = 20 * std::log10 (rms (out.data() + out.size() - n, n) + 1e-12);
        CHECK (db < c.mark, "%s: %.1f dBFS (mark under %.0f)", c.name, db, c.mark);
        char line[200];
        std::snprintf (line, sizeof (line), "| %s | %.1f dBFS | under %.0f |\n", c.name, db, c.mark);
        report += line;
    }
}

// ---------------------------------------------------------------------------------------
static void aliasing()
{
    std::printf ("Sound bench: aliasing (energy that isn't a harmonic of the note)\n");
    struct C { const char* name; std::function<void (Engine&)> f; double hz, amp, mark; };
    auto only = [] (std::initializer_list<P> on)
    {
        return [on] (Engine& e)
        {
            neutral (e);
            e.setParam (P::DriveSource, 1);
            e.setParam (P::OutIron, 0);
            e.setParam (P::OutLimit, 0);
            e.setParam (P::PreWow, 0);   // the preamp's wow is wanted movement, not aliasing
            for (auto p : on) e.setParam (p, 1);
        };
    };
    const std::vector<C> cases = {
        // marks: about 3 dB past what 0.5.5 measured. What's left in the valve isn't aliasing:
        // its sag moves each harmonic a little, which spreads it just past the bins counted
        // as the harmonic. The Cut voice runs at 2x and the limiter's knee sits above where
        // it settles (both 0.5.5)
        { "Clean path (reference)", only ({}), 2100, 0.3, -110 },
        { "Drive, model, 2.1 kHz hard", only ({ P::DriveOn }), 2100, 0.7, -80 },
        { "Drive, model, 330 Hz hard", only ({ P::DriveOn }), 330, 0.7, -78 },
        { "Valve preamp, 2.1 kHz hard", only ({ P::PreOn }), 2100, 0.7, -74 },
        { "Valve preamp, 330 Hz hard", only ({ P::PreOn }), 330, 0.7, -70 },
        { "Drive into preamp, 1.1 kHz", only ({ P::DriveOn, P::PreOn }), 1100, 0.7, -69 },
        { "Preamp flipped to Cut, 2.1 kHz", [only] (Engine& e) { only ({ P::PreOn }) (e); e.setParam (P::PreBlend, 1); }, 2100, 0.7, -70 },
        { "Output iron at full, 110 Hz", [] (Engine& e) { neutral (e); e.setParam (P::OutLimit, 0); e.setParam (P::OutIron, 1); }, 110, 0.8, -80 },
        { "Limiter working hard, 2.1 kHz", [] (Engine& e) { neutral (e); e.setParam (P::OutIron, 0); e.setParam (P::OutLevel, 12); }, 2100, 0.6, -74 },
        // Gloss (0.6.7): the pad's detuned oscillators are BLEP-smoothed. Spread and breathe at
        // zero and unison, so every line it plays is a harmonic of the note
        { "Gloss pad saw, 220 Hz, bright", [only] (Engine& e) { only ({ P::GlossOn }) (e); e.setParam (P::GlossClean, 0); e.setParam (P::GlossWeight, 0); e.setParam (P::GlossSpread, 0); e.setParam (P::GlossBreathe, 0); e.setParam (P::GlossPlays, 2); e.setParam (P::GlossBright, 8000); }, 220, 0.5, -64 },
        { "Gloss pad square, 330 Hz, bright", [only] (Engine& e) { only ({ P::GlossOn }) (e); e.setParam (P::GlossVoice, 1); e.setParam (P::GlossClean, 0); e.setParam (P::GlossWeight, 0); e.setParam (P::GlossSpread, 0); e.setParam (P::GlossBreathe, 0); e.setParam (P::GlossPlays, 2); e.setParam (P::GlossBright, 8000); }, 330, 0.5, -64 },
    };
    for (const auto& c : cases)
    {
        Engine e;
        e.prepare (kSr, 128);
        c.f (e);
        const double f = binTone (c.hz);
        auto out = render (e, steadySine (f, 3.0, c.amp), 128);
        const double db = nonHarmonicDb (out, f);
        CHECK (db < c.mark, "%s: %.1f dB (mark under %.0f)", c.name, db, c.mark);
        char line[200];
        std::snprintf (line, sizeof (line), "| Aliasing, %s | %.1f dB | under %.0f |\n", c.name, db, c.mark);
        report += line;
    }
}

// ---------------------------------------------------------------------------------------
static void levelJumps()
{
    std::printf ("Sound bench: level when each effect comes on\n");
    // a bass line at a normal playing level, the level measured once things have settled
    std::vector<float> line;
    const double notes[] = { 41.2, 55.0, 61.7, 73.4, 49.0, 82.4 };
    for (int k = 0; k < 12; ++k) { auto n = pluck (notes[k % 6], 0.5, 0.35, 0.6); line.insert (line.end(), n.begin(), n.end()); }
    auto level = [&] (const std::function<void (Engine&)>& f)
    {
        Engine e;
        e.prepare (kSr, 128);
        songDefault (e);
        f (e);
        auto out = render (e, line, 128);
        const size_t from = out.size() / 3;
        return 20 * std::log10 (rms (out.data() + from, out.size() - from) + 1e-12);
    };
    struct C { const char* name; P p; float on; double mark; };
    // Bitrot's +3 dB step is by design (0.5.1). Dimension, the envelope filter and the wah
    // match their power to what you played (0.5.5; they measured -2.1, -7.7 and -3.9 dB)
    for (const auto& c : { C { "Drive", P::DriveOn, 1, 1.5 }, C { "Valve preamp", P::PreOn, 1, 2.0 },
                           C { "Dimension", P::DimOn, 1, 1.0 }, C { "Low compressor", P::CompOn, 1, 2.0 },
                           C { "Envelope filter", P::FiltOn, 1, 2.0 }, C { "Wah", P::WahOn, 1, 2.5 },
                           C { "Grains", P::GrOn, 1, 1.0 }, C { "Delay", P::DlOn, 1, 1.0 }, C { "Reverb", P::RvOn, 1, 1.0 } })
    {
        const double off = level ([&] (Engine& e) { e.setParam (c.p, 0); });
        const double on = level ([&] (Engine& e) { e.setParam (c.p, c.on); });
        const double jump = on - off;
        CHECK (std::fabs (jump) < c.mark, "%s on: %+.1f dB (mark within %.1f)", c.name, jump, c.mark);
        char row[200];
        std::snprintf (row, sizeof (row), "| Level jump, %s on | %+.1f dB | within %.1f |\n", c.name, jump, c.mark);
        report += row;
    }
}

// ---------------------------------------------------------------------------------------
// Switches the older click test doesn't cover: the ambient section, the output, and
// jumps a part change makes (delay time and reverb type).
static void moreClicks()
{
    std::printf ("Sound bench: clicks on every switch\n");
    struct C { const char* name; P p; float a, b; };
    double worst = 0;
    const char* worstName = "";
    for (const auto& c : { C { "Preamp on", P::PreOn, 0, 1 }, C { "Dimension on", P::DimOn, 0, 1 },
                           C { "Compressor on", P::CompOn, 0, 1 }, C { "Grains on", P::GrOn, 0, 1 },
                           C { "Delay on", P::DlOn, 0, 1 }, C { "Reverb on", P::RvOn, 0, 1 },
                           C { "Delay off", P::DlOn, 1, 0 }, C { "Reverb off", P::RvOn, 1, 0 },
                           C { "Delay time 420 to 180 ms", P::DlTime, 420, 180 },
                           C { "Delay type Digital to Tape", P::DlType, 0, 2 },
                           C { "Reverb type Hall to Plate", P::RvType, 1, 2 },
                           C { "Crossover 100 to 160 Hz", P::SplitFreq, 100, 160 },
                           C { "Into Amp to PA", P::OutVoicing, 0, 1 },
                           C { "Output level 0 to +6 dB", P::OutLevel, 0, 6 } })
    {
        Engine e;
        e.prepare (kSr, 64);
        songDefault (e);
        if (c.p == P::DlTime || c.p == P::DlType) e.setParam (P::DlOn, 1);
        if (c.p == P::RvType) e.setParam (P::RvOn, 1);
        e.setParam (c.p, c.a);
        const double f = 220, amp = 0.05;
        auto in = sine (f, 1.5, amp);
        std::vector<float> out (in.size());
        const size_t s0 = (size_t) (0.75 * kSr);
        for (size_t i = 0; i < in.size(); i += 64)
        {
            if (i == s0) e.setParam (c.p, c.b);
            e.process (in.data() + i, out.data() + i, 64);
        }
        const double steady = std::max (clickScore (out, s0 - 4800, s0, f, amp), clickScore (out, s0 + 14400, s0 + 19200, f, amp));
        const double sw = clickScore (out, s0, s0 + 4800, f, amp);
        const double ratio = sw / std::max (steady, 1e-9);
        if (ratio > worst) { worst = ratio; worstName = c.name; }
        CHECK (sw < std::max (3.0, steady * 1.6), "%s: switch %.2f against steady %.2f (mark 1.6x or 3)", c.name, sw, steady);
    }
    char row[200];
    std::snprintf (row, sizeof (row), "| Clicks, worst switch (%s) | %.2fx steady | 1.6x or 3 |\n", worstName, worst);
    report += row;
}

// ---------------------------------------------------------------------------------------
static void tailCost()
{
    std::printf ("Sound bench: CPU while tails die away\n");
    Engine e;
    e.prepare (kSr, 128);
    everythingOn (e);
    e.setParam (P::RvDecay, 6);
    e.setParam (P::DlFeedback, 0.6f);
    std::vector<float> in;
    for (int k = 0; k < 4; ++k) { auto n = pluck (55.0 * (k + 1), 0.5, 0.5, 0.4); in.insert (in.end(), n.begin(), n.end()); }
    const size_t playEnd = in.size();
    in.resize (playEnd + (size_t) (40 * kSr), 0.0f);
    std::vector<float> out (in.size());
    double playSec = 0, tailSec = 0; size_t playBlocks = 0, tailBlocks = 0; double slowestTail = 0;
    for (size_t i = 0; i + 128 <= in.size(); i += 128)
    {
        const auto t0 = std::chrono::steady_clock::now();
        e.process (in.data() + i, out.data() + i, 128);
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (i < playEnd) { playSec += s; ++playBlocks; }
        else if (i > playEnd + (size_t) (10 * kSr)) { tailSec += s; ++tailBlocks; slowestTail = std::max (slowestTail, s); }
    }
    const double play = playSec / playBlocks, tail = tailSec / tailBlocks;
    const double ratio = tail / play;
    CHECK (ratio < 1.5, "a block deep in the tails costs %.2fx a block while playing (mark under 1.5x: denormals would show here)", ratio);
    char row[200];
    std::snprintf (row, sizeof (row), "| Tail block cost against playing | %.2fx | under 1.5x |\n", ratio);
    report += row;
}

static void run()
{
    noiseFloor();
    aliasing();
    levelJumps();
    moreClicks();
    tailCost();
}
} // namespace soundbench
