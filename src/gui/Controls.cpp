#include "Controls.h"
#include "WhyshoLookAndFeel.h"

namespace wsho::gui
{
namespace
{
juce::String signedNum (float v, int decimals)
{
    const float eps = 0.5f * std::pow (10.0f, (float) -decimals);
    if (std::abs (v) < eps) return decimals > 0 ? juce::String (0.0f, decimals) : juce::String ("0");
    return (v > 0 ? "+" : "-") + juce::String (std::abs (v), decimals);
}

bool isPan (P p) { return juce::String (paramDef (p).id).endsWith ("_pan"); }

juce::Colour inkColour (int ink) { return ink < 0 ? theme().accent : bandColour (ink); }
} // namespace

// ===========================================================================
juce::String formatParam (P p, float v)
{
    const auto& d = paramDef (p);
    const juce::String unit (d.unit);

    if (d.kind == ParamDef::Choice)
    {
        const auto c = paramChoices (p);
        return c[juce::jlimit (0, juce::jmax (0, c.size() - 1), juce::roundToInt (v))];
    }
    if (d.kind == ParamDef::Bool) return v > 0.5f ? "On" : "Off";

    if (p == P::fltEnv) return signedNum (v * 10.0f, 1) + " oct";       // ±1 = ±10 octaves
    if (isPan (p))
    {
        const int pc = juce::roundToInt (v * 100.0f);
        return pc == 0 ? juce::String ("C") : (pc < 0 ? "L " : "R ") + juce::String (std::abs (pc));
    }
    if (unit == "Hz")
    {
        if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (v, v < 10.0f ? 2 : v < 100.0f ? 1 : 0) + " Hz";
    }
    if (unit == "s")
    {
        if (v >= 1.0f) return juce::String (v, 2) + " s";
        const float ms = v * 1000.0f;
        return juce::String (ms, ms < 10.0f ? 1 : 0) + " ms";
    }
    if (unit == "%")
        return (d.min < 0.0f ? signedNum (v * 100.0f, 0) : juce::String (juce::roundToInt (v * 100.0f))) + " %";
    if (unit == "st")
        return (d.kind == ParamDef::Int ? (d.min < 0.0f ? signedNum (v, 0) : juce::String (juce::roundToInt (v)))
                                        : signedNum (v, 1)) + " st";
    if (unit == "ct") return signedNum (v, 0) + " ct";
    if (unit == "dB") return signedNum (v, 1) + " dB";
    if (d.kind == ParamDef::Int)
        return d.min < 0.0f ? signedNum (v, 0) : juce::String (juce::roundToInt (v));
    return juce::String (v, 2);
}

bool parseParam (P p, const juce::String& rawText, float current, float& out)
{
    const auto& d = paramDef (p);
    const juce::String unit (d.unit);
    const auto t = rawText.trim().toLowerCase();
    if (t.isEmpty()) return false;

    if (isPan (p))
    {
        if (t.startsWithChar ('c')) { out = 0.0f; return true; }
        const float mag = t.retainCharacters ("0123456789.").getFloatValue() / 100.0f;
        out = t.startsWithChar ('l') || t.startsWithChar ('-') ? -mag : mag;
        return true;
    }

    const auto num = t.retainCharacters ("0123456789.-+");
    if (! num.containsAnyOf ("0123456789")) return false;
    float v = num.getFloatValue();

    if (p == P::fltEnv)      v /= 10.0f;
    else if (unit == "Hz")   { if (t.contains ("k")) v *= 1000.0f; }
    else if (unit == "s")
    {
        // A bare number is read in whatever unit the read-out is showing.
        const bool ms = t.contains ("ms") || (! t.endsWithChar ('s') && current < 1.0f);
        if (ms) v /= 1000.0f;
    }
    else if (unit == "%")    v /= 100.0f;

    out = juce::jlimit (d.min, d.max, v);
    return true;
}

juce::Path starPath (juce::Rectangle<float> area)
{
    juce::Path p;
    const auto c = area.getCentre();
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    p.addStar (c, 5, r * 0.45f, r);
    return p;
}

juce::Path arrowPath (juce::Rectangle<float> area, bool right)
{
    juce::Path p;
    const float w = juce::jmin (area.getWidth(), area.getHeight() * 0.7f);
    auto r = area.withSizeKeepingCentre (w, w * 1.3f);
    if (right) p.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
    else       p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getCentreY());
    return p;
}

// ===========================================================================
Knob::Knob (APVTS& s, P p, const juce::String& l, Style st)
    : pid_ (p), style (st), param (*s.getParameter (pid (p))), label (l),
      attach (param, [this] (float v) { value = v; repaint(); })
{
    bipolar = paramDef (p).min < 0.0f;
    setTooltip (paramDef (p).name);
    setRepaintsOnMouseActivity (false);
    attach.sendInitialUpdate();
}

Knob::~Knob()
{
    if (clickAwaySource != nullptr) clickAwaySource->removeMouseListener (&clickAway);
    if (inGesture) attach.endGesture();
}

void Knob::ClickAway::mouseDown (const juce::MouseEvent& e)
{
    auto* field = knob.entry.get();
    if (field == nullptr || e.eventComponent == field || field->isParentOf (e.eventComponent)) return;

    // deferred: we are inside another component's mouse callback
    juce::Component::SafePointer<Knob> safe (&knob);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->endTextEntry (true); });
}

void Knob::setLabel (const juce::String& l)
{
    if (label != l) { label = l; repaint(); }
}

juce::Rectangle<int> Knob::labelArea() const
{
    return style == Style::rotary ? getLocalBounds().removeFromBottom (12) : getLocalBounds();
}

void Knob::setNorm (float n)
{
    attach.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (entry) endTextEntry (true);
    dragNorm = norm();
    lastDrag = e.position;
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! inGesture)
    {
        if (e.getDistanceFromDragStart() < 2) return;
        attach.beginGesture();
        inGesture = true;
    }
    // Relative drag so the fine modifier can be pressed / released mid-gesture.
    const auto d = e.position - lastDrag;
    lastDrag = e.position;
    const bool fine = e.mods.isShiftDown() || e.mods.isCtrlDown() || e.mods.isCommandDown();
    const float travel = fine ? 1400.0f : 190.0f;
    const float delta = style == Style::bar ? d.x / (fine ? 8.0f * (float) getWidth() : (float) getWidth())
                                            : (d.x - d.y) / travel;
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + delta);
    setNorm (dragNorm);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (inGesture) { attach.endGesture(); inGesture = false; }
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (style == Style::rotary && labelArea().contains (e.getPosition()))
        beginTextEntry();
    else
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-4f) return;
    const int steps = param.getNumSteps();
    const bool stepped = paramDef (pid_).kind != ParamDef::Float && steps > 1;
    const float dir = (w.deltaY > 0) != w.isReversed ? 1.0f : -1.0f;
    const float step = stepped ? 1.0f / (float) (steps - 1)
                               : (e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.004f : 0.02f);
    const float target = juce::jlimit (0.0f, 1.0f, norm() + dir * step);
    attach.setValueAsCompleteGesture (param.convertFrom0to1 (target));
}

void Knob::beginTextEntry()
{
    auto* host = getParentComponent();
    if (host == nullptr) return;

    entry = std::make_unique<juce::TextEditor>();
    auto r = labelArea().withSizeKeepingCentre (juce::jmax (64, getWidth()), 18) + getPosition();
    r = r.constrainedWithin (host->getLocalBounds());
    host->addAndMakeVisible (*entry);
    entry->setBounds (r);
    styleTextEditor (*entry, 11.0f);
    entry->setJustification (juce::Justification::centred);
    entry->setText (formatParam (pid_, value), false);
    entry->selectAll();
    entry->grabKeyboardFocus();

    // The editor is destroyed from its own callbacks, so defer the teardown.
    juce::Component::SafePointer<Knob> safe (this);
    auto finish = [safe] (bool commit)
    {
        juce::MessageManager::callAsync ([safe, commit] { if (safe != nullptr) safe->endTextEntry (commit); });
    };
    entry->onReturnKey = [finish] { finish (true); };
    entry->onEscapeKey = [finish] { finish (false); };
    entry->onFocusLost = [finish] { finish (true); };

    clickAwaySource = getTopLevelComponent();
    if (clickAwaySource != nullptr) clickAwaySource->addMouseListener (&clickAway, true);
}

void Knob::endTextEntry (bool commit)
{
    if (entry == nullptr) return;
    if (clickAwaySource != nullptr) clickAwaySource->removeMouseListener (&clickAway);
    clickAwaySource = nullptr;
    const auto text = entry->getText();
    entry.reset();
    float v = 0.0f;
    if (commit && parseParam (pid_, text, value, v))
        attach.setValueAsCompleteGesture (v);
    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    if (style == Style::rotary) paintRotary (g); else paintBar (g);
}

void Knob::paintRotary (juce::Graphics& g)
{
    const auto& t = theme();
    const float fade = dimmed ? 0.36f : 1.0f;
    const bool active = isMouseOverOrDragging();

    auto b = getLocalBounds().toFloat();
    const auto labelR = b.removeFromBottom (12.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight() - 2.0f) * 0.5f - 0.5f;
    const auto c = juce::Point<float> (b.getCentreX(), b.getY() + 1.0f + r + 0.5f);

    const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
    const float n = norm();
    const float angle = a0 + n * (a1 - a0);
    const float arcR = r - 1.5f, discR = r - 5.0f;
    const juce::PathStrokeType stroke (2.6f, juce::PathStrokeType::curved,
                                       t.light ? juce::PathStrokeType::butt : juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (t.grid.withMultipliedAlpha (fade));
    g.strokePath (track, stroke);

    const float from = bipolar ? a0 + param.convertTo0to1 (0.0f) * (a1 - a0) : a0;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (inkColour (ink).withMultipliedAlpha (fade));
        g.strokePath (val, stroke);
    }
    if (bipolar)   // centre detent tick
    {
        g.setColour (t.textDim.withMultipliedAlpha (fade));
        g.fillRect (c.x - 0.5f, c.y - r - 0.5f, 1.0f, 3.0f);
    }

    g.setColour ((active ? t.panel2.interpolatedWith (t.text, 0.06f) : t.panel2).withMultipliedAlpha (dimmed ? 0.6f : 1.0f));
    g.fillEllipse (c.x - discR, c.y - discR, discR * 2.0f, discR * 2.0f);
    g.setColour ((t.light ? t.text : t.grid.brighter (0.2f)).withMultipliedAlpha (fade * (t.light ? 0.85f : 1.0f)));
    g.drawEllipse (c.x - discR, c.y - discR, discR * 2.0f, discR * 2.0f, 1.0f);

    const float sn = std::sin (angle), cs = -std::cos (angle);
    g.setColour (t.text.withMultipliedAlpha (fade));
    g.drawLine (c.x + sn * discR * 0.28f, c.y + cs * discR * 0.28f,
                c.x + sn * (discR - 1.5f), c.y + cs * (discR - 1.5f), 2.0f);

    const bool showValue = active || inGesture;
    g.setFont (themeFont (showValue ? 10.0f : 9.5f, showValue));
    g.setColour ((showValue ? t.text : t.textDim).withMultipliedAlpha (dimmed && ! showValue ? 0.55f : 1.0f));
    if (entry == nullptr)
        g.drawFittedText (showValue ? formatParam (pid_, value) : caps (label),
                          labelR.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

void Knob::paintBar (juce::Graphics& g)
{
    const auto& t = theme();
    const float fade = dimmed ? 0.4f : 1.0f;
    auto b = getLocalBounds().toFloat().reduced (0.5f, 2.5f);
    const float rad = juce::jmin (t.radius, 3.0f);

    g.setColour (t.panel2.withMultipliedAlpha (dimmed ? 0.6f : 1.0f));
    g.fillRoundedRectangle (b, rad);

    const float n = norm();
    const float x0 = bipolar ? b.getX() + b.getWidth() * param.convertTo0to1 (0.0f) : b.getX();
    const float x1 = b.getX() + b.getWidth() * n;
    auto fill = juce::Rectangle<float> (juce::jmin (x0, x1), b.getY(), std::abs (x1 - x0), b.getHeight()).reduced (0.0f, 1.0f);
    g.setColour (inkColour (ink).withMultipliedAlpha ((t.light ? 0.85f : 0.75f) * fade));
    g.fillRect (fill);

    g.setColour ((isMouseOverOrDragging() ? t.textDim : t.grid).withMultipliedAlpha (fade));
    g.drawRoundedRectangle (b, rad, 1.0f);
    if (bipolar)
    {
        g.setColour (t.text.withMultipliedAlpha (0.7f * fade));
        g.fillRect (x0 - 0.5f, b.getY() - 2.0f, 1.0f, b.getHeight() + 4.0f);
    }

    // Value text sits on the empty half so it never fights the fill.
    g.setFont (themeFont (10.0f));
    g.setColour (t.text.withMultipliedAlpha (fade));
    const auto txt = formatParam (pid_, value);
    if (! bipolar)      g.drawText (txt, b.reduced (5.0f, 0.0f), juce::Justification::centredRight, false);
    else if (n >= 0.5f) g.drawText (txt, b.withRight (x0).reduced (5.0f, 0.0f), juce::Justification::centredRight, false);
    else                g.drawText (txt, b.withLeft (x0).reduced (5.0f, 0.0f), juce::Justification::centredLeft, false);
}

// ===========================================================================
ChoiceStrip::ChoiceStrip (APVTS& s, P p, juce::StringArray shortLabels, IconFn fn)
    : pid_ (p), labels (std::move (shortLabels)), names (paramChoices (p)), icons (std::move (fn)),
      attach (*s.getParameter (pid (p)), [this] (float v) { current = juce::roundToInt (v); repaint(); })
{
    if (labels.isEmpty()) labels = names;
    setTooltip (paramDef (p).name);
    attach.sendInitialUpdate();
}

int ChoiceStrip::cellAt (int x) const
{
    return juce::jlimit (0, labels.size() - 1, x * labels.size() / juce::jmax (1, getWidth()));
}

void ChoiceStrip::mouseDown (const juce::MouseEvent& e)
{
    attach.setValueAsCompleteGesture ((float) cellAt (e.x));
}

void ChoiceStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = cellAt (e.x);
    if (h != hover)
    {
        hover = h;
        setTooltip (names[h]);
        repaint();
    }
}

void ChoiceStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-4f) return;
    const int dir = (w.deltaY > 0) != w.isReversed ? -1 : 1;
    attach.setValueAsCompleteGesture ((float) juce::jlimit (0, labels.size() - 1, current + dir));
}

void ChoiceStrip::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const float fade = dimmed ? 0.4f : 1.0f;
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = juce::jmin (t.radius, 4.0f);
    const int n = labels.size();
    const float cw = b.getWidth() / (float) n;

    g.setColour (t.panel2.withMultipliedAlpha (dimmed ? 0.6f : 1.0f));
    g.fillRoundedRectangle (b, rad);

    for (int i = 0; i < n; ++i)
    {
        auto cell = juce::Rectangle<float> (b.getX() + cw * (float) i, b.getY(), cw, b.getHeight());
        const bool sel = i == current;
        if (sel)
        {
            g.setColour (inkColour (ink).withMultipliedAlpha (fade));
            g.fillRoundedRectangle (cell.reduced (1.0f), juce::jmax (0.0f, rad - 1.0f));
        }
        else if (i == hover)
        {
            g.setColour (t.text.withAlpha (0.07f));
            g.fillRect (cell.reduced (1.0f));
        }
        if (i > 0 && ! sel && i - 1 != current)
        {
            g.setColour (t.grid.withMultipliedAlpha (fade));
            g.fillRect (cell.getX() - 0.5f, cell.getY() + 3.0f, 1.0f, cell.getHeight() - 6.0f);
        }

        g.setColour ((sel ? t.onAccent : (i == hover ? t.text : t.textDim)).withMultipliedAlpha (fade));
        if (icons != nullptr)
            icons (g, cell.reduced (juce::jmin (5.0f, cw * 0.2f), juce::jmin (4.5f, cell.getHeight() * 0.25f)), i);
        else
        {
            g.setFont (themeFont (juce::jmin (10.0f, cell.getHeight() * 0.62f), sel));
            g.drawText (caps (labels[i]), cell, juce::Justification::centred, false);
        }
    }

    g.setColour ((t.light ? t.textDim : t.grid).withMultipliedAlpha (fade));
    g.drawRoundedRectangle (b, rad, 1.0f);
}

// ===========================================================================
ToggleChip::ToggleChip (APVTS& s, P p, const juce::String& txt, Style st)
    : pid_ (p), style (st), text (txt),
      attach (*s.getParameter (pid (p)), [this] (float v)
      {
          const bool now = v > 0.5f;
          if (now != on) { on = now; repaint(); if (onChange) onChange(); }
      })
{
    setTooltip (paramDef (p).name);
    attach.sendInitialUpdate();
}

void ToggleChip::mouseDown (const juce::MouseEvent&)
{
    attach.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void ToggleChip::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const float fade = dimmed ? 0.4f : 1.0f;
    const bool over = isMouseOver();
    const auto col = inkColour (ink);

    if (style == Style::power)
    {
        const float d = juce::jmin ((float) getWidth(), (float) getHeight(), 14.0f);
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (d, d).reduced (1.0f);
        const float rad = t.light ? juce::jmin (t.radius, 2.0f) : r.getWidth() * 0.5f;
        if (on)
        {
            if (t.curveGlow) { g.setColour (col.withAlpha (0.25f)); g.fillRoundedRectangle (r.expanded (2.0f), rad + 2.0f); }
            g.setColour (col);
            g.fillRoundedRectangle (r, rad);
        }
        else
        {
            g.setColour (t.panel2);
            g.fillRoundedRectangle (r, rad);
        }
        g.setColour (on ? col.interpolatedWith (t.text, 0.35f) : (over ? t.text : t.textDim));
        g.drawRoundedRectangle (r, rad, 1.0f);

        // power glyph: broken ring + stem
        const auto c = r.getCentre();
        const float gr = r.getWidth() * 0.24f;
        juce::Path p;
        p.addCentredArc (c.x, c.y + 0.3f, gr, gr, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
        p.startNewSubPath (c.x, c.y - gr - 0.8f);
        p.lineTo (c.x, c.y);
        g.setColour (on ? t.onAccent : (over ? t.text : t.textDim));
        g.strokePath (p, juce::PathStrokeType (1.1f));
        return;
    }

    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = juce::jmin (t.radius, 3.0f);
    g.setColour (on ? col.withMultipliedAlpha (fade) : t.panel2.withMultipliedAlpha (dimmed ? 0.6f : 1.0f));
    g.fillRoundedRectangle (r, rad);
    g.setColour ((on ? col.interpolatedWith (t.text, 0.3f) : (over ? t.textDim : (t.light ? t.textDim : t.grid)))
                     .withMultipliedAlpha (fade));
    g.drawRoundedRectangle (r, rad, 1.0f);
    g.setColour ((on ? t.onAccent : (over ? t.text : t.textDim)).withMultipliedAlpha (fade));
    g.setFont (kickerFont (juce::jmin (9.5f, (float) getHeight() * 0.6f)));
    g.drawText (caps (text), getLocalBounds(), juce::Justification::centred, false);
}

// ===========================================================================
ParamCombo::ParamCombo (APVTS& s, P p)
    : pid_ (p), attach (s, pid (p), (addItemList (paramChoices (p), 1), *this))
{
    setTooltip (paramDef (p).name);
    setScrollWheelEnabled (true);
}

} // namespace wsho::gui
