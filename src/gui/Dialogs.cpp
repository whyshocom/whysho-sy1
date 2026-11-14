#include "Dialogs.h"
#include "WhyshoLookAndFeel.h"
#include "UiSettings.h"

namespace wsho::gui
{
// ===========================================================================
Overlay::Overlay (const juce::String& k, const juce::String& t, int w, int h)
    : kicker (k), title (t), panelW (w), panelH (h)
{
    setWantsKeyboardFocus (true);
}

juce::Rectangle<int> Overlay::panel() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (panelW, getWidth() - 16), juce::jmin (panelH, getHeight() - 16));
}

juce::Rectangle<int> Overlay::content() const
{
    return panel().reduced (22, 18).withTrimmedTop (48);
}

void Overlay::parentHierarchyChanged()
{
    if (isShowing())
        if (auto* c = firstFocus())
            c->grabKeyboardFocus();
}

void Overlay::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.bg.withAlpha (t.light ? 0.80f : 0.84f));

    const auto p = panel().toFloat();
    if (t.light)
    {
        // riso-style hard offset shadow instead of a soft blur
        g.setColour (t.accent);
        g.fillRect (p.translated (6.0f, 6.0f));
        g.setColour (t.panel2);
        g.fillRect (p);
        g.setColour (t.text);
        g.drawRect (p, 1.5f);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (p.translated (0.0f, 6.0f).expanded (2.0f), t.radius + 6.0f);
        g.setColour (t.panel);
        g.fillRoundedRectangle (p, t.radius + 3.0f);
        g.setColour (t.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (p, t.radius + 3.0f, 1.2f);
    }

    auto head = panel().reduced (22, 18).removeFromTop (44);
    g.setFont (kickerFont (9.5f));
    g.setColour (t.accentText);
    g.drawText (caps (kicker), head.removeFromTop (12), juce::Justification::centredLeft, false);
    g.setFont (displayFont (23.0f));
    g.setColour (t.text);
    g.drawText (title, head, juce::Justification::centredLeft, true);
}

void Overlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition())) dismiss();
}

bool Overlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { dismiss(); return true; }
    if (k == juce::KeyPress::returnKey) { confirm(); return true; }
    return true;    // swallow: nothing behind a sheet should react to keys
}

// ===========================================================================
Prompt::Prompt (PromptSpec s)
    : Overlay (s.kicker, s.title, 410, s.wantsText ? 222 : 176), spec (std::move (s))
{
    message.setText (spec.message, juce::dontSendNotification);
    message.setJustificationType (juce::Justification::topLeft);
    message.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (message);

    if (spec.wantsText)
    {
        field.setText (spec.initialText, false);
        field.setSelectAllWhenFocused (true);
        field.onReturnKey = [this] { confirm(); };
        field.onEscapeKey = [this] { dismiss(); };
        addAndMakeVisible (field);
    }

    ok.setButtonText (spec.okText);
    ok.getProperties().set (primaryButton, true);
    ok.onClick = [this] { confirm(); };
    cancel.onClick = [this] { dismiss(); };
    addAndMakeVisible (ok);
    addAndMakeVisible (cancel);
    restyle();
}

void Prompt::restyle()
{
    message.setFont (themeFont (12.0f));
    message.setColour (juce::Label::textColourId, theme().text);
    styleTextEditor (field, 13.0f);
}

void Prompt::layout (juce::Rectangle<int> c)
{
    auto buttons = c.removeFromBottom (28);
    ok.setBounds (buttons.removeFromRight (120));
    buttons.removeFromRight (8);
    cancel.setBounds (buttons.removeFromRight (90));
    c.removeFromBottom (12);
    if (spec.wantsText)
    {
        field.setBounds (c.removeFromBottom (28));
        c.removeFromBottom (8);
    }
    message.setBounds (c.expanded (4, 0));
}

void Prompt::confirm()
{
    const auto text = field.getText().trim();
    if (spec.wantsText && text.isEmpty()) return;
    auto cb = spec.onOk;
    dismiss();                       // may delete this
    if (cb) cb (text);
}

// ===========================================================================
SaveDialog::SaveDialog (PresetManager& m)
    : Overlay ("Save sound", "Save preset", 450, 404), pm (m)
{
    const auto cur = pm.captureCurrent();
    const int cb = pm.currentBank();
    readOnlySource = cb >= 0 && cb < pm.numBanks() && pm.bank (cb).factory;

    name.setText (cur.name.isNotEmpty() ? cur.name : pm.currentName(), false);
    name.setTextToShowWhenEmpty ("Name your sound", theme().textDim);
    name.setSelectAllWhenFocused (true);
    author.setText (cur.author.isNotEmpty() && ! readOnlySource ? cur.author : settings::get ("author"), false);
    author.setTextToShowWhenEmpty ("Your producer name", theme().textDim);
    notes.setMultiLine (true);
    notes.setReturnKeyStartsNewLine (false);
    notes.setText (cur.notes, false);
    notes.setTextToShowWhenEmpty ("Optional: what it's for, how to play it", theme().textDim);
    newBank.setText ("User", false);
    newBank.setTextToShowWhenEmpty ("Bank name", theme().textDim);

    category.addItemList (PresetManager::categories(), 1);
    category.setTextWhenNothingSelected ("Uncategorised");
    const int ci = PresetManager::categories().indexOf (cur.category, true);
    if (ci >= 0) category.setSelectedId (ci + 1, juce::dontSendNotification);

    fillBanks();
    bank.onChange = [this] { bankChanged(); resetOverwrite(); };
    name.onTextChange = [this] { resetOverwrite(); };
    newBank.onTextChange = [this] { resetOverwrite(); };

    for (auto* ed : { &name, &author, &notes, &newBank })
    {
        ed->onReturnKey = [this] { confirm(); };
        ed->onEscapeKey = [this] { dismiss(); };
        addAndMakeVisible (ed);
    }
    addAndMakeVisible (category);
    addAndMakeVisible (bank);

    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);

    save.getProperties().set (primaryButton, true);
    save.onClick = [this] { confirm(); };
    cancel.onClick = [this] { dismiss(); };
    exportBtn.onClick = [this] { exportSound(); };
    exportBtn.setTooltip ("Write this sound to a single .sy1preset file you can send to someone");
    addAndMakeVisible (save);
    addAndMakeVisible (cancel);
    addAndMakeVisible (exportBtn);

    captions = { "Name", "Category", "Author", "Notes", "Bank", "New bank" };
    restyle();
    bankChanged();
    resetOverwrite();
}

void SaveDialog::fillBanks()
{
    bank.clear (juce::dontSendNotification);
    int firstUser = -1;
    for (int i = 0; i < pm.numBanks(); ++i)
        if (! pm.bank (i).factory)
        {
            bank.addItem (pm.bank (i).name, i + 1);
            if (firstUser < 0) firstUser = i;
        }
    if (firstUser >= 0) bank.addSeparator();
    bank.addItem ("New bank...", newBankId);

    const int cb = pm.currentBank();
    const bool curIsUser = cb >= 0 && cb < pm.numBanks() && ! pm.bank (cb).factory;
    bank.setSelectedId (curIsUser ? cb + 1 : firstUser >= 0 ? firstUser + 1 : newBankId, juce::dontSendNotification);
}

void SaveDialog::bankChanged()
{
    const bool wantsName = bank.getSelectedId() == newBankId;
    newBank.setVisible (wantsName);
    setPanelHeight (wantsName ? 404 : 372);      // the sheet grows a row for the new bank's name
}

void SaveDialog::setStatus (const juce::String& s, juce::Colour c)
{
    status.setText (s, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, c);
}

void SaveDialog::resetOverwrite()
{
    confirmedOverwrite.clear();
    save.setButtonText ("Save");
    if (readOnlySource)
        setStatus ("Factory presets are read-only, so this saves your version as a copy in a user bank.", theme().textDim);
    else
        setStatus ({}, theme().textDim);
}

void SaveDialog::restyle()
{
    styleTextEditor (name, 13.0f);
    styleTextEditor (author, 12.0f);
    styleTextEditor (notes, 12.0f);
    styleTextEditor (newBank, 12.0f);
    status.setFont (themeFont (11.0f));
}

void SaveDialog::layout (juce::Rectangle<int> c)
{
    captionAreas.clear();
    const int capW = 76, gap = 6;
    const auto row = [&] (juce::Component& comp, int h)
    {
        auto r = c.removeFromTop (h);
        captionAreas.push_back (r.removeFromLeft (capW).withHeight (26));
        comp.setBounds (r);
        c.removeFromTop (gap);
    };
    row (name, 28);
    row (category, 26);
    row (author, 26);
    row (notes, 44);
    row (bank, 26);
    if (newBank.isVisible()) row (newBank, 26);

    auto buttons = c.removeFromBottom (28);
    exportBtn.setBounds (buttons.removeFromLeft (120));
    save.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancel.setBounds (buttons.removeFromRight (84));
    c.removeFromBottom (8);
    status.setBounds (c.expanded (4, 0));
}

void SaveDialog::paint (juce::Graphics& g)
{
    Overlay::paint (g);
    g.setFont (kickerFont (9.5f));
    g.setColour (theme().textDim);
    for (size_t i = 0; i < captionAreas.size(); ++i)
        if (i != 5 || newBank.isVisible())
            g.drawText (caps (captions[(int) i]), captionAreas[i], juce::Justification::centredLeft, false);
}

Preset SaveDialog::collect() const
{
    auto p = pm.captureCurrent();
    p.name     = name.getText().trim();
    p.category = category.getSelectedId() > 0 ? category.getText() : juce::String();
    p.author   = author.getText().trim();
    p.notes    = notes.getText().trim();
    return p;
}

void SaveDialog::confirm()
{
    const auto p = collect();
    if (p.name.isEmpty())
    {
        setStatus ("Give the sound a name first.", theme().bad);
        name.grabKeyboardFocus();
        return;
    }

    int target = bank.getSelectedId() - 1;
    juce::String bankName;
    const bool makeNew = bank.getSelectedId() == newBankId;
    if (makeNew)
    {
        bankName = newBank.getText().trim();
        if (bankName.isEmpty()) { setStatus ("Give the new bank a name.", theme().bad); return; }
        target = -1;
        for (int i = 0; i < pm.numBanks(); ++i)        // typed the name of an existing user bank: use it
            if (! pm.bank (i).factory && pm.bank (i).name.equalsIgnoreCase (bankName)) target = i;
    }
    else
    {
        // resolve by name: bank indices may have shifted since the list was filled
        bankName = bank.getText();
        target = -1;
        for (int i = 0; i < pm.numBanks(); ++i)
            if (! pm.bank (i).factory && pm.bank (i).name == bankName) target = i;
        if (target < 0) { fillBanks(); bankChanged(); setStatus ("Pick a bank to save into.", theme().bad); return; }
    }

    if (target >= 0)
    {
        bool exists = false;
        for (auto& e : pm.bank (target).presets) exists = exists || e.name.equalsIgnoreCase (p.name);
        const auto key = bankName + "/" + p.name;
        if (exists && confirmedOverwrite != key)
        {
            confirmedOverwrite = key;
            save.setButtonText ("Overwrite");
            setStatus (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + p.name
                           + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d already exists in ")) + bankName
                           + ". Press Overwrite to replace it, or change the name.", theme().warn);
            return;
        }
    }
    else
    {
        target = pm.createBank (bankName, p.author);
        if (target < 0)
        {
            setStatus ("Couldn't create the bank. Check that the presets folder is writable.", theme().bad);
            return;
        }
    }

    if (! pm.savePreset (target, p))
    {
        setStatus ("Couldn't save. The bank file isn't writable.", theme().bad);
        return;
    }
    if (p.author.isNotEmpty()) settings::set ("author", p.author);
    dismiss();
}

void SaveDialog::exportSound()
{
    const auto p = collect();
    if (p.name.isEmpty()) { setStatus ("Give the sound a name first.", theme().bad); return; }

    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (juce::File::createLegalFileName (p.name) + PresetManager::presetExtension);
    chooser = std::make_unique<juce::FileChooser> ("Export sound", start, juce::String ("*") + PresetManager::presetExtension);
    juce::Component::SafePointer<SaveDialog> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, p] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension (PresetManager::presetExtension);
        if (safe->pm.exportPreset (p, f))
        {
            if (p.author.isNotEmpty()) settings::set ("author", p.author);
            safe->setStatus ("Exported " + f.getFileName(), theme().good);
        }
        else
            safe->setStatus ("Couldn't write " + f.getFileName() + ".", theme().bad);
    });
}

} // namespace wsho::gui
