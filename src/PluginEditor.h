#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WhyshoLookAndFeel.h"

namespace wsho
{
// STUB — replaced by the real editor.
class WhyshoSynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit WhyshoSynthAudioProcessorEditor (WhyshoSynthAudioProcessor&);
    void paint (juce::Graphics&) override;
private:
    WhyshoSynthAudioProcessor& proc;
};
} // namespace wsho
