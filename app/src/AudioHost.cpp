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
    const StringArray typeOrder { "Windows Audio (Exclusive Mode)", "Windows Audio (Low Latency Mode)", "Windows Audio" };
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
        setup.bufferSize = wantBuffer > 0 ? wantBuffer : 64;
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

        // smallest buffer the device allows from 64 samples up (or the one asked for)
        int best = dev->getCurrentBufferSizeSamples();
        if (wantBuffer <= 0)
        {
            int pick = 0;
            for (int b : dev->getAvailableBufferSizes())
                if (b >= 64 && (pick == 0 || b < pick)) pick = b;
            if (pick > 0 && pick != best)
            {
                setup.bufferSize = pick;
                if (deviceManager.setAudioDeviceSetup (setup, true).isEmpty()) best = pick;
            }
        }
        dev = deviceManager.getCurrentAudioDevice();
        if (dev == nullptr) { lastError = typeName + ": device closed"; continue; }
        deviceManager.addAudioCallback (this);
        {
            const ScopedLock l (statusLock);
            openType = typeName;
            openName = in == out ? in : in + " / " + out;
            lastError.clear();
        }
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
    isRunning.store (true);
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

    engine.process (monoIn.data(), monoOut.data(), n);

    // mono out on every output channel, so either output feeds the amp
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) FloatVectorOperations::copy (out[c], monoOut.data(), n);

    keeper.pushInput (monoIn.data(), monoIn.data(), n);
    keeper.pushOutput (monoOut.data(), monoOut.data(), n);
}

void AudioHost::timerCallback()
{
    engine.setCpuLoad (cpuLoad());
    if (needsReopen.exchange (false) || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        if (tryOpen()) Logger::writeToLog ("Audio: device open again");
    }
}

String AudioHost::status() const
{
    const ScopedLock l (statusLock);
    if (isRunning.load())
        return openName + ", " + String (currentRate.load() / 1000.0, 1) + " kHz, " + String (currentBuffer.load()) + " samples, "
               + String (latency.load(), 1) + " ms" + (openType.contains ("Exclusive") ? ", exclusive" : "");
    return lastError.isNotEmpty() ? "No audio: " + lastError : "Looking for the audio interface";
}
