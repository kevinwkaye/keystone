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
    // pedal jobs: the bank the pedal is working for, each job's value and whether the
    // pedal has taken it yet (a job not taken plays its saved sound)
    int pedalBank = 0;                           // 0 Grit, 1 Groove, 2 Glow
    int pedalTarget = 0;                         // 0 dirt, 1 wah, 2 volume, 3 Bloom
    float pedal = -1;                            // last raw position after calibration, -1 not moved
    float dirt = 1, wah = 0.5f, volume = 1;
    bool dirtActive = false, wahActive = false, volumeActive = false, bloomActive = false;
    bool waiting = false;                        // the pedal hasn't caught up with its job yet
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
    Spill,           // the next few blocks: delay and reverb switched off keep their tails
};

// What the expression pedal does: one job per bank, and in Groove, wah or volume.
enum class PedalJob { Dirt = 0, Wah = 1, Volume = 2, Bloom = 3 };

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
    // The expression pedal, after calibration. Goes to the job of the bank set with
    // setPedalBank: a job nobody has touched takes the pedal on its first move; a job the
    // pedal left earlier waits until the pedal passes its value (soft takeover). Volume
    // starts full and always waits, so it never drops out on a bank change.
    void setPedal (float pos01);
    void setPedalBank (int bank);         // 0 Grit (dirt), 1 Groove (wah or volume), 2 Glow (Bloom)
    void resetPedalJobs();                // new song or part: every job back to its saved sound
    void setJob (PedalJob job, float v);  // the phone's bar: set a job directly
    void setBloom (float pos01) { setJob (PedalJob::Bloom, pos01); }
    float bloom() const { return job[3].value.load(); }
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
    struct JobState { std::atomic<float> value { 0.0f }; std::atomic<bool> active { false }, caught { false }; };
    std::array<JobState, 4> job;
    std::atomic<int> pedalBank { 0 }, lastTarget { -1 };
    std::atomic<float> lastPedal { -1.0f }, cpuLoad { 0.0f };
    int pedalTarget() const;
    std::atomic<bool> lightRequest { false };
};

// Parameter value text for display and for Keeper logs.
std::string formatValue (P p, float v);
} // namespace ks
