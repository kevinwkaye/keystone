#include "PedalRack.h"
#include "keystone/Pedals.h"

#include <cmath>

using namespace juce;

PedalRack::PedalRack()
{
    formats.addFormat (new VST3PluginFormat());
}

PedalRack::~PedalRack() { shutdown(); }

void PedalRack::setFolders (const Array<File>& pedalFolders, const File& stateFolder)
{
    folders = pedalFolders;
    stateDir = stateFolder;
    rescan();
}

void PedalRack::rescan()
{
    found.clear();
    for (const auto& dir : folders)
    {
        if (! dir.isDirectory()) continue;
        // a .vst3 is a folder (a bundle) on Linux and usually on Windows, or a single file
        for (const auto& f : dir.findChildFiles (File::findFilesAndDirectories, false, "*.vst3"))
        {
            const auto id = ks::pedalIdFor (f.getFileNameWithoutExtension().toStdString());
            if (! id.empty() && found.find (id) == found.end()) found[id] = f;   // the first folder wins
        }
    }
}

std::vector<std::string> PedalRack::installed() const
{
    std::vector<std::string> ids;
    for (const auto& p : ks::pedalTable())
        if (found.count (p.id)) ids.push_back (p.id);
    for (const auto& [id, f] : found)   // anything else in the folder, after the family
        if (ks::findPedal (id) == nullptr) ids.push_back (id);
    return ids;
}

File PedalRack::stateFile (const std::string& id) const
{
    return stateDir.getChildFile (String (id) + ".state");
}

std::unique_ptr<PedalRack::Slot> PedalRack::makeSlot (const std::string& id, std::string& error)
{
    auto it = found.find (id);
    if (it == found.end()) { rescan(); it = found.find (id); }
    if (it == found.end()) { error = "That pedal isn't installed"; return {}; }

    OwnedArray<PluginDescription> types;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (types, it->second.getFullPathName());
    if (types.isEmpty()) { error = "That pedal wouldn't open"; return {}; }

    String err;
    auto plugin = formats.createPluginInstance (*types[0], rate.load(), block.load(), err);
    if (plugin == nullptr) { error = err.isNotEmpty() ? err.toStdString() : std::string ("That pedal wouldn't start"); return {}; }

    auto s = std::make_unique<Slot>();
    s->id = id;
    s->plugin = std::move (plugin);
    auto& p = *s->plugin;

    // stereo in and out if it can, else what it offers: the bass goes into every input
    AudioProcessor::BusesLayout want;
    want.inputBuses.add (AudioChannelSet::stereo());
    want.outputBuses.add (AudioChannelSet::stereo());
    if (! p.setBusesLayout (want))
    {
        AudioProcessor::BusesLayout monoIn;
        monoIn.inputBuses.add (AudioChannelSet::mono());
        monoIn.outputBuses.add (AudioChannelSet::stereo());
        if (! p.setBusesLayout (monoIn))
        {
            AudioProcessor::BusesLayout mono;
            mono.inputBuses.add (AudioChannelSet::mono());
            mono.outputBuses.add (AudioChannelSet::mono());
            p.setBusesLayout (mono);
        }
    }
    s->inChannels = p.getTotalNumInputChannels();
    s->outChannels = p.getTotalNumOutputChannels();
    if (s->outChannels < 1) { error = "That pedal has no output"; return {}; }

    // its settings from last time
    MemoryBlock state;
    if (stateFile (id).loadFileAsData (state) && state.getSize() > 0)
        p.setStateInformation (state.getData(), (int) state.getSize());

    // the plugin's own controls, in its order; a second control of the same name (the
    // Bypass the VST3 wrapper adds beside the pedal's own) is left out
    StringArray names;
    for (auto* param : p.getParameters())
        if (isShown (param, p) && ! names.contains (param->getName (64).trim()))
        {
            names.add (param->getName (64).trim());
            s->shown.push_back (param);
        }
    s->last.assign (s->shown.size(), -1.0f);
    prepareSlot (*s);
    return s;
}

bool PedalRack::isShown (AudioProcessorParameter* param, AudioPluginInstance&)
{
    if (param == nullptr || ! param->isAutomatable()) return false;
    const auto name = param->getName (64).trim();
    if (name.isEmpty() || name.startsWithIgnoreCase ("MIDI CC") || name == "Program" || name.startsWithIgnoreCase ("Ctrl ")) return false;
    return true;
}

void PedalRack::prepareSlot (Slot& s)
{
    s.plugin->setRateAndBufferSizeDetails (rate.load(), block.load());
    s.plugin->setNonRealtime (false);
    s.plugin->prepareToPlay (rate.load(), block.load());
    s.buf.setSize (std::max (s.inChannels, s.outChannels), block.load(), false, true, false);
    s.midi.ensureSize (2048);
}

void PedalRack::saveState (Slot& s)
{
    if (s.plugin == nullptr || stateDir == File()) return;
    MemoryBlock state;
    s.plugin->getStateInformation (state);
    if (state.getSize() == 0) return;
    stateDir.createDirectory();
    stateFile (s.id).replaceWithData (state.getData(), state.getSize());
}

bool PedalRack::load (const std::string& id, std::string& error)
{
    if (id == wantedId && ! swapping) return true;
    std::unique_ptr<Slot> next;
    if (! id.empty())
    {
        next = makeSlot (id, error);
        if (next == nullptr) return false;
    }
    if (swapping) finishSwap();   // a pick while the last one was still fading: settle it first
    pending = std::move (next);
    wantedId = id;
    swapping = true;
    swapStarted = Time::getMillisecondCounterHiRes() * 0.001;
    target.store (0.0f);   // the old one fades out; the timer swaps once it's quiet
    if (! isTimerRunning()) startTimerHz (30);
    if (active == nullptr) finishSwap();
    return true;
}

void PedalRack::finishSwap()
{
    {
        const std::lock_guard<std::mutex> l (swapLock);
        std::swap (active, pending);
        hasPedal.store (active != nullptr);
        gain = 0.0f;
        limitGain = 1.0f;
        while (midiFifo.getNumReady() > 0) { const auto r = midiFifo.read (midiFifo.getNumReady()); (void) r; }
    }
    swapping = false;
    target.store (active != nullptr ? 1.0f : 0.0f);
    if (pending != nullptr)
    {
        // the old pedal: keep its settings for next time, then let it go
        saveState (*pending);
        pending->plugin->releaseResources();
        pending.reset();
    }
}

void PedalRack::setMode (bool on)
{
    modeOn.store (on);
    if (on) rescan();
    if (! isTimerRunning()) startTimerHz (30);
}

void PedalRack::shutdown()
{
    stopTimer();
    std::unique_ptr<Slot> a, p;
    {
        const std::lock_guard<std::mutex> l (swapLock);
        a = std::move (active);
        p = std::move (pending);
        hasPedal.store (false);
    }
    for (auto* s : { a.get(), p.get() })
        if (s != nullptr) { saveState (*s); s->plugin->releaseResources(); }
    swapping = false;
    wantedId.clear();
}

std::string PedalRack::paramsJson()
{
    auto* s = latest();
    var arr = Array<var>();
    if (s != nullptr)
    {
        for (size_t i = 0; i < s->shown.size(); ++i)
        {
            auto* p = s->shown[i];
            DynamicObject::Ptr o (new DynamicObject());
            const float v = p->getValue();
            o->setProperty ("i", (int) i);
            o->setProperty ("name", p->getName (40));
            o->setProperty ("v", v);
            o->setProperty ("def", p->getDefaultValue());
            o->setProperty ("text", p->getText (v, 24));
            const int steps = p->getNumSteps();
            const bool discrete = p->isDiscrete() && steps > 1 && steps <= 24;
            if (p->isBoolean() || (discrete && steps == 2 && (p->getText (0.0f, 8).equalsIgnoreCase ("off") || p->getText (1.0f, 8).equalsIgnoreCase ("on"))))
                o->setProperty ("kind", "toggle");
            else if (discrete)
            {
                o->setProperty ("kind", "choice");
                Array<var> ch;
                for (int k = 0; k < steps; ++k) ch.add (p->getText ((float) k / (float) (steps - 1), 24));
                o->setProperty ("choices", ch);
            }
            else o->setProperty ("kind", "slider");
            s->last[i] = v;
            arr.append (var (o.get()));
        }
    }
    return JSON::toString (arr, true).toStdString();
}

void PedalRack::setParam (int index, float v)
{
    auto* s = latest();
    if (s == nullptr || index < 0 || index >= (int) s->shown.size()) return;
    auto* p = s->shown[(size_t) index];
    p->beginChangeGesture();
    p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v));
    p->endChangeGesture();
}

void PedalRack::pushMidi (const MidiMessage& m)
{
    if (m.getRawDataSize() > 3 || m.getRawDataSize() < 1) return;
    const SpinLock::ScopedLockType l (midiWriteLock);
    const auto w = midiFifo.write (1);
    if (w.blockSize1 > 0)
    {
        auto& d = midiData[(size_t) w.startIndex1];
        d.size = m.getRawDataSize();
        std::memcpy (d.b, m.getRawData(), (size_t) d.size);
    }
}

void PedalRack::timerCallback()
{
    if (swapping)
    {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        // the old pedal has faded, or the audio isn't running to fade it
        if (gainNow.load() < 1.0e-3f || now - swapStarted > 0.6) finishSwap();
    }
    if (wentWrong.exchange (false) && active != nullptr)
    {
        std::string err;
        load ("", err);
        if (onLost) onLost ("The pedal went wrong, so it's put away");
        return;
    }
    // controls the pedal moved by itself (its footswitches, presets): tell the phone
    if (auto* s = latest(); s != nullptr && onChanged)
    {
        Array<var> changes;
        for (size_t i = 0; i < s->shown.size(); ++i)
        {
            const float v = s->shown[i]->getValue();
            if (std::abs (v - s->last[i]) > 1.0e-4f)
            {
                s->last[i] = v;
                changes.add (Array<var> { (int) i, v, s->shown[i]->getText (v, 24) });
            }
        }
        if (! changes.isEmpty()) onChanged (JSON::toString (changes, true).toStdString());
    }
    if (! swapping && active == nullptr && ! modeOn.load()) stopTimer();
}

// ---- audio ----

void PedalRack::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (16, maxBlockSize);
    rate.store (sr);
    block.store (maxBlock);
    // the device has stopped, so the pedal can be set up again for the new rate or size
    const std::lock_guard<std::mutex> l (swapLock);
    if (active != nullptr) prepareSlot (*active);
    if (pending != nullptr) prepareSlot (*pending);
}

void PedalRack::process (const float* in, float* out, int n, bool mute)
{
    std::unique_lock<std::mutex> l (swapLock, std::try_to_lock);
    if (! l.owns_lock() || active == nullptr) { gainNow.store (0.0f); return; }
    auto& s = *active;
    auto& p = *s.plugin;

    // the footswitch's messages since the last block
    s.midi.clear();
    {
        const auto r = midiFifo.read (midiFifo.getNumReady());
        for (int k = 0; k < r.blockSize1; ++k) { const auto& d = midiData[(size_t) (r.startIndex1 + k)]; s.midi.addEvent (d.b, d.size, 0); }
        for (int k = 0; k < r.blockSize2; ++k) { const auto& d = midiData[(size_t) (r.startIndex2 + k)]; s.midi.addEvent (d.b, d.size, 0); }
    }

    const float tgt = target.load();
    const float stepOut = 1.0f / (0.25f * (float) sr), stepIn = 1.0f / (0.05f * (float) sr);
    const float release = 1.0f - std::exp (-1.0f / (0.15f * (float) sr));
    const int chans = s.buf.getNumChannels();
    for (int pos = 0; pos < n;)
    {
        const int len = std::min (n - pos, s.buf.getNumSamples());
        for (int c = 0; c < chans; ++c)
        {
            if (c < s.inChannels) FloatVectorOperations::copy (s.buf.getWritePointer (c), in + pos, len);
            else FloatVectorOperations::clear (s.buf.getWritePointer (c), len);
        }
        AudioBuffer<float> view (s.buf.getArrayOfWritePointers(), chans, len);
        p.processBlock (view, s.midi);
        s.midi.clear();
        const float* L = s.buf.getReadPointer (0);
        const float* R = s.buf.getReadPointer (s.outChannels > 1 ? 1 : 0);
        for (int i = 0; i < len; ++i)
        {
            gain += gain < tgt ? std::min (stepIn, tgt - gain) : -std::min (stepOut, gain - tgt);
            float x = 0.5f * (L[i] + R[i]);
            if (! std::isfinite (x)) { wentWrong.store (true); x = 0.0f; s.plugin->reset(); break; }
            x *= gain;
            // a pedal running away (feedback, runaway, freeze stacks) never gets past -1 dB
            const float a = std::abs (x * limitGain);
            if (a > 0.89f) limitGain = 0.89f / std::abs (x);
            else limitGain += (1.0f - limitGain) * release;
            out[pos + i] += mute ? 0.0f : x * limitGain;
        }
        pos += len;
    }
    gainNow.store (gain);
}
