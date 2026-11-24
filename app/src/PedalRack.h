#pragma once
// Kaleidoscope's rack: plays one plugin pedal (a VST3 from the Pedals folder) after the
// engine, for fun. Loading, unloading and the controls happen on the message thread; the
// audio thread only ever processes what's there. A swap fades the old pedal out, trades it
// for the new one under a lock the audio thread never waits on, and fades the new one in.
// Nothing is loaded until you pick a pedal, and putting it away frees it completely.

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class PedalRack : private juce::Timer
{
public:
    PedalRack();
    ~PedalRack() override;

    // folders to look for .vst3 pedals in, and where each pedal's settings are kept
    void setFolders (const juce::Array<juce::File>& pedalFolders, const juce::File& stateFolder);
    void rescan();
    std::vector<std::string> installed() const;   // ids, in Kaleidoscope's order

    // ---- message thread ----
    bool load (const std::string& id, std::string& error);   // "" puts the pedal away
    std::string loadedId() const { return wantedId; }
    std::string paramsJson();                     // the controls of the pedal loaded, as a JSON array
    void setParam (int index, float value01);
    void setMode (bool kaleidoscopeOn);           // up: the footswitches and expression go to the pedal
    void shutdown();                              // saves the pedal's settings and lets it go

    // the footswitch's MIDI goes to the pedal while one is loaded and Kaleidoscope is up
    bool takesPedals() const { return modeOn.load() && hasPedal.load(); }
    void pushMidi (const juce::MidiMessage& m);   // any thread

    std::function<void (const std::string& changesJson)> onChanged;   // controls the pedal moved itself
    std::function<void (const std::string& why)> onLost;              // the pedal had to be put away

    // ---- audio thread ----
    void prepare (double sampleRate, int maxBlock);   // while the device is stopped
    // adds the pedal into out (mono), fed from in (the clean bass)
    // bpm: the song's tempo, which the pedal sees as the host's (its "sync" follows the song)
    void process (const float* in, float* out, int n, bool mute, double bpm = 120.0);
    bool running() const { return hasPedal.load(); }

private:
    struct Slot
    {
        std::string id;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::AudioBuffer<float> buf;
        juce::MidiBuffer midi;
        int inChannels = 2, outChannels = 2;
        std::vector<juce::AudioProcessorParameter*> shown;   // the controls the phone sees
        std::vector<float> last;                              // for spotting changes
    };

    // The song's tempo as the pedal's host transport: always rolling, 4/4
    struct SongClock : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0, sr = 48000.0;
        int64_t samples = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            p.setPpqPosition (ppq);
            p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            p.setTimeInSamples (samples);
            p.setTimeInSeconds ((double) samples / sr);
            p.setIsPlaying (true);
            return p;
        }
    };
    SongClock clock;

    void timerCallback() override;
    std::unique_ptr<Slot> makeSlot (const std::string& id, std::string& error);
    void prepareSlot (Slot& s);
    void saveState (Slot& s);
    void finishSwap();
    Slot* latest() { return pending ? pending.get() : active.get(); }
    juce::File stateFile (const std::string& id) const;
    static bool isShown (juce::AudioProcessorParameter* p, juce::AudioPluginInstance& plugin);

    juce::AudioPluginFormatManager formats;
    std::map<std::string, juce::File> found;   // id -> .vst3
    juce::Array<juce::File> folders;
    juce::File stateDir;

    std::mutex swapLock;                  // the audio thread only ever try-locks it
    std::unique_ptr<Slot> active;         // what the audio thread plays
    std::unique_ptr<Slot> pending;        // loaded, waiting for the old one to fade out
    bool swapping = false;
    double swapStarted = 0;
    std::string wantedId;

    std::atomic<bool> modeOn { false }, hasPedal { false }, wentWrong { false };
    std::atomic<float> target { 0.0f }, gainNow { 0.0f };
    float gain = 0.0f, limitGain = 1.0f;
    double sr = 48000.0;
    int maxBlock = 512;
    std::atomic<double> rate { 48000.0 };
    std::atomic<int> block { 512 };

    // MIDI from the footswitch, for the audio thread
    struct Msg { uint8_t b[3]; int size; };
    juce::AbstractFifo midiFifo { 256 };
    std::array<Msg, 256> midiData {};
    juce::SpinLock midiWriteLock;
};
