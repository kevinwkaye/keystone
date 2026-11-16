#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Kuddle windows can be open at once. Footswitches drive one at a time: the one
// whose panel was last opened, or the one Ableton sends footswitch MIDI to.
namespace
{
SpinLock focusLock;
Array<KuddleProcessor*> instances;
std::atomic<KuddleProcessor*> focused { nullptr };

bool hasFocus (KuddleProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

thread_local bool fromDeviceNow = false;

void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    KuddleProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace

KuddleProcessor::KuddleProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KuddleState", kp::layout())
{
    for (const auto& d : kp::all()) raw[d.id] = apvts.getRawParameterValue (d.id);
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.pairEnabled = { false, true };   // A+B switches rig in the Keystone app only; C+D is Keep
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };

    loadPresetFile();
    if (! hasPreset (0) && ! hasPreset (1) && ! hasPreset (2)) seedFactoryPresets();
    // a new copy starts on the first preset (a saved set puts its own back in setStateInformation)
    for (int i = 0; i < kPresets; ++i) if (hasPreset (i)) { loadPreset (i); uiMessage = {}; break; }

    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (60);
}

KuddleProcessor::~KuddleProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void KuddleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    engine.setParams (readParams());
    engine.prepare ((float) sampleRate, samplesPerBlock);
    keeper.prepare (sampleRate);
    setLatencySamples (engine.latency());
    mono.assign ((size_t) jmax (1, samplesPerBlock), 0.0f);
    outL.assign (mono.size(), 0.0f);
    outR.assign (mono.size(), 0.0f);
    footswitches.reset();   // Jetstream lesson: never keep a footswitch held over a prepare
    squeeze.store (false);
}

bool KuddleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

kud::Params KuddleProcessor::readParams()
{
    using namespace kp::id;
    kud::Params p;
    auto pc = [this] (const char* i) { return value (i) * 0.01f; };
    auto on = [this] (const char* i) { return value (i) > 0.5f; };
    auto ix = [this] (const char* i) { return (int) std::lround (value (i)); };
    p.inGainDb = value (inGain);
    p.gateDb = value (gate);
    p.muteOn = on (muteOn); p.foam = pc (foam); p.thump = pc (thump); p.strings = ix (strings);
    p.wahOn = on (wahOn); p.wahManual = ix (wahMode) == 1; p.wahSense = pc (wahSense); p.wahRange = ix (wahRange);
    p.wahSpeed = pc (wahSpeed); p.wahPeak = pc (wahPeak); p.wahPos = pc (wahPos);
    p.diDrive = pc (diDrive); p.diAir = pc (diAir); p.diSag = pc (diSag); p.diLowCut = value (diLowCut);
    p.optoOn = on (optoOn); p.optoSqueeze = pc (optoSqueeze); p.optoFast = ix (optoSpeed) == 1; p.optoBlend = pc (optoBlend);
    p.optoHoldDb = value (optoHold); p.squeeze = squeeze.load();
    p.tapeOn = on (tapeOn); p.tapePush = pc (tapePush); p.tapeWow = pc (tapeWow); p.tapeFlutter = pc (tapeFlutter);
    p.tapeBump = pc (tapeBump); p.tapeHiss = pc (tapeHiss);
    p.toneOn = on (toneOn); p.bassDb = value (bass); p.bassHz = value (bassHz); p.midsDb = value (mids); p.midsHz = value (midsHz);
    p.trebleKHz = value (treble);
    p.roomOn = on (roomOn); p.roomType = ix (roomType); p.roomSize = pc (roomSize); p.roomDecay = value (roomDecay);
    p.roomPreMs = value (roomPre); p.roomMix = pc (roomMix);
    p.melt = pc (melt); p.meltWorksWah = on (meltWah);
    p.reachMute = pc (reachMute); p.reachTape = pc (reachTape); p.reachTreble = pc (reachTreble); p.reachRoom = pc (reachRoom);
    p.outDb = value (output);
    p.mute = tuner.load();
    return p;
}

void KuddleProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    for (const auto meta : midi) handleMidi (meta.getMessage(), false);

    if ((int) mono.size() < n) { mono.resize ((size_t) n); outL.resize ((size_t) n); outR.resize ((size_t) n); }
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);

    // bass is mono: take whichever side has the signal (a mono track in Ableton feeds both)
    float pk = 0;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        mono[(size_t) i] = m;
        pk = jmax (pk, std::abs (m));
        // tuner feed: two one-poles at about 1.5 kHz, then every 4th sample
        pitchLp1 += (m - pitchLp1) * 0.18f;
        pitchLp2 += (pitchLp1 - pitchLp2) * 0.18f;
        if (++decim >= 4)
        {
            decim = 0;
            const int pos = pitchPos.load (std::memory_order_relaxed);
            pitchRing[(size_t) pos] = pitchLp2;
            pitchPos.store ((pos + 1) % kPitchRing, std::memory_order_release);
        }
    }

    engine.setParams (readParams());
    engine.process (mono.data(), L, R, n);
    keeper.pushOutput (L, R, n);

    uiIn.store (jmax (pk, uiIn.load() * 0.9f));
    uiOut.store (jmax (engine.peakOut(), uiOut.load() * 0.9f));
    uiGr.store (engine.optoReductionDb());
}

// =====================================================================================
// MIDI: footswitches and the expression pedal
// =====================================================================================
void KuddleProcessor::queue (int kind, int a)
{
    if (fromDeviceNow)
    {
        const auto scope = directFifo.write (1);
        if (scope.blockSize1 > 0) directPending[(size_t) scope.startIndex1] = { kind, a };
        return;
    }
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, a };
}

void KuddleProcessor::handleDirectMidi (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void KuddleProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void KuddleProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    const int ch = m.getChannel();
    if (m.isProgramChange())
    {
        if (ch == settings.fsChannel[0] && (fromDevice || hasFocus (this)))
            queue (LoadPreset, m.getProgramChangeNumber());
        return;
    }
    if (! m.isController()) return;
    const int cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc);
        return;
    }

    // Straight from the footswitch device any channel will do. Through Ableton the channel
    // must match, and while the device is read directly the same messages from Ableton are
    // ignored, so nothing happens twice.
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        for (int k = 0; k < 4 && foot < 0; ++k)
            if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
        const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
        const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
        if (foot >= 0 || keep || expression)
        {
            if (! fromDevice)
            {
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* f = focused.load();
                if (f != this && (f == nullptr || ! f->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0)
            {
                // a latching footswitch sends one message per press: treat each as a tap
                if ((settings.fsToggle >> foot) & 1) { queue (FootPress, foot); queue (FootRelease, foot); }
                else queue (val >= 64 ? FootPress : FootRelease, foot);
            }
            else if (keep) { if (val >= 64) queue (KeepCC, 0); }
            else queue (Expression, val);
        }
    }
}

void KuddleProcessor::setPlain (const String& pid, float plain)
{
    if (auto* p = apvts.getParameter (pid))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void KuddleProcessor::resetToDefault (const String& pid)
{
    if (auto* d = kp::find (pid)) setPlain (pid, d->def);
}

void KuddleProcessor::toggle (const char* pid) { setPlain (pid, value (pid) > 0.5f ? 0.0f : 1.0f); }

void KuddleProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = nowSeconds();
}

void KuddleProcessor::timerCallback()
{
    const double now = nowSeconds();
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFocus (this)) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); uiFootPulse.fetch_add (1); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case LoadPreset: loadPreset (q.a); break;
                case KeepCC: keepNow(); break;
                case Expression:
                {
                    lastExpRaw.store (q.a);
                    const float v = settings.expressionValue (q.a);
                    uiExp.store (v);
                    switch ((int) std::lround (value (kp::id::expTarget)))
                    {
                        case 1: setPlain (kp::id::wahPos, v * 100.0f); break;
                        case 2: setPlain (kp::id::output, jmax (-24.0f, 40.0f * std::log10 (jmax (v, 1.0e-3f)))); break;
                        case 3: setPlain (kp::id::roomMix, v * 100.0f); break;
                        default: setPlain (kp::id::melt, v * 100.0f); break;
                    }
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper) { settings.keeperChannel = ch; settings.keeperCC = cc; say ("KEEPER IS CC " + String (cc)); }
                    else if (what == LearnExpression) { settings.expChannel = ch; settings.expCC = cc; say ("EXPRESSION IS CC " + String (cc)); }
                    settings.save();
                    settingsStamp = PedalSettings::file().getLastModificationTime();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    // edits since the preset loaded
    if (selected >= 0 && lastLoaded.isValid() && ! edited)
    {
        const auto cur = snapshot();
        for (int i = 0; i < cur.getNumProperties() && ! edited; ++i)
        {
            const auto name = cur.getPropertyName (i);
            if (std::abs ((float) cur.getProperty (name) - (float) lastLoaded.getProperty (name, cur.getProperty (name))) > 1.0e-3f) edited = true;
        }
    }

    if (tuner.load()) detectPitch();
}

// =====================================================================================
// Footswitches. Tap: A Mute, B Auto wah, C Room, D Tape. Hold: A previous preset,
// B next preset, C tuner, D Squeeze while held. Hold C and D: Keep. Any tap leaves the tuner.
// =====================================================================================
void KuddleProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic::Event;
    if (e == E::ComboHoldCD) { keepNow(); return; }
    if (e == E::Tap)
    {
        if (tuner.load()) { setTuner (false); return; }
        static const char* const taps[4] = { kp::id::muteOn, kp::id::wahOn, kp::id::roomOn, kp::id::tapeOn };
        static const char* const names[4] = { "MUTE", "AUTO WAH", "ROOM", "TAPE" };
        toggle (taps[sw]);
        say (String (names[sw]) + (value (taps[sw]) > 0.5f ? " ON" : " OFF"));
        return;
    }
    if (e == E::HoldStart)
    {
        switch (sw)
        {
            case 0: if (! tuner.load()) stepPreset (-1); break;
            case 1: if (! tuner.load()) stepPreset (1); break;
            case 2: setTuner (! tuner.load()); break;
            case 3: squeeze.store (true); say ("SQUEEZE"); break;
            default: break;
        }
        return;
    }
    if (e == E::HoldEnd && sw == 3) squeeze.store (false);
}

void KuddleProcessor::footDown (int sw) { footswitches.press (sw, nowSeconds()); uiFootPulse.fetch_add (1); }
void KuddleProcessor::footUp (int sw) { footswitches.release (sw, nowSeconds()); }

void KuddleProcessor::setTuner (bool on)
{
    tuner.store (on);
    if (! on) uiPitch.store (0.0f);
    say (on ? "TUNER" : "TUNER OFF");
}

// YIN on the decimated input (12 kHz at 48 kHz): good from low B (31 Hz) up.
void KuddleProcessor::detectPitch()
{
    const double rate = sr / 4.0;
    const int W = 1400, maxTau = jmin (600, (int) (rate / 28.0)), minTau = (int) (rate / 400.0);
    std::vector<float> x ((size_t) (W + maxTau));
    const int end = pitchPos.load (std::memory_order_acquire);
    for (int i = 0; i < (int) x.size(); ++i)
        x[(size_t) i] = pitchRing[(size_t) ((end - (int) x.size() + i + kPitchRing * 2) % kPitchRing)];
    float energy = 0;
    for (int i = 0; i < W; ++i) energy += x[(size_t) i] * x[(size_t) i];
    if (energy / (float) W < 1.0e-7f) { uiPitch.store (0.0f); return; }

    std::vector<float> d ((size_t) maxTau + 1, 0.0f), rawDiff ((size_t) maxTau + 2, 0.0f);
    float running = 0;
    d[0] = 1;
    int best = -1;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        float s = 0;
        for (int i = 0; i < W; ++i) { const float diff = x[(size_t) i] - x[(size_t) (i + tau)]; s += diff * diff; }
        rawDiff[(size_t) tau] = s;
        running += s;
        d[(size_t) tau] = running > 0 ? s * (float) tau / running : 1.0f;
    }
    for (int tau = minTau; tau < maxTau; ++tau)
        if (d[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < maxTau && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0) { uiPitch.store (0.0f); return; }
    // parabolic interpolation on the raw difference (under 0.1 cent on a bass note)
    const float a = rawDiff[(size_t) best - 1], b = rawDiff[(size_t) best], c = rawDiff[(size_t) best + 1];
    const float den = a - 2 * b + c;
    const float t = (float) best + (std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f);
    const float hz = (float) (rate / t);
    const float prev = uiPitch.load();
    uiPitch.store (prev > 0 && std::abs (hz / prev - 1.0f) < 0.03f ? prev + (hz - prev) * 0.35f : hz);
}

// =====================================================================================
// Pedal setup
// =====================================================================================
void KuddleProcessor::learnFootswitch (int sw) { learnTarget.store (sw); say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw))); }
void KuddleProcessor::learnExpression() { learnTarget.store (LearnExpression); say ("MOVE THE EXPRESSION PEDAL"); }
void KuddleProcessor::learnKeeper() { learnTarget.store (LearnKeeper); say ("PRESS YOUR KEEPER SWITCH"); }

void KuddleProcessor::setHeel()
{
    const int v = lastExpRaw.load();
    if (v < 0) { say ("MOVE THE PEDAL FIRST"); return; }
    settings.heel = v;
    settings.save();
    say ("HEEL SET");
}

void KuddleProcessor::setToe()
{
    const int v = lastExpRaw.load();
    if (v < 0) { say ("MOVE THE PEDAL FIRST"); return; }
    settings.toe = v;
    settings.save();
    say ("TOE SET");
}

void KuddleProcessor::toggleReverse()
{
    settings.reversed = ! settings.reversed;
    settings.save();
    say (settings.reversed ? "PEDAL REVERSED" : "PEDAL THE RIGHT WAY ROUND");
}

void KuddleProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

// =====================================================================================
// Keeper
// =====================================================================================
void KuddleProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Kuddle");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("rig", "Kuddle");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    e->setProperty ("preset", selected < 0 ? String ("none") : presetName (selected));
    e->setProperty ("presetEdited", edited);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) e->setProperty ("tempo", *bpm);

    Array<var> chain;
    for (auto pair : { std::pair<const char*, const char*> { "Mute", kp::id::muteOn }, { "Auto wah", kp::id::wahOn }, { "Valve DI", nullptr },
                       { "Opto", kp::id::optoOn }, { "Tape", kp::id::tapeOn }, { "Tone", kp::id::toneOn }, { "Room", kp::id::roomOn } })
    {
        DynamicObject::Ptr slot = new DynamicObject();
        slot->setProperty ("module", pair.first);
        slot->setProperty ("on", pair.second == nullptr || value (pair.second) > 0.5f);
        chain.add (var (slot.get()));
    }
    e->setProperty ("chain", chain);

    DynamicObject::Ptr exp = new DynamicObject();
    exp->setProperty ("target", kp::find (kp::id::expTarget)->choices[(size_t) jlimit (0, 3, (int) std::lround (value (kp::id::expTarget)))]);
    exp->setProperty ("position", uiExp.load());
    e->setProperty ("expression", var (exp.get()));
    e->setProperty ("squeeze", squeeze.load());

    DynamicObject::Ptr params = new DynamicObject();
    for (const auto& d : kp::all())
    {
        DynamicObject::Ptr pv = new DynamicObject();
        pv->setProperty ("value", value (d.id));
        pv->setProperty ("shown", kp::shortText (d, value (d.id)) + (String (d.unit).isEmpty() ? String() : " " + String (d.unit)));
        params->setProperty (d.id, var (pv.get()));
    }
    e->setProperty ("parameters", var (params.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File KuddleProcessor::exportKeepers()
{
    const auto f = keeper.exportZip();
    if (f.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); f.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return f;
}

void KuddleProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void KuddleProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    settings.save();
}

// =====================================================================================
// Presets: 15 named slots in AppData/Kevin Kaye/Kuddle Presets.xml, so every copy of
// Kuddle (and the Keystone app later) has the same ones.
// =====================================================================================
File KuddleProcessor::presetFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Kevin Kaye").getChildFile ("Kuddle Presets.xml");
}

ValueTree KuddleProcessor::snapshot() const
{
    ValueTree t ("Values");
    for (const auto& d : kp::all())
        if (d.inPreset) t.setProperty (Identifier (d.id), value (d.id), nullptr);
    return t;
}

void KuddleProcessor::applySnapshot (const ValueTree& t)
{
    for (const auto& d : kp::all())
        if (d.inPreset) setPlain (d.id, (float) t.getProperty (Identifier (d.id), d.def));
}

void KuddleProcessor::loadPresetFile()
{
    auto xml = parseXML (presetFile());
    if (xml == nullptr || ! xml->hasTagName ("KuddlePresets")) return;
    const auto tree = ValueTree::fromXml (*xml);
    for (int i = 0; i < kPresets; ++i) presets[(size_t) i] = {};
    for (const auto& c : tree)
    {
        const int slot = (int) c.getProperty ("slot", -1);
        if (slot < 0 || slot >= kPresets) continue;
        presets[(size_t) slot].name = c.getProperty ("name").toString();
        presets[(size_t) slot].values = c.getChildWithName ("Values").createCopy();
    }
}

void KuddleProcessor::savePresetFile() const
{
    ValueTree tree ("KuddlePresets");
    for (int i = 0; i < kPresets; ++i)
        if (hasPreset (i))
        {
            ValueTree c ("Preset");
            c.setProperty ("slot", i, nullptr);
            c.setProperty ("name", presets[(size_t) i].name, nullptr);
            c.appendChild (presets[(size_t) i].values.createCopy(), nullptr);
            tree.appendChild (c, nullptr);
        }
    presetFile().getParentDirectory().createDirectory();
    if (auto xml = tree.createXml()) xml->writeTo (presetFile());
}

void KuddleProcessor::seedFactoryPresets()
{
    using namespace kp::id;
    auto make = [this] (std::initializer_list<std::pair<const char*, float>> changes)
    {
        ValueTree t ("Values");
        for (const auto& d : kp::all()) if (d.inPreset) t.setProperty (Identifier (d.id), d.def, nullptr);
        for (const auto& c : changes) t.setProperty (Identifier (c.first), c.second, nullptr);
        return t;
    };
    // Voodoo: muted, warm, a little wah that follows your fingers (the defaults)
    presets[0] = { "Voodoo", make ({}) };
    // Khruangbin: heavy foam, round and dry, a small dark room
    presets[1] = { "Khruangbin", make ({ { foam, 65 }, { thump, 70 }, { wahOn, 0 }, { diDrive, 20 }, { diAir, 10 },
                                         { optoSqueeze, 55 }, { optoBlend, 90 }, { tapePush, 45 }, { tapeWow, 20 }, { tapeBump, 60 },
                                         { bass, 3 }, { mids, -1 }, { midsHz, 500 }, { treble, 2.4f },
                                         { roomOn, 1 }, { roomType, 0 }, { roomMix, 18 }, { roomSize, 25 } }) };
    // Dub: big and deep, scooped, a spring behind it
    presets[2] = { "Dub", make ({ { foam, 75 }, { thump, 80 }, { wahOn, 0 }, { diDrive, 25 }, { diAir, 0 }, { optoSqueeze, 60 },
                                  { tapePush, 55 }, { tapeBump, 80 }, { bass, 5 }, { bassHz, 70 }, { mids, -3 }, { midsHz, 700 }, { treble, 1.6f },
                                  { roomOn, 1 }, { roomType, 1 }, { roomMix, 25 }, { roomDecay, 1.4f } }) };
    savePresetFile();
}

void KuddleProcessor::markEdited() { edited = false; lastLoaded = snapshot(); }

void KuddleProcessor::loadPreset (int slot)
{
    if (! hasPreset (slot)) return;
    applySnapshot (presets[(size_t) slot].values);
    selected = slot;
    markEdited();
    lastLoaded = presets[(size_t) slot].values.createCopy();
    say (presetName (slot).toUpperCase());
}

void KuddleProcessor::savePreset (int slot, const String& name)
{
    if (slot < 0 || slot >= kPresets) return;
    presets[(size_t) slot] = { name.isNotEmpty() ? name : "Preset " + String (slot + 1), snapshot() };
    selected = slot;
    markEdited();
    savePresetFile();
    say ("SAVED " + presets[(size_t) slot].name.toUpperCase());
}

void KuddleProcessor::renamePreset (int slot, const String& name)
{
    if (! hasPreset (slot) || name.trim().isEmpty()) return;
    presets[(size_t) slot].name = name.trim();
    savePresetFile();
}

void KuddleProcessor::clearPreset (int slot)
{
    if (! hasPreset (slot)) return;
    presets[(size_t) slot] = {};
    if (selected == slot) selected = -1;
    savePresetFile();
}

void KuddleProcessor::stepPreset (int direction)
{
    for (int k = 1; k <= kPresets; ++k)
    {
        const int s = ((selected < 0 ? (direction > 0 ? -1 : 0) : selected) + direction * k + kPresets * 2) % kPresets;
        if (hasPreset (s)) { loadPreset (s); return; }
    }
    say ("NO PRESETS SAVED YET");
}

void KuddleProcessor::revertPreset()
{
    if (hasPreset (selected)) loadPreset (selected);
    else
    {
        for (const auto& d : kp::all()) if (d.inPreset) setPlain (d.id, d.def);
        say ("BACK TO DEFAULTS");
    }
}

// =====================================================================================
// Saving with the Ableton set: the parameters and which preset was chosen
// =====================================================================================
void KuddleProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("selectedPreset", selected, nullptr);
    state.setProperty ("edited", edited, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void KuddleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = ValueTree::fromXml (*xml);
    selected = (int) tree.getProperty ("selectedPreset", -1);
    apvts.replaceState (tree);
    lastLoaded = hasPreset (selected) ? presets[(size_t) selected].values.createCopy() : ValueTree();
    edited = (bool) tree.getProperty ("edited", false);
}

AudioProcessorEditor* KuddleProcessor::createEditor()
{
    focused.store (this);
    return new KuddleEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KuddleProcessor();
}
