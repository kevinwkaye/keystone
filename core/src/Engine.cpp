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
    MonoDimension dim;
    Grains grains;
    Delay delay;
    Reverb reverb;
    std::mutex spaceLock;
    std::string spacePath, spaceName;
    Biquad paHp, paLp;
    Limiter limiter;
    PlayLevel play;

    Smoothed lowLevel, highLevel, outLevel, tunerGate, bitrotOn, preOn, filtOn, dimOn;
    Smoothed bloomS;
    std::vector<float> lowBuf, highBuf, cleanBuf, droneBuf, synthBuf, lowMul, synthG;
    Smoothed bitrotClean;

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

    // bloom
    std::atomic<bool> bloomActive { false };
    float lastBloom = 0;

    // meters
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, outRms { 0 }, limRed { 0 };
    std::atomic<bool> clipped { false }, light { false };
    float inPk = 0, outPk = 0, outMs = 0;

    // light mode in Auto: drop effects quality before adding latency
    float loadHigh = 0, loadLow = 0;
    bool autoLight = false;

    // delay tap tempo
    std::atomic<int> tapRequests { 0 };
    int tapSeen = 0;
    int64_t sampleClock = 0, lastTap = -1;
    std::atomic<float> tappedMs { -1 };

    std::string capName;
    mutable std::mutex capLock;
    std::atomic<bool> haveCapture { false };
};

Engine::Engine() : impl (std::make_unique<Impl>())
{
    for (int i = 0; i < kNumParams; ++i) values[(size_t) i].store (paramTable()[i].def);
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
    d.low.prepare (s);
    d.drone.prepare (s);
    d.drive.prepare (s, d.maxBlock);
    d.bitrot.prepare (s);
    d.preamp.prepare (s);
    d.iron.prepare (s);
    d.filter.prepare (s);
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
    d.play.prepare (s, 0.045f);
    for (auto* sm : { &d.lowLevel, &d.highLevel, &d.outLevel, &d.tunerGate, &d.bitrotOn, &d.preOn, &d.filtOn, &d.dimOn })
        sm->setTime (0.015f, s);
    d.bloomS.setTime (0.02f, s);
    d.lowLevel.reset (1); d.highLevel.reset (1); d.outLevel.reset (1); d.tunerGate.reset (1);
    d.bitrotOn.reset (0); d.preOn.reset (1); d.filtOn.reset (0); d.dimOn.reset (1);
    d.bloomS.reset (bloomPos.load());
    d.lowBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.highBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.cleanBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.droneBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.synthBuf.assign ((size_t) d.maxBlock, 0.0f);
    d.lowMul.assign ((size_t) d.maxBlock, 1.0f);
    d.synthG.assign ((size_t) d.maxBlock, 0.0f);
    d.bitrotClean.setTime (0.03f, s); d.bitrotClean.reset (0.5f);
    d.tunerLp1.lowPass (2000, 0.54f, s);
    d.tunerLp2.lowPass (2000, 1.31f, s);
    d.tunerRing.assign (8192, 0.0f);
    d.tunerPos = 0;
    d.inPk = d.outPk = d.outMs = 0;
    d.sampleClock = 0;
    d.lastTap = -1;
}

void Engine::setParam (P p, float value)
{
    const auto& inf = info (p);
    values[(size_t) p].store (clampf (value, inf.min, inf.max), std::memory_order_relaxed);
}

void Engine::setBloom (float pos01)
{
    bloomPos.store (clampf (pos01, 0.0f, 1.0f));
    impl->bloomActive.store (true);
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
        case Action::DelayTap: d.tapRequests.fetch_add (1); break;
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
    m.bloom = bloomPos.load();
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
        const bool bloomOn = d.bloomActive.load();
        d.bloomS.set (bloomPos.load());
        const float bl = d.bloomS.target;
        const float z2 = clampf (bl * 2.0f, 0.0f, 1.0f);
        const float z3 = clampf ((bl - 0.5f) * 2.0f, 0.0f, 1.0f);
        const bool heelBack = bloomOn && bl < 0.02f;
        const bool cutTails = Vi (v, P::BloomTails) == 0;
        d.lastBloom = bl;

        // tap tempo for the delay: time between two taps under 2 s
        const int tr = d.tapRequests.load();
        if (tr != d.tapSeen)
        {
            d.tapSeen = tr;
            if (d.lastTap >= 0)
            {
                const double ms = (double) (d.sampleClock - d.lastTap) * 1000.0 / d.sr;
                if (ms > 40 && ms < 1500) { values[(size_t) P::DlTime].store ((float) ms); d.tappedMs.store ((float) ms); }
            }
            d.lastTap = d.sampleClock;
        }
        d.sampleClock += n;

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
        d.drive.update (v, bloomOn, bl, subCut, d.haveCapture.load());
        auto* cap = d.capture.take();
        d.bitrot.update (v);
        d.preamp.update (v, d.play.level);
        d.iron.set (V (v, P::OutIron));
        d.filter.update (v);
        d.dim.setMode (Vi (v, P::DimMode));
        d.dim.setMix (V (v, P::DimMix));
        d.grains.update (v, z2, bloomOn, light);
        d.delay.update (v, z3, bloomOn, heelBack, cutTails);
        d.reverb.update (v, z3, bloomOn, heelBack, cutTails, light);
        d.reverb.block();
        d.lowLevel.set (dbToGain (V (v, P::SplitLowLevel)));
        d.highLevel.set (dbToGain (V (v, P::SplitHighLevel)));
        d.outLevel.set (dbToGain (V (v, P::OutLevel)));
        d.bitrotOn.set (Vb (v, P::BitrotOn) ? 1.0f : 0.0f);
        d.bitrotClean.set (V (v, P::BitrotClean));
        d.preOn.set (Vb (v, P::PreOn) ? 1.0f : 0.0f);
        d.filtOn.set (Vb (v, P::FiltOn) ? 1.0f : 0.0f);
        d.dimOn.set (Vb (v, P::DimOn) ? 1.0f : 0.0f);
        const bool tuning = d.tunerOn.load();
        d.tunerGate.set (tuning && Vb (v, P::TunerMute) ? 0.0f : 1.0f);
        const int bitrotPlace = Vi (v, P::BitrotPlace);
        const bool bitrotFirst = bitrotPlace == 0;
        const bool limitOn = Vb (v, P::OutLimit);
        const bool pa = Vi (v, P::OutVoicing) == 1;

        // ---- split ----
        for (int i = 0; i < n; ++i)
        {
            const float s = d.dc.process (x[i]);
            d.cleanBuf[(size_t) i] = s;
            d.inPk = std::max (d.inPk * 0.9995f, std::fabs (s));
            d.play.process (s);
            float lo, hi;
            d.split.split (s, lo, hi);
            d.lowBuf[(size_t) i] = d.low.process (lo);
            (void) hi;
            const float full = d.subCut2.process (d.subCut1.process (s));
            d.highBuf[(size_t) i] = full;
            d.fullBuf[(size_t) i] = full;
            d.droneBuf[(size_t) i] = d.drone.process (s);
            // tuner feed
            const float t = d.tunerLp2.process (d.tunerLp1.process (s));
            if (++d.decim >= 4)
            {
                d.decim = 0;
                const int p = (d.tunerPos.load (std::memory_order_relaxed) + 1) & 8191;
                d.tunerRing[(size_t) p] = t;
                d.tunerPos.store (p, std::memory_order_release);
            }
        }

        // ---- high band: Bitrot (if first), drive, Bitrot, preamp ----
        float* hb = d.highBuf.data();
        // Bitrot hears the whole bass (high band plus low band) and plays its synth into the
        // high band; Clean bass sets how much of the clean signal, low band included, stays
        float* lm = d.lowMul.data();
        std::fill (lm, lm + n, 1.0f);
        float* sg = d.synthG.data();
        std::fill (sg, sg + n, 0.0f);
        auto runBitrot = [&]()
        {
            const bool active = Vb (v, P::BitrotOn) || d.bitrotOn.current() > 1.0e-4f;
            if (! active) { for (int i = 0; i < n; ++i) d.bitrotOn.next(); return; }
            float* sy = d.synthBuf.data();
            for (int i = 0; i < n; ++i) sy[i] = d.cleanBuf[(size_t) i];   // the whole bass, sub included, so it tracks the fundamental
            d.bitrot.process (sy, n);
            for (int i = 0; i < n; ++i)
            {
                const float g = d.bitrotOn.next();
                const float keep = 1.0f - g * (1.0f - d.bitrotClean.next());
                lm[i] = keep;
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
            float h = hb[i];
            const float pg = d.preOn.next();
            h = d.preamp.process (h, pg);   // always the same latency, on or off
            if (sg[i] > 0.0f) h = h * lm[i] + d.synthBuf[(size_t) i] * sg[i];
            const float fg = filtHigh ? d.filtOn.next() : 0.0f;
            if (fg > 1.0e-4f) h = h + (d.filter.process (h) - h) * fg;
            const float dg = d.dimOn.next();
            if (! dimLow && dg > 1.0e-4f) h = h + (d.dim.process (h) - h) * dg;
            h *= d.highLevel.next();

            // ---- ambient ----
            const float grainSrc = d.grains.sourceDirty() ? h : d.fullBuf[(size_t) i];
            const float gr = d.grains.process (grainSrc, playLevel);
            const float dr = d.droneBuf[(size_t) i];
            const float droneSend = dr * V (v, P::DroneSend);
            float dl = 0, rv = 0;
            if (d.delay.active()) dl = d.delay.process (h + gr);
            if (d.reverb.active() || droneSend != 0.0f) rv = d.reverb.process (h + gr + dl + droneSend);

            // ---- sum, mono out ----
            d.lowAlign.push (d.lowBuf[(size_t) i]);
            // bottom end dry, bottom end wet (Dimension), full sound
            const float lowA = d.lowAlign.at (Drive::kLatency + Preamp::kLatency);
            float bottom = lowA * d.lowLevel.next();
            const float wl = d.wetLevel.next();
            if (dimLow && dg > 1.0e-4f) bottom += (d.dim.process (lowA) - lowA) * dg * wl;
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
            if (std::fabs (o) > 0.99f) d.clipped.store (true);
            o = d.limiter.process (o, limitOn);
            y[i] = o;
            const float ao = std::fabs (o);
            d.outPk = std::max (d.outPk * 0.9995f, ao);
            d.outMs += (o * o - d.outMs) * 0.0005f;
        }
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
