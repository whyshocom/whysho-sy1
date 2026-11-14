#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PresetManager.h"
#include "Theme.h"

namespace wsho::gui
{
// In-editor replacement for native alert windows: plugins must never run a
// modal loop, and a house-style sheet looks better than an OS dialog anyway.
class Overlay : public juce::Component
{
public:
    Overlay (const juce::String& kicker, const juce::String& title, int panelW, int panelH);

    std::function<void()> onDismiss;        // owner destroys / hides the overlay

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override { layout (content()); }
    void lookAndFeelChanged() override { restyle(); repaint(); }
    void parentHierarchyChanged() override;

protected:
    virtual void layout (juce::Rectangle<int> content) = 0;
    virtual void restyle() {}
    virtual void confirm() {}
    virtual juce::Component* firstFocus() { return this; }
    void dismiss() { if (onDismiss) onDismiss(); }
    void setPanelHeight (int h) { panelH = h; resized(); repaint(); }
    juce::Rectangle<int> panel() const;
    juce::Rectangle<int> content() const;   // inside the panel, below the title

    juce::String kicker, title;

private:
    int panelW, panelH;
};

// A question, optionally with one line of text to type.
struct PromptSpec
{
    juce::String kicker, title, message, okText { "OK" }, initialText;
    bool wantsText = false;
    std::function<void (const juce::String& text)> onOk;
};

class Prompt : public Overlay
{
public:
    explicit Prompt (PromptSpec);

private:
    void layout (juce::Rectangle<int>) override;
    void restyle() override;
    void confirm() override;
    juce::Component* firstFocus() override { return spec.wantsText ? static_cast<juce::Component*> (&field) : this; }

    PromptSpec spec;
    juce::Label message;
    juce::TextEditor field;
    juce::TextButton ok, cancel { "Cancel" };
};

// Save the current sound into a user bank (or export it as a single file).
class SaveDialog : public Overlay
{
public:
    explicit SaveDialog (PresetManager&);

    void paint (juce::Graphics&) override;

private:
    void layout (juce::Rectangle<int>) override;
    void restyle() override;
    void confirm() override;
    juce::Component* firstFocus() override { return &name; }
    void fillBanks();
    void bankChanged();
    void exportSound();
    Preset collect() const;
    void setStatus (const juce::String&, juce::Colour);
    void resetOverwrite();

    PresetManager& pm;
    juce::TextEditor name, author, notes, newBank;
    juce::ComboBox category, bank;
    juce::Label status;
    juce::TextButton save { "Save" }, cancel { "Cancel" }, exportBtn { "Export file..." };
    std::vector<juce::Rectangle<int>> captionAreas;
    juce::StringArray captions;
    juce::String confirmedOverwrite;        // "bank/name" the user has agreed to replace
    bool readOnlySource = false;
    std::unique_ptr<juce::FileChooser> chooser;

    static constexpr int newBankId = 9999;
};

} // namespace wsho::gui
