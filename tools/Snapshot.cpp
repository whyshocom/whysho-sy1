// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
//
//   WhyshoSY1_Snapshot <out-dir> [scale]
//
// Renders: the synth page in every theme, the browser (full / filtered / no
// match), the save sheet, a confirm prompt, the licence dialog, and the
// smallest and largest window sizes.
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using namespace wsho;

enum class Shot { main, browser, browserSearch, browserEmpty, save, prompt, license };

void setParam (WhyshoSynthAudioProcessor& proc, P p, float v)
{
    if (auto* prm = proc.state().getParameter (pid (p)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// A patch that exercises the UI: three different oscillators, a resonant
// filter with env sweep, both sync modes, a couple of mod routes, some FX.
void makeInterestingPatch (WhyshoSynthAudioProcessor& proc)
{
    setParam (proc, P::osc1Wave, wSaw);        setParam (proc, P::osc1Shape, 0.25f);
    setParam (proc, P::osc2On, 1);             setParam (proc, P::osc2Wave, wSquare);
    setParam (proc, P::osc2Shape, 0.55f);      setParam (proc, P::osc2Oct, -1);
    setParam (proc, P::osc2Fine, 7.0f);        setParam (proc, P::osc2Pan, -0.3f);
    setParam (proc, P::osc3Wave, wSuperSaw);   setParam (proc, P::osc3Shape, 0.4f);
    setParam (proc, P::subLevel, 0.6f);        setParam (proc, P::noiseLevel, 0.08f);
    setParam (proc, P::fltCutoff, 1200.0f);    setParam (proc, P::fltReso, 0.55f);
    setParam (proc, P::fltEnv, 0.32f);         setParam (proc, P::fltKey, 0.5f);
    setParam (proc, P::ampA, 0.004f);          setParam (proc, P::ampD, 0.6f);
    setParam (proc, P::ampS, 0.7f);            setParam (proc, P::ampR, 0.4f);
    setParam (proc, P::fltD, 0.9f);            setParam (proc, P::fltS, 0.2f);
    setParam (proc, P::pitchAmt, 12.0f);
    setParam (proc, P::lfo1Wave, lTriangle);   setParam (proc, P::lfo2Wave, lSampleHold);
    setParam (proc, P::lfo2Sync, 1);
    setParam (proc, P::mod1Src, msLfo1);       setParam (proc, P::mod1Dst, mdCutoff);   setParam (proc, P::mod1Amt, 0.35f);
    setParam (proc, P::mod2Src, msVelocity);   setParam (proc, P::mod2Dst, mdLevel);    setParam (proc, P::mod2Amt, -0.5f);
    setParam (proc, P::unison, 4);
    setParam (proc, P::drvOn, 1);              setParam (proc, P::dlyOn, 1);
    setParam (proc, P::masterGain, -3.0f);
}

// Hold a chord and run the audio callback so scope, meter and voice pips have something to show.
void playSomething (WhyshoSynthAudioProcessor& proc)
{
    for (int n : { 45, 57, 64 }) proc.keyboardState().noteOn (1, n, 0.9f);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) { buf.clear(); midi.clear(); proc.processBlock (buf, midi); }
}

void render (const juce::File& out, int w, int h, int themeIndex, Shot shot, float scale)
{
    WhyshoSynthAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.state().state.setProperty ("uiTheme", themeIndex, nullptr);
    proc.state().state.setProperty ("uiW", w, nullptr);
    proc.state().state.setProperty ("uiH", h, nullptr);

    auto& pm = proc.presetManager();
    if (shot == Shot::main || shot == Shot::license || shot == Shot::prompt)
        makeInterestingPatch (proc);
    else if (pm.numBanks() > 0 && pm.bank (0).presets.size() > 12)
        pm.load (0, 12);
    if (shot == Shot::save) setParam (proc, P::fltCutoff, 900.0f);      // → "edited"

    // No addToDesktop(): software-render the component tree offscreen (headless).
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* ed = dynamic_cast<WhyshoSynthAudioProcessorEditor*> (base.get());
    jassert (ed != nullptr);
    ed->setVisible (true);
    ed->setSize (w, h);

    switch (shot)
    {
        case Shot::main:          break;
        case Shot::browser:       ed->setBrowserOpen (true); break;
        case Shot::browserSearch: ed->setBrowserOpen (true); ed->presetBrowser().setSearch ("bass"); break;
        case Shot::browserEmpty:  ed->setBrowserOpen (true); ed->presetBrowser().setSearch ("zzzz"); break;
        case Shot::save:          ed->openSaveDialog(); break;
        case Shot::license:       ed->openLicenseDialog(); break;
        case Shot::prompt:
        {
            gui::PromptSpec s;
            s.kicker = "Delete bank"; s.title = "Trap Essentials";
            s.message = "Delete this bank and its 24 preset(s) from disk? This can't be undone. Export it first if you want a backup.";
            s.okText = "Delete bank";
            ed->openPrompt (s);
            break;
        }
    }

    playSomething (proc);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, scale);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
    std::cout << "wrote " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";

    proc.keyboardState().allNotesOff (0);
}
// Robustness soak: open / close the editor many times while flipping themes,
// sizes and overlays and pumping the message loop (catches lifetime bugs).
int stress()
{
    WhyshoSynthAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    const int numThemes = gui::themeNames().size();
    for (int i = 0; i < 40; ++i)
    {
        std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
        auto* ed = dynamic_cast<WhyshoSynthAudioProcessorEditor*> (base.get());
        ed->setVisible (true);
        ed->setSize (900 + (i * 97) % 750, 600 + (i * 53) % 450);
        proc.state().state.setProperty ("uiTheme", i % numThemes, nullptr);
        if (i % 2 == 0) ed->setBrowserOpen (true);
        if (i % 3 == 0) ed->openSaveDialog();
        if (i % 4 == 0) ed->openLicenseDialog();
        if (i % 5 == 0) { gui::PromptSpec s; s.title = "x"; s.wantsText = true; ed->openPrompt (s); }
        if (i % 2 == 0) ed->presetBrowser().setSearch (i % 4 == 0 ? "ba" : "zz");
        if (proc.presetManager().numBanks() > 0 && ! proc.presetManager().bank (0).presets.empty())
            proc.presetManager().load (0, i % (int) proc.presetManager().bank (0).presets.size());
        playSomething (proc);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f);
        juce::ignoreUnused (img);
        proc.keyboardState().allNotesOff (0);
        base.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
    std::cout << "stress ok\n";
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--stress") return stress();
    const juce::File dir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    dir.createDirectory();
    const auto file = [&dir] (const juce::String& name) { return dir.getChildFile ("sy1-" + name + ".png"); };

    const int W = wsho::WhyshoSynthAudioProcessorEditor::baseW, H = wsho::WhyshoSynthAudioProcessorEditor::baseH;
    const auto names = wsho::gui::themeNames();
    for (int i = 0; i < names.size(); ++i)
        render (file ("main-" + names[i].toLowerCase()), W, H, i, Shot::main, scale);

    render (file ("browser-paper"),        W, H, 0, Shot::browser, scale);
    render (file ("browser-ultraviolet"),  W, H, 4, Shot::browser, scale);
    render (file ("browser-search-riso"),  W, H, 3, Shot::browserSearch, scale);
    render (file ("browser-nomatch"),      W, H, 0, Shot::browserEmpty, scale);
    render (file ("save-paper"),           W, H, 0, Shot::save, scale);
    render (file ("save-acid"),            W, H, 5, Shot::save, scale);
    render (file ("prompt-riso"),          W, H, 3, Shot::prompt, scale);
    render (file ("license-paper"),        W, H, 0, Shot::license, scale);
    render (file ("license-midnight"),     W, H, 1, Shot::license, scale);
    render (file ("min-paper"),            900, 600, 0, Shot::main, scale);
    render (file ("min-blood"),            900, 600, 6, Shot::main, scale);
    render (file ("min-browser"),          900, 600, 3, Shot::browser, scale);
    render (file ("max-riso"),             1650, 1050, 3, Shot::main, scale);
    return 0;
}
