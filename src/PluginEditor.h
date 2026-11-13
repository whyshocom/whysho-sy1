#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/WhyshoLookAndFeel.h"
#include "gui/TopBar.h"
#include "gui/SynthPanel.h"
#include "gui/PresetBrowser.h"
#include "gui/Dialogs.h"
#include "gui/Keyboard.h"
#include "gui/LicenseDialog.h"

namespace wsho
{
// The SY1 window: top bar, the one-page synth, keyboard; the preset browser
// and the dialogs are in-editor overlays (no native modal windows).
//
// Scaling: everything lives in `root`, laid out in logical pixels (1100x700 at
// 1x) and drawn through one uniform scale transform, so text, hairlines and
// hit areas all scale together. Any spare aspect goes to the layout.
class WhyshoSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        public juce::FileDragAndDropTarget,
                                        private juce::Timer,
                                        private juce::ChangeListener,
                                        private juce::FocusChangeListener
{
public:
    explicit WhyshoSynthAudioProcessorEditor (WhyshoSynthAudioProcessor&);
    ~WhyshoSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    // Also driven by tools/Snapshot to render each state.
    void setBrowserOpen (bool);
    gui::PresetBrowser& presetBrowser() { return browser; }
    void openSaveDialog();
    void openLicenseDialog();
    void openPrompt (gui::PromptSpec);

    static constexpr int baseW = 1100, baseH = 700, minW = 900, minH = 600, maxW = 1650, maxH = 1050;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void globalFocusChanged (juce::Component*) override;

    void applyTheme (int index, bool userChoice);
    bool notesBlocked() const;      // typing into a field / a sheet is up → keys must not play notes
    void closeOverlays();

    WhyshoSynthAudioProcessor& proc;
    gui::WhyshoLookAndFeel lnf;                 // first: must outlive every child

    juce::Component root;
    gui::TopBar topBar;
    gui::SynthPanel synth;
    gui::PresetBrowser browser;
    gui::Keyboard keys;
    std::unique_ptr<gui::SaveDialog> saveDialog;
    std::unique_ptr<gui::Prompt> prompt;
    std::unique_ptr<gui::LicenseDialog> licenseDialog;
    juce::TooltipWindow tooltips { &root, 650 };

    bool constructed = false, dragOver = false;
    int slowTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WhyshoSynthAudioProcessorEditor)
};
} // namespace wsho
