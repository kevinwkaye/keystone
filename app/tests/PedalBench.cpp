// Pedal bench: loads every pedal in a folder through Kaleidoscope's rack, plays a bass
// line through it at 48 kHz in 128-sample blocks and reports what each costs. Fails if a
// pedal won't load, makes no sound, or puts out anything that isn't a number.
//   pedal_bench <folder with .vst3 pedals> [report.md]
#include "PedalRack.h"
#include "keystone/Pedals.h"

#include <cmath>
#include <cstdio>

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juce;
    if (argc < 2) { std::printf ("pedal_bench <pedal folder> [report.md]\n"); return 2; }
    const File dir (File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const auto state = File::createTempFile ("pedalstate");
    PedalRack rack;
    rack.setFolders ({ dir }, state);
    rack.prepare (48000.0, 128);
    rack.setMode (true);
    const auto ids = rack.installed();
    String report = "# Pedal bench\n\nEach pedal in Kaleidoscope, 48 kHz, 128-sample blocks, 20 seconds of bass. "
                    "CPU is the share of one core on the build machine.\n\n| Pedal | Controls | CPU | Peak |\n|---|---|---|---|\n";
    bool ok = ! ids.empty();
    if (ids.empty()) std::printf ("FAIL no pedals in %s\n", dir.getFullPathName().toRawUTF8());
    constexpr int n = 128;
    std::vector<float> in (n), out (n);
    for (const auto& id : ids)
    {
        std::string err;
        // the old pedal fades out and the timer swaps: pump the message loop until it has
        if (! rack.load (id, err)) { std::printf ("FAIL %s: %s\n", id.c_str(), err.c_str()); ok = false; continue; }
        for (int k = 0; k < 100 && rack.loadedId() == id && ! rack.running(); ++k)
        {
            rack.process (in.data(), out.data(), n, false);
            MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        for (int k = 0; k < 60; ++k) { std::fill (out.begin(), out.end(), 0.0f); rack.process (in.data(), out.data(), n, false); MessageManager::getInstance()->runDispatchLoopUntil (5); }
        const auto params = JSON::parse (String (rack.paramsJson()));
        double phase = 0, secs = 0;
        float peak = 0;
        bool finite = true;
        const int blocks = 20 * 48000 / n;
        for (int b = 0; b < blocks; ++b)
        {
            // a plucked note every half second, walking round E, G, A, B
            static const double notes[] = { 41.2, 49.0, 55.0, 61.7 };
            for (int i = 0; i < n; ++i)
            {
                const int sample = b * n + i;
                const double t = (sample % 24000) / 48000.0;
                const double f = notes[(sample / 24000) % 4];
                phase += 2 * MathConstants<double>::pi * f / 48000.0;
                in[(size_t) i] = (float) (0.4 * std::exp (-3.0 * t) * (std::sin (phase) + 0.3 * std::sin (2 * phase)));
            }
            std::fill (out.begin(), out.end(), 0.0f);
            const double t0 = Time::getMillisecondCounterHiRes();
            rack.process (in.data(), out.data(), n, false);
            secs += (Time::getMillisecondCounterHiRes() - t0) * 0.001;
            for (float v : out) { if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::abs (v)); }
            if (b % 64 == 0) MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        const double cpu = 100.0 * secs / 20.0;
        const auto* info = ks::findPedal (id);
        const String name = info != nullptr ? info->name : String (id);
        std::printf ("%-12s %3d controls  CPU %5.1f%%  peak %.3f\n", name.toRawUTF8(), params.size(), cpu, peak);
        report << "| " << name << " | " << params.size() << " | " << String (cpu, 1) << "% | " << String (peak, 3) << " |\n";
        if (! finite) { std::printf ("FAIL %s: output isn't a number\n", id.c_str()); ok = false; }
        if (params.size() == 0) { std::printf ("FAIL %s: no controls\n", id.c_str()); ok = false; }
        if (peak < 1.0e-4f) std::printf ("note %s: silent with its saved settings (bypassed, or waiting for a footswitch)\n", id.c_str());
        if (peak > 0.9f) { std::printf ("FAIL %s: got past the safety limiter\n", id.c_str()); ok = false; }
    }
    std::string err;
    rack.load ("", err);
    for (int k = 0; k < 150; ++k) { rack.process (in.data(), out.data(), n, false); MessageManager::getInstance()->runDispatchLoopUntil (10); }
    if (rack.running()) { std::printf ("FAIL the last pedal wasn't put away\n"); ok = false; }
    rack.shutdown();
    if (argc > 2) File (File::getCurrentWorkingDirectory().getChildFile (argv[2])).replaceWithText (report);
    state.deleteRecursively();
    std::printf (ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
