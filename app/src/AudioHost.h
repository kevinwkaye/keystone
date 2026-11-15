#pragma once
// Runs the engine on the audio interface. Windows: WASAPI exclusive (Windows' low-latency
// mode), then the shared low-latency mode, then plain shared as fallbacks. Linux: ALSA.
// Prefers the Behringer UM2 (it shows up as "USB Audio CODEC") but works with any
// interface. Re-opens the device by itself if it's unplugged and plugged back in.

#include "Keeper.h"
#include "keystone/Engine.h"

#include <juce_audio_devices/juce_audio_devices.h>

class AudioHost : public juce::AudioIODeviceCallback, private juce::Timer
{
public:
    AudioHost (ks::Engine& e, Keeper& k);
    ~AudioHost() override;

    // preferredDevice: part of a device name, or empty for automatic.
    // preferredBuffer: samples, or 0 for the smallest the device allows from 64 up.
    void start (const juce::String& preferredDevice, int preferredBuffer);
    void stop();

    juce::String status() const;
    juce::String deviceList();
    int bufferSize() const { return currentBuffer.load(); }
    double sampleRate() const { return currentRate.load(); }
    double latencyMs() const { return latency.load(); }
    float cpuLoad() const { return (float) deviceManager.getCpuUsage(); }
    bool running() const { return isRunning.load(); }

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

private:
    void timerCallback() override;
    bool tryOpen();
    static bool nameMatches (const juce::String& name, const juce::String& preferred);

    ks::Engine& engine;
    Keeper& keeper;
    juce::AudioDeviceManager deviceManager;
    juce::String wantDevice, lastError, openType, openName;
    int wantBuffer = 0;
    std::vector<float> monoIn, monoOut;
    std::atomic<int> currentBuffer { 0 };
    std::atomic<double> currentRate { 0 }, latency { 0 };
    std::atomic<bool> isRunning { false }, needsReopen { false };
    // input channel choice (Auto picks the louder input, with hysteresis)
    float chanLevel[2] { 0, 0 };
    int chosen = 1, louderFor = 0;
    juce::CriticalSection statusLock;
};
