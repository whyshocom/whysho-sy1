#pragma once
#include "Controls.h"

namespace wsho { class WhyshoSynthAudioProcessor; }

namespace wsho::gui
{
// Display-only approximations of the engine's waveforms (phase 0..1 → -1..1).
float oscWaveValue (int wave, float phase, float shape);
float lfoWaveValue (int wave, float phase);
// Small glyphs for the wave selectors (stroked in the current colour).
void drawOscWaveIcon (juce::Graphics&, juce::Rectangle<float>, int wave);
void drawLfoWaveIcon (juce::Graphics&, juce::Rectangle<float>, int wave);
void drawSubWaveIcon (juce::Graphics&, juce::Rectangle<float>, int wave);

// ---------------------------------------------------------------------------
// Base for the little curve displays. They never run their own timer: the
// editor's single UI timer calls tick(), which repaints only when one of the
// watched parameters actually moved.
class ParamDisplay : public juce::Component,
                     public Dimmable
{
public:
    virtual void tick();
    void setDimmed (bool d) override { if (dimmed != d) { dimmed = d; repaint(); } }
    void setInk (int paletteIndex)   { ink = paletteIndex; repaint(); }

protected:
    void watch (APVTS&, std::initializer_list<P>);
    float val (int i) const { return watched[(size_t) i]->load (std::memory_order_relaxed); }
    juce::Colour inkColour() const { return ink < 0 ? theme().accent : bandColour (ink); }
    float fade() const { return dimmed ? 0.35f : 1.0f; }
    // display background + outline; returns the inner drawing area
    juce::Rectangle<float> paintFrame (juce::Graphics&, bool chartGrid) const;
    // filled-to-baseline curve in the house style (ink fill, dark line on paper; glow on neon themes)
    void paintCurve (juce::Graphics&, const juce::Path& line, juce::Rectangle<float> area, float baselineY) const;

    bool dimmed = false;
    int  ink = -1;

private:
    std::vector<std::atomic<float>*> watched;
    std::vector<float> last;
};

// Oscillator waveform, reflecting wave + shape.
class WavePreview : public ParamDisplay
{
public:
    WavePreview (APVTS&, int osc);
    void paint (juce::Graphics&) override;
};

// LFO waveform, two cycles.
class LfoPreview : public ParamDisplay
{
public:
    LfoPreview (APVTS&, int lfo);
    void paint (juce::Graphics&) override;
};

// Approximate filter magnitude response; drag = cutoff (x) / resonance (y).
class FilterDisplay : public ParamDisplay,
                      public juce::SettableTooltipClient
{
public:
    explicit FilterDisplay (APVTS&);
    ~FilterDisplay() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float responseDb (float hz) const;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }
    float xForHz (float hz) const;
    juce::RangedAudioParameter &cutoffP, &resoP;
    juce::ParameterAttachment cutoff, reso;
    bool dragging = false;
    float startReso = 0.0f;
};

// ADSR shape with three draggable points (attack / decay+sustain / release).
class EnvDisplay : public ParamDisplay
{
public:
    EnvDisplay (APVTS&, P attack, P decay, P sustain, P release);
    ~EnvDisplay() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Geometry { juce::Rectangle<float> area; float segW, holdW; juce::Point<float> p[5]; };
    Geometry geometry() const;
    int handleAt (juce::Point<float>) const;
    void endDrag();

    const P ids[4];
    juce::RangedAudioParameter* params[4];
    std::unique_ptr<juce::ParameterAttachment> att[4];
    int hover = -1, drag = -1;
};

// Output oscilloscope (rising-edge triggered so the trace stands still).
class ScopeDisplay : public ParamDisplay
{
public:
    explicit ScopeDisplay (WhyshoSynthAudioProcessor&);
    void tick() override;
    void paint (juce::Graphics&) override;

private:
    WhyshoSynthAudioProcessor& proc;
    std::array<float, 1024> buf {};
    int start = 0;
    bool silent = true;
};

// Stereo peak meter (horizontal bars).
class Meter : public juce::Component
{
public:
    explicit Meter (WhyshoSynthAudioProcessor&);
    void tick();
    void paint (juce::Graphics&) override;

private:
    WhyshoSynthAudioProcessor& proc;
    float level[2] { 0.0f, 0.0f };
    bool  clip[2] { false, false };
    int   clipHold[2] { 0, 0 };
};

} // namespace wsho::gui
