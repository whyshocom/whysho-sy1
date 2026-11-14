#include "WhyshoLookAndFeel.h"

using namespace wsho::gui;

WhyshoLookAndFeel::WhyshoLookAndFeel() { applyTheme(); }

void WhyshoLookAndFeel::applyTheme()
{
    const auto& t = theme();
    setColour (juce::ResizableWindow::backgroundColourId, t.bg);
    setColour (juce::Slider::textBoxTextColourId, t.text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, t.panel2);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.grid);
    setColour (juce::ComboBox::arrowColourId, t.textDim);
    setColour (juce::PopupMenu::backgroundColourId, t.panel2);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::headerTextColourId, t.textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, t.onAccent);
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, t.onAccent);
    setColour (juce::TextEditor::backgroundColourId, t.panel2);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.grid);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
    setColour (juce::TextEditor::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accentText);
    setColour (juce::CaretComponent::caretColourId, t.accentText);
    setColour (juce::TooltipWindow::backgroundColourId, t.light ? t.text : t.panel2);
    setColour (juce::TooltipWindow::textColourId, t.light ? t.bg : t.text);
    setColour (juce::TooltipWindow::outlineColourId, t.grid);
    setColour (juce::ScrollBar::thumbColourId, t.textDim);
}

juce::Font WhyshoLookAndFeel::getLabelFont (juce::Label& l) { return l.getFont(); }

void WhyshoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const auto r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = a0 + pos * (a1 - a0);

    g.setColour (theme().panel2);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, a0, a1, true);
    g.setColour (theme().grid);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, a0, angle, true);
    g.setColour (s.isEnabled() ? theme().accent : theme().textDim);
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip (c.x + (r - 6) * std::cos (angle - juce::MathConstants<float>::halfPi),
                            c.y + (r - 6) * std::sin (angle - juce::MathConstants<float>::halfPi));
    g.setColour (theme().text);
    g.drawLine ({ c, tip }, 2.0f);
    g.setColour (theme().accent);
    g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
}

void WhyshoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                          float pos, float, float,
                                          const juce::Slider::SliderStyle, juce::Slider&)
{
    auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f, (float) w, 4.0f);
    g.setColour (theme().grid);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (theme().accent);
    g.fillRoundedRectangle (track.withWidth (pos - (float) x), 2.0f);
    g.setColour (theme().text);
    g.fillEllipse (pos - 6.0f, (float) y + (float) h * 0.5f - 6.0f, 12.0f, 12.0f);
}

void WhyshoLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& t = theme();
    auto b = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    const float rad = juce::jmin (t.radius, 4.0f);
    g.setColour (t.panel2);
    g.fillRoundedRectangle (b, rad);
    g.setColour (box.hasKeyboardFocus (true) ? t.accentText : t.grid);
    g.drawRoundedRectangle (b, rad, 1.0f);
    juce::Path p;
    const float cx = (float) w - 9.0f, cy = (float) h * 0.5f;
    p.addTriangle (cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (t.textDim);
    g.fillPath (p);
}

void WhyshoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (5, 0, box.getWidth() - 19, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.7f);
}

juce::Font WhyshoLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return themeFont (juce::jlimit (10.0f, 12.0f, (float) box.getHeight() * 0.58f));
}

juce::Font WhyshoLookAndFeel::getPopupMenuFont() { return themeFont (12.5f); }

void WhyshoLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (theme().panel2);
    g.setColour (theme().light ? theme().text : theme().grid);
    g.drawRect (0, 0, w, h, 1);
}

void WhyshoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& bt, const juce::Colour&, bool hl, bool down)
{
    const auto& t = theme();
    auto b = bt.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = (bool) bt.getProperties().getWithDefault (primaryButton, false);
    const bool filled  = primary || bt.getToggleState();
    auto base = filled ? t.accent : t.panel2;
    if (down)    base = base.interpolatedWith (t.text, 0.18f);
    else if (hl) base = base.interpolatedWith (t.text, filled ? 0.08f : 0.06f);
    const float rad = juce::jmin (t.radius, 5.0f);
    g.setColour (base.withMultipliedAlpha (bt.isEnabled() ? 1.0f : 0.45f));
    g.fillRoundedRectangle (b, rad);
    g.setColour (filled ? t.accent.interpolatedWith (t.text, 0.25f) : (t.light ? t.textDim : t.grid));
    g.drawRoundedRectangle (b, rad, 1.0f);
}

juce::Font WhyshoLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return kickerFont (juce::jlimit (9.5f, 11.0f, (float) buttonHeight * 0.48f));
}

void WhyshoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& bt, bool, bool)
{
    const auto& t = theme();
    const bool filled = (bool) bt.getProperties().getWithDefault (primaryButton, false) || bt.getToggleState();
    auto c = filled ? t.onAccent
                    : (bt.isColourSpecified (juce::TextButton::textColourOffId)
                           ? bt.findColour (juce::TextButton::textColourOffId) : t.text);
    g.setColour (c.withMultipliedAlpha (bt.isEnabled() ? 1.0f : 0.4f));
    g.setFont (getTextButtonFont (bt, bt.getHeight()));
    g.drawFittedText (caps (bt.getButtonText()), bt.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1, 0.75f);
}

void WhyshoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hl, bool)
{
    const auto& t = theme();
    const bool on = b.getToggleState();
    auto bounds = b.getLocalBounds().toFloat();

    // --- pill switch ---
    const float h = juce::jmin (16.0f, bounds.getHeight());
    auto track = bounds.removeFromLeft (30.0f).withSizeKeepingCentre (30.0f, h);
    g.setColour (on ? t.accent : (t.light ? t.grid : t.panel2));
    g.fillRoundedRectangle (track, h * 0.5f);
    if (! on) { g.setColour (t.grid); g.drawRoundedRectangle (track, h * 0.5f, 1.0f); }

    const float knobR = h * 0.5f - 2.0f;
    const float kx = on ? track.getRight() - knobR - 2.0f : track.getX() + knobR + 2.0f;
    g.setColour (on ? t.onAccent : t.textDim);
    g.fillEllipse (kx - knobR, track.getCentreY() - knobR, knobR * 2, knobR * 2);

    // --- label ---
    g.setColour (on ? t.text : t.textDim.withAlpha (hl ? 1.0f : 0.85f));
    g.setFont (themeFont (11.0f));
    g.drawText (b.getButtonText(), bounds.withTrimmedLeft (6), juce::Justification::centredLeft);
}

void WhyshoLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, juce::jmin (theme().radius, 4.0f));
}

void WhyshoLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    const bool focus = ed.hasKeyboardFocus (true) && ! ed.isReadOnly();
    g.setColour (focus ? theme().accentText : theme().grid);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, juce::jmin (theme().radius, 4.0f),
                            focus ? 1.5f : 1.0f);
}

void WhyshoLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                       int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (theme().textDim.withAlpha (down ? 0.9f : over ? 0.7f : 0.45f));
    g.fillRoundedRectangle (thumb.toFloat(), 2.0f);
}

juce::Rectangle<int> WhyshoLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos,
                                                          juce::Rectangle<int> parentArea)
{
    const auto font = themeFont (11.5f);
    const int w = juce::jmin (260, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + 16);
    const int h = 22;
    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 14,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 6) : pos.y + 18, w, h)
               .constrainedWithin (parentArea);
}

void WhyshoLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRect (0, 0, w, h, 1);
    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (themeFont (11.5f));
    g.drawFittedText (text, 6, 0, w - 12, h, juce::Justification::centredLeft, 1, 0.8f);
}

void wsho::gui::styleTextEditor (juce::TextEditor& ed, float fontHeight)
{
    const auto& t = theme();
    ed.setColour (juce::TextEditor::backgroundColourId, t.panel2);
    ed.setColour (juce::TextEditor::textColourId, t.text);
    ed.setColour (juce::TextEditor::outlineColourId, t.grid);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, t.accentText);
    ed.setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
    ed.setColour (juce::CaretComponent::caretColourId, t.accentText);
    ed.setFont (themeFont (fontHeight));
    ed.applyFontToAllText (themeFont (fontHeight));
    ed.applyColourToAllText (t.text);
    if (! ed.isMultiLine()) ed.setJustification (juce::Justification::centredLeft);
    ed.setIndents (6, ed.isMultiLine() ? 5 : 0);
    ed.repaint();
}
