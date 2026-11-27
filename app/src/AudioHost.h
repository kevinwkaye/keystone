#pragma once
// Runs the engine on the audio interface. The Pi: ALSA. The laptop: WASAPI exclusive, then the
// shared low-latency mode, then plain shared as fallbacks, or ASIO if picked on the phone.
// Prefers the Behringer UM2 (it shows up as "USB Audio CODEC") but works with any
// interface. Re-opens the device by itself if it's unplugged and plugged back in.

#include "Keeper.h"
#include "PedalRack.h"
#include "keystone/Engine.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

class AudioHost : public juce::AudioIODeviceCallback, private juce::Timer
{
public:
    AudioHost (ks::Engine& e, Keeper& k, PedalRack* pedals = nullptr);
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
    bool onAudioInterface() const { return onInterface.load(); }
    void restart() { needsReopen.store (true); }
    int reconnects() const { return reconnectCount.load(); }   // the interface dropped and came back
    void showDriverPanel();   // the ASIO driver's own settings window (message thread)
    // Auto buffer: called when Keystone steps the buffer up after dropouts (step 0 is the smallest, 3 is 10 ms)
    std::function<void (int)> onBufferStepLearnt;

    // Find the best buffer: tries four sizes (2, 4, 6, 10 ms on the Pi, 3, 4, 6, 10 on the laptop)
    // for 15 s each, smallest first, and keeps
    // the smallest that ran without dropouts (message thread)
    void findBestBuffer();
    void stopFindingBuffer();
    juce::String finderJson() const;            // progress for the phone, empty when idle
    std::function<void (int step)> onBufferFound;   // the host sets Auto and the learnt step

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
    PedalRack* rack = nullptr;   // Kaleidoscope's pedal, played after the engine
    juce::AudioDeviceManager deviceManager;
    juce::String wantDevice, lastError, openType, openName;
    int wantBuffer = 0;
    std::vector<float> monoIn, monoOut;
    std::atomic<int> currentBuffer { 0 };
    std::atomic<double> currentRate { 0 }, latency { 0 };
    std::atomic<bool> isRunning { false }, needsReopen { false }, onInterface { true };
    std::atomic<int> reconnectCount { 0 };
    bool wasOpen = false;   // a device was open at the last check, so losing it is a reconnect
    // input channel choice (Auto picks the louder input, with hysteresis)
    float chanLevel[2] { 0, 0 };
    int chosen = 1, louderFor = 0;
    std::atomic<int> chosenChannel { 1 }, lateCallbacks { 0 }, silentInputBlocks { 0 }, xrunsBase { 0 };
    std::atomic<int64_t> callbacks { 0 };   // the watchdog checks these keep coming
    int64_t lastCallbacks = -1;
    int stalledTicks = 0;
    int openedBuffer = 0, dropTicks = 0, lastLate = 0, lastXrun = 0;
    juce::String openedForSetting;
    // the buffer finder
    bool finding = false;
    int findStep = 0, findTicks = 0, findLate0 = 0, findXr0 = 0, findWasMs = 0, findResult[4] { -1, -1, -1, -1 };
    float findCpu = 0, findCpuStep = 0;
    int foundMs = 0;
    double findDoneAt = 0;
    void findStartStep();
    void findFinish (int step);
    juce::String settingNow() const;
    double callbackBudgetMs = 3.0;
    juce::CriticalSection statusLock;
};
