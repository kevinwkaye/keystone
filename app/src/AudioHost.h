#pragma once
// Runs the engine on the audio interface. Windows: WASAPI exclusive (Windows' low-latency
// mode), then the shared low-latency mode, then plain shared as fallbacks. Linux: ALSA.
// Prefers the Behringer UM2 (it shows up as "USB Audio CODEC") but works with any
// interface. Re-opens the device by itself if it's unplugged and plugged back in.

#include "Keeper.h"
#include "keystone/Engine.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

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
    int dropouts() const;                      // the device's own count plus late callbacks
    int inputChannel() const { return chosenChannel.load(); }
    juce::String deviceName() const;
    bool onAudioInterface() const { return onInterface.load(); }
    void restart() { needsReopen.store (true); }
    void showDriverPanel();   // the ASIO driver's own settings window (message thread)
    // Auto buffer: called when Keystone steps the buffer up after dropouts (0 = 3 ms ... 3 = 10 ms)
    std::function<void (int)> onBufferStepLearnt;

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& message) override;

private:
    void timerCallback() override;
    bool tryOpen();
    int targetBufferMs() const;
    bool interfacePresent();
    static bool nameMatches (const juce::String& name, const juce::String& preferred);

    ks::Engine& engine;
    Keeper& keeper;
    juce::AudioDeviceManager deviceManager;
    juce::String wantDevice, lastError, openType, openName;
    int wantBuffer = 0;
    std::vector<float> monoIn, monoOut;
    std::atomic<int> currentBuffer { 0 };
    std::atomic<double> currentRate { 0 }, latency { 0 };
    std::atomic<bool> isRunning { false }, needsReopen { false }, onInterface { true };
    // input channel choice (Auto picks the louder input, with hysteresis)
    float chanLevel[2] { 0, 0 };
    int chosen = 1, louderFor = 0;
    std::atomic<int> chosenChannel { 1 }, lateCallbacks { 0 }, silentInputBlocks { 0 }, xrunsBase { 0 };
    int openedBuffer = 0, lastDropCheck = 0, dropTicks = 0, lastLate = 0, lastXrun = 0;
    juce::String openedForSetting;
    double callbackBudgetMs = 3.0;
    juce::CriticalSection statusLock;
};
