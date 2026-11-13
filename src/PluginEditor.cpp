#include "PluginEditor.h"
#include "gui/UiSettings.h"

namespace wsho
{
using namespace gui;

namespace
{
const juce::Identifier kTheme ("uiTheme"), kWidth ("uiW"), kHeight ("uiH");

bool isLibraryFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (PresetManager::bankExtension) || path.endsWithIgnoreCase (PresetManager::presetExtension);
}
} // namespace

WhyshoSynthAudioProcessorEditor::WhyshoSynthAudioProcessorEditor (WhyshoSynthAudioProcessor& p)
    : juce::AudioProcessorEditor (p), proc (p),
      topBar (p), synth (p), browser (p.presetManager()), keys (p.keyboardState())
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    setOpaque (true);

    addAndMakeVisible (root);
    root.addAndMakeVisible (synth);
    root.addChildComponent (browser);
    root.addAndMakeVisible (keys);
    root.addAndMakeVisible (topBar);

    // ---- theme: per-instance choice lives in the plugin state (as in EQ1);
    //      a brand-new instance starts with the theme the user picked last.
    auto& tree = proc.state().state;
    int themeIndex = 0;
    if (tree.hasProperty (kTheme)) themeIndex = (int) tree.getProperty (kTheme);
    else                           themeIndex = juce::jmax (0, themeNames().indexOf (settings::get ("theme", "Paper")));
    applyTheme (themeIndex, false);

    // ---- wiring ----
    auto& pm = proc.presetManager();
    topBar.onBrowse  = [this] { setBrowserOpen (! browser.isVisible()); };
    topBar.onSave    = [this] { openSaveDialog(); };
    topBar.onExport  = [this] { openSaveDialog(); };       // the save sheet has the "Export file..." button
    topBar.onLicense = [this] { openLicenseDialog(); };
    topBar.onInit    = [&pm] { pm.loadInit(); };
    topBar.onImport  = [this] { setBrowserOpen (true); browser.chooseImport(); };
    topBar.onFolder  = [this] { setBrowserOpen (true); browser.openFolder(); };
    topBar.onTheme   = [this] (int i) { applyTheme (i, true); };

    browser.onClose  = [this] { setBrowserOpen (false); };
    browser.onPrompt = [this] (PromptSpec s) { openPrompt (std::move (s)); };
    browser.onInit   = [&pm] { pm.loadInit(); };

    pm.addChangeListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    // ---- size: remembered in the plugin state ----
    setResizable (true, true);
    setResizeLimits (minW, minH, maxW, maxH);
    setSize (juce::jlimit (minW, maxW, (int) tree.getProperty (kWidth, baseW)),
             juce::jlimit (minH, maxH, (int) tree.getProperty (kHeight, baseH)));
    constructed = true;

    startTimerHz (30);
}

WhyshoSynthAudioProcessorEditor::~WhyshoSynthAudioProcessorEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    proc.presetManager().removeChangeListener (this);
    proc.keyboardState().allNotesOff (0);       // nothing may keep ringing because the window went away
    closeOverlays();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void WhyshoSynthAudioProcessorEditor::applyTheme (int index, bool userChoice)
{
    setThemeIndex (index);
    proc.state().state.setProperty (kTheme, currentThemeIndex(), nullptr);
    if (userChoice) settings::set ("theme", theme().name);

    lnf.applyTheme();
    keys.applyTheme();
    topBar.setThemeSelection (currentThemeIndex());
    sendLookAndFeelChange();        // children re-read cached fonts / colours
    repaint();
}

void WhyshoSynthAudioProcessorEditor::timerCallback()
{
    synth.tick();
    topBar.tick();

    if (++slowTick >= 6)            // 5 Hz: things that only change on user action
    {
        slowTick = 0;
        topBar.refresh();

        // A session / state recall replaces the tree under us: follow its theme.
        auto& tree = proc.state().state;
        if (! tree.hasProperty (kTheme))                              tree.setProperty (kTheme, currentThemeIndex(), nullptr);
        else if ((int) tree.getProperty (kTheme) != currentThemeIndex()) applyTheme ((int) tree.getProperty (kTheme), false);
        if (! tree.hasProperty (kWidth))
        {
            tree.setProperty (kWidth, getWidth(), nullptr);
            tree.setProperty (kHeight, getHeight(), nullptr);
        }
    }
}

void WhyshoSynthAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    topBar.refresh();
}

// ---------------------------------------------------------------------------
void WhyshoSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
}

void WhyshoSynthAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver) return;
    const auto& t = theme();
    g.fillAll (t.bg.withAlpha (0.82f));
    auto r = getLocalBounds().toFloat().reduced (18.0f);
    g.setColour (t.accent);
    g.drawRect (r, 3.0f);
    g.setColour (t.text);
    g.setFont (displayFont (juce::jmin (44.0f, (float) getWidth() * 0.04f)));
    g.drawText ("Drop to import", r, juce::Justification::centred, false);
    g.setFont (themeFont (13.0f));
    g.setColour (t.textDim);
    g.drawText (".sy1bank  /  .sy1preset", r.translated (0.0f, 36.0f), juce::Justification::centred, false);
}

void WhyshoSynthAudioProcessorEditor::resized()
{
    const float scale = juce::jmin ((float) getWidth() / (float) baseW, (float) getHeight() / (float) baseH);
    const int lw = juce::roundToInt ((float) getWidth() / scale), lh = juce::roundToInt ((float) getHeight() / scale);
    root.setTransform (juce::AffineTransform::scale (scale));
    root.setBounds (0, 0, lw, lh);

    auto r = root.getLocalBounds();
    topBar.setBounds (r.removeFromTop (TopBar::height));
    const int spare = juce::jmax (0, lh - baseH);
    auto kb = r.removeFromBottom (66 + spare * 3 / 10);
    keys.setBounds (kb.reduced (7, 0).withTrimmedBottom (7));
    synth.setBounds (r);
    browser.setBounds (r);

    for (juce::Component* o : { (juce::Component*) saveDialog.get(), (juce::Component*) prompt.get(), (juce::Component*) licenseDialog.get() })
        if (o != nullptr) o->setBounds (root.getLocalBounds());

    if (constructed)
    {
        auto& tree = proc.state().state;
        tree.setProperty (kWidth, getWidth(), nullptr);
        tree.setProperty (kHeight, getHeight(), nullptr);
    }
}

// ---------------------------------------------------------------------------
bool WhyshoSynthAudioProcessorEditor::notesBlocked() const
{
    if (saveDialog != nullptr || prompt != nullptr || licenseDialog != nullptr) return true;
    return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
}

bool WhyshoSynthAudioProcessorEditor::keyPressed (const juce::KeyPress& k)
{
    if (notesBlocked()) return false;
    if (! k.getModifiers().isAnyModifierKeyDown())
    {
        if (k.getTextCharacter() == 'z') { keys.shiftOctave (-1); return true; }
        if (k.getTextCharacter() == 'x') { keys.shiftOctave (1);  return true; }
    }
    return keys.keyPressed (k);
}

bool WhyshoSynthAudioProcessorEditor::keyStateChanged (bool isKeyDown)
{
    if (notesBlocked()) return false;
    return keys.keyStateChanged (isKeyDown);
}

void WhyshoSynthAudioProcessorEditor::globalFocusChanged (juce::Component* c)
{
    // Focus moved into a text field while a computer key was held: its key-up
    // will never reach the keyboard, so release everything now.
    if (dynamic_cast<juce::TextEditor*> (c) != nullptr && isParentOf (c))
        proc.keyboardState().allNotesOff (0);
}

// ---------------------------------------------------------------------------
void WhyshoSynthAudioProcessorEditor::setBrowserOpen (bool open)
{
    if (browser.isVisible() != open)
    {
        browser.setVisible (open);
        synth.setVisible (! open);          // hidden page = no repaints behind the browser
        topBar.setBrowserOpen (open);
    }
    if (open) browser.opened();
    else if (isShowing()) grabKeyboardFocus();
}

void WhyshoSynthAudioProcessorEditor::closeOverlays()
{
    saveDialog.reset();
    prompt.reset();
    licenseDialog.reset();
}

void WhyshoSynthAudioProcessorEditor::openSaveDialog()
{
    if (saveDialog != nullptr) return;
    proc.keyboardState().allNotesOff (0);
    saveDialog = std::make_unique<SaveDialog> (proc.presetManager());
    juce::Component::SafePointer<WhyshoSynthAudioProcessorEditor> safe (this);
    saveDialog->onDismiss = [safe]
    {
        // deferred: the sheet is still on the call stack (its own button called us)
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->saveDialog.reset(); safe->topBar.refresh(); } });
    };
    saveDialog->setBounds (root.getLocalBounds());
    root.addAndMakeVisible (*saveDialog);
    tooltips.toFront (false);
}

void WhyshoSynthAudioProcessorEditor::openPrompt (PromptSpec spec)
{
    proc.keyboardState().allNotesOff (0);
    prompt = std::make_unique<Prompt> (std::move (spec));
    juce::Component::SafePointer<WhyshoSynthAudioProcessorEditor> safe (this);
    auto* which = prompt.get();
    prompt->onDismiss = [safe, which]
    {
        juce::MessageManager::callAsync ([safe, which] { if (safe != nullptr && safe->prompt.get() == which) safe->prompt.reset(); });
    };
    prompt->setBounds (root.getLocalBounds());
    root.addAndMakeVisible (*prompt);
    tooltips.toFront (false);
}

void WhyshoSynthAudioProcessorEditor::openLicenseDialog()
{
    if (licenseDialog != nullptr) return;
    proc.keyboardState().allNotesOff (0);
    licenseDialog = std::make_unique<LicenseDialog> (proc);
    juce::Component::SafePointer<WhyshoSynthAudioProcessorEditor> safe (this);
    licenseDialog->onClose = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->licenseDialog.reset(); safe->topBar.refresh(); } });
    };
    licenseDialog->setBounds (root.getLocalBounds());
    root.addAndMakeVisible (*licenseDialog);
    licenseDialog->refreshStatus();
    tooltips.toFront (false);
}

// ---------------------------------------------------------------------------
bool WhyshoSynthAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isLibraryFile (f)) return true;
    return false;
}

void WhyshoSynthAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true;  repaint(); }
void WhyshoSynthAudioProcessorEditor::fileDragExit (const juce::StringArray&)           { dragOver = false; repaint(); }

void WhyshoSynthAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    juce::Array<juce::File> list;
    for (auto& f : files)
        if (isLibraryFile (f)) list.add (juce::File (f));
    if (list.isEmpty()) return;
    setBrowserOpen (true);
    browser.importFiles (list);
}

} // namespace wsho
