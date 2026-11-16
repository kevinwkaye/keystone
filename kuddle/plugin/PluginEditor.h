#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Kuddle's panel: the Flip Fella family in brown and orange, black flip cards with white
// digits, and Pebble in shades and a pork pie hat. Three pages: Main, Deep and Pedal.
// Every component lives in PluginEditor.cpp.
class KuddleEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KuddleEditor (KuddleProcessor&);
    ~KuddleEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    struct Impl;

private:
    void timerCallback() override;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KuddleEditor)
};
