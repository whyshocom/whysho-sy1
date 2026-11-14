// Offscreen GUI renderer: instantiates the real plugin editor and writes PNGs.
// Used to preview the UI on headless machines (run under xvfb-run). Not shipped.
//
//   WhyshoSY1_Snapshot <out-dir> [scale]
//
// Renders: the synth page in every theme, the browser (full / filtered / no
// match), the save sheet, a confirm prompt, the licence dialog, and the
// smallest and largest window sizes.
//
//   WhyshoSY1_Snapshot --stress     open / close soak
//   WhyshoSY1_Snapshot --interact   drives the real editor with synthetic mouse / keyboard input
//                                   and asserts what happens (exit code = number of failures)
//   WhyshoSY1_Snapshot --host       DAW-style lifecycle harness (threads, state, automation)
//
// Run every mode under `xvfb-run -a` with HOME and XDG_CONFIG_HOME pointing at a
// scratch directory: --interact writes banks, favourites and settings.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "presets/BankFormat.h"
#include "gui/UiSettings.h"
#include <dlfcn.h>
#include <map>
#include <set>
#include <thread>

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
    proc.uiState().theme = themeIndex;
    proc.uiState().width = w;
    proc.uiState().height = h;

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
        proc.uiState().theme = i % numThemes;
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

// ===========================================================================
//  --interact : drive the real editor with synthetic input and assert results.
//
//  Mouse and wheel go in through ComponentPeer::handleMouseEvent / handleMouseWheel
//  and text / navigation keys through ComponentPeer::handleKeyPress, i.e. JUCE's
//  own hit-testing (through the scale transform), focus handling, click counting
//  and drag logic all run. Note keys are sent as real X11 key events, because the
//  on-screen keyboard asks the window system which keys are physically down.
//  OS file dialogs and popup menus are not driven: the code they call with their
//  result is called directly instead.
// ===========================================================================
struct Checker
{
    int checks = 0, failures = 0;
    juce::String section;

    void begin (const juce::String& name) { section = name; std::cout << "[" << name << "]" << std::endl; }

    bool check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) { ++failures; std::cout << "  FAIL  " << section << ": " << what << std::endl; }
        return ok;
    }

    bool near (float actual, float expected, float tolerance, const juce::String& what)
    {
        return check (std::abs (actual - expected) <= tolerance,
                      what + " (got " + juce::String (actual, 5) + ", expected " + juce::String (expected, 5) + ")");
    }

    bool same (const juce::String& actual, const juce::String& expected, const juce::String& what)
    {
        return check (actual == expected, what + " (got \"" + actual + "\", expected \"" + expected + "\")");
    }
};

// Minimal Xlib access without its headers (their macros collide with JUCE names).
struct XKeySender
{
    struct KeyEvent
    {
        int type; unsigned long serial; int sendEvent; void* display;
        unsigned long window, root, subwindow, time;
        int x, y, xRoot, yRoot; unsigned int state, keycode; int sameScreen;
    };
    union Event { KeyEvent key; long pad[24]; };

    bool open()
    {
        lib = dlopen ("libX11.so.6", RTLD_NOW);
        if (lib == nullptr) return false;
        openDisplay    = (void* (*) (const char*)) dlsym (lib, "XOpenDisplay");
        closeDisplay   = (int (*) (void*)) dlsym (lib, "XCloseDisplay");
        sendEvent      = (int (*) (void*, unsigned long, int, long, Event*)) dlsym (lib, "XSendEvent");
        keysymToCode   = (unsigned char (*) (void*, unsigned long)) dlsym (lib, "XKeysymToKeycode");
        flush          = (int (*) (void*)) dlsym (lib, "XFlush");
        sync           = (int (*) (void*, int)) dlsym (lib, "XSync");
        rootWindow     = (unsigned long (*) (void*)) dlsym (lib, "XDefaultRootWindow");

        // JUCE only installs its X error handler in a standalone app; a plugin host
        // brings its own. Without one, Xlib exits the process on the first protocol
        // error (a bare Xvfb without a window manager produces a few harmless ones).
        using Handler = int (*) (void*, void*);
        if (auto setHandler = (Handler (*) (Handler)) dlsym (lib, "XSetErrorHandler"))
            setHandler ([] (void*, void* event) -> int
            {
                struct ErrorEvent { int type; void* display; unsigned long resource, serial; unsigned char error, request, minor; };
                const auto* e = static_cast<const ErrorEvent*> (event);
                ++xErrors()[{ (int) e->error, (int) e->request }];
                return 0;
            });

        if (! (openDisplay && closeDisplay && sendEvent && keysymToCode && flush && sync && rootWindow)) return false;
        display = openDisplay (nullptr);
        return display != nullptr;
    }

    ~XKeySender() { if (display != nullptr) closeDisplay (display); }

    static std::map<std::pair<int, int>, int>& xErrors() { static std::map<std::pair<int, int>, int> m; return m; }   // { error, request } -> count

    void key (void* nativeWindow, unsigned long keysym, bool down)
    {
        Event e {};
        e.key.type = down ? 2 : 3;                  // KeyPress / KeyRelease
        e.key.display = display;
        e.key.window = (unsigned long) nativeWindow;
        e.key.root = rootWindow (display);
        e.key.x = e.key.y = e.key.xRoot = e.key.yRoot = 1;
        e.key.keycode = keysymToCode (display, keysym);
        e.key.sameScreen = 1;
        sendEvent (display, e.key.window, 1, down ? 1L : 2L, &e);
        flush (display);
        sync (display, 0);
    }

    void* lib = nullptr;
    void* display = nullptr;
    void* (*openDisplay) (const char*) = nullptr;
    int (*closeDisplay) (void*) = nullptr;
    int (*sendEvent) (void*, unsigned long, int, long, Event*) = nullptr;
    unsigned char (*keysymToCode) (void*, unsigned long) = nullptr;
    int (*flush) (void*) = nullptr;
    int (*sync) (void*, int) = nullptr;
    unsigned long (*rootWindow) (void*) = nullptr;
};

template <typename T, typename Pred>
T* findComp (juce::Component& root, Pred&& pred)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c)) if (pred (*t)) return t;
        if (auto* r = findComp<T> (*c, pred)) return r;
    }
    return nullptr;
}

template <typename T>
T* findComp (juce::Component& root) { return findComp<T> (root, [] (T&) { return true; }); }

template <typename T>
void collect (juce::Component& root, std::vector<T*>& out)
{
    for (auto* c : root.getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c)) out.push_back (t);
        collect<T> (*c, out);
    }
}

struct GestureCounter : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override   { ++ends; }
    std::atomic<int> begins { 0 }, ends { 0 }, changes { 0 };
};

struct NoteCounter : juce::MidiKeyboardState::Listener
{
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float) override { ++ons; last = note; }
    void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override     { ++offs; }
    std::atomic<int> ons { 0 }, offs { 0 }, last { -1 };
};

// Runs processBlock on its own thread for as long as it lives, like a host's audio callback.
struct AudioRunner
{
    explicit AudioRunner (WhyshoSynthAudioProcessor& p, int blockSize = 256) : proc (p)
    {
        thread = std::thread ([this, blockSize]
        {
            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            while (! quit.load())
            {
                buf.clear(); midi.clear();
                proc.processBlock (buf, midi);
                ++blocks;
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });
    }
    ~AudioRunner() { quit = true; thread.join(); }

    WhyshoSynthAudioProcessor& proc;
    std::atomic<bool> quit { false };
    std::atomic<int> blocks { 0 };
    std::thread thread;
};

struct Ui
{
    Ui (WhyshoSynthAudioProcessor& p, WhyshoSynthAudioProcessorEditor& e, Checker& c) : proc (p), ed (e), t (c) {}

    using Mods = juce::ModifierKeys;
    static constexpr auto mouse = juce::MouseInputSource::InputSourceType::mouse;

    WhyshoSynthAudioProcessor& proc;
    WhyshoSynthAudioProcessorEditor& ed;
    Checker& t;
    juce::int64 clock = juce::Time::currentTimeMillis();
    juce::Point<float> lastPos;

    static void pump (int ms = 40) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    float raw (P p) const { return proc.state().getRawParameterValue (pid (p))->load(); }
    juce::RangedAudioParameter& param (P p) const { return *proc.state().getParameter (pid (p)); }
    float norm (P p) const { return param (p).getValue(); }
    void set (P p, float v) { param (p).setValueNotifyingHost (param (p).convertTo0to1 (v)); }

    // ---- lookup ----
    gui::Knob* knob (P p)            { return findComp<gui::Knob> (ed, [p] (gui::Knob& k) { return k.parameter() == p; }); }
    gui::ChoiceStrip* strip (P p)    { return findComp<gui::ChoiceStrip> (ed, [p] (gui::ChoiceStrip& k) { return k.parameter() == p; }); }
    gui::ToggleChip* toggle (P p)    { return findComp<gui::ToggleChip> (ed, [p] (gui::ToggleChip& k) { return k.parameter() == p; }); }
    gui::ParamCombo* combo (P p)     { return findComp<gui::ParamCombo> (ed, [p] (gui::ParamCombo& k) { return k.parameter() == p; }); }
    gui::EnvDisplay* env (P attack)  { return findComp<gui::EnvDisplay> (ed, [attack] (gui::EnvDisplay& k) { return k.parameter (0) == attack; }); }
    gui::TopBar& topBar()            { return *findComp<gui::TopBar> (ed); }
    gui::PresetBrowser& browser()    { return ed.presetBrowser(); }
    juce::TextButton* button (juce::Component& root, const juce::String& text)
    {
        return findComp<juce::TextButton> (root, [&text] (juce::TextButton& b) { return b.getButtonText() == text && b.isVisible(); });
    }
    template <typename T> T* direct (juce::Component& parent, int index)
    {
        int n = 0;
        for (auto* c : parent.getChildren())
            if (auto* x = dynamic_cast<T*> (c)) if (n++ == index) return x;
        return nullptr;
    }

    // ---- input, through the peer ----
    juce::Point<float> toPeer (juce::Component& c, juce::Point<float> local) { return ed.getLocalPoint (&c, local); }

    // Is `c` (or one of its children) really what the pointer would hit at this point?
    bool hits (juce::Component& c, juce::Point<float> local)
    {
        auto* h = ed.getComponentAt (toPeer (c, local));
        return h != nullptr && (h == &c || c.isParentOf (h));
    }

    // Mouse events carry the keyboard modifiers the windowing layer last reported
    // (ModifierKeys::currentModifiers), so shift etc. are set there, as X11 would.
    void moveTo (juce::Point<float> peerPos, Mods mods = {})
    {
        lastPos = peerPos;
        const int keyFlags = Mods::shiftModifier | Mods::ctrlModifier | Mods::altModifier;
        Mods::currentModifiers = Mods::currentModifiers.withoutFlags (keyFlags).withFlags (mods.getRawFlags() & keyFlags);
        ed.getPeer()->handleMouseEvent (mouse, peerPos, mods, juce::MouseInputSource::defaultPressure,
                                        juce::MouseInputSource::defaultOrientation, clock);
    }

    bool press (juce::Component& c, juce::Point<float> local, Mods extra = {}, bool rightButton = false)
    {
        const bool ok = t.check (c.isShowing() && hits (c, local), "pointer reaches " + describe (c));
        clock += 20;
        moveTo (toPeer (c, local));
        clock += 20;
        moveTo (toPeer (c, local), extra.withFlags (rightButton ? Mods::rightButtonModifier : Mods::leftButtonModifier));
        return ok;
    }

    void dragTo (juce::Component& c, juce::Point<float> local, Mods extra = {}, int steps = 6)
    {
        const auto from = lastPos, to = toPeer (c, local);
        for (int i = 1; i <= steps; ++i)
        {
            clock += 15;
            moveTo (from + (to - from) * ((float) i / (float) steps), extra.withFlags (Mods::leftButtonModifier));
        }
    }

    void release() { clock += 20; moveTo (lastPos); }

    bool click (juce::Component& c, juce::Point<float> local, Mods extra = {})
    {
        clock += 1000;                  // never an accidental double-click
        const bool ok = press (c, local, extra);
        release();
        pump();
        return ok;
    }
    bool click (juce::Component& c) { return click (c, c.getLocalBounds().toFloat().getCentre()); }

    void doubleClick (juce::Component& c, juce::Point<float> local)
    {
        clock += 1000;
        press (c, local); release();
        press (c, local); release();
        pump();
    }

    void wheel (juce::Component& c, juce::Point<float> local, float deltaY)
    {
        t.check (c.isShowing() && hits (c, local), "wheel reaches " + describe (c));
        clock += 500;
        moveTo (toPeer (c, local));
        juce::MouseWheelDetails d;
        d.deltaX = 0.0f; d.deltaY = deltaY; d.isReversed = false; d.isSmooth = false; d.isInertial = false;
        ed.getPeer()->handleMouseWheel (mouse, toPeer (c, local), clock, d);
        pump();
    }

    void key (const juce::KeyPress& k) { ed.getPeer()->handleKeyPress (k); pump(); }
    void key (int code)                { key (juce::KeyPress (code)); }

    void type (const juce::String& text)
    {
        for (auto c = text.getCharPointer(); ! c.isEmpty();)
        {
            const auto ch = c.getAndAdvance();
            ed.getPeer()->handleKeyPress (juce::KeyPress ((int) ch, Mods(), ch));
        }
        pump (20);
    }

    // Select everything in the focused field, then type over it.
    void retype (const juce::String& text)
    {
        ed.getPeer()->handleKeyPress (juce::KeyPress ('a', Mods::commandModifier, 0));
        type (text);
    }

    static juce::String describe (juce::Component& c)
    {
        juce::String s (typeid (c).name());
        if (auto* k = dynamic_cast<gui::Knob*> (&c))             s = "knob " + pid (k->parameter());
        else if (auto* b = dynamic_cast<juce::Button*> (&c))     s = "button " + b->getButtonText();
        else if (auto* cs = dynamic_cast<gui::ChoiceStrip*> (&c)) s = "strip " + pid (cs->parameter());
        else if (auto* tg = dynamic_cast<gui::ToggleChip*> (&c)) s = "toggle " + pid (tg->parameter());
        return s;
    }

    // ---- knob gestures ----
    juce::Point<float> dial (gui::Knob& k)  { return { (float) k.getWidth() * 0.5f, (float) (k.getHeight() - 12) * 0.5f }; }
    juce::Point<float> label (gui::Knob& k) { return { (float) k.getWidth() * 0.5f, (float) k.getHeight() - 6.0f }; }

    void dragKnob (gui::Knob& k, float dx, float dy, Mods extra = {})
    {
        clock += 1000;
        press (k, dial (k), extra);
        dragTo (k, dial (k) + juce::Point<float> (dx, dy), extra);
        release();
        pump();
    }

    // Double-click the label, type into the field that appears, press return.
    bool typeValue (gui::Knob& k, const juce::String& text, int endKey = juce::KeyPress::returnKey)
    {
        doubleClick (k, label (k));
        auto* field = findComp<juce::TextEditor> (*k.getParentComponent());
        if (! t.check (field != nullptr && field->isShowing(), "value field opens for " + describe (k))) return false;
        t.check (field->hasKeyboardFocus (true), "value field has keyboard focus (" + describe (k) + ")");
        retype (text);
        key (endKey);
        pump (60);
        return t.check (findComp<juce::TextEditor> (*k.getParentComponent()) == nullptr, "value field closes (" + describe (k) + ")");
    }
};

bool paramsMatchPreset (Ui& ui, const Preset& p, juce::String& firstDiff)
{
    const auto table = bankio::resolve (p);
    for (int i = 0; i < kNumParams; ++i)
    {
        const float want = table[(size_t) i], got = ui.raw ((P) i);
        if (std::abs (got - want) > 1.0e-3f * juce::jmax (1.0f, std::abs (want)))
        {
            firstDiff = pid (i) + " = " + juce::String (got) + ", preset says " + juce::String (want);
            return false;
        }
    }
    return true;
}

int interact()
{
    Checker t;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sy1-interact-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
    scratch.createDirectory();
    std::cout << "home: " << home.getFullPathName() << "\n";

    auto proc = std::make_unique<WhyshoSynthAudioProcessor>();
    proc->setRateAndBufferSizeDetails (48000.0, 256);
    proc->prepareToPlay (48000.0, 256);
    auto& pm = proc->presetManager();
    const auto userDir = pm.userDir();
    if (! userDir.isAChildOf (home) || pm.numBanks() != 2 || ! pm.bank (1).presets.empty())
    {
        std::cout << "refusing to run: point HOME and XDG_CONFIG_HOME at an empty scratch directory first\n";
        return 2;
    }

    GestureCounter gestures;
    proc->addListener (&gestures);
    NoteCounter notes;
    proc->keyboardState().addListener (&notes);
    AudioRunner audio (*proc);

    XKeySender xkeys;
    const bool haveX = xkeys.open();

    std::unique_ptr<juce::AudioProcessorEditor> base (proc->createEditor());
    auto* edPtr = dynamic_cast<WhyshoSynthAudioProcessorEditor*> (base.get());
    auto& ed = *edPtr;
    ed.addToDesktop (0);
    ed.setVisible (true);
    ed.toFront (true);
    Ui::pump (300);

    Ui ui (*proc, ed, t);
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    using KP = juce::KeyPress;

    t.begin ("window");
    t.check (ed.getPeer() != nullptr && ed.isShowing(), "editor is on the desktop");
    t.check (ed.getWidth() == WhyshoSynthAudioProcessorEditor::baseW && ed.getHeight() == WhyshoSynthAudioProcessorEditor::baseH, "opens at the base size");
    ed.grabKeyboardFocus();
    Ui::pump (100);
    const bool focusWorks = t.check (ed.hasKeyboardFocus (true), "editor can take keyboard focus");
    t.check (haveX, "X11 key injection available");

    // -----------------------------------------------------------------------
    t.begin ("read-out formatting");
    {
        const std::tuple<P, float, const char*> cases[] = {
            { P::fltCutoff, 440.0f, "440 Hz" }, { P::fltCutoff, 55.5f, "55.5 Hz" }, { P::fltCutoff, 1500.0f, "1.50 kHz" }, { P::fltCutoff, 12000.0f, "12.0 kHz" },
            { P::lfo1Rate, 5.0f, "5.00 Hz" }, { P::lfo1Rate, 0.25f, "0.25 Hz" },
            { P::ampA, 0.005f, "5.0 ms" }, { P::ampD, 0.15f, "150 ms" }, { P::ampR, 2.0f, "2.00 s" }, { P::glide, 0.0f, "0.0 ms" },
            { P::osc1Level, 0.5f, "50 %" }, { P::mod1Amt, -0.5f, "-50 %" }, { P::mod1Amt, 0.25f, "+25 %" }, { P::mod1Amt, 0.0f, "0 %" },
            { P::osc1Semi, 7.0f, "+7 st" }, { P::osc1Semi, -5.0f, "-5 st" }, { P::osc1Semi, 0.0f, "0 st" }, { P::bendRange, 12.0f, "12 st" },
            { P::pitchAmt, 12.0f, "+12.0 st" }, { P::osc1Fine, -12.0f, "-12 ct" }, { P::osc1Fine, 7.0f, "+7 ct" },
            { P::masterGain, -3.0f, "-3.0 dB" }, { P::masterGain, 0.0f, "0.0 dB" }, { P::masterGain, 6.0f, "+6.0 dB" },
            { P::osc1Pan, -0.3f, "L 30" }, { P::osc1Pan, 0.0f, "C" }, { P::osc1Pan, 1.0f, "R 100" },
            { P::fltEnv, 0.32f, "+3.2 oct" }, { P::fltEnv, -1.0f, "-10.0 oct" },
            { P::osc1Oct, -1.0f, "-1" }, { P::unison, 4.0f, "4" }, { P::osc1Wave, 4.0f, "SuperSaw" }, { P::dlyDiv, 10.0f, "1/8D" }, { P::osc1On, 1.0f, "On" } };
        for (const auto& [p, v, text] : cases)
            t.same (gui::formatParam (p, v), text, "format " + pid (p) + " @ " + juce::String (v));
    }

    // -----------------------------------------------------------------------
    t.begin ("knob drag");
    {
        const P knobs[] = { P::fltCutoff, P::fltReso, P::osc1Level, P::osc1Fine, P::osc1Semi, P::osc1Pan, P::ampA, P::ampR, P::masterGain,
                            P::pitchAmt, P::uniDetune, P::revSize, P::choRate, P::subLevel, P::oscFm, P::velSens };
        for (auto p : knobs)
        {
            auto* k = ui.knob (p);
            if (! t.check (k != nullptr, "knob exists: " + pid (p))) continue;
            ui.set (p, paramDef (p).def);
            ui.param (p).setValueNotifyingHost (0.4f);
            const float before = ui.norm (p);
            const int b0 = gestures.begins, e0 = gestures.ends;
            ui.dragKnob (*k, 0.0f, -57.0f);                         // 57 px up = +0.3 of the travel
            const float expected = ui.param (p).convertTo0to1 (ui.param (p).convertFrom0to1 (juce::jlimit (0.0f, 1.0f, before + 0.3f)));
            t.near (ui.norm (p), expected, paramDef (p).kind == ParamDef::Float ? 0.01f : 0.05f, "drag up moves " + pid (p));
            t.check (gestures.begins - b0 == 1 && gestures.ends - e0 == 1, "one begin / end gesture for " + pid (p));
            const float mid = ui.norm (p);
            ui.dragKnob (*k, -38.0f, 0.0f);                         // left = down
            t.check (ui.norm (p) < mid - 0.1f, "drag left lowers " + pid (p));
            const float v = ui.raw (p);
            t.check (v >= paramDef (p).min - 1.0e-4f && v <= paramDef (p).max + 1.0e-4f, pid (p) + " stays in range");
        }

        auto* cut = ui.knob (P::fltCutoff);
        ui.param (P::fltCutoff).setValueNotifyingHost (0.5f);
        ui.dragKnob (*cut, 0.0f, -70.0f, juce::ModifierKeys (juce::ModifierKeys::shiftModifier));
        t.near (ui.norm (P::fltCutoff), 0.55f, 0.006f, "shift-drag is fine (70 px = 0.05)");
        ui.dragKnob (*cut, 0.0f, -4000.0f);
        t.near (ui.raw (P::fltCutoff), 20000.0f, 1.0f, "drag past the top clamps at max");
        ui.dragKnob (*cut, 0.0f, 4000.0f);
        t.near (ui.raw (P::fltCutoff), 20.0f, 0.01f, "drag past the bottom clamps at min");

        // a click without movement must not change anything or open a gesture
        const int b0 = gestures.begins;
        ui.set (P::fltReso, 0.33f);
        ui.click (*ui.knob (P::fltReso), ui.dial (*ui.knob (P::fltReso)));
        t.near (ui.raw (P::fltReso), 0.33f, 1.0e-5f, "plain click leaves the value alone");
        t.check (gestures.begins == b0, "plain click opens no gesture");

        // bar-style knob (mod amount): horizontal
        auto* amt = ui.knob (P::mod1Amt);
        ui.set (P::mod1Amt, 0.0f);
        ui.dragKnob (*amt, (float) amt->getWidth() * 0.25f, 0.0f);
        t.near (ui.raw (P::mod1Amt), 0.5f, 0.03f, "mod amount bar: drag right a quarter width = +50 %");

        t.begin ("knob double-click / wheel");
        for (auto p : { P::fltCutoff, P::osc1Pan, P::ampD, P::masterGain, P::unison })
        {
            auto* k = ui.knob (p);
            ui.param (p).setValueNotifyingHost (0.77f);
            ui.doubleClick (*k, ui.dial (*k));
            t.near (ui.raw (p), paramDef (p).def, 1.0e-3f * juce::jmax (1.0f, std::abs (paramDef (p).def)), "double-click resets " + pid (p));
        }
        ui.param (P::fltReso).setValueNotifyingHost (0.5f);
        ui.wheel (*ui.knob (P::fltReso), ui.dial (*ui.knob (P::fltReso)), 0.1f);
        t.near (ui.norm (P::fltReso), 0.52f, 1.0e-3f, "wheel up = +2 %");
        ui.set (P::unison, 3.0f);
        ui.wheel (*ui.knob (P::unison), ui.dial (*ui.knob (P::unison)), -0.1f);
        t.near (ui.raw (P::unison), 2.0f, 1.0e-4f, "wheel down steps an integer knob by one");
    }

    // -----------------------------------------------------------------------
    t.begin ("typed values");
    if (focusWorks)
    {
        struct Case { P p; const char* text; float expected; float start; };
        const Case cases[] = {
            { P::fltCutoff, "1.2k", 1200.0f, 500.0f },   { P::fltCutoff, "440", 440.0f, 500.0f },  { P::fltCutoff, "2.5 kHz", 2500.0f, 500.0f },
            { P::fltCutoff, "99999", 20000.0f, 500.0f }, { P::fltCutoff, "440 hz", 440.0f, 5000.0f },
            { P::ampD, "250", 0.25f, 0.3f },             { P::ampD, "2 s", 2.0f, 0.3f },           { P::ampD, "80 ms", 0.08f, 3.0f },
            { P::ampD, "1.5", 1.5f, 2.0f },              { P::ampD, "0.5s", 0.5f, 0.3f },
            { P::osc1Level, "50%", 0.5f, 0.8f },         { P::osc1Level, "35", 0.35f, 0.8f },      { P::osc1Level, "250", 1.0f, 0.8f },
            { P::osc1Semi, "+7 st", 7.0f, 0.0f },        { P::osc1Semi, "-5", -5.0f, 0.0f },       { P::osc1Fine, "-12 ct", -12.0f, 0.0f },
            { P::masterGain, "-6 dB", -6.0f, 0.0f },     { P::masterGain, "3", 3.0f, 0.0f },
            { P::osc1Pan, "L 30", -0.3f, 0.0f },         { P::osc1Pan, "r50", 0.5f, 0.0f },        { P::osc1Pan, "C", 0.0f, 0.4f },
            { P::fltEnv, "-2.5 oct", -0.25f, 0.0f },     { P::fltEnv, "+4", 0.4f, 0.0f },
            { P::lfo1Rate, "2.5 Hz", 2.5f, 5.0f },       { P::pitchAmt, "12 st", 12.0f, 0.0f },    { P::unison, "5", 5.0f, 1.0f },
            { P::fltReso, "abc", 0.4f, 0.4f } };         // not a number: unchanged
        for (const auto& c : cases)
        {
            ui.set (c.p, c.start);
            if (auto* k = ui.knob (c.p))
                if (ui.typeValue (*k, c.text))
                    t.near (ui.raw (c.p), c.expected, 2.0e-3f * juce::jmax (1.0f, std::abs (c.expected)), pid (c.p) + " <- \"" + c.text + "\"");
        }
        ui.set (P::fltCutoff, 700.0f);
        ui.typeValue (*ui.knob (P::fltCutoff), "3000", KP::escapeKey);
        t.near (ui.raw (P::fltCutoff), 700.0f, 0.5f, "escape cancels typed entry");

        // clicking elsewhere commits (focus lost)
        ui.set (P::fltCutoff, 700.0f);
        ui.doubleClick (*ui.knob (P::fltCutoff), ui.label (*ui.knob (P::fltCutoff)));
        ui.retype ("900");
        ui.click (*ui.knob (P::fltDrive), ui.dial (*ui.knob (P::fltDrive)));
        Ui::pump (80);
        t.near (ui.raw (P::fltCutoff), 900.0f, 1.0f, "clicking away commits typed entry");
        t.check (findComp<juce::TextEditor> (*ui.knob (P::fltCutoff)->getParentComponent()) == nullptr, "value field closed after clicking away");
    }

    // -----------------------------------------------------------------------
    t.begin ("selectors");
    {
        const std::pair<P, int> strips[] = { { P::osc1Wave, 6 }, { P::osc2Wave, 6 }, { P::osc3Wave, 6 }, { P::fltType, 6 }, { P::voiceMode, 3 },
                                             { P::subWave, 3 }, { P::subOct, 2 }, { P::lfo1Wave, 5 }, { P::lfo2Wave, 5 } };
        for (const auto& [p, n] : strips)
        {
            auto* s = ui.strip (p);
            if (! t.check (s != nullptr, "strip exists: " + pid (p))) continue;
            for (int i = n - 1; i >= 0; --i)
            {
                ui.click (*s, { ((float) i + 0.5f) * (float) s->getWidth() / (float) n, (float) s->getHeight() * 0.5f });
                t.check (juce::roundToInt (ui.raw (p)) == i && s->selected() == i, pid (p) + " cell " + juce::String (i) + " selects choice " + juce::String (i));
            }
        }
        auto* s = ui.strip (P::fltType);
        ui.wheel (*s, s->getLocalBounds().toFloat().getCentre(), -0.1f);
        t.check (juce::roundToInt (ui.raw (P::fltType)) == 1, "wheel down on a strip selects the next choice");

        for (auto p : { P::osc2On, P::osc3On, P::drvOn, P::choOn, P::dlyOn, P::revOn, P::lfo1Retrig, P::dlyPing })
        {
            auto* tg = ui.toggle (p);
            if (! t.check (tg != nullptr, "toggle exists: " + pid (p))) continue;
            const bool before = ui.raw (p) > 0.5f;
            ui.click (*tg);
            t.check ((ui.raw (p) > 0.5f) != before && tg->isOn() != before, "click toggles " + pid (p));
            ui.click (*tg);
            t.check ((ui.raw (p) > 0.5f) == before, "second click toggles " + pid (p) + " back");
        }

        // shape knob follows the wave
        ui.set (P::osc1Wave, (float) wSquare);
        Ui::pump (80);

        t.begin ("sync swaps knob <-> division");
        const std::tuple<P, P, P> syncs[] = { { P::lfo1Sync, P::lfo1Rate, P::lfo1Div }, { P::lfo2Sync, P::lfo2Rate, P::lfo2Div }, { P::dlySync, P::dlyTime, P::dlyDiv } };
        for (const auto& [sync, freeP, divP] : syncs)
        {
            auto* tg = ui.toggle (sync); auto* k = ui.knob (freeP); auto* cb = ui.combo (divP);
            if (! t.check (tg != nullptr && k != nullptr && cb != nullptr, "sync controls exist: " + pid (sync))) continue;
            ui.set (sync, 0.0f);
            Ui::pump (80);
            t.check (k->isVisible() && ! cb->isVisible(), pid (sync) + " off: knob shown, division hidden");
            ui.click (*tg);
            Ui::pump (80);
            t.check (ui.raw (sync) > 0.5f && ! k->isVisible() && cb->isVisible(), pid (sync) + " on: division shown, knob hidden");
            t.check (ui.hits (*cb, cb->getLocalBounds().toFloat().getCentre()), "division picker is what the pointer hits");
            cb->setSelectedId (5, juce::sendNotificationSync);
            t.check (juce::roundToInt (ui.raw (divP)) == 4, "division picker sets " + pid (divP));
            ui.wheel (*cb, cb->getLocalBounds().toFloat().getCentre(), -0.25f);
            t.check (juce::roundToInt (ui.raw (divP)) == 5, "wheel on the division picker steps " + pid (divP));
            ui.click (*tg);
            Ui::pump (80);
            t.check (k->isVisible() && ! cb->isVisible() && ui.hits (*k, ui.dial (*k)), pid (sync) + " off again: knob back and clickable");
        }
        auto* src = ui.combo (P::mod1Src);
        src->setSelectedId (3, juce::sendNotificationSync);
        t.check (juce::roundToInt (ui.raw (P::mod1Src)) == 2, "mod source picker sets mod1_src");
    }

    // -----------------------------------------------------------------------
    t.begin ("envelope display");
    for (auto attack : { P::ampA, P::fltA })
    {
        auto* e = ui.env (attack);
        if (! t.check (e != nullptr, "env display exists")) continue;
        const P A = attack, D = (P) ((int) attack + 1), S = (P) ((int) attack + 2), R = (P) ((int) attack + 3);
        const P other = attack == P::ampA ? P::fltA : P::ampA;
        ui.param (A).setValueNotifyingHost (0.3f); ui.param (D).setValueNotifyingHost (0.3f);
        ui.set (S, 0.5f);                          ui.param (R).setValueNotifyingHost (0.3f);
        const float otherBefore[4] = { ui.raw (other), ui.raw ((P) ((int) other + 1)), ui.raw ((P) ((int) other + 2)), ui.raw ((P) ((int) other + 3)) };
        Ui::pump (60);

        float a0 = ui.norm (A), d0 = ui.norm (D), s0 = ui.raw (S), r0 = ui.norm (R);
        int g0 = gestures.begins - gestures.ends;
        ui.clock += 1000; ui.press (*e, e->handlePosition (0)); ui.dragTo (*e, e->handlePosition (0) + juce::Point<float> (14.0f, 0.0f)); ui.release(); Ui::pump();
        t.check (ui.norm (A) > a0 + 0.1f, pid (A) + ": dragging the attack point right lengthens the attack");
        t.check (std::abs (ui.norm (D) - d0) < 1.0e-6f && std::abs (ui.raw (S) - s0) < 1.0e-6f && std::abs (ui.norm (R) - r0) < 1.0e-6f, "attack drag leaves D / S / R alone");

        a0 = ui.norm (A);
        ui.clock += 1000; ui.press (*e, e->handlePosition (1)); ui.dragTo (*e, e->handlePosition (1) + juce::Point<float> (12.0f, 10.0f)); ui.release(); Ui::pump();
        t.check (ui.norm (D) > d0 + 0.1f, pid (D) + ": dragging the decay point right lengthens the decay");
        t.check (ui.raw (S) < s0 - 0.05f, pid (S) + ": dragging the decay point down lowers the sustain");
        t.check (std::abs (ui.norm (A) - a0) < 1.0e-6f && std::abs (ui.norm (R) - r0) < 1.0e-6f, "decay drag leaves A / R alone");

        d0 = ui.norm (D); s0 = ui.raw (S);
        ui.clock += 1000; ui.press (*e, e->handlePosition (2)); ui.dragTo (*e, e->handlePosition (2) + juce::Point<float> (-10.0f, 0.0f)); ui.release(); Ui::pump();
        t.check (ui.norm (R) < r0 - 0.05f, pid (R) + ": dragging the release point left shortens the release");
        t.check (std::abs (ui.norm (D) - d0) < 1.0e-6f && std::abs (ui.raw (S) - s0) < 1.0e-6f, "release drag leaves D / S alone");

        ui.clock += 1000; ui.press (*e, e->handlePosition (1)); ui.dragTo (*e, { 5000.0f, -5000.0f }); ui.release(); Ui::pump();
        t.near (ui.raw (D), paramDef (D).max, 1.0e-3f, "decay clamps at max when dragged far right");
        t.near (ui.raw (S), 1.0f, 1.0e-5f, "sustain clamps at 100 % when dragged far up");
        ui.clock += 1000; ui.press (*e, e->handlePosition (1)); ui.dragTo (*e, { -5000.0f, 5000.0f }); ui.release(); Ui::pump();
        t.near (ui.raw (D), paramDef (D).min, 1.0e-4f, "decay clamps at min when dragged far left");
        t.near (ui.raw (S), 0.0f, 1.0e-5f, "sustain clamps at 0 when dragged far down");
        t.check (gestures.begins - gestures.ends == g0, "env drags close every gesture they open");

        bool untouched = true;
        for (int i = 0; i < 4; ++i) untouched = untouched && std::abs (ui.raw ((P) ((int) other + i)) - otherBefore[i]) < 1.0e-6f;
        t.check (untouched, "the other envelope's parameters did not move");
    }

    t.begin ("filter display");
    if (auto* f = findComp<gui::FilterDisplay> (ed))
    {
        const float w = (float) f->getWidth(), h = (float) f->getHeight();
        const auto hzAt = [w] (float x) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - 1.0f) / (w - 2.0f))); };
        ui.set (P::fltReso, 0.3f);
        const float type0 = ui.raw (P::fltType), env0 = ui.raw (P::fltEnv), drive0 = ui.raw (P::fltDrive);
        const int g0 = gestures.begins - gestures.ends;

        ui.clock += 1000; ui.press (*f, { w * 0.5f, h * 0.5f });
        Ui::pump (20);
        t.near (ui.raw (P::fltCutoff), hzAt (w * 0.5f), hzAt (w * 0.5f) * 0.01f, "press sets the cutoff under the pointer");
        t.near (ui.raw (P::fltReso), 0.3f, 1.0e-4f, "press alone keeps the resonance");
        ui.dragTo (*f, { w * 0.75f, h * 0.5f - 20.0f });
        t.near (ui.raw (P::fltCutoff), hzAt (w * 0.75f), hzAt (w * 0.75f) * 0.01f, "drag right raises the cutoff to the pointer");
        t.near (ui.raw (P::fltReso), 0.3f + 20.0f / (h - 2.0f), 0.01f, "drag up raises the resonance");
        ui.dragTo (*f, { w * 4.0f, -5000.0f });
        t.near (ui.raw (P::fltCutoff), 20000.0f, 1.0f, "cutoff clamps at 20 kHz");
        t.near (ui.raw (P::fltReso), 1.0f, 1.0e-5f, "resonance clamps at 100 %");
        ui.dragTo (*f, { -500.0f, 5000.0f });
        t.near (ui.raw (P::fltCutoff), 20.0f, 0.01f, "cutoff clamps at 20 Hz");
        t.near (ui.raw (P::fltReso), 0.0f, 1.0e-5f, "resonance clamps at 0");
        ui.release(); Ui::pump();
        t.check (gestures.begins - gestures.ends == g0, "filter drag closes its gestures");
        t.check (juce::exactlyEqual (ui.raw (P::fltType), type0) && juce::exactlyEqual (ui.raw (P::fltEnv), env0) && juce::exactlyEqual (ui.raw (P::fltDrive), drive0),
                 "filter drag touches only cutoff and resonance");
        ui.doubleClick (*f, { w * 0.3f, h * 0.5f });
        t.near (ui.raw (P::fltCutoff), paramDef (P::fltCutoff).def, 1.0f, "double-click resets the cutoff");
        t.near (ui.raw (P::fltReso), paramDef (P::fltReso).def, 1.0e-4f, "double-click resets the resonance");
    }
    else t.check (false, "filter display exists");

    // -----------------------------------------------------------------------
    t.begin ("preset display: arrows / wheel");
    auto& display = ui.topBar().presetDisplay();
    const int factoryCount = (int) pm.bank (0).presets.size();
    {
        pm.loadInit();
        Ui::pump (80);
        auto r = ui.topBar().readout();
        t.same (r.name, "Init", "top bar shows Init");
        t.same (r.kicker, "Unsaved", "Init is unsaved");
        t.check (! r.edited, "Init is not marked edited");

        const juce::Point<float> next ((float) display.getWidth() - 12.0f, (float) display.getHeight() * 0.5f), prev (12.0f, next.y);
        ui.click (display, next);
        t.check (pm.currentBank() == 0 && pm.currentPreset() == 0, "next from Init loads the first factory preset");
        ui.click (display, next);
        t.check (pm.currentPreset() == 1, "next arrow steps forward");
        ui.click (display, prev); ui.click (display, prev);
        t.check (pm.currentPreset() == factoryCount - 1, "previous arrow wraps to the last preset");
        ui.click (display, next);
        t.check (pm.currentPreset() == 0, "next arrow wraps to the first preset");
        ui.wheel (display, display.getLocalBounds().toFloat().getCentre(), -0.1f);
        t.check (pm.currentPreset() == 1, "wheel down steps forward");
        ui.wheel (display, display.getLocalBounds().toFloat().getCentre(), 0.1f);
        t.check (pm.currentPreset() == 0, "wheel up steps back");

        const auto& p0 = pm.bank (0).presets[0];
        r = ui.topBar().readout();
        t.same (r.name, p0.name, "top bar shows the loaded preset's name");
        t.same (r.kicker, "Factory" + (p0.category.isNotEmpty() ? dot + p0.category : juce::String()), "top bar shows bank and category");
        t.same (r.index, "001 / " + juce::String (factoryCount).paddedLeft ('0', 3), "top bar shows the position in the bank");
        juce::String diff;
        t.check (paramsMatchPreset (ui, p0, diff), "parameters match the loaded preset: " + diff);
    }

    // -----------------------------------------------------------------------
    t.begin ("modified marker");
    {
        t.check (! ui.topBar().readout().edited, "fresh load is not edited");
        ui.dragKnob (*ui.knob (P::fltCutoff), 0.0f, ui.norm (P::fltCutoff) > 0.5f ? 40.0f : -40.0f);
        Ui::pump (300);
        t.check (pm.isModified() && ui.topBar().readout().edited, "EDITED appears after moving a knob");
        ui.click (display, { (float) display.getWidth() - 12.0f, (float) display.getHeight() * 0.5f });
        Ui::pump (300);
        t.check (! pm.isModified() && ! ui.topBar().readout().edited, "EDITED clears after loading a preset");
    }

    // -----------------------------------------------------------------------
    t.begin ("browser: open / click / arrows");
    auto& br = ui.browser();
    {
        ui.click (display);
        t.check (br.isShowing(), "clicking the preset display opens the browser");
        auto* search = findComp<juce::TextEditor> (br);
        if (focusWorks) t.check (search != nullptr && search->hasKeyboardFocus (true), "search field is focused when the browser opens");

        auto v = br.viewState();
        t.check ((int) v.items.size() == factoryCount && v.bankCounts[0] == factoryCount && v.bankCounts[1] == factoryCount && v.bankCounts[2] == 0,
                 "bank column: All / Factory / User counts");
        t.same (v.bankRows.joinIntoString ("|"), "All banks|Factory|User", "bank column rows");
        std::map<juce::String, int> perCat;
        for (auto& p : pm.bank (0).presets) ++perCat[p.category];
        bool countsOk = v.categoryRows[0] == "All" && v.categoryCounts[0] == factoryCount && v.categoryRows[1] == "Favourites" && v.categoryCounts[1] == 0;
        int summed = 0;
        for (int i = 2; i < v.categoryRows.size(); ++i) { countsOk = countsOk && v.categoryCounts[i] == perCat[v.categoryRows[i]]; summed += v.categoryCounts[i]; }
        t.check (countsOk && summed == factoryCount - perCat[juce::String()], "category column counts match the library");

        auto& grid = br.presetGrid();
        const auto cellPoint = [&] (int i) { const auto c = br.presetCell (i); return juce::Point<float> ((float) c.getX() + 60.0f, (float) c.getCentreY()); };
        const auto starPoint = [&] (int i) { const auto c = br.presetCell (i); return juce::Point<float> ((float) c.getX() + 10.0f, (float) c.getCentreY()); };
        const int cols = juce::jmax (1, grid.getWidth() / 184);

        ui.click (grid, cellPoint (5));
        t.check (pm.currentBank() == 0 && pm.currentPreset() == 5, "clicking a preset loads it");
        const auto p5 = pm.bank (0).presets[5];
        juce::String diff;
        t.check (paramsMatchPreset (ui, p5, diff), "parameters changed to the clicked preset: " + diff);
        auto r = ui.topBar().readout();
        t.same (r.name, p5.name, "top bar name follows the browser");
        t.same (r.kicker, "Factory" + (p5.category.isNotEmpty() ? dot + p5.category : juce::String()), "top bar category follows the browser");

        ui.key (KP::rightKey);  t.check (pm.currentPreset() == 6, "right arrow = next preset");
        ui.key (KP::downKey);   t.check (pm.currentPreset() == 6 + cols, "down arrow = one row down");
        ui.key (KP::leftKey);   t.check (pm.currentPreset() == 5 + cols, "left arrow = previous preset");
        ui.key (KP::upKey);     t.check (pm.currentPreset() == 5, "up arrow = one row up");
        ui.click (grid, cellPoint (0));
        ui.key (KP::leftKey);   t.check (pm.currentPreset() == 0, "left arrow at the first preset stays put");
        ui.key (KP::upKey);     t.check (pm.currentPreset() == 0, "up arrow in the first row stays put");

        // ---- search ----
        t.begin ("browser: search / filters");
        const auto matches = [&] (const juce::String& needle, int bankFilter, const juce::String& catFilter)
        {
            int n = 0;
            for (int b = 0; b < pm.numBanks(); ++b)
                for (auto& p : pm.bank (b).presets)
                    if ((bankFilter < 0 || bankFilter == b) && (catFilter.isEmpty() || p.category == catFilter)
                        && (needle.isEmpty() || (p.name + " " + p.category + " " + p.author).toLowerCase().contains (needle.toLowerCase()))) ++n;
            return n;
        };
        if (search != nullptr)
        {
            ui.click (*search);
            if (focusWorks) t.check (search->hasKeyboardFocus (true), "clicking the search field focuses it");
            ui.retype ("bass");
            Ui::pump (80);
            v = br.viewState();
            t.check ((int) v.items.size() == matches ("bass", -1, {}) && ! v.items.empty(), "search \"bass\" filters by name / category (" + juce::String ((int) v.items.size()) + " shown)");
            const auto someName = pm.bank (0).presets[(size_t) factoryCount / 2].name;
            ui.retype (someName.toUpperCase());
            Ui::pump (80);
            v = br.viewState();
            t.check ((int) v.items.size() == matches (someName, -1, {}) && ! v.items.empty(), "search by (upper-cased) preset name finds it");
            const auto author = pm.bank (0).presets[0].author;
            if (author.isNotEmpty())
            {
                ui.retype (author);
                Ui::pump (80);
                t.check ((int) br.viewState().items.size() == matches (author, -1, {}), "search by author");
            }
            ui.retype ("zzzzqq");
            Ui::pump (80);
            t.check (br.viewState().items.empty(), "nonsense search shows nothing");
            ui.key (KP::downKey);
            t.check (pm.currentPreset() == 0, "arrow keys with an empty result do nothing");
            ui.key (KP::escapeKey);
            Ui::pump (80);
            t.check (search->isEmpty() && (int) br.viewState().items.size() == factoryCount && br.isShowing(), "escape clears the search first");
        }

        // ---- category / bank filters ----
        auto& catCol = br.categoryColumn();
        auto& bankCol = br.bankColumn();
        const auto rowPoint = [] (int row) { return juce::Point<float> (40.0f, (float) row * 23.0f + 11.0f); };
        v = br.viewState();
        int tested = 0;
        for (int row = 2; row < v.categoryRows.size() && tested < 4; ++row)
        {
            if (v.categoryCounts[row] == 0 || (float) row * 23.0f + 11.0f > (float) catCol.getHeight()) continue;
            ++tested;
            ui.click (catCol, rowPoint (row));
            const auto now = br.viewState();
            bool all = (int) now.items.size() == v.categoryCounts[row] && now.categorySelected == row;
            for (auto& it : now.items) all = all && pm.bank (it.first).presets[(size_t) it.second].category == v.categoryRows[row];
            t.check (all, "category \"" + v.categoryRows[row] + "\" shows exactly its " + juce::String (v.categoryCounts[row]) + " presets");
        }
        ui.click (catCol, rowPoint (0));
        ui.click (bankCol, rowPoint (2));
        v = br.viewState();
        t.check (v.bankSelected == 2 && v.items.empty() && v.categoryCounts[0] == 0, "User bank selected: empty, counts follow the bank");
        t.check (ui.button (br, "Rename")->isEnabled() && ui.button (br, "Delete")->isEnabled(), "Rename / Delete enabled for a user bank");
        ui.click (bankCol, rowPoint (1));
        v = br.viewState();
        t.check (v.bankSelected == 1 && (int) v.items.size() == factoryCount, "Factory bank selected");
        t.check (! ui.button (br, "Rename")->isEnabled() && ! ui.button (br, "Delete")->isEnabled() && ui.button (br, "Export...")->isEnabled(),
                 "Rename / Delete disabled for the factory bank, Export enabled");
        ui.click (bankCol, rowPoint (0));

        // ---- favourites ----
        t.begin ("browser: favourites");
        ui.click (grid, starPoint (3));
        t.check (pm.isFavourite (0, 3), "clicking the star favourites the preset");
        t.check (pm.currentPreset() == 0, "clicking the star does not load the preset");
        ui.click (grid, starPoint (7));
        Ui::pump (100);
        v = br.viewState();
        t.check (v.categoryCounts[1] == 2, "Favourites count is 2");
        ui.click (catCol, rowPoint (1));
        v = br.viewState();
        t.check (v.items.size() == 2 && v.items[0] == std::make_pair (0, 3) && v.items[1] == std::make_pair (0, 7), "Favourites filter shows the two starred presets");
        t.check (userDir.getChildFile ("favourites.json").existsAsFile(), "favourites.json written");
        {
            WhyshoSynthAudioProcessor second;
            t.check (second.presetManager().isFavourite (0, 3) && second.presetManager().isFavourite (0, 7) && ! second.presetManager().isFavourite (0, 4),
                     "favourites persist for a new plugin instance");
        }
        ui.click (grid, starPoint (0));     // un-star the first shown one (preset 3)
        Ui::pump (100);
        v = br.viewState();
        t.check (! pm.isFavourite (0, 3) && v.items.size() == 1 && v.categoryCounts[1] == 1, "un-starring removes it from Favourites");
        ui.click (catCol, rowPoint (0));

        // right-click opens the context menu without doing anything by itself
        ui.clock += 1000; ui.press (grid, cellPoint (2), {}, true); ui.release(); Ui::pump (150);
        t.check (pm.currentPreset() == 0, "right-click does not load the preset");
        juce::PopupMenu::dismissAllActiveMenus();
        Ui::pump (150);

        // ---- escape closes ----
        if (search != nullptr) { ui.click (*search); ui.key (KP::escapeKey); }
        Ui::pump (100);
        t.check (! br.isVisible(), "escape with an empty search closes the browser");
        t.check (ui.knob (P::fltCutoff)->isShowing(), "synth page is back");
    }

    // -----------------------------------------------------------------------
    t.begin ("computer keyboard notes");
    void* window = ed.getPeer()->getNativeHandle();
    const auto tapNote = [&] (unsigned long keysym) -> int
    {
        const int before = notes.ons;
        xkeys.key (window, keysym, true);
        Ui::pump (120);
        const int got = notes.ons - before;
        xkeys.key (window, keysym, false);
        Ui::pump (120);
        return got;
    };
    if (haveX)
    {
        ed.grabKeyboardFocus();
        Ui::pump (100);
        const int offs0 = notes.offs;
        t.check (tapNote ('a') == 1 && notes.last == 48, "A plays C at the base octave (note " + juce::String (notes.last.load()) + ")");
        t.check (notes.offs - offs0 == 1, "releasing the key releases the note");
        t.check (tapNote ('w') == 1 && notes.last == 49, "W plays C#");
        ui.type ("x");
        t.check (tapNote ('a') == 1 && notes.last == 60, "X shifts the keyboard up an octave");
        ui.type ("z");
        t.check (tapNote ('a') == 1 && notes.last == 48, "Z shifts it back down");
        t.check (proc->keyboardState().isNoteOn (1, 48) == false, "no note left hanging");

        // held note is released when a text field takes focus
        xkeys.key (window, 's', true);
        Ui::pump (120);
        t.check (proc->keyboardState().isNoteOn (1, 50), "S held = D sounding");
        if (focusWorks)
        {
            ui.doubleClick (*ui.knob (P::fltCutoff), ui.label (*ui.knob (P::fltCutoff)));
            t.check (! proc->keyboardState().isNoteOn (1, 50), "held note is released when a value field opens");
            xkeys.key (window, 's', false);
            Ui::pump (120);
            t.check (tapNote ('a') == 0, "typing in a value field plays no notes");
            ui.key (KP::escapeKey);
            Ui::pump (120);
            t.check (tapNote ('a') == 1, "notes work again after the value field closes");

            ui.click (display);
            t.check (tapNote ('a') == 0, "typing in the browser's search field plays no notes");
            ui.key (KP::returnKey);
            t.check (tapNote ('a') == 1, "after Return in the search field the keys audition presets again");
            ui.key (KP::escapeKey);
            Ui::pump (100);
            t.check (! br.isVisible(), "escape closes the browser when the list has focus");
            br.setSearch ({});
        }
        else
        {
            xkeys.key (window, 's', false);
            Ui::pump (120);
        }
    }

    // -----------------------------------------------------------------------
    t.begin ("save flow");
    const auto userFile = userDir.getChildFile ("User.sy1bank");
    {
        pm.load (0, 4);
        Ui::pump (100);
        ui.dragKnob (*ui.knob (P::fltReso), 0.0f, ui.norm (P::fltReso) > 0.5f ? 50.0f : -50.0f);
        Ui::pump (250);
        t.check (ui.topBar().readout().edited, "edited before saving");
        const float savedReso = ui.raw (P::fltReso);

        ui.click (*ui.button (ui.topBar(), "Save"));
        auto* dlg = findComp<gui::SaveDialog> (ed);
        if (t.check (dlg != nullptr && dlg->isShowing(), "Save opens the save sheet"))
        {
            auto* nameField = ui.direct<juce::TextEditor> (*dlg, 0);
            auto* bankBox = ui.direct<juce::ComboBox> (*dlg, 1);
            auto* status = ui.direct<juce::Label> (*dlg, 0);
            t.same (nameField->getText(), pm.bank (0).presets[4].name, "name field starts with the current name");
            if (focusWorks) t.check (nameField->hasKeyboardFocus (true), "name field is focused");
            t.check (status->getText().containsIgnoreCase ("read-only"), "sheet explains that factory presets are read-only");
            bool factoryOffered = false;
            for (int i = 0; i < bankBox->getNumItems(); ++i) factoryOffered = factoryOffered || bankBox->getItemText (i) == "Factory";
            t.check (! factoryOffered && bankBox->getText() == "User", "Factory is not a save target; User is preselected");
            if (haveX) t.check (tapNote ('a') == 0, "no notes while the save sheet is up");

            // empty name is refused
            ui.click (*nameField);
            ui.retype (" ");
            ui.click (*ui.button (*dlg, "Save"));
            t.check (findComp<gui::SaveDialog> (ed) != nullptr && status->getText().containsIgnoreCase ("name"), "empty name is refused with a message");

            ui.click (*nameField);
            ui.retype ("My Test Lead");
            ui.click (*ui.button (*dlg, "Save"));
            Ui::pump (150);
            t.check (findComp<gui::SaveDialog> (ed) == nullptr, "sheet closes after saving");
        }
        t.check (userFile.existsAsFile(), "User.sy1bank written under the scratch HOME");
        bankio::Parsed parsed;
        t.check (bankio::readFile (userFile, parsed).wasOk() && parsed.isBank && parsed.bank.presets.size() == 1 && parsed.bank.presets[0].name == "My Test Lead",
                 "the file on disk holds the saved preset");
        if (! parsed.bank.presets.empty())
            t.near (bankio::resolve (parsed.bank.presets[0])[(size_t) P::fltReso], savedReso, 1.0e-5f, "saved file holds the edited value");
        t.check (pm.currentBank() == pm.userBankIndex() && pm.currentName() == "My Test Lead", "the saved preset is current");
        auto r = ui.topBar().readout();
        t.check (! r.edited && r.name == "My Test Lead" && r.kicker.startsWith ("User"), "top bar: saved name, User bank, EDITED cleared");
        t.check (pm.bank (0).presets.size() == (size_t) factoryCount && pm.bank (0).presets[4].name != "My Test Lead", "factory bank untouched");

        // ---- overwrite confirmation ----
        t.begin ("save flow: overwrite");
        ui.set (P::fltReso, 0.91f);
        Ui::pump (250);
        t.check (ui.topBar().readout().edited, "edited again");
        ui.click (*ui.button (ui.topBar(), "Save"));
        dlg = findComp<gui::SaveDialog> (ed);
        if (t.check (dlg != nullptr, "save sheet opens again"))
        {
            auto* status = ui.direct<juce::Label> (*dlg, 0);
            t.same (ui.direct<juce::ComboBox> (*dlg, 1)->getText(), "User", "the current user bank is preselected");
            ui.click (*ui.button (*dlg, "Save"));
            t.check (findComp<gui::SaveDialog> (ed) != nullptr && ui.button (*dlg, "Overwrite") != nullptr && status->getText().contains ("already exists"),
                     "same name asks before overwriting");
            parsed = {};
            bankio::readFile (userFile, parsed);
            t.check (parsed.bank.presets.size() == 1 && std::abs (bankio::resolve (parsed.bank.presets[0])[(size_t) P::fltReso] - savedReso) < 1.0e-5f,
                     "nothing written before the confirmation");
            if (auto* ow = ui.button (*dlg, "Overwrite")) ui.click (*ow);
            Ui::pump (150);
            t.check (findComp<gui::SaveDialog> (ed) == nullptr, "Overwrite saves and closes");
        }
        parsed = {};
        bankio::readFile (userFile, parsed);
        t.check (parsed.bank.presets.size() == 1, "still one preset of that name");
        if (! parsed.bank.presets.empty())
            t.near (bankio::resolve (parsed.bank.presets[0])[(size_t) P::fltReso], 0.91f, 1.0e-5f, "the overwrite landed on disk");

        // changing the name after the warning resets the confirmation
        ui.click (*ui.button (ui.topBar(), "Save"));
        dlg = findComp<gui::SaveDialog> (ed);
        if (dlg != nullptr)
        {
            ui.click (*ui.button (*dlg, "Save"));
            t.check (ui.button (*dlg, "Overwrite") != nullptr, "warned again");
            ui.click (*ui.direct<juce::TextEditor> (*dlg, 0));
            ui.retype ("Second Sound");
            Ui::pump (80);
            t.check (ui.button (*dlg, "Save") != nullptr && ui.button (*dlg, "Overwrite") == nullptr, "editing the name turns Overwrite back into Save");

            // ---- new bank from the sheet ----
            auto* bankBox = ui.direct<juce::ComboBox> (*dlg, 1);
            bankBox->setSelectedId (9999, juce::sendNotificationSync);
            Ui::pump (60);
            auto* newBankField = ui.direct<juce::TextEditor> (*dlg, 3);
            if (t.check (newBankField != nullptr && newBankField->isShowing(), "\"New bank...\" reveals the bank name field"))
            {
                ui.click (*newBankField);
                ui.retype ("Pack A");
                ui.click (*ui.button (*dlg, "Save"));
                Ui::pump (150);
            }
            t.check (findComp<gui::SaveDialog> (ed) == nullptr, "saving into a new bank closes the sheet");
        }
        const auto packFile = userDir.getChildFile ("Pack A.sy1bank");
        t.check (packFile.existsAsFile(), "Pack A.sy1bank created");
        t.check (pm.numBanks() == 3 && pm.bank (2).name == "Pack A" && pm.currentBank() == 2 && pm.currentName() == "Second Sound", "new bank holds the current preset");

        // cancel leaves everything alone
        ui.click (*ui.button (ui.topBar(), "Save"));
        dlg = findComp<gui::SaveDialog> (ed);
        if (dlg != nullptr)
        {
            ui.click (*ui.direct<juce::TextEditor> (*dlg, 0));
            ui.retype ("Never Saved");
            ui.click (*ui.button (*dlg, "Cancel"));
            Ui::pump (150);
        }
        t.check (findComp<gui::SaveDialog> (ed) == nullptr && pm.bank (2).presets.size() == 1 && pm.currentName() == "Second Sound", "Cancel saves nothing");
        ui.click (*ui.button (ui.topBar(), "Save"));
        ui.key (KP::escapeKey);
        Ui::pump (150);
        t.check (findComp<gui::SaveDialog> (ed) == nullptr, "Escape closes the save sheet");

        // single-preset export (what the sheet's file dialog would call) and re-import
        const auto presetFile = scratch.getChildFile ("one.sy1preset");
        t.check (pm.exportPreset (pm.captureCurrent(), presetFile) && presetFile.existsAsFile(), "single preset exports to a file");
    }

    // -----------------------------------------------------------------------
    t.begin ("bank management");
    const auto promptOf = [&] () { return findComp<gui::Prompt> (ed); };
    const auto selectedBankName = [&] () { const auto v = br.viewState(); return v.bankRows[v.bankSelected]; };
    {
        ui.click (display);
        t.check (br.isShowing(), "browser open");
        auto& bankCol = br.bankColumn();
        const auto rowPoint = [] (int row) { return juce::Point<float> (40.0f, (float) row * 23.0f + 11.0f); };
        t.same (br.viewState().bankRows.joinIntoString ("|"), "All banks|Factory|User|Pack A", "bank list after saving");

        // new bank (cancel first)
        ui.click (*ui.button (br, "New"));
        t.check (promptOf() != nullptr, "New opens a prompt");
        if (auto* pr = promptOf()) ui.click (*ui.button (*pr, "Cancel"));
        Ui::pump (150);
        t.check (promptOf() == nullptr && pm.numBanks() == 3, "Cancel creates nothing");

        ui.click (*ui.button (br, "New"));
        if (auto* pr = promptOf())
        {
            auto* field = findComp<juce::TextEditor> (*pr);
            if (focusWorks) t.check (field->hasKeyboardFocus (true), "prompt's text field is focused");
            else field->grabKeyboardFocus();
            ui.retype ("Zeta Bank");
            ui.key (KP::returnKey);
            Ui::pump (200);
        }
        t.check (promptOf() == nullptr && pm.numBanks() == 4 && pm.bank (3).name == "Zeta Bank", "Return in the prompt creates the bank");
        t.check (userDir.getChildFile ("Zeta Bank.sy1bank").existsAsFile(), "Zeta Bank.sy1bank on disk");
        t.same (selectedBankName(), "Zeta Bank", "the new bank is selected");
        t.check (br.viewState().message.contains ("Created") && ! br.viewState().messageIsError, "browser confirms the new bank");

        // put a preset into it via the save sheet, so the current preset lives in a bank that will move
        ui.click (*ui.button (ui.topBar(), "Save"));
        if (auto* dlg = findComp<gui::SaveDialog> (ed))
        {
            ui.click (*ui.direct<juce::TextEditor> (*dlg, 0));
            ui.retype ("Zed One");
            auto* bankBox = ui.direct<juce::ComboBox> (*dlg, 1);
            for (int i = 0; i < bankBox->getNumItems(); ++i)
                if (bankBox->getItemText (i) == "Zeta Bank") bankBox->setSelectedId (bankBox->getItemId (i), juce::sendNotificationSync);
            ui.click (*ui.button (*dlg, "Save"));
            Ui::pump (200);
        }
        t.check (pm.currentBank() == 3 && pm.currentName() == "Zed One" && pm.bank (3).presets.size() == 1, "saved into Zeta Bank");
        t.check (br.viewState().bankCounts[4] == 1, "browser count follows the save (change message)");

        // rename: Zeta -> Alpha, which sorts before Pack A, so every index after User shifts
        ui.click (bankCol, rowPoint (4));
        t.same (selectedBankName(), "Zeta Bank", "Zeta Bank selected");
        ui.click (*ui.button (br, "Rename"));
        if (auto* pr = promptOf())
        {
            if (! focusWorks) findComp<juce::TextEditor> (*pr)->grabKeyboardFocus();
            ui.retype ("Alpha Bank");
            ui.click (*ui.button (*pr, "Rename"));
            Ui::pump (200);
        }
        t.same (br.viewState().bankRows.joinIntoString ("|"), "All banks|Factory|User|Alpha Bank|Pack A", "renamed bank re-sorts");
        t.same (selectedBankName(), "Alpha Bank", "selection follows the bank to its new index");
        t.check (userDir.getChildFile ("Alpha Bank.sy1bank").existsAsFile() && ! userDir.getChildFile ("Zeta Bank.sy1bank").exists(), "the file follows the name");
        t.check (pm.currentBank() == 2 && pm.bank (pm.currentBank()).name == "Alpha Bank" && pm.currentPreset() == 0, "current preset follows the renamed bank");
        auto r = ui.topBar().readout();
        t.check (r.kicker.startsWith ("Alpha Bank") && r.name == "Zed One" && ! r.edited, "top bar follows the rename (" + r.kicker + ")");
        t.check ((int) br.viewState().items.size() == 1, "the renamed bank still lists its preset");

        // renaming to a taken name is refused, visibly
        ui.click (*ui.button (br, "Rename"));
        if (auto* pr = promptOf())
        {
            if (! focusWorks) findComp<juce::TextEditor> (*pr)->grabKeyboardFocus();
            ui.retype ("pack a");
            ui.click (*ui.button (*pr, "Rename"));
            Ui::pump (200);
        }
        t.check (pm.bank (2).name == "Alpha Bank" && br.viewState().messageIsError, "renaming to an existing name fails with a message");

        // delete Pack A (index 3) while Alpha Bank (index 2) holds the current preset
        ui.click (bankCol, rowPoint (4));
        t.same (selectedBankName(), "Pack A", "Pack A selected");
        ui.click (*ui.button (br, "Delete"));
        if (auto* pr = promptOf())
        {
            t.check (findComp<juce::Label> (*pr)->getText().contains ("1 preset"), "delete prompt names what will be lost");
            ui.key (KP::escapeKey);
            Ui::pump (150);
        }
        t.check (pm.numBanks() == 4, "Escape cancels the delete");
        ui.click (*ui.button (br, "Delete"));
        if (auto* pr = promptOf()) { ui.click (*ui.button (*pr, "Delete bank")); Ui::pump (200); }
        t.check (pm.numBanks() == 3 && ! userDir.getChildFile ("Pack A.sy1bank").exists(), "bank and file are gone");
        t.check (br.viewState().bankSelected == 0 && br.viewState().message.contains ("Deleted"), "selection falls back to All banks, with a confirmation");
        t.check (pm.currentBank() == 2 && pm.currentName() == "Zed One", "current preset in another bank is unaffected");

        // delete the bank holding the current preset: it becomes an unsaved sound
        ui.click (bankCol, rowPoint (3));
        ui.click (*ui.button (br, "Delete"));
        if (auto* pr = promptOf()) { ui.click (*ui.button (*pr, "Delete bank")); Ui::pump (250); }
        r = ui.topBar().readout();
        t.check (pm.numBanks() == 2 && pm.currentBank() == -1, "deleting the current preset's bank detaches it");
        t.check (r.name == "Zed One" && r.kicker.startsWith ("Unsaved") && r.edited, "top bar: name kept, Unsaved, EDITED (" + r.kicker + ")");
    }

    // -----------------------------------------------------------------------
    t.begin ("export / import");
    {
        auto& bankCol = br.bankColumn();
        const auto rowPoint = [] (int row) { return juce::Point<float> (40.0f, (float) row * 23.0f + 11.0f); };
        ui.click (bankCol, rowPoint (2));                       // User
        const auto exported = scratch.getChildFile ("shared bank.sy1bank");
        br.exportBankTo ("User", exported.withFileExtension (""));
        t.check (exported.existsAsFile() && br.viewState().message.startsWith ("Exported") && ! br.viewState().messageIsError, "bank export writes the file and says so");
        const auto original = pm.bank (1);

        br.importFiles ({ exported });
        Ui::pump (300);
        auto v = br.viewState();
        t.check (pm.numBanks() == 3 && pm.bank (2).name == "User 2", "importing the exported bank adds it under a de-duplicated name");
        t.same (v.bankRows[v.bankSelected], "User 2", "the imported bank is selected");
        t.check (v.message.startsWith ("Imported") && ! v.messageIsError, "import confirmation is still shown after the message loop ran (\"" + v.message + "\")");
        bool same = pm.bank (2).presets.size() == original.presets.size();
        for (size_t i = 0; same && i < original.presets.size(); ++i)
        {
            const auto& a = original.presets[i]; const auto& b = pm.bank (2).presets[i];
            same = a.name == b.name && a.category == b.category && a.author == b.author && a.notes == b.notes && bankio::resolve (a) == bankio::resolve (b);
        }
        t.check (same && ! original.presets.empty(), "round trip preserves every preset bit-exactly");

        // factory export too
        const auto factoryFile = scratch.getChildFile ("factory.sy1bank");
        br.exportBankTo ("Factory", factoryFile);
        bankio::Parsed parsed;
        t.check (bankio::readFile (factoryFile, parsed).wasOk() && (int) parsed.bank.presets.size() == factoryCount, "factory bank exports completely");
        br.exportBankTo ("No Such Bank", scratch.getChildFile ("x.sy1bank"));
        t.check (br.viewState().messageIsError, "exporting a bank that vanished reports an error");

        // malformed files
        const int banksBefore = pm.numBanks();
        const auto garbage = scratch.getChildFile ("broken.sy1bank");
        garbage.replaceWithText ("{ \"format\": \"whysho-sy1-bank\", \"presets\": [ { \"name\": ");
        br.importFiles ({ garbage });
        Ui::pump (300);
        v = br.viewState();
        t.check (v.messageIsError && v.message.contains ("broken.sy1bank") && pm.numBanks() == banksBefore, "malformed bank: inline error, nothing imported (\"" + v.message + "\")");
        const auto wrong = scratch.getChildFile ("other.sy1bank");
        wrong.replaceWithText ("{ \"format\": \"something-else\", \"presets\": [] }");
        const auto binary = scratch.getChildFile ("binary.sy1preset");
        { juce::MemoryBlock mb (4096); juce::Random rng (7); for (size_t i = 0; i < mb.getSize(); ++i) mb[i] = (char) rng.nextInt (256); binary.replaceWithData (mb.getData(), mb.getSize()); }
        const auto missing = scratch.getChildFile ("missing.sy1bank");
        br.importFiles ({ wrong, binary, missing });
        Ui::pump (300);
        v = br.viewState();
        t.check (v.messageIsError && v.message.contains ("other.sy1bank") && v.message.contains ("binary.sy1preset") && v.message.contains ("missing.sy1bank") && pm.numBanks() == banksBefore,
                 "wrong format / binary / missing files: each named in the error");

        // one good, one bad
        const auto presetFile = scratch.getChildFile ("one.sy1preset");
        const int userBefore = (int) pm.bank (1).presets.size();
        br.importFiles ({ presetFile, garbage });
        Ui::pump (300);
        v = br.viewState();
        t.check ((int) pm.bank (1).presets.size() == userBefore + 1 && v.messageIsError && v.message.startsWith ("Imported 1, but"), "mixed import: the good file lands, the bad one is reported");

        // ---- drag and drop ----
        t.begin ("drag and drop");
        ed.setBrowserOpen (false);
        Ui::pump (60);
        t.check (! ed.isInterestedInFileDrag ({ "/tmp/song.wav", "/tmp/notes.txt" }), "other file types are not accepted");
        t.check (ed.isInterestedInFileDrag ({ "/tmp/song.wav", exported.getFullPathName() }), "a .sy1bank among the files is accepted");
        ed.fileDragEnter ({ exported.getFullPathName() }, 100, 100);
        Ui::pump (60);
        ed.filesDropped ({ "/tmp/song.wav", exported.getFullPathName() }, 100, 100);
        Ui::pump (300);
        v = br.viewState();
        t.check (br.isShowing() && pm.numBanks() == banksBefore + 1 && pm.bank (3).name == "User 3", "dropping a bank opens the browser and imports it");
        t.check (v.bankRows[v.bankSelected] == "User 3" && v.message.startsWith ("Imported") && ! v.messageIsError, "dropped bank is selected and confirmed");
        ed.fileDragEnter ({ garbage.getFullPathName() }, 10, 10);
        ed.fileDragExit ({ garbage.getFullPathName() });
        ed.filesDropped ({ garbage.getFullPathName() }, 10, 10);
        Ui::pump (300);
        t.check (br.viewState().messageIsError && pm.numBanks() == banksBefore + 1, "dropping a broken file shows the error inline");

        // a rescan from disk (another instance / the user) keeps the UI consistent
        userDir.getChildFile ("User 3.sy1bank").deleteFile();
        pm.rescan();
        Ui::pump (200);
        v = br.viewState();
        t.check (pm.numBanks() == banksBefore && v.bankRows.size() == banksBefore + 1 && v.bankSelected == 0, "a bank deleted on disk disappears; selection falls back");
        auto img = ed.createComponentSnapshot (ed.getLocalBounds());
        t.check (img.isValid(), "browser repaints after the rescan");
    }

    // -----------------------------------------------------------------------
    t.begin ("init");
    {
        pm.load (0, 9);
        Ui::pump (100);
        ui.click (*ui.button (br, "Init patch"));
        Ui::pump (250);
        bool allDefault = true;
        for (int i = 0; i < kNumParams; ++i) allDefault = allDefault && std::abs (ui.raw ((P) i) - paramDef (i).def) <= 1.0e-4f * juce::jmax (1.0f, std::abs (paramDef (i).def));
        const auto r = ui.topBar().readout();
        t.check (allDefault, "Init patch resets every parameter to its default");
        t.check (r.name == "Init" && r.kicker == "Unsaved" && ! r.edited && pm.currentBank() == -1, "top bar shows Init / Unsaved, not edited");
        ui.click (*ui.button (br, "Close"));
        Ui::pump (100);
        t.check (! br.isVisible(), "Close button closes the browser");
        pm.load (0, 2);
        ui.topBar().onInit();
        Ui::pump (250);
        t.check (pm.currentName() == "Init" && std::abs (ui.raw (P::fltCutoff) - 20000.0f) < 1.0f, "the menu's Init entry does the same");
    }

    // -----------------------------------------------------------------------
    t.begin ("license dialog");
    {
        auto* chip = findComp<juce::Button> (ui.topBar(), [] (juce::Button& b) { return b.getComponentID() == "license"; });
        const auto& li = proc->licenseInfo;
        const auto chipText = ui.topBar().readout().license;
        std::cout << "  licence state " << (int) li.state << ", chip \"" << chipText << "\"\n";
        t.check (li.state == lic::LicenseState::Trial ? chipText.startsWith ("Trial") : chipText.isNotEmpty(), "chip reflects the licence state");
        if (t.check (chip != nullptr, "licence chip exists"))
        {
            ui.click (*chip);
            auto* dlg = findComp<gui::LicenseDialog> (ed);
            if (t.check (dlg != nullptr && dlg->isShowing(), "chip opens the licence dialog"))
            {
                const auto status = ui.direct<juce::Label> (*dlg, 1)->getText();
                const auto days = juce::jmax ((juce::int64) 0, (li.expiresAt - juce::Time::currentTimeMillis() / 1000 + 86399) / 86400);
                if (li.state == lic::LicenseState::Trial) t.same (status, "Trial: " + juce::String (days) + (days == 1 ? " day left" : " days left"), "dialog status matches licenseInfo");
                else                                      t.check (status.isNotEmpty(), "dialog shows a status");
                if (haveX) t.check (tapNote ('a') == 0, "no notes while the licence dialog is up");
                const float reso = ui.raw (P::fltReso);
                ed.getPeer()->handleMouseEvent (Ui::mouse, { 30.0f, 300.0f }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, ui.clock += 1000);
                ed.getPeer()->handleMouseEvent (Ui::mouse, { 30.0f, 200.0f }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, ui.clock += 20);
                ed.getPeer()->handleMouseEvent (Ui::mouse, { 30.0f, 200.0f }, {}, 0.0f, 0.0f, ui.clock += 20);
                Ui::pump (60);
                t.check (juce::exactlyEqual (ui.raw (P::fltReso), reso) && findComp<gui::LicenseDialog> (ed) != nullptr, "the dialog swallows clicks meant for the page behind it");

                auto* close = findComp<juce::Button> (*dlg, [] (juce::Button& b) { return b.getButtonText() == "Close"; });
                ui.click (*close);
                Ui::pump (150);
                t.check (findComp<gui::LicenseDialog> (ed) == nullptr, "Close removes the dialog");
            }
            ui.click (*chip);
            ui.key (KP::escapeKey);
            Ui::pump (150);
            t.check (findComp<gui::LicenseDialog> (ed) == nullptr, "Escape removes the dialog");
            if (haveX) t.check (tapNote ('a') == 1, "notes work again after the dialog closes");
        }
    }

    // -----------------------------------------------------------------------
    t.begin ("themes");
    {
        auto* themeBox = ui.direct<juce::ComboBox> (ui.topBar(), 0);
        const auto names = gui::themeNames();
        for (int i = names.size(); --i >= 0;)
        {
            themeBox->setSelectedId (i + 1, juce::sendNotificationSync);
            Ui::pump (80);
            const auto& th = gui::theme();
            t.check (gui::currentThemeIndex() == i && proc->uiState().theme.load() == i, names[i] + ": active and stored in the instance state");
            t.check (ed.getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId) == th.panel2, names[i] + ": look-and-feel colours follow");
            t.check (findComp<juce::MidiKeyboardComponent> (ed)->findColour (juce::MidiKeyboardComponent::whiteNoteColourId) == th.keyWhite, names[i] + ": keyboard colours follow");
            const auto img = ed.createComponentSnapshot (ed.getLocalBounds());
            t.check (img.getPixelAt (4, 4) == th.panel.withAlpha (1.0f), names[i] + ": top bar is painted in the theme's panel colour (" + img.getPixelAt (4, 4).toString() + " vs " + th.panel.toString() + ")");
            t.same (gui::settings::get ("theme"), names[i], names[i] + ": remembered as the default for new instances");
        }

        // overlays restyle too
        ed.setBrowserOpen (true);
        ed.openSaveDialog();
        themeBox->setSelectedId (5, juce::sendNotificationSync);
        Ui::pump (120);
        auto img = ed.createComponentSnapshot (ed.getLocalBounds());
        t.check (gui::currentThemeIndex() == 4 && img.isValid(), "theme switch with browser and save sheet open");
        ui.key (KP::escapeKey);
        Ui::pump (120);
        t.check (findComp<gui::SaveDialog> (ed) == nullptr && br.isVisible(), "Escape closes the sheet, not the browser under it");
        ed.setBrowserOpen (false);

        // persists through the plugin state into a new instance and its editor
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        {
            WhyshoSynthAudioProcessor second;
            second.setStateInformation (state.getData(), (int) state.getSize());
            t.check (second.uiState().theme.load() == 4, "theme index survives getState -> setState");
            second.uiState().theme = 2;                      // this instance was saved with another theme
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (second.createEditor());
            ed2->setVisible (true);
            Ui::pump (150);
            t.check (gui::currentThemeIndex() == 2, "opening a second instance's editor activates its theme");

            // Two open editors on different saved themes: they must settle, not fight.
            int flips = 0, last = gui::currentThemeIndex();
            for (int i = 0; i < 30; ++i)
            {
                Ui::pump (50);
                if (gui::currentThemeIndex() != last) { ++flips; last = gui::currentThemeIndex(); }
            }
            t.check (flips == 0, "two instances with different themes do not flip the theme back and forth (" + juce::String (flips) + " flips in 1.5 s)");
            t.check (proc->uiState().theme.load() == 4 && second.uiState().theme.load() == 2, "each instance keeps its own saved theme");
            t.check (ed.getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId) == gui::theme().panel2,
                     "the first editor restyled itself to the shared active theme (no half-themed window)");
            t.check (themeBox->getSelectedId() == 3, "its theme picker shows the active theme");

            themeBox->setSelectedId (7, juce::sendNotificationSync);       // user picks a theme in the first window
            Ui::pump (300);
            t.check (gui::currentThemeIndex() == 6 && proc->uiState().theme.load() == 6 && second.uiState().theme.load() == 2,
                     "picking a theme in one window changes that instance's saved theme only");
            t.check (ed2->getLookAndFeel().findColour (juce::PopupMenu::backgroundColourId) == gui::theme().panel2, "the other window follows visually");
        }
        Ui::pump (100);

        // a state recall while the editor is open is followed
        themeBox->setSelectedId (1, juce::sendNotificationSync);
        Ui::pump (80);
        proc->setStateInformation (state.getData(), (int) state.getSize());
        Ui::pump (300);
        t.check (gui::currentThemeIndex() == 4 && themeBox->getSelectedId() == 5, "recalling a state switches the open editor to its theme");
        themeBox->setSelectedId (1, juce::sendNotificationSync);
        Ui::pump (80);
    }

    // -----------------------------------------------------------------------
    for (auto size : { juce::Point<int> (900, 600), juce::Point<int> (1650, 1050), juce::Point<int> (1300, 600), juce::Point<int> (1100, 700) })
    {
        t.begin ("resize " + juce::String (size.x) + "x" + juce::String (size.y));
        ed.setSize (size.x, size.y);
        Ui::pump (150);
        t.check (ed.getWidth() == size.x && ed.getHeight() == size.y, "editor takes the size");
        t.check (proc->uiState().width.load() == size.x && proc->uiState().height.load() == size.y, "size is remembered in the instance state");

        std::vector<gui::Knob*> knobs;       collect (ed, knobs);
        std::vector<gui::ChoiceStrip*> strips; collect (ed, strips);
        std::vector<gui::ToggleChip*> toggles; collect (ed, toggles);
        int missed = 0, shown = 0;
        juce::String firstMiss;
        const auto probe = [&] (juce::Component& c, juce::Point<float> local)
        {
            if (! c.isShowing()) return;
            ++shown;
            const auto inEditor = ed.getLocalPoint (&c, local);
            if (! ed.getLocalBounds().toFloat().contains (inEditor) || ! ui.hits (c, local)) { if (missed++ == 0) firstMiss = Ui::describe (c); }
        };
        for (auto* k : knobs)   { probe (*k, ui.dial (*k)); probe (*k, ui.label (*k)); }
        for (auto* s : strips)  { probe (*s, { 3.0f, 3.0f }); probe (*s, { (float) s->getWidth() - 3.0f, (float) s->getHeight() - 3.0f }); }
        for (auto* g : toggles) probe (*g, g->getLocalBounds().toFloat().getCentre());
        t.check (missed == 0 && shown > 100, juce::String (shown) + " control hit points all reach their control (" + juce::String (missed) + " missed, first: " + firstMiss + ")");

        auto* k = ui.knob (P::revMix);
        ui.param (P::revMix).setValueNotifyingHost (0.2f);
        ui.dragKnob (*k, 0.0f, -57.0f);
        // the drag is given in the knob's own (logical) pixels, so the travel is the same at every scale
        t.near (ui.norm (P::revMix), 0.5f, 0.012f, "knob drag: same travel in logical pixels at this scale");

        auto* s = ui.strip (P::fltType);
        ui.click (*s, { 4.5f * (float) s->getWidth() / 6.0f, (float) s->getHeight() * 0.5f });
        t.check (juce::roundToInt (ui.raw (P::fltType)) == 4, "filter type strip: fifth cell still selects HP 24");
        ui.click (*s, { 0.5f * (float) s->getWidth() / 6.0f, (float) s->getHeight() * 0.5f });

        auto* e = ui.env (P::ampA);
        ui.param (P::ampA).setValueNotifyingHost (0.3f);
        const float a0 = ui.norm (P::ampA);
        ui.clock += 1000; ui.press (*e, e->handlePosition (0)); ui.dragTo (*e, e->handlePosition (0) + juce::Point<float> (10.0f, 0.0f)); ui.release(); Ui::pump();
        t.check (ui.norm (P::ampA) > a0 + 0.05f, "envelope handle is still grabbable");

        pm.load (0, 0);
        ui.click (display, { (float) display.getWidth() - 12.0f, (float) display.getHeight() * 0.5f });
        t.check (pm.currentPreset() == 1, "preset arrows still work");
        ui.click (display);
        t.check (br.isShowing(), "browser opens");
        ui.click (br.presetGrid(), { (float) br.presetCell (3).getX() + 60.0f, (float) br.presetCell (3).getCentreY() });
        t.check (pm.currentPreset() == 3, "browser cell click loads the right preset");
        ui.click (*ui.button (br, "Close"));
        if (haveX) t.check (tapNote ('a') == 1, "keyboard notes still work");
        const auto img = ed.createComponentSnapshot (ed.getLocalBounds());
        t.check (img.getWidth() == size.x && img.getHeight() == size.y, "renders at this size");
    }
    t.begin ("resize limits");
    t.check (ed.isResizable() && ed.getConstrainer() != nullptr
                 && ed.getConstrainer()->getMinimumWidth() == WhyshoSynthAudioProcessorEditor::minW && ed.getConstrainer()->getMaximumHeight() == WhyshoSynthAudioProcessorEditor::maxH,
             "host-visible resize limits are installed");

    // -----------------------------------------------------------------------
    t.begin ("state round trip");
    {
        pm.load (0, 11);
        ui.set (P::fltCutoff, 3210.0f); ui.set (P::osc2Wave, (float) wTriangle); ui.set (P::unison, 5.0f); ui.set (P::dlySync, 0.0f);
        ed.setSize (1234, 777);
        ui.direct<juce::ComboBox> (ui.topBar(), 0)->setSelectedId (4, juce::sendNotificationSync);
        Ui::pump (300);
        juce::MemoryBlock state;
        proc->getStateInformation (state);

        WhyshoSynthAudioProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        Ui::pump (100);
        bool same = true; juce::String diff;
        for (int i = 0; i < kNumParams; ++i)
        {
            const float a = ui.raw ((P) i), b = other.state().getRawParameterValue (pid (i))->load();
            if (std::abs (a - b) > 1.0e-5f * juce::jmax (1.0f, std::abs (a))) { same = false; diff = pid (i); }
        }
        t.check (same, "every parameter survives (" + diff + ")");
        auto& opm = other.presetManager();
        t.check (opm.currentBank() == 0 && opm.currentPreset() == 11 && opm.currentName() == pm.currentName(), "current preset survives");
        t.check (opm.isModified() && pm.isModified(), "modified flag survives");
        t.check (opm.captureCurrent().category == pm.captureCurrent().category, "category survives");
        t.check (other.uiState().theme.load() == 3 && other.uiState().width.load() == 1234 && other.uiState().height.load() == 777, "theme and window size survive");
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed2 (other.createEditor());
            t.check (ed2->getWidth() == 1234 && ed2->getHeight() == 777, "a new editor opens at the saved size");
        }
        // undoing the edit clears the flag in the restored instance too
        const auto& p11 = pm.bank (0).presets[11];
        const auto table = bankio::resolve (p11);
        for (auto p : { P::fltCutoff, P::osc2Wave, P::unison, P::dlySync })
            other.state().getParameter (pid (p))->setValueNotifyingHost (other.state().getParameter (pid (p))->convertTo0to1 (table[(size_t) p]));
        t.check (! opm.isModified(), "restored instance: undoing the edits clears the modified flag");

        juce::MemoryBlock again;
        other.setStateInformation (state.getData(), (int) state.getSize());
        other.getStateInformation (again);
        t.check (again.getSize() == state.getSize(), "getState -> setState -> getState gives a state of the same shape");

        // a state whose bank is not installed keeps the name and reads as unsaved
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        if (auto* node = xml != nullptr ? xml->getChildByName ("PRESET") : nullptr)
        {
            node->setAttribute ("bank", "Somebody Else's Bank");
            node->setAttribute ("name", "Borrowed Sound");
            juce::MemoryBlock foreign;
            juce::AudioProcessor::copyXmlToBinary (*xml, foreign);
            other.setStateInformation (foreign.getData(), (int) foreign.getSize());
            t.check (opm.currentBank() == -1 && opm.currentName() == "Borrowed Sound" && opm.isModified(), "preset from a bank that isn't installed: name kept, shown as unsaved");
            juce::MemoryBlock back;
            other.getStateInformation (back);
            auto xml2 = juce::AudioProcessor::getXmlFromBinary (back.getData(), (int) back.getSize());
            t.check (xml2 != nullptr && xml2->getChildByName ("PRESET") != nullptr && xml2->getChildByName ("PRESET")->getStringAttribute ("bank") == "Somebody Else's Bank",
                     "...and the bank name is passed on when saved again");
        }
        else t.check (false, "state contains the PRESET node");

        // hostile / old state
        t.begin ("state: garbage");
        other.setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock before;
        other.getStateInformation (before);
        juce::Random rng (99);
        for (int n : { 0, 1, 7, 64, 4096 })
        {
            juce::MemoryBlock junk ((size_t) n);
            for (size_t i = 0; i < junk.getSize(); ++i) junk[i] = (char) rng.nextInt (256);
            other.setStateInformation (junk.getData(), n);
        }
        other.setStateInformation (nullptr, 0);
        for (size_t cut : { (size_t) 9, state.getSize() / 2, state.getSize() - 1 })
            other.setStateInformation (state.getData(), (int) cut);
        juce::MemoryBlock afterJunk;
        other.getStateInformation (afterJunk);
        t.check (afterJunk.getSize() == before.getSize() && opm.currentName() == pm.currentName() && opm.currentPreset() == 11
                     && std::abs (other.state().getRawParameterValue ("flt_cutoff")->load() - 3210.0f) < 1.0f,
                 "random bytes and truncated states are ignored");

        const auto feed = [&] (const juce::String& xmlText)
        {
            if (auto x = juce::parseXML (xmlText)) { juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (*x, mb); other.setStateInformation (mb.getData(), (int) mb.getSize()); }
            bool ok = true;
            for (int i = 0; i < kNumParams; ++i)
            {
                const float v = other.state().getRawParameterValue (pid (i))->load();
                ok = ok && std::isfinite (v) && v >= paramDef (i).min - 1.0e-3f && v <= paramDef (i).max + 1.0e-3f;
            }
            juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            other.prepareToPlay (44100.0, 128);
            other.processBlock (buf, midi);
            for (int i = 0; i < 128; ++i) ok = ok && std::isfinite (buf.getSample (0, i)) && std::isfinite (buf.getSample (1, i));
            return ok;
        };
        t.check (feed ("<SOMETHING_ELSE><PARAM id=\"flt_cutoff\" value=\"1\"/></SOMETHING_ELSE>"), "another plugin's state is ignored");
        t.check (std::abs (other.state().getRawParameterValue ("flt_cutoff")->load() - 3210.0f) < 1.0f, "...and leaves the parameters alone");
        t.check (feed ("<WHYSHO_SY1/>"), "an empty state of ours loads (defaults)");
        t.near (other.state().getRawParameterValue ("flt_cutoff")->load(), 20000.0f, 1.0f, "missing parameters fall back to their defaults");
        t.check (other.presetManager().currentName() == "Init" && ! other.presetManager().isModified(), "no preset info = Init");
        t.check (feed ("<WHYSHO_SY1 uiTheme=\"-7\" uiW=\"99999\" uiH=\"-3\"><PARAM id=\"flt_cutoff\" value=\"1e30\"/><PARAM id=\"flt_reso\" value=\"nan\"/>"
                       "<PARAM id=\"osc1_wave\" value=\"77\"/><PARAM id=\"unison\" value=\"-4\"/><PARAM id=\"amp_a\" value=\"inf\"/><PARAM id=\"no_such\" value=\"3\"/>"
                       "<PARAM value=\"3\"/><PRESET bank=\"" + juce::String::repeatedString ("x", 5000) + "\" name=\"a&#10;b\" modified=\"banana\"/><JUNK><JUNK/></JUNK></WHYSHO_SY1>"),
                 "out-of-range / NaN / unknown values stay in range and render finite audio");
        t.check (other.presetManager().currentName().length() <= 64 && ! other.presetManager().currentName().containsChar ('\n'), "preset meta from the state is cleaned");
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed3 (other.createEditor());
            t.check (ed3->getWidth() <= WhyshoSynthAudioProcessorEditor::maxW && ed3->getHeight() >= WhyshoSynthAudioProcessorEditor::minH, "absurd saved window size is clamped");
        }
        ui.direct<juce::ComboBox> (ui.topBar(), 0)->setSelectedId (1, juce::sendNotificationSync);
        Ui::pump (100);
    }

    // -----------------------------------------------------------------------
    t.begin ("teardown");
    ed.openSaveDialog();
    ed.setBrowserOpen (true);
    { gui::PromptSpec s; s.title = "x"; s.wantsText = true; s.onOk = [&ed] (const juce::String&) { gui::PromptSpec s2; s2.title = "second"; ed.openPrompt (s2); }; ed.openPrompt (s); }
    Ui::pump (60);
    if (auto* pr = promptOf())
    {
        // a prompt that opens the next prompt from its own OK button
        findComp<juce::TextEditor> (*pr)->setText ("go", false);
        ui.click (*ui.button (*pr, "OK"));
        Ui::pump (150);
        auto* now = promptOf();
        t.check (now != nullptr && now != pr && now->isShowing(), "a prompt can open a follow-up prompt from its own button");
    }
    ed.openLicenseDialog();
    xkeys.key (window, 'd', true);          // a key held while the editor goes away
    Ui::pump (80);
    t.check (gestures.begins == gestures.ends, "every parameter gesture that began also ended (" + juce::String (gestures.begins.load()) + ")");
    base.reset();
    Ui::pump (150);
    bool anyNote = false;
    for (int n = 0; n < 128; ++n) anyNote = anyNote || proc->keyboardState().isNoteOn (1, n);
    t.check (! anyNote, "closing the editor releases held notes");
    t.check (audio.blocks.load() > 100, "audio thread kept running throughout (" + juce::String (audio.blocks.load()) + " blocks)");

    proc->keyboardState().removeListener (&notes);
    proc->removeListener (&gestures);
    scratch.deleteRecursively();

    for (const auto& [what, count] : XKeySender::xErrors())
        std::cout << "(ignored " << count << " X protocol errors: error code " << what.first << ", request " << what.second << ")\n";
    std::cout << "\ninteract: " << t.checks << " checks, " << t.failures << " failed\n";
    return juce::jmin (t.failures, 100);
}

// ===========================================================================
//  --host : what a DAW does to a plugin, without a DAW.
//  Bus-layout queries, prepare / release at many rates and block sizes, odd block
//  lengths (zero, one, varying), MIDI on block boundaries, state save / load across
//  instances and from a non-message thread, editor open / close while audio runs,
//  automation delivered on the audio thread while the editor is open, preset
//  loads during playback, several instances at once. Meant to be run under
//  AddressSanitizer and ThreadSanitizer builds as well as normally.
// ===========================================================================
struct HostAudio
{
    HostAudio (WhyshoSynthAudioProcessor& p, int maxBlock, bool automateParams, int seed)
        : proc (p), automate (automateParams)
    {
        thread = std::thread ([this, maxBlock, seed]
        {
            juce::AudioBuffer<float> storage (2, maxBlock);
            juce::MidiBuffer midi;
            juce::Random rng (seed);
            const auto& params = proc.getParameters();
            int held = -1, heldFor = 0;

            for (long block = 0; ! quit.load(); ++block)
            {
                // hosts hand over anything from 0 to the prepared maximum
                const int n = block % 13 == 12 ? 0 : (block % 5 == 4 ? 1 + rng.nextInt (maxBlock) : maxBlock);
                juce::AudioBuffer<float> buffer (storage.getArrayOfWritePointers(), 2, n);
                buffer.clear();
                midi.clear();

                if (n > 0)
                {
                    if (held < 0 && block % 9 == 0)
                    {
                        held = 36 + rng.nextInt (48);
                        heldFor = 0;
                        midi.addEvent (juce::MidiMessage::noteOn (1, held, (juce::uint8) (40 + rng.nextInt (87))), block % 2 == 0 ? 0 : n - 1);
                    }
                    else if (held >= 0 && ++heldFor > 6)
                    {
                        midi.addEvent (juce::MidiMessage::noteOff (1, held), block % 2 == 0 ? n - 1 : 0);
                        held = -1;
                    }
                    if (block % 31 == 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, rng.nextInt (16384)), n / 2);
                    if (block % 37 == 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rng.nextInt (128)), 0);
                }

                if (automate.load())
                {
                    // the way JUCE's plugin wrappers deliver host automation: on the audio thread
                    for (int i = 0; i < 6; ++i)
                    {
                        auto* prm = params[rng.nextInt (params.size())];
                        const float v = rng.nextFloat();
                        prm->setValue (v);
                        prm->sendValueChangedMessageToListeners (v);
                    }
                }

                proc.processBlock (buffer, midi);

                float pk = 0.0f;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const float s = buffer.getSample (c, i);
                        if (! std::isfinite (s)) ++nonFinite;
                        pk = juce::jmax (pk, std::abs (s));
                    }
                if (pk > peak.load()) peak = pk;
                if (pk > 1.0e-4f) ++audible;
                ++blocks;
                std::this_thread::sleep_for (std::chrono::microseconds (300));
            }
        });
    }

    ~HostAudio() { stop(); }
    void stop() { quit = true; if (thread.joinable()) thread.join(); }

    void report (Checker& t, const juce::String& what)
    {
        stop();
        t.check (blocks.load() > 20, what + ": audio thread ran (" + juce::String ((int) blocks.load()) + " blocks)");
        t.check (nonFinite.load() == 0, what + ": output stayed finite");
        t.check (peak.load() <= 1.5f, what + ": output stayed bounded (peak " + juce::String (peak.load(), 3) + ")");
    }

    WhyshoSynthAudioProcessor& proc;
    std::atomic<bool> quit { false }, automate;
    std::atomic<long> blocks { 0 }, nonFinite { 0 }, audible { 0 };
    std::atomic<float> peak { 0.0f };
    std::thread thread;
};

void prepare (WhyshoSynthAudioProcessor& p, double sr, int block)
{
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
}

int hostHarness()
{
    Checker t;
    const auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    const auto stereo = juce::AudioChannelSet::stereo();

    // -----------------------------------------------------------------------
    t.begin ("plugin description / buses");
    {
        WhyshoSynthAudioProcessor p;
        const auto layout = [] (std::initializer_list<juce::AudioChannelSet> ins, std::initializer_list<juce::AudioChannelSet> outs)
        {
            juce::AudioProcessor::BusesLayout l;
            for (auto& s : ins) l.inputBuses.add (s);
            for (auto& s : outs) l.outputBuses.add (s);
            return l;
        };
        t.check (p.getBusCount (true) == 0 && p.getBusCount (false) == 1, "no inputs, one output bus");
        t.check (p.getTotalNumInputChannels() == 0 && p.getTotalNumOutputChannels() == 2, "2 output channels by default");
        t.check (p.checkBusesLayoutSupported (layout ({}, { stereo })), "stereo out is supported");
        t.check (! p.checkBusesLayoutSupported (layout ({}, { juce::AudioChannelSet::mono() })), "mono out is refused");
        t.check (! p.checkBusesLayoutSupported (layout ({}, { juce::AudioChannelSet::disabled() })), "disabled out is refused");
        t.check (! p.checkBusesLayoutSupported (layout ({}, { juce::AudioChannelSet::create5point1() })), "5.1 out is refused");
        t.check (! p.checkBusesLayoutSupported (layout ({ stereo }, { stereo })), "an input bus is refused");
        t.check (! p.checkBusesLayoutSupported (layout ({}, { stereo, stereo })), "a second output bus is refused");
        t.check (p.setBusesLayout (layout ({}, { stereo })) && ! p.setBusesLayout (layout ({}, { juce::AudioChannelSet::mono() }))
                     && p.getTotalNumOutputChannels() == 2, "setBusesLayout: stereo accepted, mono rejected, layout unchanged");
        t.check (p.acceptsMidi() && ! p.producesMidi() && ! p.isMidiEffect() && p.hasEditor(), "instrument flags");
        t.check (p.getTailLengthSeconds() > 0.0 && p.getLatencySamples() == 0, "tail reported, no latency");
        t.check (p.getNumPrograms() >= 1 && p.getProgramName (0).isNotEmpty(), "program API answers");
        p.setCurrentProgram (0); p.changeProgramName (0, "x");

        const auto& params = p.getParameters();
        bool ok = params.size() == kNumParams;
        std::set<juce::String> ids;
        for (int i = 0; ok && i < params.size(); ++i)
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[i]);
            ok = r != nullptr && r->paramID == paramDef (i).id && ids.insert (r->paramID).second && r->getVersionHint() != 0
                 && std::abs (r->convertFrom0to1 (r->getDefaultValue()) - paramDef (i).def) <= 1.0e-4f * juce::jmax (1.0f, std::abs (paramDef (i).def))
                 && r->getName (64).isNotEmpty() && r->getText (r->getValue(), 64).isNotEmpty();
        }
        t.check (ok, "host parameter list matches the parameter table (ids, order, defaults, version hints)");
    }

    // -----------------------------------------------------------------------
    t.begin ("prepare / release matrix");
    {
        WhyshoSynthAudioProcessor p;
        int combos = 0, bad = 0, silent = 0;
        float worst = 0.0f;
        for (double sr : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (int bs : { 1, 16, 64, 512, 2048, 4096 })
            {
                ++combos;
                prepare (p, sr, bs);
                if (combos % 3 == 0) prepare (p, sr, bs);                  // some hosts prepare twice without releasing
                juce::AudioBuffer<float> buf (2, bs);
                juce::MidiBuffer midi;
                const int total = (int) (sr * 0.12);
                float pk = 0.0f;
                bool finite = true;
                for (int done = 0, block = 0; done < total; ++block)
                {
                    int n = bs;
                    if (block % 7 == 3) n = 0;
                    else if (block % 5 == 2) n = juce::jmax (1, bs / 3);
                    juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                    view.clear(); midi.clear();
                    if (block == 0)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
                        midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.7f), juce::jmax (0, n - 1));       // last sample of the block
                        midi.addEvent (juce::MidiMessage::noteOn (2, 67, 0.7f), juce::jmax (0, n - 1));
                        midi.addEvent (juce::MidiMessage::noteOff (2, 67), juce::jmax (0, n - 1));             // on and off on the same sample
                    }
                    if (n > 0 && done > total * 3 / 4 && done - n <= total * 3 / 4)
                        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                    p.processBlock (view, midi);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < n; ++i) { const float s = view.getSample (c, i); finite = finite && std::isfinite (s); pk = juce::jmax (pk, std::abs (s)); }
                    done += juce::jmax (n, 1);
                }
                if (! finite || pk > 1.5f) ++bad;
                if (pk < 1.0e-3f) ++silent;
                worst = juce::jmax (worst, pk);
                if (combos % 2 == 0) p.releaseResources();
                if (combos % 4 == 0) p.reset();
            }
        t.check (bad == 0, juce::String (combos) + " rate / block combinations render finite, bounded audio (worst peak " + juce::String (worst, 3) + ")");
        t.check (silent == 0, "every combination actually produced sound (" + juce::String (silent) + " silent)");

        // a note-off that arrives in a zero-length block must not be lost
        prepare (p, 48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256), empty (2, 0);
        juce::MidiBuffer midi;
        const auto render = [&] (int blocks) { float pk = 0.0f; for (int i = 0; i < blocks; ++i) { buf.clear(); p.processBlock (buf, midi); midi.clear(); pk = juce::jmax (pk, buf.getMagnitude (0, 256)); } return pk; };
        p.presetManager().loadInit();
        render (400);                                                   // let earlier tails die away
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        p.processBlock (empty, midi); midi.clear();
        const float sounding = render (20);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        p.processBlock (empty, midi); midi.clear();
        render (300);
        const float after = render (20);
        std::cout << "  zero-length blocks: after note-on " << sounding << ", after note-off " << after << "\n";
        t.check (after < 1.0e-4f, "a note-off delivered in a zero-length block still releases the note");
        t.check (sounding > 1.0e-3f, "a note-on delivered in a zero-length block still sounds");

        // on-screen keyboard events queued while a zero-length block passes
        p.keyboardState().noteOn (1, 62, 0.8f);
        p.processBlock (empty, midi);
        const float kb = render (20);
        p.keyboardState().noteOff (1, 62, 0.0f);
        p.processBlock (empty, midi);
        render (300);
        t.check (kb > 1.0e-3f && render (20) < 1.0e-4f, "on-screen keyboard note on / off survive zero-length blocks");
        t.check (p.activeVoices() == 0, "no voice left running");
    }

    // -----------------------------------------------------------------------
    t.begin ("state across instances");
    {
        juce::Random rng (2024);
        auto a = std::make_unique<WhyshoSynthAudioProcessor>();
        int mismatches = 0, metaMismatches = 0;
        for (int round = 0; round < 40; ++round)
        {
            auto& pm = a->presetManager();
            if (round % 3 == 0)      pm.load (0, rng.nextInt ((int) pm.bank (0).presets.size()));
            else if (round % 3 == 1) pm.loadInit();
            for (int i = 0; i < 12; ++i)
            {
                // stepped parameters get one of their steps, as a host's own control would send
                auto* prm = a->getParameters()[rng.nextInt (kNumParams)];
                const int steps = prm->getNumSteps();
                prm->setValueNotifyingHost (prm->isDiscrete() && steps > 1 ? (float) rng.nextInt (steps) / (float) (steps - 1) : rng.nextFloat());
            }
            a->uiState().theme = rng.nextInt (7);
            a->uiState().width = 900 + rng.nextInt (700);
            a->uiState().height = 600 + rng.nextInt (400);

            juce::MemoryBlock state;
            a->getStateInformation (state);
            auto b = std::make_unique<WhyshoSynthAudioProcessor>();
            if (round % 2 == 0) prepare (*b, 44100.0, 128);
            b->setStateInformation (state.getData(), (int) state.getSize());
            for (int i = 0; i < kNumParams; ++i)
                if (std::abs (a->getParameters()[i]->getValue() - b->getParameters()[i]->getValue()) > 1.0e-6f)
                    if (mismatches++ == 0) std::cout << "  first difference: " << pid (i) << " " << a->getParameters()[i]->getValue() << " -> " << b->getParameters()[i]->getValue() << "\n";
            auto& bpm = b->presetManager();
            const bool metaSame = bpm.currentName() == pm.currentName() && bpm.currentBank() == pm.currentBank() && bpm.currentPreset() == pm.currentPreset()
                                  && bpm.isModified() == pm.isModified() && b->uiState().theme.load() == a->uiState().theme.load()
                                  && b->uiState().width.load() == a->uiState().width.load() && b->uiState().height.load() == a->uiState().height.load();
            // (Not compared byte for byte: a skewed parameter can come back one float
            //  ulp away after the host's normalised round trip, which changes its text.)
            juce::MemoryBlock again;
            b->getStateInformation (again);
            const auto x1 = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            const auto x2 = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            const bool nodeSame = x1 != nullptr && x2 != nullptr && x1->getChildByName ("PRESET") != nullptr && x2->getChildByName ("PRESET") != nullptr
                                  && x1->getChildByName ("PRESET")->isEquivalentTo (x2->getChildByName ("PRESET"), true)
                                  && x1->getNumChildElements() == x2->getNumChildElements();
            if (! metaSame || ! nodeSame)
                if (metaMismatches++ == 0)
                    std::cout << "  first difference (round " << round << "): \"" << pm.currentName() << "\" bank " << pm.currentBank() << " preset " << pm.currentPreset()
                              << " modified " << (int) pm.isModified() << "  ->  \"" << bpm.currentName() << "\" bank " << bpm.currentBank() << " preset " << bpm.currentPreset()
                              << " modified " << (int) bpm.isModified() << (nodeSame ? "" : "  (re-saved PRESET node differs)") << "\n";
            a = std::move (b);                                          // the copy becomes the next source
        }
        t.check (mismatches == 0, "parameters survive 40 save / load hops between instances (" + juce::String (mismatches) + " differences)");
        t.check (metaMismatches == 0, "preset name, bank, modified flag, theme and size survive too (" + juce::String (metaMismatches) + " differences)");
    }

    // -----------------------------------------------------------------------
    t.begin ("editor open / close while audio runs");
    {
        WhyshoSynthAudioProcessor p;
        prepare (p, 48000.0, 256);
        HostAudio audio (p, 256, false, 1);
        for (int i = 0; i < 40; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
            auto* ed = dynamic_cast<WhyshoSynthAudioProcessorEditor*> (base.get());
            ed->setVisible (true);
            ed->setSize (900 + (i * 131) % 750, 600 + (i * 71) % 450);
            if (i % 3 == 0) ed->setBrowserOpen (true);
            if (i % 4 == 1) ed->openSaveDialog();
            if (i % 5 == 2) ed->openLicenseDialog();
            p.presetManager().load (0, i % (int) p.presetManager().bank (0).presets.size());
            pump (40);
            if (i % 4 == 0) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f); juce::ignoreUnused (img); }
            base.reset();
            pump (5);
        }
        audio.report (t, "open / close x40");
    }

    // -----------------------------------------------------------------------
    t.begin ("automation on the audio thread while the editor is open");
    {
        WhyshoSynthAudioProcessor p;
        prepare (p, 44100.0, 128);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<WhyshoSynthAudioProcessorEditor*> (base.get());
        ed->setVisible (true);
        HostAudio audio (p, 128, true, 2);
        for (int i = 0; i < 40; ++i)
        {
            pump (50);
            if (i % 8 == 0) ed->setBrowserOpen (i % 16 == 0);
            if (i % 5 == 0) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f); juce::ignoreUnused (img); }
            if (i == 20) { base.reset(); pump (20); base.reset (p.createEditor()); ed = dynamic_cast<WhyshoSynthAudioProcessorEditor*> (base.get()); ed->setVisible (true); }
        }
        audio.report (t, "automated");
        bool inRange = true;
        for (int i = 0; i < kNumParams; ++i)
        {
            const float v = p.state().getRawParameterValue (pid (i))->load();
            inRange = inRange && v >= paramDef (i).min - 1.0e-3f && v <= paramDef (i).max + 1.0e-3f;
        }
        t.check (inRange, "every parameter is still inside its range");
        pump (100);
    }

    // -----------------------------------------------------------------------
    t.begin ("preset loads during playback");
    {
        WhyshoSynthAudioProcessor p;
        prepare (p, 48000.0, 512);
        HostAudio audio (p, 512, false, 3);
        auto& pm = p.presetManager();
        const int n = (int) pm.bank (0).presets.size();
        for (int i = 0; i < 3 * n; ++i)
        {
            pm.load (0, (i * 7) % n);
            pump (6);
            if (i % 11 == 0) pm.step (i % 2 == 0 ? 1 : -1);
            if (i % 29 == 0) pm.loadInit();
        }
        // save / rename / delete in the user bank while playing
        auto mine = pm.captureCurrent();
        mine.name = "Harness A";
        t.check (pm.savePreset (pm.userBankIndex(), mine), "save during playback");
        mine.name = "Harness B";
        t.check (pm.savePreset (pm.userBankIndex(), mine) && pm.bank (pm.userBankIndex()).presets.size() == 2, "second save");
        t.check (pm.renamePreset (pm.userBankIndex(), 0, "Harness C") && pm.bank (pm.userBankIndex()).presets[0].name == "Harness C", "rename");
        t.check (pm.deletePreset (pm.userBankIndex(), 1) && pm.currentBank() == -1 && pm.isModified(), "deleting the current preset detaches it");
        t.check (pm.deleteBank (pm.userBankIndex()) && pm.bank (pm.userBankIndex()).presets.empty(), "deleting the User bank leaves a fresh empty one");
        pump (50);
        t.check (audio.audible.load() > 10, "audio was audible while presets changed");
        audio.report (t, "preset changes");
    }

    // -----------------------------------------------------------------------
    t.begin ("state save / load from a host thread while the editor is open");
    {
        WhyshoSynthAudioProcessor p;
        prepare (p, 48000.0, 256);
        auto& pm = p.presetManager();

        std::vector<juce::MemoryBlock> states (4);
        juce::StringArray names;
        for (int i = 0; i < 4; ++i)
        {
            pm.load (0, 3 + i * 5);
            p.uiState().theme = i;
            p.getStateInformation (states[(size_t) i]);
            names.add (pm.currentName());
        }

        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        base->setVisible (true);
        HostAudio audio (p, 256, false, 4);

        std::atomic<bool> quit { false };
        std::atomic<int> loads { 0 }, last { -1 }, broken { 0 };
        std::thread hostThread ([&]
        {
            juce::Random rng (5);
            while (! quit.load())
            {
                const int which = rng.nextInt (4);
                p.setStateInformation (states[(size_t) which].getData(), (int) states[(size_t) which].getSize());
                last = which;
                ++loads;
                juce::MemoryBlock out;
                p.getStateInformation (out);                            // must describe the state just loaded, even before the UI caught up
                if (auto xml = juce::AudioProcessor::getXmlFromBinary (out.getData(), (int) out.getSize()))
                {
                    auto* node = xml->getChildByName ("PRESET");
                    if (node == nullptr || node->getStringAttribute ("name") != names[which] || xml->getIntAttribute ("uiTheme", -1) != which) ++broken;
                }
                else ++broken;
                std::this_thread::sleep_for (std::chrono::milliseconds (3));
            }
        });
        for (int i = 0; i < 60; ++i) pump (25);
        quit = true;
        hostThread.join();
        pump (400);

        t.check (loads.load() > 50, "host thread loaded " + juce::String (loads.load()) + " states");
        t.check (broken.load() == 0, "getStateInformation right after an off-thread setStateInformation returns that state (" + juce::String (broken.load()) + " wrong)");
        t.check (pm.currentName() == names[last.load()], "once the message thread caught up, the preset manager shows the last loaded state");
        t.check (gui::currentThemeIndex() == last.load(), "the open editor followed the state's theme");
        juce::String diff;
        bool same = true;
        const auto xml = juce::AudioProcessor::getXmlFromBinary (states[(size_t) last.load()].getData(), (int) states[(size_t) last.load()].getSize());
        if (xml != nullptr)
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                if (const int idx = paramIndex (e->getStringAttribute ("id")); idx >= 0)
                    same = same && std::abs ((float) e->getDoubleAttribute ("value") - p.state().getRawParameterValue (pid (idx))->load()) < 1.0e-4f * juce::jmax (1.0f, std::abs ((float) e->getDoubleAttribute ("value")));
        t.check (same, "parameters hold the last loaded state");
        base.reset();
        audio.report (t, "off-thread state");
    }

    // -----------------------------------------------------------------------
    t.begin ("several instances at once");
    {
        std::vector<std::unique_ptr<WhyshoSynthAudioProcessor>> procs;
        std::vector<std::unique_ptr<HostAudio>> audios;
        std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;
        for (int i = 0; i < 4; ++i)
        {
            procs.push_back (std::make_unique<WhyshoSynthAudioProcessor>());
            prepare (*procs.back(), i % 2 == 0 ? 44100.0 : 96000.0, 64 << i);
            procs.back()->uiState().theme = i + 1;
            audios.push_back (std::make_unique<HostAudio> (*procs.back(), 64 << i, i % 2 == 1, 10 + i));
            editors.emplace_back (procs.back()->createEditor());
            editors.back()->setVisible (true);
        }
        int flips = 0, lastTheme = gui::currentThemeIndex();
        for (int i = 0; i < 40; ++i)
        {
            pump (25);
            if (gui::currentThemeIndex() != lastTheme) { ++flips; lastTheme = gui::currentThemeIndex(); }
            procs[(size_t) (i % 4)]->presetManager().load (0, i);
            if (i == 20) { editors[1].reset(); editors[1].reset (procs[1]->createEditor()); editors[1]->setVisible (true); }
        }
        t.check (flips <= 1, "four open editors with four different saved themes settle on one (" + juce::String (flips) + " changes)");
        bool kept = true;
        for (int i = 0; i < 4; ++i) kept = kept && procs[(size_t) i]->uiState().theme.load() == i + 1;
        t.check (kept, "each instance kept its own saved theme");
        // a favourite set in one instance is seen by the others (shared library on disk)
        procs[0]->presetManager().setFavourite (0, 1, true);
        procs[2]->presetManager().rescan();
        t.check (procs[2]->presetManager().isFavourite (0, 1), "library changes are shared between instances");
        procs[0]->presetManager().setFavourite (0, 1, false);
        // hosts destroy in any order: editor first, then the processor, others keep running
        editors[2].reset(); audios[2]->report (t, "instance 3"); audios[2].reset(); procs[2].reset();
        pump (100);
        editors.clear();
        for (int i = 0; i < 4; ++i) if (audios[(size_t) i] != nullptr) audios[(size_t) i]->report (t, "instance " + juce::String (i + 1));
        audios.clear();
        procs.clear();
        pump (50);
    }

    // -----------------------------------------------------------------------
    t.begin ("instance created and destroyed on another thread (plugin scan)");
    {
        std::atomic<bool> done { false };
        std::atomic<int> ok { 0 };
        std::thread scanner ([&]
        {
            for (int i = 0; i < 6; ++i)
            {
                WhyshoSynthAudioProcessor p;
                prepare (p, 48000.0, 128);
                juce::AudioBuffer<float> buf (2, 128);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
                p.processBlock (buf, midi);
                juce::MemoryBlock mb;
                p.getStateInformation (mb);
                p.setStateInformation (mb.getData(), (int) mb.getSize());
                if (mb.getSize() > 100 && p.getParameters().size() == kNumParams) ++ok;
            }
            done = true;
        });
        while (! done.load()) pump (20);
        scanner.join();
        pump (100);
        t.check (ok.load() == 6, "six instances lived and died on a worker thread");
    }

    std::cout << "\nhost: " << t.checks << " checks, " << t.failures << " failed\n";
    return juce::jmin (t.failures, 100);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--stress") return stress();
    if (argc > 1 && juce::String (argv[1]) == "--interact") return interact();
    if (argc > 1 && juce::String (argv[1]) == "--host") return hostHarness();
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
