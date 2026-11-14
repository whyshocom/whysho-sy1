#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"

namespace wsho::gui
{
// The on-screen keys, drawn flat in theme colours (paper keys with ink
// sharps on the light themes, the reverse on the dark ones).
class Keyboard : public juce::MidiKeyboardComponent
{
public:
    explicit Keyboard (juce::MidiKeyboardState& s)
        : juce::MidiKeyboardComponent (s, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        setScrollButtonsVisible (false);
        setAvailableRange (firstNote, lastNote);
        setKeyPressBaseOctave (4);              // "A" key = C3: bass territory, Z / X shift octaves
        setWantsKeyboardFocus (false);          // the editor forwards keys, so focus can stay anywhere
        setMouseClickGrabsKeyboardFocus (false);
        applyTheme();
    }

    void applyTheme()
    {
        const auto& t = theme();
        setColour (whiteNoteColourId, t.keyWhite);
        setColour (blackNoteColourId, t.keyBlack);
        setColour (keySeparatorLineColourId, t.light ? t.text.withAlpha (0.55f) : t.bg);
        setColour (mouseOverKeyOverlayColourId, t.accent.withAlpha (0.25f));
        setColour (keyDownOverlayColourId, t.accent);
        setColour (textLabelColourId, t.light ? t.textDim : t.bg.withAlpha (0.7f));
        setColour (shadowColourId, juce::Colours::transparentBlack);
        repaint();
    }

    void resized() override
    {
        setKeyWidth ((float) getWidth() / (float) numWhiteKeys);
        setBlackNoteLengthProportion (0.62f);
        juce::MidiKeyboardComponent::resized();
    }

    void shiftOctave (int delta)
    {
        octave = juce::jlimit (1, 7, octave + delta);
        setKeyPressBaseOctave (octave);
    }

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override
    {
        const auto& t = theme();
        g.setColour (isDown ? t.accent : isOver ? t.keyWhite.interpolatedWith (t.accent, 0.22f) : t.keyWhite);
        g.fillRect (area);
        g.setColour (lineColour);
        g.fillRect (area.withWidth (1.0f));
        if (note == lastNote) g.fillRect (area.withLeft (area.getRight() - 1.0f));
        if (note % 12 == 0)
        {
            g.setColour (isDown ? t.onAccent : textColour);
            g.setFont (themeFont (8.5f));
            g.drawText ("C" + juce::String (note / 12 - 2), area.withTrimmedBottom (3.0f).withTrimmedLeft (1.0f),
                        juce::Justification::centredBottom, false);
        }
    }

    void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour) override
    {
        const auto& t = theme();
        g.setColour (isDown ? t.accent : isOver ? t.keyBlack.interpolatedWith (t.accent, 0.45f) : t.keyBlack);
        g.fillRect (area);
        if (! t.light)
        {
            g.setColour (t.grid);
            g.drawRect (area, 1.0f);
        }
    }

    static constexpr int firstNote = 24, lastNote = 107, numWhiteKeys = 49;     // C0 … B6, seven octaves

private:
    int octave = 4;
};

} // namespace wsho::gui
