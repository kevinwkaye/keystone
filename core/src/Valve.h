#pragma once
// Keystone's preamp, warm but modern:
//
//   learnt gain staging -> two valve stages -> passive tone stack --.
//                                     '-> Cut voice (tight, modern) --+-> blend -> tape -> out
//
// Valve: an asymmetric triode curve (harder on top, softer underneath, so even harmonics),
// run with first-order antiderivative anti-aliasing so there is no digital fizz and no added
// latency. The bias shifts as you dig in, the way grid current charges a coupling cap, so
// hard notes thicken and compress a little and soft notes stay round.
// Tone stack: the classic passive bass/middle/treble network (Yeh and Smith's analysis of
// the 1959 bass amp stack), so the knobs interact like a real amp's, plus Ultra Lo and
// Ultra Hi switches.
// Tape: pre-emphasis, soft saturation, de-emphasis, a head bump and a little wow and flutter.
//
// Gain staging learns how loud you play (from note peaks, slowly), so the valve is hit the
// same way whatever the interface gain, and the level is put back afterwards.

#include "Modules.h"
#include <complex>
#include "keystone/Params.h"

namespace ks
{
// Asymmetric triode curve with its antiderivative. Above zero it bends over sooner (grid
// conduction), below zero it rounds off later (towards cutoff).
// Third-order filter from an analogue prototype by the bilinear transform.
struct ThirdOrder
{
    double b[4] {}, a[4] {}, z[3] {};
    void reset() { z[0] = z[1] = z[2] = 0; }
    // H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3)
    void set (double b1, double b2, double b3, double a1, double a2, double a3, double sr)
    {
        const double c = 2.0 * sr, c2 = c * c, c3 = c2 * c;
        double B[4] = { -b1 * c - b2 * c2 - b3 * c3, -b1 * c + b2 * c2 + 3 * b3 * c3,
                        b1 * c + b2 * c2 - 3 * b3 * c3, b1 * c - b2 * c2 + b3 * c3 };
        double A[4] = { -1 - a1 * c - a2 * c2 - a3 * c3, -3 - a1 * c + a2 * c2 + 3 * a3 * c3,
                        -3 + a1 * c + a2 * c2 - 3 * a3 * c3, -1 + a1 * c - a2 * c2 + a3 * c3 };
        for (int i = 0; i < 4; ++i) { b[i] = B[i] / A[0]; a[i] = A[i] / A[0]; }
    }
    float process (float xf)
    {
        const double x = xf, y = b[0] * x + z[0];
        z[0] = b[1] * x - a[1] * y + z[1];
        z[1] = b[2] * x - a[2] * y + z[2];
        z[2] = b[3] * x - a[3] * y;
        return (float) y;
    }
};

class ToneStack
{
public:
    // t, m, l: treble, middle, bass, 0..1. The bass pot is a log taper, as in the amp.
    void set (float t, float m, float l, float sr)
    {
        const double lt = std::pow (10.0, 2.0 * (clampf (l, 0, 1) - 1.0));
        double b1, b2, b3, a1, a2, a3;
        coeffs (clampf (t, 0, 1), clampf (m, 0, 1), lt, b1, b2, b3, a1, a2, a3);
        f.set (b1, b2, b3, a1, a2, a3, sr);
    }
    float process (float x) { return f.process (x); }
    void reset() { f.reset(); }

    static void coeffs (double t, double m, double l, double& b1, double& b2, double& b3, double& a1, double& a2, double& a3)
    {
        const double R1 = 250e3, R2 = 1e6, R3 = 25e3, R4 = 56e3, C1 = 250e-12, C2 = 20e-9, C3 = 20e-9;
        b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
        b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
        b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
           - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
           + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
           + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
           + t * l * C1 * C2 * C3 * R1 * R2 * R4;
        a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
        a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
           - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
           + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
           + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
        a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
           - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
           + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
           + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
    }

private:
    ThirdOrder f;
};

class Preamp
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        blendS.setTime (0.03f, sr);
        stageS.setTime (2.0f, sr);   // gain staging follows your playing slowly
        const float os = 2.0f * sr;   // the valve runs at twice the rate
        envA = 1.0f - std::exp (-1.0f / (0.004f * os));
        envR = 1.0f - std::exp (-1.0f / (0.15f * os));
        couple1.highPass (25.0f, 0.707f, os);
        couple2.highPass (20.0f, 0.707f, os);
        miller1.lowPass (11000.0f, 0.6f, os);
        miller2.lowPass (8500.0f, 0.6f, os);
        dry.prepare (Oversampler2x::kLatency + 4);
        calibrate();
        cHp.highPass (140.0f, 0.707f, sr);
        cLp.lowPass (7000.0f, 0.707f, sr);
        tapePre.highShelf (2500.0f, 6.0f, sr, 0.7f);
        tapeDe.highShelf (2500.0f, -6.0f, sr, 0.7f);
        line.prepare ((int) (0.01f * sr));
        // the stack's own loss at noon, put back so the knobs move around unity
        double b1, b2, b3, a1, a2, a3;
        ToneStack::coeffs (0.5, 0.6, std::pow (10.0, -1.0), b1, b2, b3, a1, a2, a3);
        ThirdOrder probe; probe.set (b1, b2, b3, a1, a2, a3, sr);
        stackMakeup = 1.0f / (float) magAt (probe, 300.0, sr);
        reset();
        init = false;
    }
    void reset()
    {
        for (auto* b : { &couple1, &couple2, &miller1, &miller2, &cHp, &cPeak, &cLp, &detail, &ultraLo1, &ultraLo2, &ultraHi,
                         &tapePre, &tapeDe, &bump, &tapeLp, &growlPk, &growlDe, &cab, &cab2, &midFill })
            b->reset();
        tri1.reset(); tri2.reset(); ovs.reset(); dry.reset(); cSat.reset(); tapeSat.reset(); stack.reset(); line.reset();
        blendS.reset (0.3f); stageS.reset (4.0f);
        env = 0; wowPh = flutPh = 0;
    }
    void update (const Vals& v, float playLevel)
    {
        const float t = V (v, P::PreTreble), m = V (v, P::PreMiddle), b = V (v, P::PreBass);
        if (! init || t != lt || m != lm || b != lb) { stack.set (t, m, b, sr); lt = t; lm = m; lb = b; }
        const bool ulo = Vb (v, P::PreUltraLo), uhi = Vb (v, P::PreUltraHi);
        if (! init || ulo != lulo) { ultraLo1.lowShelf (45.0f, ulo ? 3.0f : 0.0f, sr); ultraLo2.peak (500.0f, 0.8f, ulo ? -6.0f : 0.0f, sr); lulo = ulo; }
        if (! init || uhi != luhi) { ultraHi.highShelf (5500.0f, uhi ? 6.0f : 0.0f, sr, 0.8f); luhi = uhi; }
        const float mf = V (v, P::PreMidFreq), mp = V (v, P::PreMid), det = V (v, P::PreDetail);
        if (! init || mf != lmf || mp != lmp) { cPeak.peak (mf, 1.1f, 10.0f * mp, sr); lmf = mf; lmp = mp; }
        if (! init || det != ldet) { detail.peak (3000.0f, 0.7f, det, sr); ldet = det; }
        {   // growl: the valve breaks up in the low mids, and the top is rolled off like a cab
            const float gg = V (v, P::PreGain);
            if (! init || gg != lgrowl)
            {
                growlPk.peak (600.0f, 0.7f, 6.0f + 6.0f * gg, sr);      // the low mids hit the valve hardest
                growlDe.peak (600.0f, 0.7f, -(3.0f + 3.0f * gg), sr);   // half taken back after, so it leans, not honks
                midFill.peak (550.0f, 0.8f, 10.0f, sr);                // fills the passive stack's scoop: an amp, not a hi-fi
                const float fc = 6000.0f - 5000.0f * gg;                // the cab: darker as it's pushed
                cab.lowPass (fc, 0.54f, sr); cab2.lowPass (fc, 1.31f, sr);
                lgrowl = gg;
            }
        }
        const float tp = V (v, P::PreTape);
        if (! init || tp != ltape)
        {
            bump.peak (75.0f, 1.2f, 2.0f * tp, sr);                   // the head bump
            tapeLp.lowPass (18000.0f - 8000.0f * tp, 0.6f, sr);       // tape's gentle top end
            ltape = tp;
        }
        init = true;

        gain = V (v, P::PreGain);
        drive1 = dbToGain (-14.0f + 40.0f * gain);
        drive2 = 1.0f + 2.0f * gain;
        bias1 = 0.04f + 0.16f * gain;
        bias2 = 0.03f + 0.10f * gain;
        {
            const float gi = gain * (kCal - 1);
            const int i0 = std::min (kCal - 2, (int) gi);
            makeup = calTable[i0] + (calTable[i0 + 1] - calTable[i0]) * (gi - (float) i0);
        }
        sag = V (v, P::PreSag);
        cDrive = dbToGain (6.0f + 22.0f * V (v, P::PreDrive));
        blendS.set (V (v, P::PreBlend));
        tape = tp;
        wow = V (v, P::PreWow);
        level = dbToGain (V (v, P::PreLevel));
        // gain staging: your playing peaks land around -12 dBFS at the valve, whatever the
        // interface gain (up to 24 dB of lift for a quiet input, never more)
        stageS.set (clampf (0.25f / std::max (playLevel, 1.0e-4f), 0.5f, 16.0f));
    }

    // x: the full sound path. on: the preamp's on/off fade. Always kLatency samples late,
    // on or off, so the full sound stays lined up with the bottom end.
    static constexpr int kLatency = Oversampler2x::kLatency;
    float process (float x, float on)
    {
        dry.push (x);
        const float xd = dry.at (kLatency);
        if (on < 1.0e-4f) { stageS.next(); return xd; }
        return xd + (wet (x, xd) - xd) * on;
    }

private:
    float valveAt2x (float in)
    {
        const float ax = std::fabs (in);
        env += (ax - env) * (ax > env ? envA : envR);
        const float push = std::min (1.0f, env * drive1);
        const float b1 = bias1 - 0.3f * sag * push, b2 = bias2 - 0.2f * sag * push;
        float v1 = tri1.process (in * drive1 + b1) - Triode::f (b1);
        v1 = miller1.process (couple1.process (v1));
        float v2 = tri2.process (v1 * drive2 + b2) - Triode::f (b2);
        return miller2.process (couple2.process (v2));
    }

    float wet (float x, float xd)
    {
        const float sg = stageS.next();

        // ---- valve: two triode stages at twice the rate, bias shifting as you dig in ----
        float u0, u1;
        ovs.up (growlPk.process (x * sg), u0, u1);
        const float o0 = valveAt2x (u0), o1 = valveAt2x (u1);
        float valve = ovs.down (o0, o1) / (drive1 * drive2) * makeup;
        valve = cab2.process (cab.process (growlDe.process (valve)));
        valve = midFill.process (stack.process (valve)) * stackMakeup;
        valve = ultraHi.process (ultraLo2.process (ultraLo1.process (valve)));
        const float in = xd * sg;   // the Cut voice works on the same, lined-up signal

        // ---- Cut: the modern voice, tight and clear, in parallel ----
        float c = cPeak.process (cHp.process (in));
        c = cSat.process (c * cDrive) / cDrive * 1.25f;
        c = cLp.process (c);

        const float bl = blendS.next();
        float y = (1.0f - bl) * valve + bl * c;

        // ---- tape: pre-emphasis, saturation, de-emphasis, head bump, wow and flutter ----
        if (tape > 0.001f)
        {
            const float tg = 1.0f + 4.0f * tape;
            float s = tapePre.process (y);
            s = tapeSat.process (s * tg) / tg;
            s = tapeLp.process (bump.process (tapeDe.process (s)));
            y = y + (s - y) * std::min (1.0f, 2.0f * tape);
        }
        if (wow > 0.001f)
        {
            // slow wow and a faint flutter, in fractions of a millisecond: felt, not heard
            wowPh += 0.55f / sr; if (wowPh >= 1) wowPh -= 1;
            flutPh += 6.3f / sr; if (flutPh >= 1) flutPh -= 1;
            line.push (y);
            const float ms = 0.6f + wow * (0.35f * std::sin (kTwoPi * wowPh) + 0.04f * std::sin (kTwoPi * flutPh));
            const float w = line.hermite (ms * 0.001f * sr);
            y = y + (w - y) * std::min (1.0f, 3.0f * wow);
        }
        return detail.process (y) * level / sg;
    }

    // How much level each Gain setting loses to the valve's compression, measured once on a
    // low note at the staged level, so turning Gain changes the tone, not the volume.
    void calibrate()
    {
        for (int k = 0; k < kCal; ++k)
        {
            gain = (float) k / (kCal - 1);
            drive1 = dbToGain (-14.0f + 40.0f * gain); drive2 = 1.0f + 2.0f * gain;
            bias1 = 0.04f + 0.16f * gain; bias2 = 0.03f + 0.10f * gain; sag = 0.4f;
            tri1.reset(); tri2.reset(); ovs.reset(); env = 0;
            for (auto* b : { &couple1, &couple2, &miller1, &miller2 }) b->reset();
            double si = 0, so = 0;
            const int n = (int) (0.4f * sr);
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.25f * std::sin (kTwoPi * 82.4f * (float) i / sr);
                float u0, u1;
                ovs.up (x, u0, u1);
                const float o0 = valveAt2x (u0), o1 = valveAt2x (u1);
                const float y = ovs.down (o0, o1) / (drive1 * drive2);
                if (i > n / 2) { si += (double) x * x; so += (double) y * y; }
            }
            calTable[k] = 0.71f * (float) std::sqrt (si / std::max (1.0e-12, so));   // sits level with the preamp off
        }
        tri1.reset(); tri2.reset(); ovs.reset(); env = 0; gain = 0;
        for (auto* b : { &couple1, &couple2, &miller1, &miller2 }) b->reset();
    }

    static double magAt (const ThirdOrder& f, double hz, double sr)
    {
        const double w = 2.0 * M_PI * hz / sr;
        std::complex<double> z = std::polar (1.0, -w), num = 0, den = 0, zp = 1;
        for (int i = 0; i < 4; ++i) { num += f.b[i] * zp; den += f.a[i] * zp; zp *= z; }
        return std::abs (num / den);
    }

    static constexpr int kCal = 11;
    float calTable[kCal] {}, makeup = 1, bias1 = 0.04f, bias2 = 0.03f;
    Oversampler2x ovs;
    DelayLine dry;
    float sr = 48000, gain = 0, drive1 = 1, drive2 = 1, sag = 0, cDrive = 1, tape = 0, wow = 0, level = 1;
    float env = 0, envA = 0.01f, envR = 0.001f, stackMakeup = 1, wowPh = 0, flutPh = 0;
    float lgrowl = -1, lt = -1, lm = -1, lb = -1, lmf = -1, lmp = -1, ldet = -99, ltape = -1;
    bool lulo = false, luhi = false, init = false;
    Triode tri1, tri2;
    ToneStack stack;
    Biquad couple1, couple2, miller1, miller2, cHp, cPeak, cLp, detail, ultraLo1, ultraLo2, ultraHi, tapePre, tapeDe, bump, tapeLp, growlPk, growlDe, cab, cab2, midFill;
    AdaaTanh cSat, tapeSat;
    DelayLine line;
    Smoothed blendS, stageS;
};

// Output iron: a transformer after the three paths are mixed. Iron saturates on the lows
// first, so the low end thickens and its peaks soften, with a slight lean to even harmonics.
class OutputIron
{
public:
    void prepare (float sr)
    {
        lp1.lowPass (140.0f, 0.6f, sr);
        top.lowPass (19000.0f, 0.6f, sr);
        reset();
    }
    void reset() { lp1.reset(); top.reset(); sat.reset(); }
    void set (float amount) { amt = clampf (amount, 0, 1); }
    float process (float x)
    {
        if (amt < 0.001f) return x;
        const float lo = lp1.process (x), hi = x - lo;
        const float g = 1.0f + 5.0f * amt, b = 0.08f * amt;
        const float sl = (sat.process (lo * g + b) - std::tanh (b)) / g;
        return top.process (hi + lo + (sl - lo) * std::min (1.0f, 1.5f * amt));
    }

private:
    float amt = 0;
    Biquad lp1, top;
    AdaaTanh sat;
};
} // namespace ks
