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
    int pedalTarget = 0;                         // 0 dirt, 1 wah, 2 volume, 3 Bloom, 4 Bitrot
    float pedal = -1;                            // last raw position after calibration, -1 not moved
    float dirt = 1, wah = 0.5f, volume = 1, bitrot = 0;
    bool dirtActive = false, wahActive = false, volumeActive = false, bloomActive = false, bitrotActive = false;
    bool waiting = false;                        // the pedal hasn't caught up with its job yet
    int rig = 0;                                 // 0 Keystone, 1 Kuddle, 2 Kaleidoscope
    float melt = 0;                              // Kuddle's pedal
    float kuddleSqueezeDb = 0;                   // the Opto's gain reduction
    float cpuLoad = 0;                           // filled by the host
    bool light = false;                          // effects in light mode right now
    // the listener (0.6.1), for the projector: how hard you're playing against your own
    // playing level (about 1 on a normal note), the top end the same way, a count that goes
    // up on every note start with how hard it was hit, and the note you're on (pitch class
    // 0 = C, -1 none) with how sure it is. The note only runs while setListening is on.
    float listenLevel = 0, listenHigh = 0, onsetStrength = 0;
    int onsets = 0;
    int notePc = -1;
    float noteClarity = 0;
};

enum class Action
{
    DroneCapture,    // tap D: grab a note, tap again to stack
    DroneClear,
    TunerOn,
    TunerOff,
    TunerToggle,
    Spill,           // the next few blocks: delay and reverb switched off keep their tails
};

// What the expression pedal does: one job per bank, and in Groove, wah or volume. In Grit
// it's the dirt blend, or Bitrot's own job when Bitrot is on and its Pedal does isn't Blend.
enum class PedalJob { Dirt = 0, Wah = 1, Volume = 2, Bloom = 3, Bitrot = 4 };

class Recorder;

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
    int target() const { return pedalTarget(); }
    void setPedalBank (int bank);         // 0 Grit (dirt or Bitrot), 1 Groove (wah or volume), 2 Glow (Bloom)
    void resetPedalJobs();                // new song or part: every job back to its saved sound
    void setJob (PedalJob job, float v);  // the phone's bar: set a job directly
    void releaseJob (PedalJob job);       // back to its saved sound, as if the pedal never touched it
    // a watch has been used: Bitrot with its own pedal job plays fully wet, no clean under it
    void setWatchConnected (bool on) { watchOn.store (on); }
    // the watch working Sweep or Rev and sweep: Rev and the filter apart (0..1 each, filter
    // 1 = open, as if off), in place of the pedal's job while live and gliding back
    // the watch is live or gliding back: Bitrot's level match holds still meanwhile
    void setWatchMoving (bool on) { watchMoving.store (on); }
    void setWatchBitrot (bool on, float rev, float cut) { watchRev.store (rev); watchCut.store (cut); watchBit.store (on); }
    bool jobActive (PedalJob job) const { return job_ (job).active.load(); }
    float jobValue (PedalJob job) const { return job_ (job).value.load(); }
    void setBloom (float pos01) { setJob (PedalJob::Bloom, pos01); }

    // ---- the two rigs: Keystone (band) and Kuddle (home), both loaded ----
    // Switching crossfades in 50 ms. Keystone stops hearing the bass while Kuddle plays,
    // so its delay and reverb tails ring out; Kuddle only runs while it's heard.
    // 2 is Kaleidoscope: both rest (Keystone's tails ring out, then it sleeps) while the
    // host plays a plugin pedal; only the tuner and the input meter still hear the bass.
    void setRig (int r);
    bool tunerMuting() const;   // the tuner is up and set to mute: the host mutes its pedal too
    int rig() const { return rigNow.load(); }
    void setMelt (float pos01);            // Kuddle's pedal, from the phone
    void setSqueeze (bool on) { squeezeHeld.store (on); }   // Kuddle: hold D
    // Runaway (Glow: hold C): the delay and reverb that are on climb past self-oscillation
    // over a second and a half, then fall back when you let go. Grotto's footswitch B.
    void setRunaway (bool on) { runawayHeld.store (on); }
    float runawayAmount() const { return runawayNow.load(); }
    // Vinyl brake (Groove: hold C): Lathe's brake on the whole sound
    void setBrake (bool on) { brakeHeld.store (on); }

    // the rehearsal recorder hears the dry bass and the output of every block
    void setRecorder (Recorder* r) { recorder.store (r); }
    // A host that adds to the output after process() (Kaleidoscope's pedals) records the
    // finished block itself with record(), so the take has what really went to the amp (0.5.3)
    void setHostRecords (bool on) { hostRecords.store (on); }
    void record (const float* dry, const float* out, int n);
    void action (Action a);
    // the projector is reacting: work out the note you're on as well (housekeeping thread)
    void setListening (bool on) { listening.store (on); }
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
    std::array<JobState, 5> job;
    std::atomic<bool> watchOn { false }, watchBit { false }, watchMoving { false };
    std::atomic<float> watchRev { 0.0f }, watchCut { 1.0f };
    std::atomic<int> pedalBank { 0 }, lastTarget { -1 };
    std::atomic<float> lastPedal { -1.0f }, cpuLoad { 0.0f };
    std::atomic<int> rigNow { 0 };
    std::atomic<Recorder*> recorder { nullptr };
    std::atomic<bool> hostRecords { false };
    std::atomic<float> meltPos { 0.0f };
    std::atomic<bool> squeezeHeld { false }, runawayHeld { false }, brakeHeld { false };
    std::atomic<float> runawayNow { 0.0f };
    float runAmt = 0.0f;
    int pedalTarget() const;
    const JobState& job_ (PedalJob j) const { return job[(size_t) j]; }
    std::atomic<bool> listening { false };
};

// Parameter value text for display and for Keeper logs.
std::string formatValue (P p, float v);
} // namespace ks
