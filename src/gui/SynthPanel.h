#pragma once
#include "Controls.h"
#include "Displays.h"

namespace wsho { class WhyshoSynthAudioProcessor; }

namespace wsho::gui
{
// Tiny all-caps text used for column heads / control captions.
class Caption : public juce::Component,
                public Dimmable
{
public:
    explicit Caption (const juce::String& t, juce::Justification j = juce::Justification::centred)
        : text (t), just (j) { setInterceptsMouseClicks (false, false); }
    void setDimmed (bool d) override { if (dimmed != d) { dimmed = d; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    juce::String text;
    juce::Justification just;
    bool dimmed = false;
};

// One titled block of the synth page: a strong rule, a kicker, optionally a
// power LED, and the controls it owns. Layout is supplied by the builder.
class Section : public juce::Component
{
public:
    explicit Section (const juce::String& title);

    template <typename T, typename... Args>
    T& add (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c);
        addAndMakeVisible (c);
        return *c;
    }

    void setPower (ToggleChip& chip) { power = &chip; resized(); }
    void setDimmed (bool);          // greys everything but the power LED
    bool isDimmed() const { return dimmed; }
    void tickDisplays();

    std::function<void (juce::Rectangle<int> body)> onLayout;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headerH = 20;

private:
    juce::String title;
    juce::OwnedArray<juce::Component> owned;
    ToggleChip* power = nullptr;
    bool dimmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Section)
};

// The whole instrument on one page, signal flow left → right, top → bottom:
//   OSC 1 · OSC 2 · OSC 3 · MIX · FILTER
//   AMP ENV · FILTER ENV · PITCH · LFO 1 · LFO 2 · VOICE
//   MOD MATRIX · DRIVE · CHORUS · DELAY · REVERB · OUT
class SynthPanel : public juce::Component
{
public:
    explicit SynthPanel (WhyshoSynthAudioProcessor&);

    void tick();                    // from the editor's UI timer: dimming, relabels, displays
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Section& section (const juce::String& title);
    float raw (P p) const { return rawValues[(size_t) p]->load (std::memory_order_relaxed); }

    void buildOsc (int i);
    void buildMix();
    void buildFilter();
    void buildEnv (const juce::String& title, P a, int ink);
    void buildPitch();
    void buildLfo (int i);
    void buildVoice();
    void buildMod();
    void buildFx();
    void buildOut();

    WhyshoSynthAudioProcessor& proc;
    APVTS& state;
    std::array<std::atomic<float>*, (size_t) kNumParams> rawValues {};

    juce::OwnedArray<Section> sections;
    std::vector<Section*> rows[3];
    std::vector<int> rowWidths[3];

    // things tick() needs to poke
    struct OscRefs { Section* sec; Knob* shape; };
    OscRefs osc[kNumOscs] {};
    struct SyncRefs { juce::Component* free; juce::Component* synced; juce::Component* syncedCaption; P sync; };
    std::vector<SyncRefs> syncs;
    struct FxRefs { Section* sec; P on; };
    std::vector<FxRefs> fx;
    Knob* modAmt[kNumModSlots] {};
    Knob* uniDetune = nullptr;
    Knob* uniSpread = nullptr;
    Knob* glide = nullptr;
    Meter* meter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthPanel)
};

} // namespace wsho::gui
