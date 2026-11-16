#pragma once
// Afterglow-style drone from the clean signal: tap D to hold the note you are playing,
// tap again to let it trail off.
// Each layer is rebuilt as a set of sine partials measured from the note (the Afterglow
// lesson: under a cent of error, pure, steady level), ducks while you play and swells
// back in the gaps, with a tone control, an octave up and its own deeper dimension.
//
// Analysis runs on the housekeeping thread; the audio thread only records, synthesises
// and swaps in finished layers through a single-slot mailbox.

#include "Analysis.h"
#include "Modules.h"

#include <atomic>

namespace ks
{
class Drone
{
public:
    static constexpr int kLayers = 4, kPartials = 10;
    static constexpr int kRing = 1 << 15;          // 0.68 s at 48 kHz
    static constexpr int kFft = 16384, kHop = 256;

    struct LayerSpec
    {
        int n = 0;
        float freq[kPartials] {};
        float amp[kPartials] {};
    };

    void prepare (float sr_)
    {
        sr = sr_;
        // bass needs a bigger window (lesson): at 96 kHz double the FFT to keep resolution
        fftSize = sr > 60000 ? kFft * 2 : kFft;
        ring.assign ((size_t) kRing * (sr > 60000 ? 2 : 1), 0.0f);
        ringMask = (int) ring.size() - 1;
        snapshot.assign ((size_t) fftSize + kHop, 0.0f);
        tone1.reset(); tone2.reset();
        subSplit.reset();
        dim.prepare (sr);
        dim.setMix (0.65f);
        duckFollow.set (0.005f, 0.25f, sr);
        duckS.setTime (0.03f, sr);
        levelS.setTime (0.05f, sr);
        octS.setTime (0.05f, sr);
        subSplit.setLowPass (120.0f, sr);
        duckAtt = onePoleCoef (0.03f, sr);
        duckRel = onePoleCoef (0.6f, sr);
        fadeOutStep = 1.0f / (0.25f * sr);
        reset();
    }
    void reset()
    {
        for (auto& l : layers) l.active = false;
        writePos = 0;
        std::fill (ring.begin(), ring.end(), 0.0f);
        duckFollow.reset();
        duckS.reset (1);
        levelS.reset (0);
        octS.reset (0);
        lastTone = -1;
    }

    // ---- control, any thread ----
    void requestCapture() { captureRequests.fetch_add (1); }
    void requestClear() { clearRequests.fetch_add (1); }
    int layerCount() const { return activeLayers.load(); }
    bool swelling() const { return isSwelling.load(); }

    // ---- audio thread ----
    void update (const Vals& v, float playLevel)
    {
        on = Vb (v, P::DroneOn);
        const float tone = V (v, P::DroneTone);
        if (tone != lastTone)
        {
            tone1.lowPass (tone, 0.6f, sr);
            tone2.lowPass (tone * 1.5f, 0.7f, sr);
            lastTone = tone;
        }
        swellSec = V (v, P::DroneSwell);
        duck = V (v, P::DroneDuck);
        dbPlay = playLevel;
        // the drone has its own controls, apart from the expression pedal:
        // footswitch D swells it in, a double tap lets it fade out over the same swell time
        const float level = V (v, P::DroneLevel), sustain = V (v, P::DroneSustain);
        fadeOutStep = 1.0f / (std::max (0.25f, V (v, P::DroneRelease)) * sr);
        levelS.set (on ? level : 0.0f);
        sustainV = sustain;
        octS.set (Vb (v, P::DroneOctave) ? 1.0f : 0.0f);
        const int dm = Vi (v, P::DroneDim);
        dimOn = dm > 0;
        if (dimOn) dim.setMode (dm - 1);
        tuningDrop = Vi (v, P::DroneTuning) == 1;
        decayCoef = sustain >= 0.995f ? 1.0f : std::exp (-1.0f / (0.3f * std::pow (100.0f, sustain) * sr));
    }

    // Records the clean signal, picks up requests, and returns the drone signal.
    float process (float cleanIn)
    {
        writePos = (writePos + 1) & ringMask;
        ring[(size_t) writePos] = cleanIn;

        const float e = duckFollow.process (cleanIn);
        const float d = clampf (e / std::max (dbPlay, 1.0e-4f), 0.0f, 1.0f) * duck;
        const float dg = 1.0f - 0.9f * d;
        // fast to duck, slow to swell back
        duckS.coef = dg < duckS.value ? duckAtt : duckRel;
        duckS.set (dg);
        const float duckGain = duckS.next();

        float sum = 0, sumOct = 0;
        const float oct = octS.next();
        for (auto& l : layers)
        {
            if (! l.active) continue;
            // envelope: swell in, then hold or decay; fading layers go out over 0.25 s
            if (l.fadingOut) { l.env -= fadeOutStep; if (l.env <= 0) { l.active = false; continue; } }
            else if (l.swell < 1.0f)
            {
                l.swell = std::min (1.0f, l.swell + l.swellStep);
                l.env = 0.5f - 0.5f * std::cos (kPi * l.swell);
            }
            else
            {
                l.env *= decayCoef;
                if (l.env < 1.0e-4f) { l.active = false; continue; }
            }
            for (int k = 0; k < l.n; ++k)
            {
                // rotating phasor: 4 multiplies per partial per sample
                const float c = l.re[k] * l.cr[k] - l.im[k] * l.ci[k];
                l.im[k] = l.re[k] * l.ci[k] + l.im[k] * l.cr[k];
                l.re[k] = c;
                sum += l.im[k] * l.amp[k] * l.env;
            }
            if (oct > 0.001f)
                for (int k = 0; k < l.no; ++k)
                {
                    const float c = l.ore[k] * l.ocr[k] - l.oim[k] * l.oci[k];
                    l.oim[k] = l.ore[k] * l.oci[k] + l.oim[k] * l.ocr[k];
                    l.ore[k] = c;
                    sumOct += l.oim[k] * l.oamp[k] * l.env;
                }
        }
        float y = sum + sumOct * oct * 0.6f;
        y = tone2.process (tone1.process (y));
        y *= levelS.next() * duckGain;
        if (dimOn)
        {
            const float lo = subSplit.lp (y);   // keep the sub dry
            y = lo + dim.process (y - lo);
        }
        return y;
    }

    // once per block on the audio thread: renormalise phasors, add drift, take new layers
    void block (int numSamples)
    {
        // clear requests
        const int cr = clearRequests.load();
        if (cr != clearSeen) { clearSeen = cr; for (auto& l : layers) if (l.active) l.fadingOut = true; }
        // capture requests: hand the recording position to the housekeeping thread
        const int rq = captureRequests.load();
        if (rq != captureSeen)
        {
            captureSeen = rq;
            if (! analysisBusy.load())
            {
                analysisEnd.store (writePos);
                analysisWindowDrop.store (tuningDrop);
                analysisBusy.store (true);
                analysisPending.store (true);
            }
        }
        // a finished analysis
        if (resultReady.load (std::memory_order_acquire))
        {
            addLayer (result);
            resultReady.store (false, std::memory_order_release);
        }
        // drift and renormalise (keeps the phasors on the unit circle)
        blockCount += numSamples;
        int count = 0;
        for (auto& l : layers)
        {
            if (! l.active) continue;
            if (! l.fadingOut) ++count;   // held notes; one trailing off does not count
            l.driftPhase += (float) numSamples / sr * 0.07f;
            for (int k = 0; k < l.n; ++k)
            {
                const float m = std::sqrt (l.re[k] * l.re[k] + l.im[k] * l.im[k]);
                if (m > 0) { l.re[k] /= m; l.im[k] /= m; }
                const float cents = 0.8f * std::sin (kTwoPi * l.driftPhase + (float) k * 1.7f);
                setRot (l.cr[k], l.ci[k], l.freq[k] * std::pow (2.0f, cents / 1200.0f));
            }
            for (int k = 0; k < l.no; ++k)
            {
                const float m = std::sqrt (l.ore[k] * l.ore[k] + l.oim[k] * l.oim[k]);
                if (m > 0) { l.ore[k] /= m; l.oim[k] /= m; }
            }
        }
        activeLayers.store (count);
        bool sw = false;
        for (auto& l : layers) sw = sw || (l.active && l.swell < 1.0f);
        isSwelling.store (sw);
    }

    // ---- housekeeping thread ----
    // Returns true if it analysed something (accepted or rejected).
    bool analyse (float playLevel)
    {
        if (! analysisPending.load()) return false;
        analysisPending.store (false);
        const int end = analysisEnd.load();
        const int n = fftSize + kHop;
        for (int i = 0; i < n; ++i)
            snapshot[(size_t) i] = ring[(size_t) ((end - n + 1 + i) & ringMask)];

        // judge silence against how loud Kevin has been playing (Speckle lesson)
        double rms = 0;
        const int tail = std::min (n, (int) (0.1f * sr));
        for (int i = n - tail; i < n; ++i) rms += snapshot[(size_t) i] * snapshot[(size_t) i];
        rms = std::sqrt (rms / tail);
        lastRejected = false;
        if (rms * 1.414 < std::max (playLevel * 0.0708f, 2.0e-5f) || rms < 1.0e-4)
        {
            lastRejected = true;
            analysisBusy.store (false);
            return true;
        }

        const bool drop = analysisWindowDrop.load();
        double clarity = 0;
        // pitch from the most recent 4096 samples (enough for 34 Hz at 48 kHz)
        const int yinN = std::min (n, (int) (sr * 0.09f));
        const double f0 = yinPitch (snapshot.data() + n - yinN, yinN, sr, drop ? 33.0 : 38.0, 400.0, clarity);
        if (f0 <= 0 || clarity < 0.6)
        {
            lastRejected = true;
            analysisBusy.store (false);
            return true;
        }
        auto parts = estimatePartials (snapshot.data(), fftSize, kHop, sr, f0, kPartials, 1400.0);
        if (parts.empty())
        {
            lastRejected = true;
            analysisBusy.store (false);
            return true;
        }
        LayerSpec s;
        double total = 0;
        for (auto& p : parts) total += p.amp;
        for (size_t i = 0; i < parts.size() && s.n < kPartials; ++i)
        {
            s.freq[s.n] = (float) parts[i].freq;
            s.amp[s.n] = (float) (parts[i].amp / std::max (total, 1.0e-9));   // normalised: sums to 1
            ++s.n;
        }
        lastF0 = f0;
        // wait for the audio thread to take the previous result
        for (int i = 0; i < 100 && resultReady.load (std::memory_order_acquire); ++i) {}
        if (! resultReady.load (std::memory_order_acquire))
        {
            result = s;
            resultReady.store (true, std::memory_order_release);
        }
        analysisBusy.store (false);
        return true;
    }
    bool rejectedLast() const { return lastRejected; }
    double lastPitch() const { return lastF0; }

private:
    struct Layer
    {
        bool active = false, fadingOut = false;
        int n = 0, no = 0;
        float freq[kPartials] {}, amp[kPartials] {};
        float re[kPartials] {}, im[kPartials] {}, cr[kPartials] {}, ci[kPartials] {};
        float ore[kPartials] {}, oim[kPartials] {}, ocr[kPartials] {}, oci[kPartials] {}, oamp[kPartials] {};
        float env = 0, swell = 0, swellStep = 0, driftPhase = 0;
        int born = 0;
    };

    void setRot (float& cr, float& ci, float f) const
    {
        const float w = kTwoPi * f / sr;
        cr = std::cos (w);
        ci = std::sin (w);
    }

    void addLayer (const LayerSpec& s)
    {
        // one note held at a time: a new capture takes over and the old one trails off
        for (auto& l : layers)
            if (l.active && ! l.fadingOut) l.fadingOut = true;
        Layer* slot = nullptr;
        for (auto& l : layers) if (! l.active) { slot = &l; break; }
        if (slot == nullptr)   // every slot busy: reuse the quietest fading one
            for (auto& l : layers) if (l.fadingOut && (slot == nullptr || l.env < slot->env)) slot = &l;
        if (slot == nullptr) return;
        Layer& l = *slot;
        l = Layer {};
        l.active = true;
        l.born = ++births;
        l.n = s.n;
        const float layerGain = 0.28f;   // about -11 dBFS peak per layer before level
        l.no = 0;
        for (int k = 0; k < s.n; ++k)
        {
            l.freq[k] = s.freq[k];
            l.amp[k] = s.amp[k] * layerGain;
            const float ph = rng.uni() * kTwoPi;
            l.re[k] = std::cos (ph); l.im[k] = std::sin (ph);
            setRot (l.cr[k], l.ci[k], l.freq[k]);
            if (s.freq[k] * 2.0f < 2500.0f)
            {
                const int o = l.no++;
                l.oamp[o] = l.amp[k];
                l.ore[o] = 1; l.oim[o] = 0;
                setRot (l.ocr[o], l.oci[o], s.freq[k] * 2.0f);
            }
        }
        l.swell = 0;
        l.swellStep = 1.0f / std::max (1.0f, swellSec * sr);
        l.driftPhase = rng.uni();
    }

    float sr = 48000;
    int fftSize = kFft;
    std::vector<float> ring, snapshot;
    int ringMask = 0, writePos = 0;
    std::array<Layer, kLayers + 1> layers {};
    int births = 0;
    Rng rng;

    bool on = true, dimOn = true, tuningDrop = false;
    float swellSec = 1.2f, duck = 0.6f, dbPlay = 0.05f, sustainV = 1, decayCoef = 1;
    float duckAtt = 0.9993f, duckRel = 0.99996f, fadeOutStep = 1.0f / 12000.0f;
    Biquad tone1, tone2;
    float lastTone = -1;
    OnePole subSplit;
    MonoDimension dim;
    Follower duckFollow;
    Smoothed duckS, levelS, octS;
    int64_t blockCount = 0;

    std::atomic<int> captureRequests { 0 }, clearRequests { 0 }, activeLayers { 0 };
    int captureSeen = 0, clearSeen = 0;
    std::atomic<bool> isSwelling { false };
    std::atomic<int> analysisEnd { 0 };
    std::atomic<bool> analysisPending { false }, analysisBusy { false }, analysisWindowDrop { false };
    std::atomic<bool> resultReady { false };
    LayerSpec result;
    bool lastRejected = false;
    double lastF0 = 0;
};
} // namespace ks
