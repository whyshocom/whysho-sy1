#include "TopBar.h"
#include "WhyshoLookAndFeel.h"
#include "../PluginProcessor.h"

namespace wsho::gui
{
// ===========================================================================
// The ZENOLOGY-style read-out: bank + category small, the name big, a marker
// when the sound has been edited. Arrows step, the middle opens the browser.
class TopBar::PresetDisplay : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    explicit PresetDisplay (TopBar& o) : owner (o)
    {
        setTooltip ("Click to browse sounds. Arrows or mouse wheel step through the bank");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void set (const juce::String& k, const juce::String& n, const juce::String& idx, bool mod)
    {
        if (k == kicker && n == name && idx == index && mod == modified) return;
        kicker = k; name = n; index = idx; modified = mod;
        repaint();
    }

    void setOpen (bool o) { if (open != o) { open = o; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = juce::jmin (t.radius, 5.0f);

        g.setColour (t.display);
        g.fillRoundedRectangle (b, rad);
        g.setColour (open ? t.accentText : (t.light ? t.text : t.grid.brighter (0.2f)));
        g.drawRoundedRectangle (b, rad, open ? 1.6f : 1.0f);

        for (int side = 0; side < 2; ++side)
        {
            auto z = zone (side);
            if (hover == side) { g.setColour (t.text.withAlpha (0.08f)); g.fillRect (z.reduced (1)); }
            g.setColour (hover == side ? t.text : t.textDim);
            g.fillPath (arrowPath (z.toFloat().withSizeKeepingCentre (8.0f, 11.0f), side == 1));
            g.setColour (t.grid);
            g.fillRect (side == 0 ? z.getRight() : z.getX() - 1, 6, 1, getHeight() - 12);
        }

        auto mid = getLocalBounds().withTrimmedLeft (arrowW + 10).withTrimmedRight (arrowW + 10);
        auto top = mid.removeFromTop (15).withTrimmedTop (4);

        g.setFont (themeFont (9.5f));
        g.setColour (t.textDim);
        const int iw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), index) + 2;
        g.drawText (index, top.removeFromRight (iw), juce::Justification::centredRight, false);

        if (modified)
        {
            g.setFont (kickerFont (8.5f));
            const juce::String tag ("EDITED");
            const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tag) + 8;
            auto tagR = top.removeFromRight (tw + 6).withTrimmedRight (6).toFloat().expanded (0.0f, 0.5f);
            g.setColour (t.accent);
            g.fillRoundedRectangle (tagR, juce::jmin (t.radius, 2.0f));
            g.setColour (t.onAccent);
            g.drawText (tag, tagR, juce::Justification::centred, false);
        }

        g.setFont (kickerFont (9.0f));
        g.setColour (t.accentText);
        g.drawText (caps (kicker), top, juce::Justification::centredLeft, true);

        g.setFont (displayFont (20.0f));
        g.setColour (t.text);
        g.drawText (name, mid.withTrimmedBottom (3), juce::Justification::centredLeft, true);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (zoneAt (e.x)); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int z = zoneAt (e.x);
        if (z < 0) { if (owner.onBrowse) owner.onBrowse(); }
        else owner.proc.presetManager().step (z == 0 ? -1 : 1);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaY) > 1.0e-4f)
            owner.proc.presetManager().step ((w.deltaY > 0) != w.isReversed ? -1 : 1);
    }

private:
    static constexpr int arrowW = 28;
    juce::Rectangle<int> zone (int side) const
    {
        return side == 0 ? getLocalBounds().removeFromLeft (arrowW) : getLocalBounds().removeFromRight (arrowW);
    }
    int zoneAt (int x) const { return x < arrowW ? 0 : x >= getWidth() - arrowW ? 1 : -1; }
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }

    TopBar& owner;
    juce::String kicker, name, index;
    bool modified = false, open = false;
    int hover = -1;
};

// ===========================================================================
// Account / licence chip: a status dot plus "Trial · 12d", the account email
// or "Sign in".
class TopBar::LicenseButton : public juce::Button
{
public:
    LicenseButton() : juce::Button ("license") { setComponentID ("license"); }

    void set (const juce::String& t, juce::Colour c)
    {
        if (t == text && c == dot) return;
        text = t; dot = c;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = theme();
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = juce::jmin (t.radius, 5.0f);
        g.setColour (down ? t.panel2.interpolatedWith (t.text, 0.14f) : over ? t.panel2.interpolatedWith (t.text, 0.06f) : t.panel2);
        g.fillRoundedRectangle (b, rad);
        g.setColour (t.light ? t.textDim : t.grid);
        g.drawRoundedRectangle (b, rad, 1.0f);

        auto r = getLocalBounds().reduced (9, 0);
        auto d = r.removeFromLeft (8).toFloat().withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (dot);
        g.fillEllipse (d);
        r.removeFromLeft (6);
        g.setColour (t.text);
        g.setFont (themeFont (11.0f));
        g.drawFittedText (text, r, juce::Justification::centredLeft, 1, 0.75f);
    }

private:
    juce::String text;
    juce::Colour dot;
};

// ===========================================================================
TopBar::TopBar (WhyshoSynthAudioProcessor& p) : proc (p), meter (p)
{
    preset = std::make_unique<PresetDisplay> (*this);
    license = std::make_unique<LicenseButton>();
    addAndMakeVisible (*preset);
    addAndMakeVisible (*license);
    addAndMakeVisible (meter);

    saveBtn.setTooltip ("Save this sound into one of your banks");
    saveBtn.onClick = [this] { if (onSave) onSave(); };
    menuBtn.setTooltip ("Init patch, import, export");
    menuBtn.onClick = [this] { showMenu(); };
    license->setTooltip ("Account and licence");
    license->onClick = [this] { if (onLicense) onLicense(); };
    for (juce::Button* b : { (juce::Button*) &saveBtn, (juce::Button*) &menuBtn, (juce::Button*) license.get() })
        b->setWantsKeyboardFocus (false);
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (menuBtn);

    themeBox.addItemList (themeNames(), 1);
    themeBox.setTooltip ("Colour theme");
    themeBox.setWantsKeyboardFocus (false);
    themeBox.onChange = [this] { if (onTheme) onTheme (themeBox.getSelectedId() - 1); };
    addAndMakeVisible (themeBox);

    refresh();
}

TopBar::~TopBar() = default;

void TopBar::setBrowserOpen (bool o) { preset->setOpen (o); }

void TopBar::setThemeSelection (int index)
{
    themeBox.setSelectedId (index + 1, juce::dontSendNotification);
}

void TopBar::showMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Init patch");
    m.addSeparator();
    m.addItem (2, "Save preset...");
    m.addItem (3, "Export this sound as a file...");
    m.addSeparator();
    m.addItem (4, "Import bank or preset...");
    m.addItem (5, "Open presets folder");

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto call = [] (std::function<void()>& f) { if (f) f(); };
        switch (r)
        {
            case 1: call (safe->onInit); break;
            case 2: call (safe->onSave); break;
            case 3: call (safe->onExport); break;
            case 4: call (safe->onImport); break;
            case 5: call (safe->onFolder); break;
            default: break;
        }
    });
}

void TopBar::refresh()
{
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    // ---- preset ----
    auto& pm = proc.presetManager();
    const int cb = pm.currentBank(), cp = pm.currentPreset();
    juce::String kicker ("Unsaved"), index;
    if (cb >= 0 && cb < pm.numBanks())
    {
        const auto& bk = pm.bank (cb);
        kicker = bk.name;
        if (cp >= 0 && cp < (int) bk.presets.size())
        {
            if (bk.presets[(size_t) cp].category.isNotEmpty()) kicker << dot << bk.presets[(size_t) cp].category;
            index = juce::String (cp + 1).paddedLeft ('0', 3) + " / " + juce::String ((int) bk.presets.size()).paddedLeft ('0', 3);
        }
    }
    else
    {
        const auto cat = pm.captureCurrent().category;      // session-restored sounds still know their category
        if (cat.isNotEmpty()) kicker << dot << cat;
    }
    auto name = pm.currentName();
    if (name.isEmpty()) name = "Init";
    preset->set (kicker, name, index, pm.isModified());

    // ---- licence ----
    const auto& li = proc.licenseInfo;
    const auto& t = theme();
    juce::String text;
    juce::Colour c = t.bad;
    switch (li.state)
    {
        case lic::LicenseState::Licensed:
            text = li.email.isNotEmpty() ? li.email : juce::String ("Licensed");
            c = t.good;
            break;
        case lic::LicenseState::Trial:
        {
            const auto secs = li.expiresAt - juce::Time::getCurrentTime().toMilliseconds() / 1000;
            text = "Trial" + dot + juce::String (juce::jmax ((juce::int64) 0, (secs + 86399) / 86400)) + "d";
            c = t.warn;
            break;
        }
        case lic::LicenseState::Expired:          text = "Expired" + dot + "Activate"; break;
        case lic::LicenseState::MachineMismatch:
        case lic::LicenseState::Tampered:         text = "Licence problem"; break;
        case lic::LicenseState::Unlicensed:
        default:                                  text = "Sign in"; break;
    }
    license->set (text, c);
}

void TopBar::tick()
{
    meter.tick();
    const int v = proc.activeVoices();
    if (v != voices) { voices = v; repaint (statusArea); }
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.panel);
    g.setColour (t.light ? t.rule : t.grid);
    g.fillRect (0.0f, (float) getHeight() - (t.light ? 1.5f : 1.0f), (float) getWidth(), t.light ? 1.5f : 1.0f);

    // ---- brand: the product code large, riso-style misregistered accent behind the ink ----
    auto b = brandArea;
    g.setFont (displayFont (38.0f));
    auto code = b.removeFromLeft (76);
    g.setColour (t.accent);
    g.drawText ("SY1", code.translated (2, 2), juce::Justification::centredLeft, false);
    g.setColour (t.text);
    g.drawText ("SY1", code, juce::Justification::centredLeft, false);

    b.removeFromLeft (6);
    auto lines = b.withSizeKeepingCentre (b.getWidth(), 28);
    g.setFont (themeFont (13.0f, true));
    g.setColour (t.text);
    g.drawText ("whysho", lines.removeFromTop (15), juce::Justification::centredLeft, false);
    g.setFont (kickerFont (8.5f));
    g.setColour (t.textDim);
    g.drawText ("SYNTHESIZER", lines, juce::Justification::centredLeft, false);

    // ---- status: voices as 16 pips, output meter underneath ----
    auto s = statusArea;
    auto vrow = s.removeFromTop (s.getHeight() / 2);
    g.setFont (kickerFont (8.0f));
    g.setColour (t.textDim);
    g.drawText ("VOX", vrow.removeFromLeft (26), juce::Justification::centredLeft, false);
    g.drawText ("OUT", s.removeFromLeft (26), juce::Justification::centredLeft, false);
    const float pw = (float) vrow.getWidth() / (float) kMaxVoices;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto pip = juce::Rectangle<float> ((float) vrow.getX() + pw * (float) i, (float) vrow.getCentreY() - 4.0f, pw - 1.5f, 8.0f);
        g.setColour (i < voices ? (t.light ? t.text : t.accent) : (t.light ? t.grid.withAlpha (0.8f) : t.panel2));
        g.fillRect (pip);
    }
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (8).withTrimmedBottom (10);
    brandArea = r.removeFromLeft (158);

    license->setBounds (r.removeFromRight (132).reduced (0, 5));
    r.removeFromRight (8);
    themeBox.setBounds (r.removeFromRight (104).reduced (0, 5));
    r.removeFromRight (12);
    statusArea = r.removeFromRight (98).reduced (0, 4);
    meter.setBounds (statusArea.withTrimmedTop (statusArea.getHeight() / 2).withTrimmedLeft (26).withSizeKeepingCentre (72, 9)
                         .withX (statusArea.getX() + 26));
    r.removeFromRight (14);
    menuBtn.setBounds (r.removeFromRight (34).reduced (0, 5));
    r.removeFromRight (6);
    saveBtn.setBounds (r.removeFromRight (62).reduced (0, 5));
    r.removeFromRight (8);
    preset->setBounds (r);
}

} // namespace wsho::gui
