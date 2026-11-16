#include "AudioHost.h"

using namespace juce;

AudioHost::AudioHost (ks::Engine& e, Keeper& k) : engine (e), keeper (k) {}

AudioHost::~AudioHost() { stop(); }

bool AudioHost::nameMatches (const String& name, const String& preferred)
{
    if (preferred.isNotEmpty()) return name.containsIgnoreCase (preferred);
    // the UM2 appears as "USB Audio CODEC" on Windows and "UMC" or "UM2" on Linux
    for (auto* want : { "UM2", "USB Audio CODEC", "Behringer", "U-PHORIA" })
        if (name.containsIgnoreCase (want)) return true;
    return false;
}

void AudioHost::start (const String& preferredDevice, int preferredBuffer)
{
    wantDevice = preferredDevice;
    wantBuffer = preferredBuffer;
    if (! tryOpen()) Logger::writeToLog ("Audio: no device yet (" + lastError + "), retrying every 3 seconds");
    startTimer (3000);
}

void AudioHost::stop()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

String AudioHost::deviceList()
{
    String s;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        s << type->getTypeName() << "\n  inputs: " << type->getDeviceNames (true).joinIntoString (", ")
          << "\n  outputs: " << type->getDeviceNames (false).joinIntoString (", ") << "\n";
    }
    return s;
}

bool AudioHost::tryOpen()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

#if JUCE_WINDOWS
    // Exclusive talks to the interface directly; Shared goes through Windows' own mixer,
    // which copes better with some USB interfaces that drop data in exclusive mode
    // ASIO goes through an ASIO driver (ASIO4ALL, or the interface's own), which many USB
    // interfaces need for clean low latency on Windows
    const int mode = (int) std::lround (engine.getParam (ks::P::OutMode));
    const StringArray typeOrder = mode == 2 ? StringArray { "ASIO", "Windows Audio (Exclusive Mode)", "Windows Audio (Low Latency Mode)", "Windows Audio" }
                                : mode == 1 ? StringArray { "Windows Audio (Low Latency Mode)", "Windows Audio", "Windows Audio (Exclusive Mode)" }
                                            : StringArray { "Windows Audio (Exclusive Mode)", "Windows Audio (Low Latency Mode)", "Windows Audio" };
#elif JUCE_LINUX
    const StringArray typeOrder { "ALSA", "JACK" };
#else
    const StringArray typeOrder { "CoreAudio" };
#endif
    auto& types = deviceManager.getAvailableDeviceTypes();
    for (const auto& typeName : typeOrder)
    {
        AudioIODeviceType* type = nullptr;
        for (auto* t : types) if (t->getTypeName() == typeName) type = t;
        if (type == nullptr) continue;
        type->scanForDevices();
        const auto ins = type->getDeviceNames (true), outs = type->getDeviceNames (false);
        String in, out;
        for (const auto& n : ins) if (in.isEmpty() && nameMatches (n, wantDevice)) in = n;
        for (const auto& n : outs) if (out.isEmpty() && nameMatches (n, wantDevice)) out = n;
        bool found = in.isNotEmpty() && out.isNotEmpty();
        const bool asio = typeName == "ASIO";
        if (asio)
        {
            // one ASIO driver does input and output: the interface's own if it has one,
            // else a universal one (ASIO4ALL, FlexASIO)
            in.clear();
            for (auto* want : { "UM2", "USB Audio CODEC", "Behringer", "ASIO4ALL", "FlexASIO" })
                for (const auto& n : outs) if (in.isEmpty() && n.containsIgnoreCase (want)) in = n;
            if (in.isEmpty() && outs.size() > 0) in = outs[0];
            out = in;
            found = in.isNotEmpty();
        }
        if (in.isEmpty() && ins.size() > 0) in = ins[std::max (0, type->getDefaultDeviceIndex (true))];
        if (out.isEmpty() && outs.size() > 0) out = outs[std::max (0, type->getDefaultDeviceIndex (false))];
        if (in.isEmpty() || out.isEmpty()) { lastError = "no input or output device"; continue; }

        deviceManager.setCurrentAudioDeviceType (typeName, true);
        AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);
        setup.inputDeviceName = in;
        setup.outputDeviceName = out;
        setup.useDefaultInputChannels = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, 2, true);
        setup.sampleRate = 48000.0;
        const int wantSamples = wantBuffer > 0 ? wantBuffer : (int) std::lround (targetBufferMs() * 0.001 * 48000.0);
        setup.bufferSize = wantSamples;
        auto err = deviceManager.setAudioDeviceSetup (setup, true);
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (err.isNotEmpty() || dev == nullptr)
        {
            // try 44.1 kHz, which every class-compliant interface offers
            setup.sampleRate = 44100.0;
            err = deviceManager.setAudioDeviceSetup (setup, true);
            dev = deviceManager.getCurrentAudioDevice();
        }
        if (err.isNotEmpty() || dev == nullptr) { lastError = typeName + ": " + err; continue; }
        if (asio)
        {
            // a universal driver lists every device's channels: use the interface's pair
            const auto inNames = dev->getInputChannelNames(), outNames = dev->getOutputChannelNames();
            Logger::writeToLog ("Audio: ASIO inputs: " + inNames.joinIntoString (", ") + ". Outputs: " + outNames.joinIntoString (", "));
            auto pick = [] (const StringArray& names)
            {
                BigInteger bits;
                for (int i = 0; i < names.size(); ++i)
                    if (nameMatches (names[i], {}) && bits.countNumberOfSetBits() < 2) bits.setBit (i);
                if (bits.isZero()) bits.setRange (0, std::min (2, names.size()), true);
                return bits;
            };
            setup.inputChannels = pick (inNames);
            setup.outputChannels = pick (outNames);
            err = deviceManager.setAudioDeviceSetup (setup, true);
            dev = deviceManager.getCurrentAudioDevice();
            if (err.isNotEmpty() || dev == nullptr) { lastError = typeName + ": " + err; continue; }
        }

        // the size asked for, or the nearest the device offers at or above it
        int best = dev->getCurrentBufferSizeSamples();
        {
            String sizes;
            for (int b : dev->getAvailableBufferSizes()) sizes << b << " ";
            Logger::writeToLog ("Audio: buffer sizes offered: " + sizes.trim());
        }
        if (wantBuffer <= 0)
        {
            const int want = (int) std::lround (targetBufferMs() * 0.001 * dev->getCurrentSampleRate());
            int pick = 0;
            for (int b : dev->getAvailableBufferSizes())
                if (b >= std::max (64, want) && (pick == 0 || b < pick)) pick = b;
            if (pick == 0) for (int b : dev->getAvailableBufferSizes()) pick = std::max (pick, b);
            if (pick > 0 && pick != best)
            {
                setup.bufferSize = pick;
                if (deviceManager.setAudioDeviceSetup (setup, true).isEmpty()) best = pick;
            }
        }
        dev = deviceManager.getCurrentAudioDevice();
        if (dev == nullptr) { lastError = typeName + ": device closed"; continue; }
        openedBuffer = dev->getCurrentBufferSizeSamples();
        silentInputBlocks.store (0);
        xrunsBase.store (std::max (0, dev->getXRunCount()));
        lastLate = lateCallbacks.load();
        lastXrun = 0;
        dropTicks = 0;
        deviceManager.addAudioCallback (this);
        {
            const ScopedLock l (statusLock);
            openType = typeName;
            openName = in == out ? in : in + " / " + out;
            lastError.clear();
        }
        onInterface.store (found);
        if (! found)
            Logger::writeToLog ("Audio: the audio interface isn't plugged in, so Keystone is using the laptop's own sound for now."
                                " It switches over as soon as the interface appears.");
        Logger::writeToLog ("Audio: " + openType + ", " + openName + ", " + String (dev->getCurrentSampleRate()) + " Hz, "
                            + String (dev->getCurrentBufferSizeSamples()) + " samples, round trip about "
                            + String (latency.load(), 1) + " ms");
        return true;
    }
    return false;
}

void AudioHost::audioDeviceAboutToStart (AudioIODevice* dev)
{
    const double sr = dev->getCurrentSampleRate();
    const int bs = dev->getCurrentBufferSizeSamples();
    monoIn.assign ((size_t) std::max (bs, 4096), 0.0f);
    monoOut.assign ((size_t) std::max (bs, 4096), 0.0f);
    engine.prepare (sr, std::max (16, std::min (bs, 256)));
    keeper.prepare (sr);
    currentBuffer.store (bs);
    currentRate.store (sr);
    // round trip: device's own input and output latency plus the engine's alignment
    const int lat = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples() + engine.latencySamples();
    latency.store (lat * 1000.0 / sr);
    callbackBudgetMs = bs * 1000.0 / sr;
    isRunning.store (true);
}

int AudioHost::dropouts() const
{
    int n = lateCallbacks.load();
    if (auto* dev = const_cast<AudioDeviceManager&> (deviceManager).getCurrentAudioDevice())
        n += std::max (0, dev->getXRunCount());
    return n;
}

String AudioHost::deviceName() const
{
    const ScopedLock l (statusLock);
    return openName;
}

void AudioHost::audioDeviceStopped() { isRunning.store (false); }

void AudioHost::audioDeviceError (const String& message)
{
    {
        const ScopedLock l (statusLock);
        lastError = message;
    }
    needsReopen.store (true);
}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                  const AudioIODeviceCallbackContext&)
{
    const double startMs = Time::getMillisecondCounterHiRes();
    if ((size_t) n > monoIn.size())
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) FloatVectorOperations::clear (out[c], n);
        return;
    }
    // which input carries the bass: forced, or Auto (the louder one, held for a second)
    const int choice = (int) std::lround (engine.getParam (ks::P::OutInput));
    const int avail = numIn;
    int use = 0;
    if (avail >= 2)
    {
        for (int c = 0; c < 2; ++c)
        {
            float m = 0;
            if (in[c] != nullptr) for (int i = 0; i < n; ++i) m = std::max (m, std::abs (in[c][i]));
            chanLevel[c] = std::max (m, chanLevel[c] * 0.995f);
        }
        // which input carries the bass is easy to lose track of: log it when it changes
        if (choice == 1) chosen = 0;
        else if (choice == 2) chosen = 1;
        else
        {
            const int other = 1 - chosen;
            if (chanLevel[other] > chanLevel[chosen] * 2.0f && chanLevel[other] > 1.0e-3f) louderFor += n;
            else louderFor = 0;
            if (louderFor > (int) currentRate.load()) { chosen = other; louderFor = 0; }
        }
        use = chosen;
    }
    if (avail > 0 && in[use] != nullptr) FloatVectorOperations::copy (monoIn.data(), in[use], n);
    else FloatVectorOperations::clear (monoIn.data(), n);
    // a live interface input always has a little noise; exact digital silence on every
    // input means the input stream has stopped
    bool allZero = true;
    for (int c = 0; c < numIn && allZero; ++c)
        if (in[c] != nullptr)
            for (int i = 0; i < n; ++i) if (in[c][i] != 0.0f) { allZero = false; break; }
    if (allZero && numIn > 0) silentInputBlocks.fetch_add (1); else silentInputBlocks.store (0);

    engine.process (monoIn.data(), monoOut.data(), n);

    // mono out on every output channel, so either output feeds the amp
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) FloatVectorOperations::copy (out[c], monoOut.data(), n);

    keeper.pushInput (monoIn.data(), monoIn.data(), n);
    keeper.pushOutput (monoOut.data(), monoOut.data(), n);
    chosenChannel.store (use);
    // a callback that took longer than its own buffer is a dropout in the making
    if (Time::getMillisecondCounterHiRes() - startMs > callbackBudgetMs) lateCallbacks.fetch_add (1);
}

void AudioHost::timerCallback()
{
    engine.setCpuLoad (cpuLoad());
    if (needsReopen.exchange (false) || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        if (tryOpen()) Logger::writeToLog ("Audio: device open again");
        return;
    }
    auto* dev = deviceManager.getCurrentAudioDevice();
    // the input stream stopped (digital silence for 2 s): open the device again
    if (dev != nullptr && isRunning.load() && openedBuffer > 0
        && silentInputBlocks.load() * (double) openedBuffer > 2.0 * currentRate.load())
    {
        Logger::writeToLog ("Audio: the interface's input went silent (the stream stopped), opening it again");
        silentInputBlocks.store (0);
        if (tryOpen()) Logger::writeToLog ("Audio: device open again");
        return;
    }
    // dropouts: log the two kinds separately every 30 s, and in Auto step the buffer up
    if (dev != nullptr && isRunning.load() && ++dropTicks >= 10)
    {
        dropTicks = 0;
        const int late = lateCallbacks.load() - lastLate;
        const int xr = std::max (0, dev->getXRunCount() - xrunsBase.load()) - lastXrun;
        lastLate += late;
        lastXrun += xr;
        if (late + xr > 0)
            Logger::writeToLog ("Audio: in the last 30 s, " + String (xr) + " dropouts reported by the interface, "
                                + String (late) + " blocks where Keystone ran late (buffer " + String (openedBuffer) + " samples)");
        const int choice = (int) std::lround (engine.getParam (ks::P::OutBuffer));
        const int step = std::clamp ((int) std::lround (engine.getParam (ks::P::OutBufferStep)), 0, 3);
        // only when Keystone itself ran late: dropouts the interface reports come from its
        // USB connection, and a bigger buffer only adds delay without curing them
        if (choice == 0 && late >= 6 && step < 3 && wantBuffer <= 0)
        {
            Logger::writeToLog ("Audio: too many dropouts, moving the buffer up a step");
            if (onBufferStepLearnt) onBufferStepLearnt (step + 1);
            openedForSetting = String (engine.getParam (ks::P::OutBuffer)) + "/" + String (engine.getParam (ks::P::OutBufferStep)) + "/" + String (engine.getParam (ks::P::OutMode));
            if (tryOpen()) Logger::writeToLog ("Audio: device open again");
            return;
        }
    }
    // the buffer setting was changed on the phone
    {
        const String setting = String (engine.getParam (ks::P::OutBuffer)) + "/" + String (engine.getParam (ks::P::OutBufferStep)) + "/" + String (engine.getParam (ks::P::OutMode));
        if (openedForSetting.isEmpty()) openedForSetting = setting;
        else if (setting != openedForSetting)
        {
            openedForSetting = setting;
            Logger::writeToLog ("Audio: buffer or mode setting changed");
            if (tryOpen()) Logger::writeToLog ("Audio: device open again");
            return;
        }
    }
    // running on the laptop's own sound: keep looking for the interface and move to it
    if (! onInterface.load() && interfacePresent())
    {
        Logger::writeToLog ("Audio: the audio interface has appeared, switching to it");
        if (tryOpen()) Logger::writeToLog ("Audio: now on the audio interface");
    }
}

// Auto: the buffer Keystone has learnt works on this computer (starts at 3 ms); or the
// size chosen on the phone (Tone, Output, Setup).
void AudioHost::showDriverPanel()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr || ! dev->hasControlPanel()) { Logger::writeToLog ("Audio: this driver has no settings panel"); return; }
    Logger::writeToLog ("Audio: opening the driver's panel");
    dev->showControlPanel();
    restart();   // reopen so a new buffer size takes effect
}

int AudioHost::targetBufferMs() const
{
    static const double stepMs[4] = { 3.0, 4.0, 6.0, 10.0 };
    const int choice = (int) std::lround (engine.getParam (ks::P::OutBuffer));
    if (choice >= 1 && choice <= 4) return (int) stepMs[choice - 1];
    const int step = std::clamp ((int) std::lround (engine.getParam (ks::P::OutBufferStep)), 0, 3);
    return (int) stepMs[step];
}

bool AudioHost::interfacePresent()
{
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr) return false;
    type->scanForDevices();
    bool in = false, out = false;
    for (const auto& n : type->getDeviceNames (true)) in = in || nameMatches (n, wantDevice);
    for (const auto& n : type->getDeviceNames (false)) out = out || nameMatches (n, wantDevice);
    return in && out;
}

String AudioHost::status() const
{
    const ScopedLock l (statusLock);
    if (isRunning.load())
        return (onInterface.load() ? String() : String ("NOT THE AUDIO INTERFACE (plug it in): ")) + openName + ", " + String (currentRate.load() / 1000.0, 1) + " kHz, " + String (currentBuffer.load()) + " samples, "
               + String (latency.load(), 1) + " ms" + (openType.contains ("Exclusive") ? ", exclusive" : "");
    return lastError.isNotEmpty() ? "No audio: " + lastError : "Looking for the audio interface";
}
