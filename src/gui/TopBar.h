#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Displays.h"

namespace wsho { class WhyshoSynthAudioProcessor; }

namespace wsho::gui
{
// Brand, the big preset read-out, save / menu, live status, theme, account.
class TopBar : public juce::Component
{
public:
    explicit TopBar (WhyshoSynthAudioProcessor&);
    ~TopBar() override;

    std::function<void()> onBrowse, onSave, onLicense, onInit, onImport, onExport, onFolder;
    std::function<void (int)> onTheme;

    void refresh();                     // preset name / modified flag / licence state (slow poll + change messages)
    void tick();                        // meter + voice count (UI timer)
    void setBrowserOpen (bool);
    void setThemeSelection (int index);

    // What the bar currently shows (read by the UI test harness, tools/Snapshot --interact).
    struct Readout { juce::String kicker, name, index, license; bool edited = false; };
    Readout readout() const;
    juce::Component& presetDisplay();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 54;

private:
    class PresetDisplay;
    class LicenseButton;
    void showMenu();

    WhyshoSynthAudioProcessor& proc;
    std::unique_ptr<PresetDisplay> preset;
    std::unique_ptr<LicenseButton> license;
    juce::TextButton saveBtn { "Save" }, menuBtn { "..." };
    juce::ComboBox themeBox;
    Meter meter;
    juce::Rectangle<int> brandArea, statusArea;
    int voices = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace wsho::gui
