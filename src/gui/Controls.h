#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Parameters.h"
#include "Theme.h"

namespace wsho::gui
{
using APVTS = juce::AudioProcessorValueTreeState;

// Parameter value <-> text in the units a producer thinks in ("1.2 kHz",
// "150 ms", "+7 st", "L 30"). Derived from ParamDef (unit, range, kind).
juce::String formatParam (P p, float value);
bool parseParam (P p, const juce::String& text, float current, float& out);

// Controls that can be greyed while still editable (oscillator off, FX off).
struct Dimmable
{
    virtual ~Dimmable() = default;
    virtual void setDimmed (bool) = 0;
};

// Shared glyphs, drawn as paths so they look the same with any system font.
juce::Path starPath (juce::Rectangle<float> area);
juce::Path arrowPath (juce::Rectangle<float> area, bool pointsRight);

// ---------------------------------------------------------------------------
// The house knob. Rotary dial with the name underneath; while hovered or
// dragged the name is replaced by the live value. Drag (shift/ctrl = fine),
// wheel, double-click the dial = default, double-click the label = type a value.
// `bar` style is the horizontal bipolar slider used by the mod matrix.
class Knob : public juce::Component,
             public juce::SettableTooltipClient,
             public Dimmable
{
public:
    enum class Style { rotary, bar };

    Knob (APVTS&, P, const juce::String& label, Style = Style::rotary);
    ~Knob() override;

    void setLabel (const juce::String&);
    void setInk (int paletteIndex)  { ink = paletteIndex; repaint(); }   // -1 = theme accent
    void setDimmed (bool d) override { if (dimmed != d) { dimmed = d; repaint(); } }
    P parameter() const { return pid_; }

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<int> labelArea() const;
    float norm() const { return param.convertTo0to1 (value); }
    void setNorm (float n);
    void beginTextEntry();
    void endTextEntry (bool commit);
    void paintRotary (juce::Graphics&);
    void paintBar (juce::Graphics&);

    const P pid_;
    const Style style;
    juce::RangedAudioParameter& param;
    juce::String label;
    float value = 0.0f;
    int   ink = -1;
    bool  bipolar = false, dimmed = false, inGesture = false;
    float dragNorm = 0.0f;
    juce::Point<float> lastDrag;
    std::unique_ptr<juce::TextEditor> entry;

    // While the value field is open: a click anywhere else in the window commits
    // it. (Focus alone doesn't: JUCE leaves focus where it is when the click lands
    // on a non-focusable sibling, e.g. another knob of the same section.)
    struct ClickAway : juce::MouseListener
    {
        explicit ClickAway (Knob& k) : knob (k) {}
        void mouseDown (const juce::MouseEvent&) override;
        Knob& knob;
    };
    ClickAway clickAway { *this };
    juce::Component::SafePointer<juce::Component> clickAwaySource;

    juce::ParameterAttachment attach;      // last: detaches before the rest dies

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

// ---------------------------------------------------------------------------
// One-of-N selector drawn as a segmented strip (waveforms, filter types, voice
// mode). Segments show short text or an icon.
class ChoiceStrip : public juce::Component,
                    public juce::SettableTooltipClient,
                    public Dimmable
{
public:
    using IconFn = std::function<void (juce::Graphics&, juce::Rectangle<float>, int index)>;

    ChoiceStrip (APVTS&, P, juce::StringArray shortLabels = {}, IconFn icons = nullptr);

    void setInk (int paletteIndex)   { ink = paletteIndex; repaint(); }
    void setDimmed (bool d) override { if (dimmed != d) { dimmed = d; repaint(); } }
    int  selected() const            { return current; }
    P    parameter() const           { return pid_; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int cellAt (int x) const;

    const P pid_;
    juce::StringArray labels, names;
    IconFn icons;
    int current = 0, hover = -1, ink = -1;
    bool dimmed = false;
    juce::ParameterAttachment attach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceStrip)
};

// ---------------------------------------------------------------------------
// Bool parameter as a power LED (section on/off) or a small text chip (SYNC).
class ToggleChip : public juce::Component,
                   public juce::SettableTooltipClient,
                   public Dimmable
{
public:
    enum class Style { power, text };

    ToggleChip (APVTS&, P, const juce::String& text, Style);

    bool isOn() const                { return on; }
    P    parameter() const           { return pid_; }
    void setInk (int paletteIndex)   { ink = paletteIndex; repaint(); }
    void setDimmed (bool d) override { if (dimmed != d) { dimmed = d; repaint(); } }
    std::function<void()> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    const P pid_;
    const Style style;
    juce::String text;
    bool on = false, dimmed = false;
    int  ink = -1;
    juce::ParameterAttachment attach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleChip)
};

// ---------------------------------------------------------------------------
// Choice parameter as a themed drop-down.
class ParamCombo : public juce::ComboBox,
                   public Dimmable
{
public:
    ParamCombo (APVTS&, P);
    void setDimmed (bool d) override { setAlpha (d ? 0.4f : 1.0f); }
    P    parameter() const           { return pid_; }

private:
    const P pid_;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment attach;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamCombo)
};

} // namespace wsho::gui
