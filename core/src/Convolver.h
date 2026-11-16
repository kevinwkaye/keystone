#pragma once
// Impulse reverb: your own .wav spaces, played by uniformly partitioned FFT convolution
// (overlap-save, 128-sample hop). Mono. Impulses are trimmed to 2 seconds.
//
// The impulse is built on a housekeeping thread and handed to the audio thread through a
// pending / retired pair of pointers, the same way captures are, so the audio thread never
// allocates or frees.

#include "Dsp.h"
#include "kdd/DspUtil.h"

#include <atomic>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ks
{
class Convolver
{
public:
    static constexpr int kHop = 128, kN = 256, kBins = kN / 2 + 1;
    static constexpr float kMaxSeconds = 2.0f;

    struct Impulse
    {
        int parts = 0;
        std::vector<std::complex<float>> spec;   // parts x kBins
    };

    ~Convolver()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }

    void prepare (float sr)
    {
        fft.init (kN);
        maxParts = (int) std::ceil (kMaxSeconds * sr / kHop) + 1;
        fdl.assign ((size_t) maxParts * kBins, {});
        frame.assign ((size_t) kN, {});
        acc.assign ((size_t) kN, {});
        inHist.assign ((size_t) kN, 0.0f);
        out.assign ((size_t) kHop, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill (fdl.begin(), fdl.end(), std::complex<float>());
        std::fill (inHist.begin(), inHist.end(), 0.0f);
        std::fill (out.begin(), out.end(), 0.0f);
        pos = 0;
        head = 0;
    }

    // ---- housekeeping thread ----
    // Builds the impulse's partition spectra. samples are at the engine's rate.
    Impulse* build (const std::vector<float>& h) const
    {
        auto* imp = new Impulse();
        const int len = (int) std::min (h.size(), (size_t) ((maxParts - 1) * kHop));
        imp->parts = std::max (1, (len + kHop - 1) / kHop);
        imp->spec.assign ((size_t) imp->parts * kBins, {});
        kdd::Fft f;
        f.init (kN);
        std::vector<std::complex<float>> buf ((size_t) kN);
        for (int p = 0; p < imp->parts; ++p)
        {
            std::fill (buf.begin(), buf.end(), std::complex<float>());
            for (int i = 0; i < kHop; ++i)
            {
                const int k = p * kHop + i;
                if (k < len) buf[(size_t) i] = h[(size_t) k];
            }
            f.run (buf.data(), false);
            for (int b = 0; b < kBins; ++b) imp->spec[(size_t) (p * kBins + b)] = buf[(size_t) b];
        }
        return imp;
    }
    bool offer (Impulse* imp)   // nullptr clears
    {
        if (pending.load() != nullptr || retired.load() != nullptr) return false;
        pending.store (imp == nullptr ? clearMarker() : imp);
        return true;
    }
    void collect() { auto* r = retired.exchange (nullptr); if (r != clearMarker()) delete r; }
    bool loaded() const { return hasImpulse.load(); }

    // ---- audio thread ----
    void block()   // once per audio block: pick up a new impulse
    {
        if (retired.load() != nullptr) return;
        if (Impulse* p = pending.exchange (nullptr))
        {
            retired.store (current);
            current = p == clearMarker() ? nullptr : p;
            hasImpulse.store (current != nullptr);
            reset();
        }
    }
    bool active() const { return current != nullptr; }

    float process (float x)
    {
        const float y = out[(size_t) pos];
        inHist[(size_t) (kHop + pos)] = x;
        if (++pos >= kHop)
        {
            pos = 0;
            runBlock();
        }
        return y;
    }

private:
    static Impulse* clearMarker() { static char c; return reinterpret_cast<Impulse*> (&c); }

    void runBlock()
    {
        if (current == nullptr) { std::fill (out.begin(), out.end(), 0.0f); shiftHistory(); return; }
        for (int i = 0; i < kN; ++i) frame[(size_t) i] = inHist[(size_t) i];
        fft.run (frame.data(), false);
        head = (head + 1) % maxParts;
        std::complex<float>* slot = &fdl[(size_t) head * kBins];
        for (int b = 0; b < kBins; ++b) slot[b] = frame[(size_t) b];

        std::fill (acc.begin(), acc.end(), std::complex<float>());
        const int parts = current->parts;
        for (int p = 0; p < parts; ++p)
        {
            int idx = head - p;
            if (idx < 0) idx += maxParts;
            const std::complex<float>* xs = &fdl[(size_t) idx * kBins];
            const std::complex<float>* hs = &current->spec[(size_t) p * kBins];
            for (int b = 0; b < kBins; ++b) acc[(size_t) b] += xs[b] * hs[b];
        }
        for (int b = 1; b < kN / 2; ++b) acc[(size_t) (kN - b)] = std::conj (acc[(size_t) b]);
        fft.run (acc.data(), true);
        for (int i = 0; i < kHop; ++i) out[(size_t) i] = acc[(size_t) (kHop + i)].real();   // the inverse FFT scales
        shiftHistory();
    }
    void shiftHistory()
    {
        std::memmove (inHist.data(), inHist.data() + kHop, sizeof (float) * kHop);
    }

    kdd::Fft fft;
    int maxParts = 1, pos = 0, head = 0;
    std::vector<std::complex<float>> fdl, frame, acc;
    std::vector<float> inHist, out;
    std::atomic<Impulse*> pending { nullptr }, retired { nullptr };
    Impulse* current = nullptr;
    std::atomic<bool> hasImpulse { false };
};

// Reads a .wav (16, 24 or 32-bit PCM, or 32-bit float, any channel count), folds it to
// mono and resamples it to the engine's rate. Returns false with a reason on failure.
inline bool readWavMono (const std::string& path, float targetRate, std::vector<float>& outSamples, std::string& error)
{
    std::ifstream f (std::filesystem::path (std::u8string (path.begin(), path.end())), std::ios::binary);
    if (! f) { error = "Couldn't open that file"; return false; }
    std::vector<char> data ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    auto u16 = [&] (size_t o) { return (uint16_t) ((uint8_t) data[o] | ((uint8_t) data[o + 1] << 8)); };
    auto u32 = [&] (size_t o) { return (uint32_t) u16 (o) | ((uint32_t) u16 (o + 2) << 16); };
    if (data.size() < 12 || std::memcmp (data.data(), "RIFF", 4) != 0 || std::memcmp (data.data() + 8, "WAVE", 4) != 0)
    { error = "That isn't a .wav file"; return false; }
    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    size_t dataAt = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= data.size();)
    {
        const uint32_t len = u32 (o + 4);
        if (std::memcmp (data.data() + o, "fmt ", 4) == 0 && o + 24 <= data.size())
        {
            format = u16 (o + 8);
            channels = u16 (o + 10);
            rate = u32 (o + 12);
            bits = u16 (o + 22);
            if (format == 0xFFFE && len >= 26 && o + 34 <= data.size()) format = u16 (o + 32);
        }
        else if (std::memcmp (data.data() + o, "data", 4) == 0)
        {
            dataAt = o + 8;
            dataLen = std::min ((size_t) len, data.size() - dataAt);
            break;
        }
        o += 8 + len + (len & 1);
    }
    if (dataAt == 0 || channels < 1 || rate < 8000) { error = "That .wav file has no audio Keystone can read"; return false; }
    if (! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)))
    { error = "Use 16, 24 or 32-bit .wav files"; return false; }
    const int bytes = bits / 8;
    const size_t frames = dataLen / (size_t) (bytes * channels);
    const size_t keep = std::min (frames, (size_t) (rate * (Convolver::kMaxSeconds + 0.1f)));
    std::vector<float> mono (keep, 0.0f);
    for (size_t i = 0; i < keep; ++i)
    {
        float sum = 0;
        for (int c = 0; c < channels; ++c)
        {
            const size_t o = dataAt + (i * (size_t) channels + (size_t) c) * (size_t) bytes;
            float v = 0;
            if (format == 3) { uint32_t u = u32 (o); std::memcpy (&v, &u, 4); }
            else if (bits == 16) v = (float) (int16_t) u16 (o) / 32768.0f;
            else if (bits == 24) v = (float) ((int32_t) (((uint32_t) (uint8_t) data[o] << 8) | ((uint32_t) (uint8_t) data[o + 1] << 16) | ((uint32_t) (uint8_t) data[o + 2] << 24)) >> 8) / 8388608.0f;
            else v = (float) (int32_t) u32 (o) / 2147483648.0f;
            if (! std::isfinite (v)) v = 0;
            sum += v;
        }
        mono[i] = sum / (float) channels;
    }
    // resample (linear is plenty for a reverb tail)
    const double step = (double) rate / targetRate;
    const size_t n = (size_t) ((double) keep / step);
    outSamples.assign (n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        const double p = (double) i * step;
        const size_t a = (size_t) p;
        const float fr = (float) (p - (double) a);
        const float x0 = mono[std::min (a, keep - 1)], x1 = mono[std::min (a + 1, keep - 1)];
        outSamples[i] = x0 + (x1 - x0) * fr;
    }
    // trim the silence at the end, fade the last 50 ms, cap at 2 s
    size_t last = outSamples.size();
    float peak = 0;
    for (float v : outSamples) peak = std::max (peak, std::fabs (v));
    if (peak <= 1.0e-6f) { error = "That file is silent"; return false; }
    while (last > 0 && std::fabs (outSamples[last - 1]) < peak * 1.0e-4f) --last;
    last = std::min (last, (size_t) (Convolver::kMaxSeconds * targetRate));
    outSamples.resize (std::max<size_t> (last, 64));
    const size_t fade = std::min (outSamples.size() / 2, (size_t) (0.05f * targetRate));
    for (size_t i = 0; i < fade; ++i) outSamples[outSamples.size() - 1 - i] *= (float) i / (float) fade;
    // level: the same energy as the built-in reverbs, roughly
    double e = 0;
    for (float v : outSamples) e += (double) v * v;
    const float g = (float) (1.25 / std::sqrt (e + 1e-12));
    for (float& v : outSamples) v *= g;
    return true;
}
} // namespace ks
