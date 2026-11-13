#include "PluginEditor.h"
namespace wsho
{
WhyshoSynthAudioProcessorEditor::WhyshoSynthAudioProcessorEditor (WhyshoSynthAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p) { setSize (1100, 700); }
void WhyshoSynthAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (gui::theme().bg); }
}
