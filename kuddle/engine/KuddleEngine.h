#pragma once
// Kuddle: Kevin's home rig for clean, warm bass (hip-hop, soul and jazz).
//
//   in -> gate -> Mute -> Auto wah -> Valve DI -> Opto -> Tape -> Tone -> Melt roll-off -> Room -> out
//
// Plain C++ with no platform code, built on Keystone's DSP helpers (core/src/Dsp.h), so the
// same engine runs in the VST3 now and in the Keystone app (and the Pi) later.
// Mono in, stereo out: only the Room spreads. Nothing allocates after prepare().
//
// The Melt sweep (the expression pedal) is the opposite of Keystone's Bloom: heel is open
// and clear, towards the toe the mute closes in, the tape pushes harder and the treble rolls
// off. Each part's Reach sets how far it moves at the toe.

#include "Dsp.h"
#include <array>

namespace kud
{
using namespace ks;

// ---------------------------------------------------------------------------------------
// Everything the engine reads, in plain units. The plugin fills this once per block.
struct Params
{
    // input
    float inGainDb = 0, gateDb = -70;          // gate at -90 or below is off
    // Mute
    bool muteOn = true;
    float foam = 0.4f, thump = 0.55f;
    int strings = 2;                            // 0 new, 1 worn, 2 old
    // Auto wah
    bool wahOn = true, wahManual = false;
    float wahSense = 0.55f, wahSpeed = 0.3f, wahPeak = 0.4f, wahPos = 0.5f;
    int wahRange = 0;                           // 0 low, 1 mid
    // Valve DI (always on)
    float diDrive = 0.3f, diAir = 0.2f, diSag = 0.35f, diLowCut = 30.0f;
    // Opto
    bool optoOn = true, optoFast = false, squeeze = false;
    float optoSqueeze = 0.45f, optoBlend = 0.8f, optoHoldDb = 6.0f;
    // Tape
    bool tapeOn = true;
    float tapePush = 0.35f, tapeWow = 0.1f, tapeFlutter = 0.15f, tapeBump = 0.4f, tapeHiss = 0.0f;
    // Tone
    bool toneOn = true;
    float bassDb = 2, bassHz = 80, midsDb = 1, midsHz = 400, trebleKHz = 3.2f;
    // Room
    bool roomOn = false;
    int roomType = 0;                           // 0 dark room, 1 spring
    float roomSize = 0.3f, roomDecay = 0.8f, roomPreMs = 10.0f, roomMix = 0.2f;
    // Melt and its reach
    float melt = 0.0f;                          // 0 heel .. 1 toe
    bool meltWorksWah = true;                   // with the wah in manual, the pedal works the wah instead
    float reachMute = 0.6f, reachTape = 0.5f, reachTreble = 0.7f, reachRoom = 0.0f;
    // output
    float outDb = -3.0f;
    bool mute = false;                          // tuner: silence the output
};

// ---------------------------------------------------------------------------------------
// How loud Kevin is playing, learnt from note peaks (the Quack and Speckle lessons):
// halfway towards a louder note, a third of the way towards a softer one, holding
// through silence. A swell's peak is where it levels off.
struct PlayLevel
{
    float level = 0.05f, riseMax = 0, env = 0, holdLeft = 0;
    float att = 0, rel = 0, holdSamples = 0, stallSamples = 0, sinceRise = 0;
    bool rising = false;
    void prepare (float sr, float holdSec = 0.045f)
    {
        att = onePoleCoef (0.002f, sr);
        rel = onePoleCoef (0.12f, sr);
        holdSamples = holdSec * sr;
        stallSamples = 0.25f * sr;
        reset();
    }
    void reset() { level = 0.05f; riseMax = env = holdLeft = sinceRise = 0; rising = false; }
    float process (float x)
    {
        const float a = std::fabs (x);
        if (a >= env) { env = a + (env - a) * att; holdLeft = holdSamples; }
        else if (holdLeft > 0) holdLeft -= 1;
        else env = a + (env - a) * rel;
        env = undenorm (env);
        if (env > riseMax * 1.02f && env > 1.0e-4f) { riseMax = env; rising = true; sinceRise = 0; }
        else if (rising)
        {
            sinceRise += 1;
            if (env < riseMax * 0.7f || sinceRise > stallSamples) { learn (riseMax); rising = false; riseMax = env; }
        }
        else riseMax = std::min (riseMax, env + 1.0e-6f);
        return env;
    }
    void learn (float peak)
    {
        if (peak < 3.0e-4f) return;
        level += (peak - level) * (peak > level ? 0.5f : 0.333f);
    }
};

// ---------------------------------------------------------------------------------------
// Gate: a gentle downward expander, so hum between notes drops away without chopping tails.
class Gate
{
public:
    void prepare (float sr) { att = onePoleCoef (0.001f, sr); rel = onePoleCoef (0.15f, sr); gGlide = onePoleCoef (0.01f, sr); reset(); }
    void reset() { env = 0; g = 1; }
    void set (float db) { thr = db <= -89.5f ? 0.0f : dbToGain (db); }
    float process (float x)
    {
        if (thr <= 0.0f) return x;
        const float a = std::fabs (x);
        env = undenorm (a > env ? a + (env - a) * att : a + (env - a) * rel);
        const float want = env >= thr ? 1.0f : std::pow (env / thr, 2.0f);   // 1:3 below the threshold
        g = want + (g - want) * gGlide;
        return x * g;
    }

private:
    float thr = 0, env = 0, g = 1, att = 0, rel = 0, gGlide = 0;
};

// ---------------------------------------------------------------------------------------
// Mute: flatwounds and a foam mute. Each note opens briefly then closes down, highs die
// fast, sustain is shortened (peaks kept, tails pulled in), and Thump adds the low-mid push
// a foam mute leaves behind.
class Mute
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        det.prepare (sr, 0.045f);
        slowCoef = onePoleCoef (0.03f, sr);
        slowAtt = onePoleCoef (0.002f, sr);
        onS.setTime (0.02f, sr);
        cutGlide = onePoleCoef (0.003f, sr);
        reset();
    }
    void reset() { lp1.reset(); lp2.reset(); thumpF.reset(); det.reset(); open = 0; peak = 0; g = 1; onS.reset (1); cut = cutA = 3000; eSlow = 0; }
    void update (const Params& p, float meltFoam)
    {
        foam = clampf (meltFoam, 0, 1);
        const float strMul = p.strings == 0 ? 1.0f : (p.strings == 1 ? 0.72f : 0.52f);
        baseCut = lerp (7000.0f, 650.0f, std::pow (foam, 0.8f)) * strMul;
        openDecay = onePoleCoef (p.strings == 0 ? 0.25f : (p.strings == 1 ? 0.12f : 0.06f) * (1.0f - 0.5f * foam), sr);
        if (std::fabs (p.thump - lastThump) > 1.0e-4f) { thumpF.peak (120.0f, 0.9f, 5.0f * p.thump, sr); lastThump = p.thump; }
        sustainPow = 0.6f * foam;
        peakRel = onePoleCoef (lerp (2.5f, 0.6f, foam), sr);
        onS.set (p.muteOn ? 1.0f : 0.0f);
    }
    float process (float x)
    {
        const float on = onS.next();

        // a new note: the held envelope (45 ms, so a low B never ripples it) rises well
        // above where it was 30 ms ago
        const float e = det.process (x);
        const float lvl = std::max (det.level, 1.0e-4f);
        eSlow = undenorm (e + (eSlow - e) * slowCoef);
        const float attack = clampf ((e - eSlow) / lvl * 3.0f, 0.0f, 1.0f);
        open = std::max (open * openDecay, attack);
        const float wantCut = clampf (baseCut * (1.0f + 1.6f * open), 80.0f, 16000.0f);
        // two glide stages, so a jump in the cut-off never clicks (Quack lesson)
        cutA = wantCut + (cutA - wantCut) * cutGlide;
        cut = cutA + (cut - cutA) * cutGlide;
        if ((counter++ & 3) == 0) { lp1.lowPass (cut, 0.55f, sr); lp2.lowPass (cut * 1.6f, 0.5f, sr); }
        float y = lp2.process (lp1.process (x));
        y = thumpF.process (y);

        // sustain: pull the tail down against the note's own peak, never the peak itself
        peak = e > peak ? e + (peak - e) * slowAtt : e + (peak - e) * peakRel;
        peak = undenorm (peak);
        const float ratio = peak > 1.0e-6f ? clampf (e / peak, 1.0e-3f, 1.0f) : 1.0f;
        const float want = std::pow (ratio, sustainPow);
        g = want + (g - want) * 0.999f;
        y *= g;
        return x + (y - x) * on;
    }

private:
    float sr = 48000, foam = 0.4f, baseCut = 3000, cut = 3000, cutA = 3000, cutGlide = 0, open = 0, openDecay = 0, eSlow = 0, slowCoef = 0;
    float peak = 0, peakRel = 0, slowAtt = 0, sustainPow = 0, g = 1, lastThump = -1;
    unsigned counter = 0;
    Biquad lp1, lp2, thumpF;
    PlayLevel det;
    Smoothed onS;
};

// ---------------------------------------------------------------------------------------
// Auto wah: Quack's engine with a darker, vocal voicing. The sweep follows each note
// against the playing level, glides through two one-pole stages, and the lows below
// 120 Hz stay dry (LR4 split), so the bottom never thins out.
class AutoWah
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        det.prepare (sr, 0.045f);
        glide = onePoleCoef (0.012f, sr);
        manGlide = onePoleCoef (0.015f, sr);   // pedal steps glide (expression lesson)
        envAtt = onePoleCoef (0.002f, sr);
        envRel = onePoleCoef (0.6f, sr);
        makeupGlide = 1.0f - onePoleCoef (0.25f, sr);
        split.set (120.0f, sr);
        splitW.set (120.0f, sr);
        onS.setTime (0.02f, sr);
        reset();
    }
    void reset() { svf.reset(); det.reset(); split.reset(); splitW.reset(); env = 0; glideA = glideB = std::log (500.0f); manPos = 0.5f; onS.reset (1); makeup = 2.0f; inEnv = wetEnv = 0; }
    void update (const Params& p, float pedal, bool pedalDrives)
    {
        manual = p.wahManual;
        sens = p.wahSense;
        lo = p.wahRange == 0 ? 250.0f : 400.0f;
        hi = p.wahRange == 0 ? 1300.0f : 2100.0f;
        res = p.wahPeak;
        pos = manual && pedalDrives ? pedal : p.wahPos;
        relCoef = onePoleCoef (0.05f + 0.45f * (1.0f - p.wahSpeed), sr);
        onS.set (p.wahOn ? 1.0f : 0.0f);
    }
    float process (float x)
    {
        const float on = onS.next();
        const float e = det.process (x);
        env = e > env ? e : e + (env - e) * relCoef;
        float pp;
        if (manual) { manPos = pos + (manPos - pos) * manGlide; pp = manPos; }
        else
        {
            const float rel = env / std::max (det.level, 1.0e-4f);
            pp = clampf (std::pow (rel, 0.6f) * (0.35f + 1.1f * sens), 0.0f, 1.0f);
        }
        const float target = std::log (lo) + (std::log (hi) - std::log (lo)) * pp;
        glideA = target + (glideA - target) * glide;
        glideB = glideA + (glideB - glideA) * glide;
        if ((counter++ & 7) == 0) svf.set (std::exp (glideB), 0.25f + 0.65f * res, sr);
        float l, b;
        svf.process (x, l, b);
        // dark and vocal: mostly the low-pass with the peak sung on top
        float wet = 0.75f * l + b * (0.5f + 0.9f * res);
        const float ax = std::fabs (x), aw = std::fabs (wet);
        inEnv = ax > inEnv ? ax + (inEnv - ax) * envAtt : ax + (inEnv - ax) * envRel;
        wetEnv = aw > wetEnv ? aw + (wetEnv - aw) * envAtt : aw + (wetEnv - aw) * envRel;
        const float want = inEnv > 1.0e-3f && wetEnv > 1.0e-6f ? clampf (inEnv / wetEnv, 1.0f, 3.0f) : 1.0f;
        makeup += (want - makeup) * makeupGlide;
        wet = std::tanh (wet * makeup * 1.3f) / 1.3f;
        // keep the lows dry
        float dLo, dHi, wLo, wHi;
        split.split (x, dLo, dHi);
        splitW.split (wet, wLo, wHi);
        const float y = dLo + wHi;
        return x + (y - x) * on;
    }
    float position01() const { return (glideB - std::log (lo)) / std::max (1.0e-3f, std::log (hi) - std::log (lo)); }

private:
    float sr = 48000;
    Svf svf;
    PlayLevel det;
    LR4 split, splitW;
    float env = 0, relCoef = 0, glide = 0, manGlide = 0, glideA = 0, glideB = 0, manPos = 0.5f;
    bool manual = false;
    float sens = 0.55f, lo = 250, hi = 1300, res = 0.4f, pos = 0.5f;
    float inEnv = 0, wetEnv = 0, makeup = 2.0f, envAtt = 0, envRel = 0, makeupGlide = 0;
    unsigned counter = 0;
    Smoothed onS;
};

// ---------------------------------------------------------------------------------------
// Valve DI: one triode stage, 2x oversampled with antiderivative anti-aliasing, hit at the
// same level whatever the interface gain (learnt from note peaks), with the bias shifting
// as you dig in (Sag). Then a coupling cap, Air (a gentle top shelf) and the output
// transformer's soft top. The level is put back afterwards, gliding (Keystone lesson).
class ValveDI
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        det.prepare (sr, 0.045f);
        couple.highPass (16.0f, 0.6f, sr * 2.0f);
        top.lowPass (14000.0f, 0.6f, sr);
        envA = onePoleCoef (0.005f, sr);
        envR = onePoleCoef (0.25f, sr);
        inRms = outRms = 1.0e-4f;
        rmsCoef = onePoleCoef (0.3f, sr);
        trimGlide = onePoleCoef (0.08f, sr);
        driveS.setTime (0.05f, sr);
        normS.setTime (0.06f, sr);
        reset();
    }
    void reset() { normS.reset (0.35f / 0.05f); ovs.reset(); tri.reset(); couple.reset(); lowCut.reset(); air.reset(); top.reset(); det.reset(); env = 0; trim = 1; inRms = outRms = 1.0e-4f; driveS.reset (drive); }
    void update (const Params& p)
    {
        if (std::fabs (p.diLowCut - lastCut) > 0.01f) { lowCut.highPass (p.diLowCut, 0.6f, sr); lastCut = p.diLowCut; }
        if (std::fabs (p.diAir - lastAir) > 1.0e-4f) { air.highShelf (4500.0f, 7.0f * p.diAir - 1.0f, sr, 0.7f); lastAir = p.diAir; }
        drive = p.diDrive;
        driveS.set (drive);
        sag = p.diSag;
    }
    float process (float x)
    {
        x = lowCut.process (x);
        det.process (x);
        // hit the valve the same way whatever the input level: a typical note peaks at 0.35
        // the learnt level moves in steps (one per note), so glide the gain it sets
        normS.set (0.35f / std::max (det.level, 2.0e-3f));
        const float norm = normS.next();
        const float d = driveS.next();
        const float g = norm * (0.6f + 5.4f * d * d);
        const float a = std::fabs (x);
        env = undenorm (a > env ? a + (env - a) * envA : a + (env - a) * envR);
        const float bias = 0.05f + 0.25f * sag * clampf (env * norm, 0.0f, 1.5f);
        float u0, u1;
        ovs.up (x * g, u0, u1);
        const float b0 = Triode::f (-bias);
        u0 = couple.process (tri.process (u0 - bias) - b0);
        u1 = couple.process (tri.process (u1 - bias) - b0);
        float y = ovs.down (u0, u1);
        y = top.process (air.process (y));
        // level match on RMS, gliding (never stepped)
        const float xd = delayIn (x);
        inRms = undenorm (xd * xd + (inRms - xd * xd) * rmsCoef);
        outRms = undenorm (y * y + (outRms - y * y) * rmsCoef);
        const float want = inRms > 1.0e-9f && outRms > 1.0e-12f ? clampf (std::sqrt (inRms / outRms), 1.0e-4f, 20.0f) : trim;
        trim = want + (trim - want) * trimGlide;
        // a touch louder as the drive comes up, the way a valve feels
        return y * trim * (1.0f + 0.25f * d);
    }
    static constexpr int kLatency = Oversampler2x::kLatency;

private:
    float delayIn (float x) { hist[hp] = x; hp = (hp + 1) % (kLatency + 1); return hist[hp]; }
    float sr = 48000, drive = 0.3f, sag = 0.35f, env = 0, envA = 0, envR = 0, lastCut = -1, lastAir = -1;
    float inRms = 0, outRms = 0, rmsCoef = 0, trim = 1, trimGlide = 0;
    float hist[kLatency + 1] {};
    int hp = 0;
    PlayLevel det;
    Oversampler2x ovs;
    Triode tri;
    Biquad couple, lowCut, air, top;
    Smoothed driveS, normS;
};

// ---------------------------------------------------------------------------------------
// Opto: a slow, smooth leveller in the style of an optical compressor. The threshold sits
// under the playing level (learnt), the knee is wide, and the release is programme
// dependent: short notes recover quickly, long squeezes recover slowly, like a light cell
// that holds its glow. Blend mixes the dry note back in. Squeeze (hold D) digs deeper.
class Opto
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        det.prepare (sr, 0.045f);
        detLp.setLowPass (15.0f, sr);
        chargeUp = onePoleCoef (0.8f, sr);
        relCoef = onePoleCoef (0.05f, sr);
        chargeDown = onePoleCoef (3.0f, sr);
        gainGlide = onePoleCoef (0.004f, sr);
        onS.setTime (0.03f, sr);
        blendS.setTime (0.03f, sr);
        squeezeS.setTime (0.08f, sr);
        makeupS.setTime (0.4f, sr);
        reset();
    }
    void reset() { det.reset(); env = 0; gr = 0; charge = 0; gainDb = 0; onS.reset (1); blendS.reset (0.8f); squeezeS.reset (0); makeupS.reset (0); }
    void update (const Params& p)
    {
        amount = p.optoSqueeze;
        attack = onePoleCoef (p.optoFast ? 0.003f : 0.010f, sr);
        relFast = p.optoFast ? 0.04f : 0.07f;
        relSlow = p.optoFast ? 0.8f : 2.2f;
        holdDb = p.optoHoldDb;
        onS.set (p.optoOn ? 1.0f : 0.0f);
        blendS.set (p.optoBlend);
        squeezeS.set (p.squeeze && p.optoOn ? 1.0f : 0.0f);
    }
    float process (float x)
    {
        det.process (x);
        const float on = onS.next(), sq = squeezeS.next(), bl = blendS.next();
        const float p = x * x;
        env = undenorm (p > env ? p + (env - p) * attack : p + (env - p) * relCoef);
        const float lvl = 10.0f * std::log10 (env + 1.0e-12f);
        const float play = gainToDb (std::max (det.level, 1.0e-4f)) - 3.0f;   // a typical note's RMS-ish level
        const float thr = play - 2.0f - 16.0f * amount - holdDb * sq;
        const float ratio = 2.0f + 3.0f * amount + 4.0f * sq;
        const float over = lvl - thr, knee = 10.0f;
        float want = 0;
        if (over > knee * 0.5f) want = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) want = (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee) * (1.0f - 1.0f / ratio);
        // the light cell: attack follows quickly, release slows the longer it has glowed
        if (want > gr) { gr = want + (gr - want) * attack; charge = 1.0f + (charge - 1.0f) * chargeUp; }
        else
        {
            const float rel = onePoleCoef (lerp (relFast, relSlow, charge), sr);
            gr = want + (gr - want) * rel;
            charge = charge * chargeDown;
        }
        // make-up follows the average squeeze slowly, so the level stays put
        makeupS.set ((0.65f + 0.3f * sq) * gr);   // holding D squeezes harder without dropping the level much
        const float mk = makeupS.next();
        const float gDb = (mk - gr) * on;
        gainDb = gDb + (gainDb - gDb) * gainGlide;
        grShown = gr * on;
        const float wet = x * dbToGain (gainDb);
        const float b = bl * on;
        return x * (1.0f - b) + wet * b;
    }
    float reductionDb() const { return grShown; }

private:
    float sr = 48000, amount = 0.45f, holdDb = 6, attack = 0, relFast = 0.07f, relSlow = 2.2f, relCoef = 0.999f;
    float env = 0, gr = 0, grShown = 0, charge = 0, chargeUp = 0, chargeDown = 0, gainDb = 0, gainGlide = 0;
    PlayLevel det;
    OnePole detLp;
    Smoothed onS, blendS, squeezeS, makeupS;
};

// ---------------------------------------------------------------------------------------
// Tape: pre-emphasis, soft saturation, de-emphasis, head bump, a little wow and flutter
// and optional hiss. The wow and flutter run off a 1 ms centre delay that is always
// there, so switching Tape on or off never changes the latency.
class Tape
{
public:
    static constexpr int kCentre = 48;   // samples at 48 kHz (scaled in prepare)

    void prepare (float sr_)
    {
        sr = sr_;
        centre = std::max (8, (int) std::lround (0.001f * sr));
        line.prepare (centre * 2 + 8);
        pre.highShelf (2500.0f, 6.0f, sr, 0.7f);
        de.highShelf (2500.0f, -6.0f, sr, 0.7f);
        onS.setTime (0.02f, sr);
        pushS.setTime (0.05f, sr);
        reset();
    }
    int latency() const { return centre; }
    void reset() { line.reset(); pre.reset(); de.reset(); bump.reset(); hf.reset(); sat.reset(); hissLp.reset(); onS.reset (1); wowPh = flutPh = 0; pushS.reset (push); }
    void update (const Params& p, float meltPush)
    {
        push = clampf (meltPush, 0, 1);
        pushS.set (push);
        wow = p.tapeWow;
        flutter = p.tapeFlutter;
        hiss = p.tapeHiss;
        if (std::fabs (p.tapeBump - lastBump) > 1.0e-4f) { bump.peak (90.0f, 1.1f, 4.0f * p.tapeBump, sr); lastBump = p.tapeBump; }
        const float hfCut = lerp (16000.0f, 8500.0f, push);
        if (std::fabs (hfCut - lastHf) > 20.0f) { hf.lowPass (hfCut, 0.6f, sr); lastHf = hfCut; }
        onS.set (p.tapeOn ? 1.0f : 0.0f);
        hissLp.setLowPass (6000.0f, sr);
    }
    float process (float x)
    {
        const float on = onS.next();
        line.push (x);
        // wow 0.55 Hz up to 0.8 ms, flutter 6.7 Hz up to 0.08 ms
        wowPh += 0.55f / sr; if (wowPh >= 1.0f) wowPh -= 1.0f;
        flutPh += 6.7f / sr; if (flutPh >= 1.0f) flutPh -= 1.0f;
        const float scale = sr / 48000.0f;
        const float mod = on * (wow * 38.0f * std::sin (kTwoPi * wowPh) + flutter * 3.8f * std::sin (kTwoPi * flutPh)) * scale;
        const float d = (float) centre + clampf (mod, -(float) centre + 2.0f, (float) centre - 2.0f);
        const float xd = line.hermite (d);

        const float pu = pushS.next();
        const float drive = 0.6f + 3.4f * pu * pu;
        float y = pre.process (xd);
        y = sat.process (y * drive) / drive * (1.0f + 0.35f * pu);   // unity for small signals, rounder peaks
        y = de.process (y);
        y = bump.process (y);
        y = hf.process (y);
        if (hiss > 0.001f) y += hissLp.lp (rng.bi()) * hiss * 0.0012f;
        return xd + (y - xd) * on;
    }

private:
    float sr = 48000, push = 0.35f, wow = 0.1f, flutter = 0.15f, hiss = 0, lastBump = -1, lastHf = -1, wowPh = 0, flutPh = 0;
    int centre = kCentre;
    DelayLine line;
    Biquad pre, de, bump, hf;
    AdaaTanh sat;
    OnePole hissLp;
    Rng rng;
    Smoothed onS, pushS;
};

// ---------------------------------------------------------------------------------------
// Tone: bass shelf, mids peak and a treble roll-off, all where you put them in hertz.
class Tone
{
public:
    void prepare (float sr_) { sr = sr_; onS.setTime (0.02f, sr); reset(); }
    void reset() { lowS.reset(); mid.reset(); lp.reset(); onS.reset (1); }
    void update (const Params& p)
    {
        if (p.bassDb != lb || p.bassHz != lbh) { lowS.lowShelf (p.bassHz, p.bassDb, sr, 0.8f); lb = p.bassDb; lbh = p.bassHz; }
        if (p.midsDb != lm || p.midsHz != lmh) { mid.peak (p.midsHz, 0.9f, p.midsDb, sr); lm = p.midsDb; lmh = p.midsHz; }
        if (p.trebleKHz != lt) { lp.lowPass (p.trebleKHz * 1000.0f, 0.6f, sr); lt = p.trebleKHz; }
        onS.set (p.toneOn ? 1.0f : 0.0f);
    }
    float process (float x)
    {
        const float on = onS.next();
        const float y = lp.process (mid.process (lowS.process (x)));
        return x + (y - x) * on;
    }

private:
    float sr = 48000, lb = -99, lbh = -1, lm = -99, lmh = -1, lt = -1;
    Biquad lowS, mid, lp;
    Smoothed onS;
};

// ---------------------------------------------------------------------------------------
// Room: a small dark room (four-line feedback delay network, damped hard) or a short
// spring (dispersive allpass chains in a feedback loop, so it chirps and boings).
// The dry note stays mono, the reflections spread a little left and right.
class Room
{
public:
    void prepare (float sr_)
    {
        sr = sr_;
        pre.prepare ((int) (0.07f * sr) + 4);
        for (auto& l : lines) l.prepare ((int) (0.09f * sr) + 4);
        for (auto& s : spring) s.prepare ((int) (0.09f * sr) + 4);
        mixS.setTime (0.03f, sr);
        sizeS.setTime (0.2f, sr);
        inHp.highPass (110.0f, 0.6f, sr);   // keep the room off the bottom octave
        reset();
    }
    void reset()
    {
        pre.reset();
        for (auto& l : lines) l.reset();
        for (auto& s : spring) s.reset();
        for (auto& d : damp) d.reset();
        for (auto& a : ap) a = {};
        inHp.reset();
        mixS.reset (0);
    }
    void update (const Params& p, float meltRoom)
    {
        type = p.roomType;
        size = p.roomSize;
        sizeS.set (size);
        preSamples = clampf (p.roomPreMs, 0.0f, 60.0f) * 0.001f * sr;
        decay = clampf (p.roomDecay, 0.15f, 4.0f);
        const float mix = p.roomOn ? clampf (p.roomMix + meltRoom, 0.0f, 1.0f) : 0.0f;
        mixS.set (mix);
        const float dampHz = type == 0 ? lerp (3200.0f, 1800.0f, size) : 4200.0f;
        for (auto& d : damp) d.setLowPass (dampHz, sr);
    }
    void process (float x, float& l, float& r)
    {
        const float m = mixS.next();
        if (m < 1.0e-4f && mixS.settled() && idle > sr * 5.0f) { l = r = x; return; }
        idle = m < 1.0e-4f ? idle + 1.0f : 0.0f;
        pre.push (inHp.process (x));
        const float in = pre.frac (std::max (1.0f, preSamples));
        const float sz = sizeS.next();
        float wl = 0, wr = 0;
        if (type == 0) dark (in, sz, wl, wr);
        else springy (in, sz, wl, wr);
        l = x + wl * m;
        r = x + wr * m;
    }

private:
    void dark (float in, float sz, float& wl, float& wr)
    {
        static constexpr float base[4] = { 0.0131f, 0.0173f, 0.0211f, 0.0257f };
        float d[4];
        for (int i = 0; i < 4; ++i)
        {
            const float len = base[i] * (0.6f + 1.4f * sz) * sr;
            d[i] = lines[(size_t) i].frac (len);
            const float g = std::pow (10.0f, -3.0f * (len / sr) / decay);
            d[i] = damp[(size_t) i].lp (d[i]) * g;
        }
        // Householder mix
        const float s = 0.5f * (d[0] + d[1] + d[2] + d[3]);
        for (int i = 0; i < 4; ++i) lines[(size_t) i].push (undenorm (d[i] - s + in * 0.5f));
        wl = 0.6f * (d[0] + d[2]);
        wr = 0.6f * (d[1] + d[3]);
    }
    void springy (float in, float sz, float& wl, float& wr)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float len = (c == 0 ? 0.033f : 0.041f) * (0.7f + 0.8f * sz) * sr;
            float y = spring[(size_t) c].frac (len);
            const float g = std::pow (10.0f, -3.0f * (len / sr) / decay);
            // dispersion: a cascade of first-order allpasses chirps the reflections
            for (int k = 0; k < 6; ++k) y = ap[(size_t) (c * 6 + k)].process (y, 0.62f);
            y = damp[(size_t) c].lp (y);
            spring[(size_t) c].push (undenorm (in * 0.7f + y * g));
            (c == 0 ? wl : wr) = y * 0.9f;
        }
    }
    struct Ap1
    {
        float z = 0;
        float process (float x, float a) { const float y = -a * x + z; z = undenorm (x + a * y); return y; }
    };
    float sr = 48000, size = 0.3f, decay = 0.8f, preSamples = 0, idle = 0;
    int type = 0;
    DelayLine pre;
    std::array<DelayLine, 4> lines;
    std::array<DelayLine, 2> spring;
    std::array<OnePole, 4> damp;
    std::array<Ap1, 12> ap {};
    Biquad inHp;
    Smoothed mixS, sizeS;
};

// ---------------------------------------------------------------------------------------
class Engine
{
public:
    void prepare (float sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        gate.prepare (sr);
        mute.prepare (sr);
        wah.prepare (sr);
        di.prepare (sr);
        opto.prepare (sr);
        tape.prepare (sr);
        tone.prepare (sr);
        room.prepare (sr);
        dc.prepare (sr);
        meltS.setTime (0.03f, sr);
        inS.setTime (0.03f, sr);
        outS.setTime (0.03f, sr);
        muteS.setTime (0.01f, sr);
        limAtt = onePoleCoef (0.0003f, sr);
        limRel = onePoleCoef (0.12f, sr);
        reset();
    }
    void reset()
    {
        gate.reset(); mute.reset(); wah.reset(); di.reset(); opto.reset(); tape.reset(); tone.reset(); room.reset(); dc.reset();
        meltLp1.reset(); meltLp2.reset();
        meltS.reset (p.melt); inS.reset (dbToGain (p.inGainDb)); outS.reset (dbToGain (p.outDb)); muteS.reset (1);
        limGain = 1;
        lastMeltCut = -1;
    }
    // total latency in samples: the valve's oversampler and the tape's centre delay
    int latency() const { return ValveDI::kLatency + tape.latency(); }

    void setParams (const Params& np) { p = np; }

    // Mono in (already summed), stereo out.
    void process (const float* in, float* outL, float* outR, int n)
    {
        inS.set (dbToGain (p.inGainDb));
        outS.set (dbToGain (p.outDb));
        muteS.set (p.mute ? 0.0f : 1.0f);
        gate.set (p.gateDb);

        // block-rate Melt for the modules (smoothed inside each). With the wah in manual and
        // the pedal working it, the pedal is the wah and nothing else melts.
        const bool pedalIsWah = p.wahOn && p.wahManual && p.meltWorksWah;
        meltS.set (pedalIsWah ? 0.0f : clampf (p.melt, 0, 1));
        const float mlt = meltS.current();
        mute.update (p, p.foam + p.reachMute * mlt * (1.0f - p.foam));
        wah.update (p, clampf (p.melt, 0, 1), p.meltWorksWah);
        di.update (p);
        opto.update (p);
        tape.update (p, p.tapePush + p.reachTape * mlt * (1.0f - p.tapePush));
        tone.update (p);
        room.update (p, p.reachRoom * mlt * 0.5f);

        float peakIn = 0, peakOut = 0;
        for (int i = 0; i < n; ++i)
        {
            const float m = meltS.next();
            if ((i & 15) == 0)
            {
                // the Melt roll-off: 20 kHz at the heel, down by up to 5 octaves at the toe (1.8 kHz at the default reach)
                const float cut = 20000.0f * std::pow (2.0f, -5.0f * p.reachTreble * m);
                if (std::fabs (cut - lastMeltCut) > 1.0f) { meltLp1.lowPass (std::min (cut, sr * 0.45f), 0.55f, sr); meltLp2.lowPass (std::min (cut * 1.3f, sr * 0.45f), 0.5f, sr); lastMeltCut = cut; }
            }
            float x = in[i] * inS.next();
            peakIn = std::max (peakIn, std::fabs (x));
            x = gate.process (x);
            x = mute.process (x);
            x = wah.process (x);
            x = di.process (x);
            x = opto.process (x);
            x = tape.process (x);
            x = tone.process (x);
            x = meltLp2.process (meltLp1.process (x));
            x = dc.process (x);
            float l, r;
            room.process (x, l, r);
            const float o = outS.next() * muteS.next();
            l *= o; r *= o;
            // limiter at -1 dBFS plus a soft ceiling
            const float a = std::max (std::fabs (l), std::fabs (r));
            const float want = a > 0.891f ? 0.891f / a : 1.0f;
            limGain = want < limGain ? want + (limGain - want) * limAtt : want + (limGain - want) * limRel;
            l = soft (l * limGain); r = soft (r * limGain);
            outL[i] = l; outR[i] = r;
            peakOut = std::max (peakOut, std::max (std::fabs (l), std::fabs (r)));
        }
        lastPeakIn = peakIn;
        lastPeakOut = peakOut;
    }

    // for the panel
    float optoReductionDb() const { return opto.reductionDb(); }
    float wahPosition() const { return wah.position01(); }
    float peakIn() const { return lastPeakIn; }
    float peakOut() const { return lastPeakOut; }
    float meltNow() const { return meltS.current(); }
    const Params& params() const { return p; }

private:
    static float soft (float y)
    {
        const float ay = std::fabs (y);
        return ay > 0.85f ? std::copysign (0.85f + 0.13f * std::tanh ((ay - 0.85f) / 0.13f), y) : y;
    }
    float sr = 48000;
    Params p;
    Gate gate;
    Mute mute;
    AutoWah wah;
    ValveDI di;
    Opto opto;
    Tape tape;
    Tone tone;
    Room room;
    DcBlock dc;
    Biquad meltLp1, meltLp2;
    float lastMeltCut = -1, limGain = 1, limAtt = 0, limRel = 0, lastPeakIn = 0, lastPeakOut = 0;
    Smoothed meltS, inS, outS, muteS;
};
} // namespace kud
