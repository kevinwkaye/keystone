#pragma once
// Keystone sound engine: portable C++ core, mono in, mono out.
//
// Threads:
//  - audio thread: process() only. No locks, no allocation.
//  - any other thread: setParam(), trigger actions, read meters.
//  - one housekeeping thread: idle(), about 50 to 100 times a second. Runs the tuner,
//    drone capture analysis and capture (NAM) loading, never on the audio thread.

#include "Params.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ks
{
struct Meters
{
    float inPeak = 0, outPeak = 0, outRms = 0;   // linear
    int droneLayers = 0;
    bool droneSwelling = false;
    float grainsActivity = 0;                    // 0..1
    float limiterReduction = 0;                  // dB, positive
    bool tunerOn = false;
    int tunerMidi = -1;
    float tunerCents = 0, tunerFreq = 0;
    bool clipped = false;
    float bloom = 0;
    float cpuLoad = 0;                           // filled by the host
    bool light = false;                          // effects in light mode right now
};

enum class Action
{
    DroneCapture,    // tap D: grab a note, tap again to stack
    DroneClear,
    TunerOn,
    TunerOff,
    TunerToggle,
    DelayTap,        // tap tempo for the delay
};

class Engine
{
public:
    Engine();
    ~Engine();

    // ---- setup (not on the audio thread) ----
    void prepare (double sampleRate, int maxBlockSize);
    double sampleRate() const { return sr; }
    int latencySamples() const;   // engine-added latency (high band alignment)

    // ---- audio thread ----
    void process (const float* in, float* out, int numSamples);

    // ---- control, any thread ----
    void setParam (P p, float value);
    float getParam (P p) const { return values[(size_t) p].load (std::memory_order_relaxed); }
    void setBloom (float pos01);          // expression position after calibration
    float bloom() const { return bloomPos.load(); }
    void action (Action a);
    void setLightMode (bool light) { lightRequest.store (light); }
    Meters meters() const;
    void setCpuLoad (float l) { cpuLoad.store (l); }

    // ---- captures (NAM) ----
    // Loads a .nam capture on the calling thread (never the audio thread) and hands it
    // to the audio thread. Returns false and fills error if it can't be used.
    bool loadCapture (const std::string& path, std::string& error);
    void clearCapture();
    std::string captureName() const;
    bool hasCapture() const;
    static bool namAvailable();

    // ---- spaces (impulse responses for the Impulse reverb) ----
    // Loads a .wav on the calling thread (never the audio thread). Empty path clears.
    bool loadSpace (const std::string& path, std::string& error);
    std::string spaceName() const;

    // ---- housekeeping thread ----
    void idle();

    // For tests: run idle work synchronously.
    void runIdleNow() { idle(); }

    // The last few seconds of input and output for Keeper are handled by the host.

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    double sr = 48000.0;
    std::array<std::atomic<float>, kNumParams> values;
    std::atomic<float> bloomPos { 0.0f }, cpuLoad { 0.0f };
    std::atomic<bool> lightRequest { false };
};

// Parameter value text for display and for Keeper logs.
std::string formatValue (P p, float v);
} // namespace ks
