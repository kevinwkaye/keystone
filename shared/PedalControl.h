#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "Footswitch.h"
#include <array>
#include <functional>

// Footswitch and expression setup, shared by every Flip Fella plugin on this computer,
// so a footswitch set up once works in all of them. Nothing here names a brand:
// any MIDI footswitch with momentary switches and any expression input works.
//
// Defaults: footswitches A to D send momentary CC 20, 21, 22, 23 on channel 16,
// expression sends CC 11 on channel 16. Learn remaps any of them.
// Copied from Prism Patch (the house reference). Changes: save() keeps attributes written
// by other plugins (Backtrack's fix), heel/toe calibration with Reverse, and the portable
// FootswitchLogic from core/src/Footswitch.h. Shared by the Keystone app and the Kuddle
// plugin. With no device chosen, the app opens every MIDI input (nothing else is
// listening); the plugin (KS_PEDALS_IN_PLUGIN=1) opens only what looks like a footswitch,
// so Ableton keeps the rest.
struct PedalSettings
{
    std::array<int, 4> fsChannel { 16, 16, 16, 16 };
    std::array<int, 4> fsCC { 20, 21, 22, 23 };   // 128 + note number for a switch that sends notes
    // a footswitch's own two-switch combos, learnt (the Chocolate Plus's E for A+B, F for C+D):
    // off until learnt. 128 + note number for one that sends notes
    std::array<int, 2> comboChannel { 0, 0 };
    std::array<int, 2> comboCC { -1, -1 };
    int fsToggle = 0;            // bit per switch: sends one message per press (latching), so it can only tap
    int expChannel = 16, expCC = 11;
    int heel = 0, toe = 127;     // calibrated ends of the expression pedal; toe may be lower than heel
    bool reversed = false;       // flips the pedal after calibration, for a pedal that reads backwards
    int curve = 0;               // 0 linear, 1 slow start, 2 fast start
    int pedalMapChannel = 0;     // 0 off, else the channel that speaks the original pedal's own CC map
    int keeperChannel = 0, keeperCC = -1;  // a learned switch just for Keeper; off until learned
    bool keeperLive = false;     // Keepers are tagged Live or Studio
    juce::String footDevice;     // MIDI input read directly: empty = find the footswitch automatically, "none" = off, else a device name

    void load();
    void save() const;
    void resetFootswitches();
    float expressionValue (int cc) const;    // 0..1 after calibration and curve
    static juce::File file();
};

// FootswitchLogic lives in the portable core (core/src/Footswitch.h) so the Pi build shares it.
using ks::FootswitchLogic;

// Reads the footswitch's MIDI device directly, so every copy of a plugin hears the
// footswitches without routing a MIDI track to it. One per process, shared by all copies.
// Messages arrive on a MIDI thread and go to the sink, which picks the copy in charge.
class DirectMidiInput : private juce::MidiInputCallback, private juce::Timer
{
public:
    DirectMidiInput();
    ~DirectMidiInput() override;

    using Sink = void (*) (const juce::MidiMessage&);
    void setSink (Sink s) { sink.store (s); }
    void setWanted (const juce::String& device);   // see PedalSettings::footDevice
    void refresh();

    bool connected() const { return isConnected.load(); }
    juce::String status() const;                   // a short line for the panel
    juce::StringArray openDevices() const;
    static juce::StringArray deviceNames();
    static bool looksLikeFootswitch (const juce::String& name);
    static bool isVirtualPort (const juce::String& name);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override { refresh(); }

    std::atomic<Sink> sink { nullptr };
    std::atomic<bool> isConnected { false };
    mutable juce::CriticalSection lock;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    juce::StringArray busy;
    juce::String wanted;
};
