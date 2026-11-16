#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include "KuddleEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Kuddle as a VST3 for Ableton. The sound comes from kuddle/engine (plain C++, shared with
// the Keystone app later). This class adds parameters, presets, footswitches, the Melt
// pedal, the tuner and Keeper.
class KuddleProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kPresets = 15;

    KuddleProcessor();
    ~KuddleProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kuddle"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    float value (const char* pid) const { return raw.at (pid)->load (std::memory_order_relaxed); }
    void setPlain (const juce::String& pid, float plain);
    void resetToDefault (const juce::String& pid);

    // ---- presets: 15 named slots, kept on this computer so every copy has them ----
    struct Preset { juce::String name; juce::ValueTree values; };
    bool hasPreset (int slot) const { return slot >= 0 && slot < kPresets && presets[(size_t) slot].values.isValid(); }
    juce::String presetName (int slot) const { return hasPreset (slot) ? presets[(size_t) slot].name : juce::String(); }
    int selectedPreset() const { return selected; }
    bool presetEdited() const { return edited; }
    void loadPreset (int slot);
    void savePreset (int slot, const juce::String& name);
    void renamePreset (int slot, const juce::String& name);
    void clearPreset (int slot);
    void stepPreset (int direction);
    void revertPreset();     // Deep page: put the selected preset back as saved

    // ---- footswitches and the pedal (message thread) ----
    void footDown (int sw);
    void footUp (int sw);
    PedalSettings& pedals() { return settings; }
    void learnFootswitch (int sw);
    void learnExpression();
    void learnKeeper();
    void setHeel();
    void setToe();
    void toggleReverse();
    void resetFootswitches();
    bool isLearning() const { return learnTarget.load() != LearnNone; }
    int learningWhat() const { return learnTarget.load(); }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5 };
    int lastExpressionRaw() const { return lastExpRaw.load(); }
    float expressionLevel() const { return uiExp.load(); }

    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    juce::String footDevice() const { return settings.footDevice; }
    void setFootDevice (const juce::String& device);
    std::atomic<bool> hostFeedsPedals { false };

    // ---- tuner (hold C) ----
    bool tunerOn() const { return tuner.load(); }
    void setTuner (bool on);
    float tunerHz() const { return uiPitch.load(); }      // 0 when nothing is heard

    // ---- squeeze (hold D) ----
    bool squeezing() const { return squeeze.load(); }

    // ---- Keeper ----
    void keepNow();
    void addKeeperNote (int number, const juce::String& note) { keeper.addNote (number, note); }
    juce::File exportKeepers();
    void openKeepersFolder();
    bool keeperLive() const { return settings.keeperLive; }
    void setKeeperLive (bool live);
    int lastKeeper() const { return keeper.lastNumber(); }

    // ---- what the panel and Pebble show ----
    float inputLevel() const { return uiIn.load(); }
    float outputLevel() const { return uiOut.load(); }
    float optoReduction() const { return uiGr.load(); }
    std::atomic<int> uiKeepPulse { 0 }, uiFootPulse { 0 }, uiFootDown { 0 };
    juce::String uiMessage;
    double uiMessageAt = 0.0;
    void say (const juce::String& s);

private:
    void timerCallback() override;
    kud::Params readParams();
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void toggle (const char* pid);
    void detectPitch();

    juce::ValueTree snapshot() const;
    void applySnapshot (const juce::ValueTree&);
    void loadPresetFile();
    void savePresetFile() const;
    void seedFactoryPresets();
    static juce::File presetFile();
    void markEdited();

    kud::Engine engine;
    Keeper keeper;
    std::map<juce::String, std::atomic<float>*> raw;

    std::array<Preset, kPresets> presets;
    int selected = -1;
    bool edited = false;
    juce::ValueTree lastLoaded;

    // MIDI handed to the message thread
    struct Pending { int kind = 0; int a = 0; };
    enum PendingKind { FootPress, FootRelease, Expression, LoadPreset, Learned, KeepCC };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::AbstractFifo directFifo { 512 };
    std::array<Pending, 512> directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    juce::Time settingsStamp;
    bool wasDirect = false;
    double nextSettingsCheck = 0.0;
    void queue (int kind, int a);

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    std::atomic<int> lastExpRaw { -1 };
    std::atomic<bool> tuner { false }, squeeze { false };

    // tuner: mono input decimated by 4 into a ring the message thread reads
    static constexpr int kPitchRing = 8192;
    std::array<float, kPitchRing> pitchRing {};
    std::atomic<int> pitchPos { 0 };
    int decim = 0;
    float decimAcc = 0, pitchLp1 = 0, pitchLp2 = 0;
    double sr = 48000.0;
    std::vector<float> mono, outL, outR;

    std::atomic<float> uiIn { 0 }, uiOut { 0 }, uiGr { 0 }, uiExp { 0 }, uiPitch { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuddleProcessor)
};
