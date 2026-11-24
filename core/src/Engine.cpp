#include "keystone/Engine.h"

#include "Ambient.h"
#include "Delay.h"
#include "Reverb.h"
#include "Analysis.h"
#include "Drive.h"
#include "Drone.h"
#include "Grains.h"
#include "Valve.h"
#include "Modules.h"
#include "KuddleEngine.h"
#include "Recorder.h"

#include <chrono>
#include <filesystem>
#include <thread>
#include <cstdio>
#include <mutex>

#ifdef KEYSTONE_WITH_NAM
#include "NAM/activations.h"
#include "NAM/get_dsp.h"
#endif

namespace ks
{
// Bitrot on at Level 40%: +3 dB over the sound going into it
static constexpr float kBitStep = 1.41254f;
struct Engine::Impl
{
    float sr = 48000;
    int maxBlock = 512;
    Vals v {};

    DcBlock dc;
    LR4 split;
    Biquad subCut1, subCut2;
    float lastSubCut = -1;
    Smoothed wetLevel;
    std::vector<float> fullBuf;
    DelayLine lowAlign;            // lines the low band up with the high band's latency
    LowBand low;
    Drone drone;
    Drive drive;
    CaptureSlot capture;
    Bitrot bitrot;
    Preamp preamp;
    OutputIron iron;
    EnvFilter filter;
    EnvFilter wah;                 // the pedal wah, its own filter so it stacks with the envelope one
    MonoDimension dim;
    Grains grains;
    Delay delay;
    Reverb reverb;
    std::mutex spaceLock;
    std::string spacePath, spaceName;
    Biquad paHp, paLp;
    Limiter limiter;
    VinylBrake brake;
    PlayLevel play;

    Smoothed lowLevel, highLevel, outLevel, tunerGate, bitrotOn, preOn, filtOn, dimOn, wahOn, volume;
    Smoothed bloomS;
    std::vector<float> lowBuf, highBuf, cleanBuf, droneBuf, synthBuf, lowMul, synthG;
    Smoothed bitrotClean;
    // Bitrot's synth, matched to the sound reaching it (after the drive by default), then
    // stepped up like a gain stage: at Level 40% switching it on is +3 dB (0.5.1)
    float bitPowIn = 0, bitPowSy = 0, bitMatch = 1, bitAvg = 0.0005f, bitTrim = 1;
    float bitPostPow = 0;   // the sound after the preamp, for Bitrot placed there
    // the step up is held at the preamp's output, so the valve can't squash it back: the
    // preamp's gain is learnt while Bitrot is off, and a make-up gain after it lands the step
    double bitRefAcc = 0, bitOutAcc = 0;
    float bitRefS = 0, bitOutS = 0, bitDryGain = 1, bitMake = 1;
    std::vector<float> bitG;
    DelayLine bitLow;   // the bottom end, lined up with the drive for Bitrot's reference
    bool bitFresh = true;
    float bitPedal = 0; bool bitPedalFresh = true;   // Bitrot's pedal job, glided
    float wRev = 0, wCut = 1; bool watchFresh = true, bitHold = false;   // the watch on Bitrot

    // tuner: input decimated by 4 into a ring, analysed on the housekeeping thread
    Biquad tunerLp1, tunerLp2;
    std::vector<float> tunerRing;
    std::atomic<int> tunerPos { 0 };
    int decim = 0;
    std::atomic<bool> tunerOn { false };
    std::atomic<int> tunerMidi { -1 };
    std::atomic<float> tunerCents { 0 }, tunerFreq { 0 };
    double tunerSmoothCents = 0;
    int tunerLastMidi = -1;
    std::chrono::steady_clock::time_point lastTunerRun {};

    // tails ring over a part change: a short window in which delay and reverb switched
    // off keep their tails instead of fading them out
    // Kuddle
    kud::Engine kuddle;
    std::vector<float> kudL, kudR;
    Smoothed rigMix, ksIn;
    int64_t ksQuiet = 0;       // samples Keystone has been silent while Kuddle plays

    // the tuner hears the bass at a quarter of the rate, whichever rig is playing
    void feedTuner (float raw)
    {
        const float t = tunerLp2.process (tunerLp1.process (raw));
        if (++decim >= 4)
        {
            decim = 0;
            const int p = (tunerPos.load (std::memory_order_relaxed) + 1) & 8191;
            tunerRing[(size_t) p] = t;
            tunerPos.store (p, std::memory_order_release);
        }
    }
    bool kuddleRunning = false;
    std::atomic<float> kudGr { 0 };

    std::atomic<int> spillRequests { 0 };
    int spillSeen = 0;
    int64_t spillLeft = 0;

    // meters
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, outRms { 0 }, limRed { 0 };
    std::atomic<bool> clipped { false }, light { false };
    float inPk = 0, outPk = 0, outMs = 0;

    // light mode in Auto: drop effects quality before adding latency
    float loadHigh = 0, loadLow = 0;
    bool autoLight = false;

    // delay tap tempo

    std::string capName;
    mutable std::mutex capLock;
    std::atomic<bool> haveCapture { false };
};

Engine::Engine() : impl (std::make_unique<Impl>())
{
    for (int i = 0; i < kNumParams; ++i) values[(size_t) i].store (paramTable()[i].def);
    resetPedalJobs();
}

Engine::~Engine() = default;

bool Engine::namAvailable()
{
#ifdef KEYSTONE_WITH_NAM
    return true;
#else
    return false;
#endif
}

int Engine::latencySamples() const { return Drive::kLatency + Preamp::kLatency; }

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    auto& d = *impl;
    d.sr = (float) sampleRate;
    if (auto* r = recorder.load()) r->setRate (sampleRate);
    d.maxBlock = std::max (16, maxBlockSize);
    const float s = d.sr;
    d.dc.prepare (s); d.dc.reset();
    d.split.freq = -1;
    d.split.set (100, s);
    d.split.reset();
    d.lastSubCut = -1;
    d.wetLevel.setTime (0.03f, s); d.wetLevel.reset (1);
    d.fullBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.lowAlign.prepare (Drive::kLatency + Preamp::kLatency + 4);
    d.bitLow.prepare (Drive::kLatency + 4);
    d.low.prepare (s);
    d.drone.prepare (s);
    d.drive.prepare (s, d.maxBlock);
    d.bitrot.prepare (s);
    d.bitAvg = 1.0f - std::exp (-1.0f / (0.3f * (float) s)); d.bitPowIn = d.bitPowSy = 0; d.bitMatch = 1; d.bitPostPow = 0; d.bitRefS = d.bitOutS = 0; d.bitDryGain = 1; d.bitMake = 1;
    d.preamp.prepare (s);
    d.iron.prepare (s);
    d.filter.prepare (s);
    d.wah.prepare (s);
    d.kuddle.prepare (s, d.maxBlock);
    d.kudL.assign ((size_t) d.maxBlock, 0.0f);
    d.kudR.assign ((size_t) d.maxBlock, 0.0f);
    d.rigMix.setTime (0.05f, s); d.rigMix.reset (rigNow.load() == 1 ? 1.0f : 0.0f);
    d.ksIn.setTime (0.05f, s); d.ksIn.reset (rigNow.load() == 0 ? 1.0f : 0.0f);
    d.kuddleRunning = false;
    d.ksQuiet = 0;
    d.dim.prepare (s);
    d.grains.prepare (s);
    d.delay.prepare (s);
    d.reverb.prepare (s);
    {
        // an impulse built for the old rate: build it again for this one
        std::string path;
        { std::lock_guard<std::mutex> l (d.spaceLock); path = d.spacePath; }
        if (! path.empty())
        {
            std::vector<float> h;
            std::string err;
            if (readWavMono (path, s, h, err) && d.reverb.convolver().offer (d.reverb.convolver().build (h)))
            {
                d.reverb.block();
                d.reverb.convolver().collect();
            }
        }
    }
    d.paHp.highPass (45, 0.707f, s);
    d.paLp.lowPass (5500, 0.707f, s);
    d.limiter.prepare (s);
    d.brake.prepare ((float) s);
    d.play.prepare (s, 0.045f);
    for (auto* sm : { &d.lowLevel, &d.highLevel, &d.outLevel, &d.tunerGate, &d.bitrotOn, &d.preOn, &d.filtOn, &d.dimOn, &d.wahOn })
        sm->setTime (0.015f, s);
    d.volume.setTime (0.012f, s); d.volume.reset (1);
    d.wahOn.reset (0);
    d.bloomS.setTime (0.02f, s);
    d.lowLevel.reset (1); d.highLevel.reset (1); d.outLevel.reset (1); d.tunerGate.reset (1);
    d.bitrotOn.reset (0); d.preOn.reset (1); d.filtOn.reset (0); d.dimOn.reset (1);
    d.bloomS.reset (job[3].value.load());
    d.lowBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.highBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.cleanBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.droneBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.synthBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.lowMul.assign ((size_t) d.maxBlock, 1.0f);
    d.synthG.assign ((size_t) d.maxBlock, 0.0f);
    d.bitG.assign ((size_t) d.maxBlock, 0.0f);
    d.bitrotClean.setTime (0.03f, s); d.bitrotClean.reset (0.5f);
    d.tunerLp1.lowPass (2000, 0.54f, s);
    d.tunerLp2.lowPass (2000, 1.31f, s);
    d.tunerRing.assign (8192, 0.0f);
    d.tunerPos = 0;
    d.inPk = d.outPk = d.outMs = 0;
}

void Engine::setParam (P p, float value)
{
    const auto& inf = info (p);
    values[(size_t) p].store (clampf (value, inf.min, inf.max), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------
// The expression pedal's jobs
// ---------------------------------------------------------------------------------------
int Engine::pedalTarget() const
{
    switch (pedalBank.load())
    {
        case 0: return getParam (P::BitrotOn) > 0.5f && getParam (P::BitrotPedal) > 0.5f ? (int) PedalJob::Bitrot : (int) PedalJob::Dirt;
        case 1: return getParam (P::WahOn) > 0.5f ? (int) PedalJob::Wah : (int) PedalJob::Volume;
        default: return (int) PedalJob::Bloom;
    }
}

void Engine::setRig (int r) { rigNow.store (r == 1 || r == 2 ? r : 0); }

bool Engine::tunerMuting() const { return impl->tunerOn.load() && getParam (P::TunerMute) > 0.5f; }
void Engine::setMelt (float pos) { meltPos.store (clampf (pos, 0.0f, 1.0f)); }

void Engine::setPedal (float pos)
{
    pos = clampf (pos, 0.0f, 1.0f);
    if (rigNow.load() == 1) { meltPos.store (pos); lastPedal.store (pos); return; }   // Kuddle: the Melt sweep
    if (rigNow.load() == 2) return;   // Kaleidoscope: the pedal belongs to the loaded plugin
    const int t = pedalTarget();
    auto& j = job[(size_t) t];
    const float last = lastPedal.load();
    // the pedal has just started working for this job (bank change, or wah switched in
    // or out): a job it set earlier waits for the pedal to come back to it
    if (lastTarget.exchange (t) != t) j.caught.store (false);
    const bool waits = t == (int) PedalJob::Volume || j.active.load();
    if (! waits) { j.value.store (pos); j.active.store (true); j.caught.store (true); }
    else
    {
        if (! j.caught.load())
        {
            const float v = j.value.load();
            const bool crossed = last >= 0 && (last - v) * (pos - v) <= 0.0f;
            if (crossed || std::fabs (pos - v) < 0.03f) j.caught.store (true);
        }
        if (j.caught.load()) { j.value.store (pos); j.active.store (true); }
    }
    lastPedal.store (pos);
}

void Engine::setPedalBank (int bank) { pedalBank.store (std::clamp (bank, 0, 2)); }

void Engine::resetPedalJobs()
{
    for (auto& j : job) { j.active.store (false); j.caught.store (false); }
    job[(size_t) PedalJob::Dirt].value.store (1.0f);
    job[(size_t) PedalJob::Wah].value.store (0.5f);
    job[(size_t) PedalJob::Volume].value.store (1.0f);
    job[(size_t) PedalJob::Bitrot].value.store (0.0f);
    lastTarget.store (-1);
}

void Engine::setJob (PedalJob which, float v)
{
    auto& j = job[(size_t) which];
    j.value.store (clampf (v, 0.0f, 1.0f));
    j.active.store (true);
    j.caught.store (false);   // the pedal catches up with what the phone set
    if ((int) which == pedalTarget()) lastTarget.store ((int) which);
}

void Engine::releaseJob (PedalJob which)
{
    auto& j = job[(size_t) which];
    j.active.store (false);
    j.caught.store (false);
    lastTarget.store (-1);
}

void Engine::action (Action a)
{
    auto& d = *impl;
    switch (a)
    {
        case Action::DroneCapture: d.drone.requestCapture(); break;
        case Action::DroneClear: d.drone.requestClear(); break;
        case Action::TunerOn: d.tunerOn.store (true); break;
        case Action::TunerOff: d.tunerOn.store (false); break;
        case Action::TunerToggle: d.tunerOn.store (! d.tunerOn.load()); break;
        case Action::Spill: d.spillRequests.fetch_add (1); break;
    }
}

Meters Engine::meters() const
{
    auto& d = *impl;
    Meters m;
    m.inPeak = d.inPeak.load();
    m.outPeak = d.outPeak.load();
    m.outRms = d.outRms.load();
    m.droneLayers = d.drone.layerCount();
    m.droneSwelling = d.drone.swelling();
    m.grainsActivity = d.grains.activityLevel();
    m.limiterReduction = d.limRed.load();
    m.tunerOn = d.tunerOn.load();
    m.tunerMidi = d.tunerMidi.load();
    m.tunerCents = d.tunerCents.load();
    m.tunerFreq = d.tunerFreq.load();
    m.clipped = d.clipped.exchange (false);
    m.bloom = job[3].value.load();
    m.pedalBank = pedalBank.load();
    m.pedalTarget = pedalTarget();
    m.pedal = lastPedal.load();
    m.dirt = job[0].value.load(); m.dirtActive = job[0].active.load();
    m.wah = job[1].value.load(); m.wahActive = job[1].active.load();
    m.volume = job[2].value.load(); m.volumeActive = job[2].active.load();
    m.bloomActive = job[3].active.load();
    m.bitrot = job[4].value.load(); m.bitrotActive = job[4].active.load();
    m.rig = rigNow.load();
    m.melt = meltPos.load();
    m.kuddleSqueezeDb = d.kudGr.load();
    {
        const auto& j = job[(size_t) m.pedalTarget];
        m.waiting = (m.pedalTarget == 2 || j.active.load()) && ! j.caught.load();
    }
    m.cpuLoad = cpuLoad.load();
    m.light = d.light.load();
    return m;
}

void Engine::process (const float* in, float* out, int numSamples)
{
    auto& d = *impl;
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (d.maxBlock, numSamples - done);
        const float* x = in + done;
        float* y = out + done;
        done += n;

        // ---- parameter snapshot, once per block ----
        for (int i = 0; i < kNumParams; ++i) d.v[(size_t) i] = values[(size_t) i].load (std::memory_order_relaxed);
        const Vals& v = d.v;

        // light mode: forced, or Auto from the host's CPU reading
        const int q = Vi (v, P::OutQuality);
        const float load = cpuLoad.load();
        const float blockSec = (float) n / d.sr;
        if (load > 0.7f) { d.loadHigh += blockSec; d.loadLow = 0; }
        else if (load < 0.45f) { d.loadLow += blockSec; d.loadHigh = 0; }
        if (d.loadHigh > 2.0f) d.autoLight = true;
        if (d.loadLow > 10.0f) d.autoLight = false;
        const bool light = q == 2 || (q == 0 && (d.autoLight || lightRequest.load()));
        d.light.store (light);

        // bloom zones: heel to halfway brings in grains, halfway to toe the wash.
        // The drone is not on the sweep: footswitch D and the Drone page run it.
        const bool bloomOn = job[3].active.load();
        d.bloomS.set (job[3].value.load());
        const float bl = d.bloomS.target;
        const float z2 = clampf (bl * 2.0f, 0.0f, 1.0f);
        const float z3 = clampf ((bl - 0.5f) * 2.0f, 0.0f, 1.0f);
        const bool heelBack = bloomOn && bl < 0.02f;
        const bool cutTails = Vi (v, P::BloomTails) == 0;

        // tap tempo for the delay: time between two taps under 2 s

        // ---- the other pedal jobs ----
        // dirt blend: heel clean, the drive fully in by halfway, Bitrot from "joins at" to
        // its full level at the toe. A job the pedal hasn't taken plays the saved sound.
        const float dirt = job[0].active.load() ? job[0].value.load() : 1.0f;
        const float driveMix = clampf (dirt * 2.0f, 0.0f, 1.0f);
        const float bitAt = V (v, P::PedalBitrotAt);
        // Bitrot's own pedal job (Pedal does, not Blend): Bitrot plays at its full level and
        // the pedal works one of its configurations; the drive stays where the blend left it
        const int bitPedal = Vi (v, P::BitrotPedal);
        const bool bitOwnsPedal = bitPedal > 0;
        const float bitMix = bitOwnsPedal ? 1.0f : clampf ((dirt - bitAt) / std::max (0.05f, 1.0f - bitAt), 0.0f, 1.0f);
        {
            // a job the pedal hasn't taken plays the saved sound
            const bool bpOn = bitOwnsPedal && job[4].active.load();
            if (bpOn)
            {
                // glide over the pedal's steps (about 20 ms), so sweeps don't zip
                const float target = job[4].value.load();
                if (d.bitPedalFresh) { d.bitPedal = target; d.bitPedalFresh = false; }
                d.bitPedal += (target - d.bitPedal) * (1.0f - std::exp (-(float) n / (0.02f * d.sr)));
            }
            else d.bitPedalFresh = true;
            const float bp = d.bitPedal;
            auto set = [&] (P p, float x) { d.v[(size_t) p] = x; };
            const bool rev = bitPedal == 1 || bitPedal == 3, sweep = bitPedal == 2 || bitPedal == 3;
            // the watch on Sweep or Rev and sweep: Rev rises and the filter closes from where
            // they were, each on its own glide
            const bool wb = bitOwnsPedal && sweep && watchBit.load();
            const float a = 1.0f - std::exp (-(float) n / (0.02f * d.sr));
            if (wb)
            {
                if (d.watchFresh) { d.wRev = watchRev.load(); d.wCut = watchCut.load(); d.watchFresh = false; }
                d.wRev += (watchRev.load() - d.wRev) * a;
                d.wCut += (watchCut.load() - d.wCut) * a;
            }
            else d.watchFresh = true;
            // the level match holds still while the watch is working Bitrot, so a gesture
            // isn't followed by the level swelling back
            d.bitHold = bitOwnsPedal && watchMoving.load();
            if (wb)
            {
                const float b0 = V (v, P::BitrotBend);
                if (bitPedal == 3) set (P::BitrotBend, b0 + (1.0f - b0) * d.wRev);
                d.bitrot.setSweep (d.wCut < 0.995f, 150.0f * std::pow (40.0f, d.wCut), V (v, P::BitrotPeak));
            }
            if (bpOn && rev && ! wb)
            {
                // heel: the song's own Bend; toe: three octaves up
                const float b0 = V (v, P::BitrotBend);
                set (P::BitrotBend, b0 + (1.0f - b0) * bp);
            }
            if (bpOn && bitPedal == 4) set (P::BitrotCorrupt, bp);   // heel tight, toe wild
            if (bpOn && bitPedal == 5)
            {
                // heel the sub alone, the square in the middle, the oscillator at the toe,
                // equal power, at the loudest of the song's three voice levels
                const float top = std::max ({ V (v, P::BitrotSquare), V (v, P::BitrotSub), V (v, P::BitrotOsc), 0.3f });
                const float x = bp * 2.0f;
                set (P::BitrotSub, top * std::sqrt (clampf (1.0f - x, 0.0f, 1.0f)));
                set (P::BitrotSquare, top * std::sqrt (clampf (1.0f - std::fabs (x - 1.0f), 0.0f, 1.0f)));
                set (P::BitrotOsc, top * std::sqrt (clampf (x - 1.0f, 0.0f, 1.0f)));
            }
            // Sweep: a resonant low-pass on the synth alone, 150 Hz at the heel to 6 kHz at the toe
            if (! wb) d.bitrot.setSweep (bpOn && sweep, 150.0f * std::pow (40.0f, bp), V (v, P::BitrotPeak));
        }
        const float wahPos = job[1].active.load() ? job[1].value.load() : 0.5f;
        float volGain = 1.0f;
        if (job[2].active.load())
        {
            // a volume pedal's taper: -24 dB at a quarter, -12 dB at halfway; the heel is
            // silent, or the song's floor
            const float vp = job[2].value.load();
            volGain = vp * vp;
            const float floorDb = V (v, P::PedalVolFloor);
            if (floorDb > -59.5f) volGain = std::max (volGain, dbToGain (floorDb));
        }
        d.volume.set (volGain);
        // the second preamp blend, flipped to by tap C in Grit
        if (Vb (v, P::PreFlip)) d.v[(size_t) P::PreBlend] = V (v, P::PreBlendB);

        // tails ring over a part change: the window stays open for a quarter of a second
        const int sr2 = d.spillRequests.load();
        if (sr2 != d.spillSeen) { d.spillSeen = sr2; d.spillLeft = (int64_t) (0.25f * d.sr); }
        const bool spillWindow = d.spillLeft > 0;
        d.spillLeft = std::max<int64_t> (0, d.spillLeft - n);

        // ---- module updates ----
        const float splitF = V (v, P::SplitFreq);
        d.split.set (splitF, d.sr);
        // the full sound path: the whole bass with only the sub cut (4th order, so it sums
        // with the bottom end within 2 dB)
        const float subCut = V (v, P::SplitSubCut);
        if (std::fabs (subCut - d.lastSubCut) > 0.1f)
        {
            d.subCut1.highPass (subCut, 0.5412f, d.sr);
            d.subCut2.highPass (subCut, 1.3066f, d.sr);
            if (d.lastSubCut < 0) { d.subCut1.reset(); d.subCut2.reset(); }
            d.lastSubCut = subCut;
        }
        d.wetLevel.set (V (v, P::SplitWetLevel));
        const bool dimLow = Vi (v, P::DimPath) == 0;
        d.low.update (v);
        d.drone.update (v, d.play.level);
        d.drone.block (n);
        d.drive.update (v, bloomOn, bl, subCut, d.haveCapture.load(), driveMix);
        auto* cap = d.capture.take();
        d.bitrot.update (v);
        d.preamp.update (v, d.play.level);
        d.iron.set (V (v, P::OutIron));
        d.filter.update (v);
        d.wah.updateWah (v, wahPos);
        d.wahOn.set (Vb (v, P::WahOn) ? 1.0f : 0.0f);
        d.dim.setMode (Vi (v, P::DimMode));
        d.dim.setMix (V (v, P::DimMix));
        d.grains.update (v, z2, bloomOn, light);
        const bool braking = brakeHeld.load();
        // runaway: up over a second and a half, down in a third of a second
        {
            const float blockSec = (float) n / d.sr;
            runAmt = runawayHeld.load() ? std::min (1.0f, runAmt + blockSec / 1.5f) : std::max (0.0f, runAmt - blockSec / 0.35f);
            runawayNow.store (runAmt);
        }
        d.delay.update (v, z3, bloomOn, heelBack, cutTails, spillWindow, runAmt);
        d.reverb.update (v, z3, bloomOn, heelBack, cutTails, light, spillWindow, runAmt);
        d.reverb.block();
        d.lowLevel.set (dbToGain (V (v, P::SplitLowLevel)));
        d.highLevel.set (dbToGain (V (v, P::SplitHighLevel)));
        d.outLevel.set (dbToGain (V (v, P::OutLevel)));
        d.bitrotOn.set (Vb (v, P::BitrotOn) ? bitMix : 0.0f);
        // with the watch on and the pedal Bitrot's own, Bitrot is all you hear
        d.bitrotClean.set (Vi (v, P::BitrotPedal) > 0 && watchOn.load() ? 0.0f : V (v, P::BitrotClean));
        {
            // Level is a gain stage after the drive: at 40% the clean you keep plus the synth
            // come out +3 dB over the sound going in (2x the power), a standard step up
            const float keep = Vi (v, P::BitrotPedal) > 0 && watchOn.load() ? 0.0f : V (v, P::BitrotClean);
            d.bitTrim = std::sqrt (std::max (kBitStep * kBitStep - keep * keep, 0.25f)) * V (v, P::BitrotVol) / 0.4f;
        }
        d.preOn.set (Vb (v, P::PreOn) ? 1.0f : 0.0f);
        d.filtOn.set (Vb (v, P::FiltOn) ? 1.0f : 0.0f);
        d.dimOn.set (Vb (v, P::DimOn) ? 1.0f : 0.0f);
        const bool tuning = d.tunerOn.load();
        d.tunerGate.set (tuning && Vb (v, P::TunerMute) ? 0.0f : 1.0f);
        const int bitrotPlace = Vi (v, P::BitrotPlace);
        const bool bitrotFirst = bitrotPlace == 0;
        const bool limitOn = Vb (v, P::OutLimit);
        const bool pa = Vi (v, P::OutVoicing) == 1;

        // ---- Keystone ----
        // While Kuddle plays and Keystone's tails have died away, Keystone sleeps: only the
        // tuner and the input meter still hear the bass, so the two rigs never cost double.
        const bool ksAsleep = rigNow.load() != 0 && d.ksQuiet > (int64_t) (0.5f * d.sr);
        if (ksAsleep)
        {
            for (int i = 0; i < n; ++i)
            {
                const float raw = d.dc.process (x[i]);
                d.inPk = std::max (d.inPk * 0.9995f, std::fabs (raw));
                d.feedTuner (raw);
                y[i] = 0.0f;
            }
        }
        else
        {
            // ---- split ----
            for (int i = 0; i < n; ++i)
            {
                // the tuner and the input meter hear the bass whichever rig is playing; Keystone's
                // chain stops hearing it while Kuddle plays, so its tails ring out
                const float raw = d.dc.process (x[i]);
                const float s = raw * d.ksIn.next();
                d.cleanBuf[(size_t) i] = s;
                d.inPk = std::max (d.inPk * 0.9995f, std::fabs (raw));
                d.play.process (s);
                float lo, hi;
                d.split.split (s, lo, hi);
                d.lowBuf[(size_t) i] = d.low.process (lo);
                (void) hi;
                const float full = d.subCut2.process (d.subCut1.process (s));
                d.highBuf[(size_t) i] = full;
                d.fullBuf[(size_t) i] = full;
                d.droneBuf[(size_t) i] = d.drone.process (s);
                d.feedTuner (raw);
            }

            // ---- high band: Bitrot (if first), drive, Bitrot, preamp ----
            float* hb = d.highBuf.data();
            // Bitrot hears the whole bass (high band plus low band) and plays its synth into the
            // high band; Clean bass sets how much of the clean signal, low band included, stays
            float* lm = d.lowMul.data();
            std::fill (lm, lm + n, 1.0f);
            float* sg = d.synthG.data();
            std::fill (sg, sg + n, 0.0f);
            float* bg = d.bitG.data();
            std::fill (bg, bg + n, 0.0f);
            d.bitRefAcc = d.bitOutAcc = 0;
            auto runBitrot = [&]()
            {
                // the dry level at Bitrot's place, on or off, for the step up (after the preamp,
                // that's measured in the loop below)
                if (bitrotPlace != 2)
                {
                    const float ll = d.lowLevel.current();
                    const int lag = bitrotFirst ? 0 : Drive::kLatency;
                    for (int i = 0; i < n; ++i)
                    {
                        d.bitLow.push (d.lowBuf[(size_t) i]);
                        const float r = hb[i] + d.bitLow.at (lag) * ll;
                        d.bitRefAcc += (double) r * r;
                    }
                }
                const bool active = Vb (v, P::BitrotOn) || d.bitrotOn.current() > 1.0e-4f;
                if (! active) { for (int i = 0; i < n; ++i) d.bitrotOn.next(); d.bitFresh = true; return; }
                float* sy = d.synthBuf.data();
                for (int i = 0; i < n; ++i) sy[i] = d.cleanBuf[(size_t) i];   // the whole bass, sub included, so it tracks the fundamental
                d.bitrot.process (sy, n);
                // the level it matches: the sound at its own place in the chain, bottom end included,
                // so after the drive it matches the drive, not the clean bass
                const float ll = d.lowLevel.current();
                const bool post = bitrotPlace == 2;
                auto refPow = [&] (int i)
                {
                    if (post) return d.bitPostPow;
                    const float lo = d.lowBuf[(size_t) i] * ll;
                    return hb[i] * hb[i] + lo * lo;
                };
                if (d.bitFresh)
                {
                    // just switched on: start from this block's levels, so it lands level at once
                    double pi = 0, ps = 0;
                    for (int i = 0; i < n; ++i) { pi += (double) refPow (i); ps += (double) sy[i] * sy[i]; }
                    d.bitPowIn = (float) (pi / n); d.bitPowSy = (float) (ps / n);
                    if (d.bitPowIn > 1.0e-6f && d.bitPowSy > 1.0e-8f) d.bitMatch = clampf (std::sqrt (d.bitPowIn / d.bitPowSy), 0.05f, 4.0f);
                    d.bitFresh = false;
                }
                for (int i = 0; i < n; ++i)
                {
                    const float q = sy[i];
                    d.bitPowIn += (refPow (i) - d.bitPowIn) * d.bitAvg;
                    d.bitPowSy += (q * q - d.bitPowSy) * d.bitAvg;
                    // only learn while a note sounds, so the gap between notes can't pull the synth up
                    if (d.bitPowIn > 1.0e-6f && d.bitPowSy > 1.0e-8f)
                    {
                        const float want = clampf (std::sqrt (d.bitPowIn / d.bitPowSy), 0.05f, 4.0f);
                        if (! d.bitHold) d.bitMatch += (want - d.bitMatch) * d.bitAvg * 0.25f;
                    }
                    sy[i] = q * d.bitMatch * d.bitTrim;
                }
                // the pedal's sweep goes after the level match, so the match can't undo it
                d.bitrot.sweep (sy, n);
                for (int i = 0; i < n; ++i)
                {
                    const float g = d.bitrotOn.next();
                    const float keep = 1.0f - g * (1.0f - d.bitrotClean.next());
                    lm[i] = keep;
                    bg[i] = g;
                    // after the preamp, the synth joins the full sound later, untouched by the valve
                    if (bitrotPlace == 2) { sg[i] = g; continue; }
                    hb[i] = hb[i] * keep + sy[i] * g;
                }
            };
            if (bitrotFirst) runBitrot();
            d.drive.process (hb, d.lowBuf.data(), n, cap);
            if (! bitrotFirst) runBitrot();

            float playLevel = d.play.level;
            const bool filtHigh = Vi (v, P::FiltRange) == 1;
            for (int i = 0; i < n; ++i)
            {
                d.lowAlign.push (d.lowBuf[(size_t) i]);
                // bottom end dry, lined up with the full sound
                const float lowA = d.lowAlign.at (Drive::kLatency + Preamp::kLatency);
                const float llNow = d.lowLevel.current();
                float h = hb[i];
                const float pg = d.preOn.next();
                h = d.preamp.process (h, pg);   // always the same latency, on or off
                {
                    // what Bitrot after the preamp matches: this sound, bottom end included
                    const float r = h + lowA * llNow;
                    d.bitPostPow += (r * r - d.bitPostPow) * d.bitAvg * 8.0f;
                }
                if (bitrotPlace == 2)
                {
                    const float r = h + lowA * llNow;
                    d.bitRefAcc += (double) r * r;
                }
                if (sg[i] > 0.0f) h = h * lm[i] + d.synthBuf[(size_t) i] * sg[i];
                {
                    const float r = h + lowA * llNow * lm[i];
                    d.bitOutAcc += (double) r * r;
                }
                // Bitrot's make-up, after the preamp and before everything else
                const float mk = 1.0f + bg[i] * (d.bitMake - 1.0f);
                h *= mk;
                const float fg = filtHigh ? d.filtOn.next() : 0.0f;
                if (fg > 1.0e-4f) h = h + (d.filter.process (h) - h) * fg;
                // the pedal wah on the full sound, so the bottom end stays solid under it
                const float wg = d.wahOn.next();
                if (wg > 1.0e-4f) h = h + (d.wah.process (h) - h) * wg;
                const float dg = d.dimOn.next();
                if (! dimLow && dg > 1.0e-4f) h = h + (d.dim.process (h) - h) * dg;
                // the volume pedal sits before the ambient section, so tails ring as you pull back
                const float vg = d.volume.next();
                h *= d.highLevel.next() * vg;

                // ---- ambient ----
                // Speckle always takes the clean full sound, so its pitch effects never shift the dirt
                const float grainSrc = d.fullBuf[(size_t) i] * vg;
                const float gr = d.grains.process (grainSrc, playLevel);
                const float dr = d.droneBuf[(size_t) i];
                const float droneSend = dr * V (v, P::DroneSend);
                float dl = 0, rv = 0;
                if (d.delay.active()) dl = d.delay.process (h + gr);
                if (d.reverb.active() || droneSend != 0.0f) rv = d.reverb.process (h + gr + dl + droneSend);

                // ---- sum, mono out ----
                // bottom end dry, bottom end wet (Dimension), full sound
                float bottom = lowA * d.lowLevel.next() * vg;
                const float wl = d.wetLevel.next();
                if (dimLow && dg > 1.0e-4f) bottom += (d.dim.process (lowA) - lowA) * dg * wl;
                bottom *= mk;
                float core = bottom * lm[i] + h;
                if (! filtHigh)
                {
                    // the envelope filter on the whole bass, so it sweeps the note you play
                    const float wg = d.filtOn.next();
                    if (wg > 1.0e-4f) core = core + (d.filter.process (core) - core) * wg;
                }
                float o = core + dr * wl + gr + dl + rv;
                o = d.iron.process (o);
                o *= d.outLevel.next();
                if (pa) o = d.paLp.process (d.paHp.process (o));
                o *= d.tunerGate.next();
                o = d.brake.process (o, braking);
                if (std::fabs (o) > 0.99f) d.clipped.store (true);
                o = d.limiter.process (o, limitOn);
                y[i] = o;
            }
            // ---- Bitrot's step up ----
            // Off: learn how the preamp changes the level. On: set the make-up so the sound
            // after the preamp sits Level's step over what it would be with Bitrot off.
            {
                const float a = 1.0f - std::exp (-(float) n / (0.3f * d.sr));
                const float ref = (float) (d.bitRefAcc / n), out = (float) (d.bitOutAcc / n);
                const bool sounding = ref > 1.0e-6f;   // only while a note sounds
                if (sounding)
                {
                    d.bitRefS += (ref - d.bitRefS) * a;
                    d.bitOutS += (out - d.bitOutS) * a;
                }
                const float g = d.bitrotOn.current();
                if (sounding && g < 1.0e-3f && d.bitRefS > 1.0e-7f)
                    d.bitDryGain += (clampf (d.bitOutS / d.bitRefS, 0.01f, 100.0f) - d.bitDryGain) * a;
                else if (sounding && g > 0.5f && ! d.bitHold && d.bitOutS > 1.0e-8f)
                {
                    const float lv = V (v, P::BitrotVol) / 0.4f;
                    const float target = kBitStep * kBitStep * lv * lv * d.bitDryGain * d.bitRefS;
                    const float want = clampf (std::sqrt (target / d.bitOutS), 0.25f, 4.0f);
                    d.bitMake += (want - d.bitMake) * a;
                }
            }

            // asleep once Kuddle has the bass and nothing is left ringing
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::fabs (y[i]));
            d.ksQuiet = rigNow.load() != 0 && d.ksIn.current() < 1.0e-6f && pk < 1.0e-5f ? d.ksQuiet + n : 0;
        }

        // ---- Kuddle: runs while it's heard, crossfaded with Keystone ----
        const bool kudWanted = rigNow.load() == 1;
        d.rigMix.set (kudWanted ? 1.0f : 0.0f);
        d.ksIn.set (rigNow.load() == 0 ? 1.0f : 0.0f);   // Kuddle or Kaleidoscope: Keystone's tails ring out
        const bool kudHeard = kudWanted || d.rigMix.current() > 1.0e-4f;
        if (kudHeard)
        {
            if (! d.kuddleRunning) { d.kuddle.reset(); d.kuddleRunning = true; }
            kud::Params kp;
            kp.inGainDb = V (v, P::KInGain); kp.gateDb = V (v, P::KGate);
            kp.muteOn = Vb (v, P::KMuteOn); kp.foam = V (v, P::KFoam); kp.thump = V (v, P::KThump); kp.strings = Vi (v, P::KStrings);
            kp.wahOn = Vb (v, P::KWahOn); kp.wahManual = Vi (v, P::KWahMode) == 1; kp.wahSense = V (v, P::KWahSense); kp.wahRange = Vi (v, P::KWahRange);
            kp.wahSpeed = V (v, P::KWahSpeed); kp.wahPeak = V (v, P::KWahPeak); kp.wahPos = V (v, P::KWahPos);
            kp.diDrive = V (v, P::KDiDrive); kp.diAir = V (v, P::KDiAir); kp.diSag = V (v, P::KDiSag); kp.diLowCut = V (v, P::KDiLowCut);
            kp.optoOn = Vb (v, P::KOptoOn); kp.optoSqueeze = V (v, P::KOptoSqueeze); kp.optoFast = Vi (v, P::KOptoSpeed) == 1;
            kp.optoBlend = V (v, P::KOptoBlend); kp.optoHoldDb = V (v, P::KOptoHold); kp.squeeze = squeezeHeld.load();
            kp.tapeOn = Vb (v, P::KTapeOn); kp.tapePush = V (v, P::KTapePush); kp.tapeWow = V (v, P::KTapeWow);
            kp.tapeFlutter = V (v, P::KTapeFlutter); kp.tapeBump = V (v, P::KTapeBump); kp.tapeHiss = V (v, P::KTapeHiss);
            kp.toneOn = Vb (v, P::KToneOn); kp.bassDb = V (v, P::KBass); kp.bassHz = V (v, P::KBassHz); kp.midsDb = V (v, P::KMids);
            kp.midsHz = V (v, P::KMidsHz); kp.trebleKHz = V (v, P::KTreble) * 0.001f;
            kp.roomOn = Vb (v, P::KRoomOn); kp.roomType = Vi (v, P::KRoomType); kp.roomSize = V (v, P::KRoomSize);
            kp.roomDecay = V (v, P::KRoomDecay); kp.roomPreMs = V (v, P::KRoomPre); kp.roomMix = V (v, P::KRoomMix);
            kp.melt = meltPos.load(); kp.meltWorksWah = Vb (v, P::KMeltWah);
            kp.reachMute = V (v, P::KReachMute); kp.reachTape = V (v, P::KReachTape); kp.reachTreble = V (v, P::KReachTreble); kp.reachRoom = V (v, P::KReachRoom);
            kp.outDb = V (v, P::KOutput);
            kp.mute = tuning && Vb (v, P::TunerMute);
            d.kuddle.setParams (kp);
            d.kuddle.process (x, d.kudL.data(), d.kudR.data(), n);
            d.kudGr.store (d.kuddle.optoReductionDb());
            for (int i = 0; i < n; ++i)
            {
                const float k = d.rigMix.next();
                // Keystone's input has already faded, so what's left of it is its tails
                y[i] += 0.5f * (d.kudL[(size_t) i] + d.kudR[(size_t) i]) * k;   // mono out
            }
        }
        else d.kuddleRunning = false;
        for (int i = 0; i < n; ++i)
        {
            const float o = y[i];
            d.outPk = std::max (d.outPk * 0.9995f, std::fabs (o));
            d.outMs += (o * o - d.outMs) * 0.0005f;
        }
        if (auto* r = recorder.load()) r->push (x, y, n);   // the dry bass as it came in, and what goes out
        d.inPeak.store (d.inPk);
        d.outPeak.store (d.outPk);
        d.outRms.store (std::sqrt (d.outMs));
        d.limRed.store (d.limiter.reductionDb());
    }
}

void Engine::idle()
{
    auto& d = *impl;
    d.drone.analyse (d.play.level);
    d.capture.collect();
    d.reverb.convolver().collect();

    // tuner, about 20 times a second while it's on
    if (! d.tunerOn.load()) { d.tunerMidi.store (-1); return; }
    const auto now = std::chrono::steady_clock::now();
    if (now - d.lastTunerRun < std::chrono::milliseconds (45)) return;
    d.lastTunerRun = now;
    const int n = 2048;   // 170 ms at 12 kHz: three cycles of low B at 31 Hz
    std::vector<float> buf ((size_t) n);
    const int end = d.tunerPos.load (std::memory_order_acquire);
    double peak = 0;
    for (int i = 0; i < n; ++i)
    {
        buf[(size_t) i] = d.tunerRing[(size_t) ((end - n + 1 + i) & 8191)];
        peak = std::max (peak, (double) std::fabs (buf[(size_t) i]));
    }
    if (peak < 2.0e-3) { d.tunerMidi.store (-1); d.tunerLastMidi = -1; return; }
    double clarity = 0;
    const double f = yinPitch (buf.data(), n, d.sr / 4.0, 28.0, 1000.0, clarity);
    if (f <= 0 || clarity < 0.75) return;   // keep the last reading through a brief unclear patch
    const auto r = nameNote (f, getParam (P::TunerRef));
    if (r.midi != d.tunerLastMidi) { d.tunerSmoothCents = r.cents; d.tunerLastMidi = r.midi; }
    else d.tunerSmoothCents += (r.cents - d.tunerSmoothCents) * 0.5;
    d.tunerMidi.store (r.midi);
    d.tunerCents.store ((float) d.tunerSmoothCents);
    d.tunerFreq.store ((float) f);
}

bool Engine::loadCapture (const std::string& path, std::string& error)
{
#ifdef KEYSTONE_WITH_NAM
    try
    {
        nam::activations::Activation::enable_fast_tanh();
        auto model = nam::get_dsp (std::filesystem::path (std::u8string (path.begin(), path.end())));
        if (model == nullptr) { error = "That file isn't a capture Keystone can read"; return false; }
        if (model->NumInputChannels() != 1 || model->NumOutputChannels() != 1) { error = "Only mono captures are supported"; return false; }
        model->Reset (sr, impl->maxBlock);
        // hand it over; wait while a previous swap is still in flight
        auto* raw = model.release();
        for (int i = 0; i < 200; ++i)
        {
            if (impl->capture.offer (raw))
            {
                std::lock_guard<std::mutex> l (impl->capLock);
                const auto slash = path.find_last_of ("/\\");
                std::string name = slash == std::string::npos ? path : path.substr (slash + 1);
                if (name.size() > 4 && name.substr (name.size() - 4) == ".nam") name.resize (name.size() - 4);
                impl->capName = name;
                impl->haveCapture.store (true);
                return true;
            }
            impl->capture.collect();
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        delete raw;
        error = "The engine is busy, try again";
        return false;
    }
    catch (const std::exception& e)
    {
        error = std::string ("Couldn't load the capture: ") + e.what();
        return false;
    }
#else
    (void) path;
    error = "This build has no capture support";
    return false;
#endif
}

void Engine::clearCapture()
{
    impl->haveCapture.store (false);
    for (int i = 0; i < 200 && ! impl->capture.offer (CaptureSlot::clearMarker()); ++i)
    {
        impl->capture.collect();
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    std::lock_guard<std::mutex> l (impl->capLock);
    impl->capName.clear();
}

std::string Engine::captureName() const
{
    std::lock_guard<std::mutex> l (impl->capLock);
    return impl->capName;
}

bool Engine::hasCapture() const { return impl->haveCapture.load(); }

bool Engine::loadSpace (const std::string& path, std::string& error)
{
    auto& d = *impl;
    Convolver::Impulse* imp = nullptr;
    if (! path.empty())
    {
        std::vector<float> h;
        if (! readWavMono (path, d.sr, h, error)) return false;
        imp = d.reverb.convolver().build (h);
    }
    for (int i = 0; i < 300; ++i)
    {
        if (d.reverb.convolver().offer (imp))
        {
            std::lock_guard<std::mutex> l (d.spaceLock);
            d.spacePath = path;
            const auto slash = path.find_last_of ("/\\");
            std::string name = slash == std::string::npos ? path : path.substr (slash + 1);
            if (name.size() > 4 && (name.substr (name.size() - 4) == ".wav" || name.substr (name.size() - 4) == ".WAV")) name.resize (name.size() - 4);
            d.spaceName = name;
            return true;
        }
        d.reverb.convolver().collect();
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    delete imp;
    error = "Keystone is busy, try again";
    return false;
}

std::string Engine::spaceName() const
{
    std::lock_guard<std::mutex> l (impl->spaceLock);
    return impl->spaceName;
}
} // namespace ks
