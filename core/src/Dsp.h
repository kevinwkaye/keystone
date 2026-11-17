#pragma once
// Small DSP building blocks shared by every Keystone module. Plain C++, no platform code.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ks
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep (float t) { t = clampf (t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// Time constant to one-pole coefficient.
inline float onePoleCoef (float seconds, float sr)
{
    return seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * sr));
}

// Flush denormals the portable way.
inline float undenorm (float v) { return std::fabs (v) < 1.0e-20f ? 0.0f : v; }

// ---------------------------------------------------------------------------------------
// Parameter glide: one-pole towards a target, per sample.
struct Smoothed
{
    float value = 0.0f, target = 0.0f, coef = 0.0f;
    void setTime (float seconds, float sr) { coef = onePoleCoef (seconds, sr); }
    void reset (float v) { value = target = v; }
    void set (float t) { target = t; }
    float next() { value = target + (value - target) * coef; return value; }
    float current() const { return value; }
    bool settled() const { return std::fabs (value - target) < 1.0e-5f; }
};

// ---------------------------------------------------------------------------------------
// Fast xorshift random, audio-thread safe.
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }    // 0..1
    float bi() { return uni() * 2.0f - 1.0f; }                              // -1..1
};

// ---------------------------------------------------------------------------------------
// RBJ biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = undenorm (b1 * x - a1 * y + z2);
        z2 = undenorm (b2 * x - a2 * y);
        return y;
    }
    void setNorm (double b0_, double b1_, double b2_, double a0, double a1_, double a2_)
    {
        b0 = (float) (b0_ / a0); b1 = (float) (b1_ / a0); b2 = (float) (b2_ / a0);
        a1 = (float) (a1_ / a0); a2 = (float) (a2_ / a0);
    }
    void lowPass (float f, float q, float sr)
    {
        const double w = 2.0 * M_PI * clampf (f, 5.0f, sr * 0.49f) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
        setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void highPass (float f, float q, float sr)
    {
        const double w = 2.0 * M_PI * clampf (f, 5.0f, sr * 0.49f) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
        setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void bandPass (float f, float q, float sr)
    {
        const double w = 2.0 * M_PI * clampf (f, 5.0f, sr * 0.49f) / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
        setNorm (a, 0, -a, 1 + a, -2 * c, 1 - a);
    }
    void peak (float f, float q, float db, float sr)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * M_PI * clampf (f, 5.0f, sr * 0.49f) / sr;
        const double c = std::cos (w), a = std::sin (w) / (2.0 * q);
        setNorm (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
    }
    void lowShelf (float f, float db, float sr, float slope = 1.0f)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * M_PI * clampf (f, 5.0f, sr * 0.49f) / sr;
        const double c = std::cos (w), s = std::sin (w);
        const double a = s / 2 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2), sq = 2 * std::sqrt (A) * a;
        setNorm (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                 (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (float f, float db, float sr, float slope = 1.0f)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * M_PI * clampf (f, 5.0f, sr * 0.49f) / sr;
        const double c = std::cos (w), s = std::sin (w);
        const double a = s / 2 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2), sq = 2 * std::sqrt (A) * a;
        setNorm (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                 (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    // Tilt around a centre: +db brightens, -db darkens (shelves at the centre).
};

// First-order filters.
struct OnePole
{
    float a = 0, z = 0;
    void setLowPass (float f, float sr) { a = std::exp (-kTwoPi * clampf (f, 1.0f, sr * 0.49f) / sr); }
    float lp (float x) { z = undenorm (x + (z - x) * a); return z; }
    float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

// DC blocker.
struct DcBlock
{
    float x1 = 0, y1 = 0, r = 0.9995f;
    void prepare (float sr) { r = 1.0f - (kTwoPi * 8.0f / sr); }
    float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = undenorm (y); return y; }
    void reset() { x1 = y1 = 0; }
};

// ---------------------------------------------------------------------------------------
// Linkwitz-Riley 4th order crossover: two cascaded Butterworth sections each side.
// Low + high sums to an allpass, so the bands meet flat.
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;
    float freq = -1;
    void set (float f, float sr)
    {
        if (std::fabs (f - freq) < 0.01f) return;
        freq = f;
        Biquad t;
        const float q = 0.70710678f;
        t.lowPass (f, q, sr);
        copyCoefs (lp1, t); copyCoefs (lp2, t);
        t.highPass (f, q, sr);
        copyCoefs (hp1, t); copyCoefs (hp2, t);
    }
    static void copyCoefs (Biquad& d, const Biquad& s) { d.b0 = s.b0; d.b1 = s.b1; d.b2 = s.b2; d.a1 = s.a1; d.a2 = s.a2; }
    void split (float x, float& lo, float& hi)
    {
        lo = lp2.process (lp1.process (x));
        hi = hp2.process (hp1.process (x));
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
};

// ---------------------------------------------------------------------------------------
// Antiderivative anti-aliased saturators (first order), as in the Jetstream lesson.
// log(cosh x), the antiderivative of tanh, written as |x| + log1p(exp(-2|x|)) - ln 2: the
// same value with one exp and one log1p, and no overflow for large |x|.
inline float logCosh (float x)
{
    const float ax = std::fabs (x);
    return ax + std::log1p (std::exp (-2.0f * ax)) - 0.69314718f;
}

// First-order antiderivative anti-aliased tanh. F of the last sample is kept, so each
// sample works out F once.
struct AdaaTanh
{
    float x1 = 0, F1 = 0;
    float process (float x)
    {
        const float d = x - x1;
        const float F = logCosh (x);
        const float y = std::fabs (d) < 1.0e-4f ? std::tanh (0.5f * (x + x1)) : (F - F1) / d;
        x1 = x; F1 = F;
        return y;
    }
    void reset() { x1 = 0; F1 = 0; }
};

// A triode's grid curve, with its antiderivative: above zero it bends over sooner (grid
// conduction), below zero it rounds off later (towards cutoff). Keystone's preamp and
// Kuddle's Valve DI both use it.
struct Triode
{
    float u1 = 0, F1 = 0;
    static float f (float u) { return u >= 0 ? u / (1.0f + u) : std::tanh (0.7f * u) / 0.7f; }
    static float F (float u) { return u >= 0 ? u - std::log1p (u) : logCosh (0.7f * u) / 0.49f; }
    float process (float u)
    {
        const float d = u - u1;
        const float Fu = F (u);
        const float y = std::fabs (d) < 1.0e-4f ? f (0.5f * (u + u1)) : (Fu - F1) / d;
        u1 = u; F1 = Fu;
        return y;
    }
    void reset() { u1 = 0; F1 = 0; }
};

// ---------------------------------------------------------------------------------------
// Halfband 2x oversampler with an exact whole-sample latency.
// 31-tap linear-phase halfband: 15 samples at 2x each way, 15 samples at 1x in total.
class Oversampler2x
{
public:
    static constexpr int kTaps = 31;
    static constexpr int kLatency = 15;   // at the base rate, up + down

    Oversampler2x()
    {
        // Kaiser windowed sinc halfband (beta 7), cutoff at a quarter of the 2x rate.
        const int c = kTaps / 2;
        double sum = 0;
        for (int i = 0; i < kTaps; ++i)
        {
            const int n = i - c;
            double h = n == 0 ? 0.5 : std::sin (M_PI * n / 2.0) / (M_PI * n);
            const double r = (double) n / c;
            h *= besselI0 (7.0 * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (7.0);
            coefs[i] = (float) h;
            sum += h;
        }
        for (auto& v : coefs) v = (float) (v / sum);
    }
    void reset()
    {
        std::fill (std::begin (upHist), std::end (upHist), 0.0f);
        std::fill (std::begin (dnHist), std::end (dnHist), 0.0f);
        upPos = dnPos = 0;
    }
    // One input sample to two oversampled samples.
    void up (float x, float& y0, float& y1)
    {
        pushUp (x * 2.0f); y0 = firUp();
        pushUp (0.0f);     y1 = firUp();
    }
    // Two oversampled samples to one output sample.
    float down (float a, float b)
    {
        pushDn (a);
        pushDn (b);
        return firDn();
    }

private:
    static double besselI0 (double x)
    {
        double s = 1, t = 1;
        for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; }
        return s;
    }
    void pushUp (float x) { upHist[upPos] = upHist[upPos + kTaps] = x; upPos = (upPos + 1) % kTaps; }
    void pushDn (float x) { dnHist[dnPos] = dnHist[dnPos + kTaps] = x; dnPos = (dnPos + 1) % kTaps; }
    float firUp() const { float s = 0; const float* h = upHist + upPos; for (int i = 0; i < kTaps; ++i) s += coefs[kTaps - 1 - i] * h[i]; return s; }
    float firDn() const { float s = 0; const float* h = dnHist + dnPos; for (int i = 0; i < kTaps; ++i) s += coefs[kTaps - 1 - i] * h[i]; return s; }

    float coefs[kTaps] {};
    float upHist[kTaps * 2] {}, dnHist[kTaps * 2] {};
    int upPos = 0, dnPos = 0;
};

// ---------------------------------------------------------------------------------------
// Power-of-two delay line. at(0) is the newest sample.
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        pos = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { pos = (pos + 1) & mask; buf[(size_t) pos] = x; }
    float at (int d) const { return buf[(size_t) ((pos - d) & mask)]; }
    float frac (float d) const
    {
        const int i = (int) d;
        const float f = d - (float) i;
        return at (i) + (at (i + 1) - at (i)) * f;
    }
    float hermite (float d) const
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    int size() const { return mask + 1; }

private:
    std::vector<float> buf { 0.0f };
    int mask = 0, pos = 0;
};

// ---------------------------------------------------------------------------------------
// Topology-preserving state variable filter (Zavalishin), for the envelope filter.
struct Svf
{
    float ic1 = 0, ic2 = 0;
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    void set (float f, float res, float sr)
    {
        g = std::tan (kPi * clampf (f, 10.0f, sr * 0.45f) / sr);
        k = 2.0f - 1.9f * clampf (res, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process (float x, float& lp, float& bp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm (2 * v1 - ic1);
        ic2 = undenorm (2 * v2 - ic2);
        lp = v2; bp = v1;
    }
    void reset() { ic1 = ic2 = 0; }
};

// ---------------------------------------------------------------------------------------
// Envelope follower with separate attack and release.
struct Follower
{
    float env = 0, att = 0, rel = 0;
    void set (float attackSec, float releaseSec, float sr) { att = onePoleCoef (attackSec, sr); rel = onePoleCoef (releaseSec, sr); }
    float process (float x)
    {
        const float a = std::fabs (x);
        env = a > env ? a + (env - a) * att : a + (env - a) * rel;
        env = undenorm (env);
        return env;
    }
    void reset() { env = 0; }
};
} // namespace ks
