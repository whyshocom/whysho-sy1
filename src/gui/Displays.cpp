#include "Displays.h"
#include "../PluginProcessor.h"

namespace wsho::gui
{
namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

float frac (float x) { return x - std::floor (x); }

// ---- The engine's oscillator maths (dsp/Voice.cpp, dsp/Tables.cpp), without band-limiting ----
// Triangle wavefolder (dsp::foldTri): identity inside ±1, reflects beyond.
float fold (float x) { return 1.0f - 4.0f * std::abs (frac (x * 0.25f + 0.25f) - 0.5f); }

// Wavetable phases: saw = 0 at phase 0, rising, step at 0.5; triangle = 0 at phase 0, peak at 0.25.
float sawTable (float ph) { return 2.0f * frac (ph + 0.5f) - 1.0f; }
float triTable (float ph) { return 1.0f - 4.0f * std::abs (frac (ph + 0.25f) - 0.5f); }

// Shape on Saw / Triangle / Sine: drive x1 (0) -> x5 (1) into the folder, corners rounded.
float foldShape (float raw, float shape)
{
    if (shape <= 0.001f) return raw;
    const float v = fold (raw * (1.0f + 4.0f * shape));
    return v + 0.5f * juce::jmin (1.0f, shape * 8.0f) * (v - v * v * v);
}

// The 7 detuned saws of the SuperSaw and their mono weight ((L + R) / 2 / sqrt (7)).
constexpr float kSsOffsets[7] = { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f };
constexpr float kSsWeight[7]  = { 0.925f, 0.925f, 0.925f, 1.0f, 0.925f, 0.925f, 0.925f };
constexpr float kSsNorm = 0.37796447f;

// Cheap deterministic noise so previews don't shimmer between repaints.
float hash (int i)
{
    auto x = (juce::uint32) i * 0x9E3779B9u;
    x ^= x >> 15; x *= 0x85EBCA6Bu; x ^= x >> 13;
    return (float) (x & 0xFFFF) / 32767.5f - 1.0f;
}

void polyline (juce::Graphics& g, juce::Rectangle<float> r, std::initializer_list<juce::Point<float>> pts, float thickness = 1.3f)
{
    juce::Path p;
    bool first = true;
    for (auto pt : pts)
    {
        const juce::Point<float> q (r.getX() + pt.x * r.getWidth(), r.getCentreY() - pt.y * r.getHeight() * 0.5f);
        if (first) p.startNewSubPath (q); else p.lineTo (q);
        first = false;
    }
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::mitered));
}

void sineIcon (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::Path p;
    for (int i = 0; i <= 16; ++i)
    {
        const float x = (float) i / 16.0f;
        const juce::Point<float> q (r.getX() + x * r.getWidth(), r.getCentreY() - std::sin (x * kTwoPi) * r.getHeight() * 0.5f);
        if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
    }
    g.strokePath (p, juce::PathStrokeType (1.3f));
}
} // namespace

// ===========================================================================
float oscWaveValue (int wave, float phase, float shape)
{
    const float ph = frac (phase);
    shape = juce::jlimit (0.0f, 1.0f, shape);
    switch (wave)
    {
        case wSaw:      return foldShape (sawTable (ph), shape);
        case wTriangle: return foldShape (triTable (ph), shape);
        case wSine:     return foldShape (std::sin (ph * kTwoPi), shape);
        case wSquare:
        {
            // Two saws, `off` apart: a zero-mean pulse, width 50 % (shape 0) -> 5 % (shape 1).
            // The engine's pulse swings -2*off .. 2 - 2*off; drawn normalised to full height.
            const float off = 0.5f + 0.45f * shape;
            return (sawTable (ph) - sawTable (ph + off)) / (2.0f * off);
        }
        case wSuperSaw:
        {
            // Detune spread: +-15 ct (shape 0) -> +-1.8 st (shape 1), quadratic. The engine
            // starts each saw at a random phase; the preview uses fixed ones so it stands still.
            const float d = 0.08f + 0.92f * shape * shape;
            float sum = 0.0f;
            for (int k = 0; k < 7; ++k)
                sum += kSsWeight[k] * sawTable (phase * (1.0f + kSsOffsets[k] * d) + (float) k * 0.137f);
            return sum * kSsNorm * 0.85f;
        }
        default:        return hash ((int) (phase * 40.0f)) * 0.9f;     // white; WavePreview draws the coloured noise
    }
}

float lfoWaveValue (int wave, float phase)
{
    const float ph = frac (phase);
    switch (wave)
    {
        case lSine:     return std::sin (ph * kTwoPi);
        case lTriangle: return 1.0f - 4.0f * std::abs (frac (ph + 0.25f) - 0.5f);
        case lSaw:      return 1.0f - 2.0f * ph;
        case lSquare:   return ph < 0.5f ? 1.0f : -1.0f;
        default:        return hash (11 + (int) std::floor (phase * 4.0f)) * 0.9f;
    }
}

void drawOscWaveIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave)
{
    switch (wave)
    {
        case wSaw:      polyline (g, r, { { 0, -1 }, { 0.5f, 1 }, { 0.5f, -1 }, { 1, 1 } }); break;
        case wSquare:   polyline (g, r, { { 0, -1 }, { 0, 1 }, { 0.5f, 1 }, { 0.5f, -1 }, { 1, -1 }, { 1, 1 } }); break;
        case wTriangle: polyline (g, r, { { 0, -1 }, { 0.5f, 1 }, { 1, -1 } }); break;
        case wSine:     sineIcon (g, r); break;
        case wSuperSaw:
            polyline (g, r, { { 0, -1 }, { 0.36f, 0.5f }, { 0.36f, -1 }, { 0.72f, 0.5f }, { 0.72f, -1 }, { 1, 0.2f } }, 1.1f);
            polyline (g, r, { { 0.14f, -0.4f }, { 0.5f, 1 }, { 0.5f, -0.4f }, { 0.86f, 1 }, { 0.86f, -0.4f }, { 1, 0.2f } }, 1.1f);
            break;
        default:
            polyline (g, r, { { 0, 0.1f }, { 0.1f, -0.7f }, { 0.2f, 0.9f }, { 0.3f, -0.3f }, { 0.4f, 0.5f }, { 0.5f, -1 },
                              { 0.6f, 0.7f }, { 0.7f, -0.5f }, { 0.8f, 1 }, { 0.9f, -0.8f }, { 1, 0.2f } }, 1.0f);
            break;
    }
}

void drawLfoWaveIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave)
{
    switch (wave)
    {
        case lSine:     sineIcon (g, r); break;
        case lTriangle: polyline (g, r, { { 0, 0 }, { 0.25f, 1 }, { 0.75f, -1 }, { 1, 0 } }); break;
        case lSaw:      polyline (g, r, { { 0, 1 }, { 1, -1 }, { 1, 1 } }); break;
        case lSquare:   polyline (g, r, { { 0, 1 }, { 0.5f, 1 }, { 0.5f, -1 }, { 1, -1 } }); break;
        default:        polyline (g, r, { { 0, 0.3f }, { 0.25f, 0.3f }, { 0.25f, -0.8f }, { 0.5f, -0.8f }, { 0.5f, 1 },
                                          { 0.75f, 1 }, { 0.75f, -0.2f }, { 1, -0.2f } }); break;
    }
}

void drawSubWaveIcon (juce::Graphics& g, juce::Rectangle<float> r, int wave)
{
    static const int map[] = { lSine, lSquare, lTriangle };
    drawLfoWaveIcon (g, r, map[juce::jlimit (0, 2, wave)]);
}

// ===========================================================================
void ParamDisplay::watch (APVTS& s, std::initializer_list<P> ids)
{
    for (auto p : ids)
    {
        watched.push_back (s.getRawParameterValue (pid (p)));
        last.push_back (watched.back()->load());
    }
}

void ParamDisplay::tick()
{
    bool changed = false;
    for (size_t i = 0; i < watched.size(); ++i)
    {
        const float v = watched[i]->load (std::memory_order_relaxed);
        if (! juce::exactlyEqual (v, last[i])) { last[i] = v; changed = true; }
    }
    if (changed) repaint();
}

juce::Rectangle<float> ParamDisplay::paintFrame (juce::Graphics& g, bool chartGrid) const
{
    const auto& t = theme();
    const auto b = getLocalBounds().toFloat();
    const float rad = juce::jmin (t.radius, 4.0f);
    g.setColour (t.display.withMultipliedAlpha (dimmed ? 0.55f : 1.0f));
    g.fillRoundedRectangle (b, rad);
    if (chartGrid)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (getLocalBounds().reduced (1));
        g.setOpacity (fade());
        drawLogGrid (g, b, juce::jmax (40.0f, b.getWidth() / 2.0f), 4);
    }
    g.setColour ((t.light ? t.textDim : t.grid).withMultipliedAlpha (fade()));
    g.drawRoundedRectangle (b.reduced (0.5f), rad, 1.0f);
    return b.reduced (1.0f);
}

void ParamDisplay::paintCurve (juce::Graphics& g, const juce::Path& line, juce::Rectangle<float> area, float baselineY) const
{
    const auto& t = theme();
    const auto col = inkColour();

    juce::Path fill (line);
    const auto bounds = line.getBounds();
    fill.lineTo (bounds.getRight(), baselineY);
    fill.lineTo (bounds.getX(), baselineY);
    fill.closeSubPath();

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.getSmallestIntegerContainer());
    g.setColour (col.withAlpha ((t.light ? 0.62f : 0.20f) * fade()));
    g.fillPath (fill);

    const juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded);
    if (t.curveGlow && ! dimmed)
    {
        g.setColour (col.withAlpha (0.16f));
        g.strokePath (line, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour ((t.light ? t.curve : col).withMultipliedAlpha (fade()));
    g.strokePath (line, stroke);
}

// ===========================================================================
WavePreview::WavePreview (APVTS& s, int osc)
{
    watch (s, { oscParam (osc, P::osc1Wave), oscParam (osc, P::osc1Shape) });
    setInterceptsMouseClicks (false, false);
}

void WavePreview::paint (juce::Graphics& g)
{
    const auto area = paintFrame (g, true);
    const auto r = area.reduced (2.0f, 6.0f);
    const int wave = juce::roundToInt (val (0));
    const float shape = val (1);
    const float cycles = wave == wSuperSaw ? 3.0f : 2.0f;

    g.setColour (theme().textDim.withAlpha (0.5f * fade()));
    g.fillRect (area.getX(), r.getCentreY() - 0.5f, area.getWidth(), 1.0f);

    juce::Path p;
    const int n = juce::jmax (8, (int) (r.getWidth() * 2.0f));

    // Noise colour, as in the engine: white through a one-pole low-pass whose cutoff
    // falls from open (shape 0) to ~150 Hz (shape 1), renormalised to constant RMS.
    // One point = one sample at 48 kHz, so darker noise visibly wanders more slowly.
    const float nc = shape <= 0.0f ? 1.0f : 1.0f - std::exp (-kTwoPi * 20000.0f * std::exp2 (-7.0f * shape) / 48000.0f);
    const float nNorm = std::sqrt ((2.0f - nc) / nc) * 0.55f;
    float nLp = 0.0f;
    if (wave == wNoise)
        for (int i = -600; i < 0; ++i) nLp += nc * (hash (i) - nLp);        // settle the filter

    for (int i = 0; i <= n; ++i)
    {
        const float x = (float) i / (float) n;
        float y;
        if (wave == wNoise) { nLp += nc * (hash (i) - nLp); y = nLp * nNorm; }
        else                y = oscWaveValue (wave, x * cycles * 0.9999f, shape);
        y = juce::jlimit (-1.0f, 1.0f, y);
        const juce::Point<float> q (r.getX() + x * r.getWidth(), r.getCentreY() - y * r.getHeight() * 0.5f);
        if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
    }
    paintCurve (g, p, area, r.getCentreY());
}

LfoPreview::LfoPreview (APVTS& s, int lfo)
{
    watch (s, { lfoParam (lfo, P::lfo1Wave) });
    setInterceptsMouseClicks (false, false);
}

void LfoPreview::paint (juce::Graphics& g)
{
    const auto area = paintFrame (g, true);
    const auto r = area.reduced (2.0f, 5.0f);
    const int wave = juce::roundToInt (val (0));

    g.setColour (theme().textDim.withAlpha (0.5f * fade()));
    g.fillRect (area.getX(), r.getCentreY() - 0.5f, area.getWidth(), 1.0f);

    juce::Path p;
    const int n = juce::jmax (8, (int) (r.getWidth() * 2.0f));
    for (int i = 0; i <= n; ++i)
    {
        const float x = (float) i / (float) n;
        const float y = lfoWaveValue (wave, x * 1.9999f);
        const juce::Point<float> q (r.getX() + x * r.getWidth(), r.getCentreY() - y * r.getHeight() * 0.5f);
        if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
    }
    paintCurve (g, p, area, r.getCentreY());
}

// ===========================================================================
namespace
{
constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kTopDb = 28.0f, kBotDb = -44.0f;   // Q 40 peaks at about +23 dB
}

FilterDisplay::FilterDisplay (APVTS& s)
    : cutoffP (*s.getParameter (pid (P::fltCutoff))), resoP (*s.getParameter (pid (P::fltReso))),
      cutoff (cutoffP, [] (float) {}), reso (resoP, [] (float) {})
{
    watch (s, { P::fltType, P::fltCutoff, P::fltReso, P::fltEnv });
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

FilterDisplay::~FilterDisplay()
{
    if (dragging) { cutoff.endGesture(); reso.endGesture(); }
}

float FilterDisplay::xForHz (float hz) const
{
    const auto r = plot();
    return r.getX() + r.getWidth() * std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
}

// Small-signal magnitude of the engine's filter (dsp/Voice.cpp: runFilter + the
// coefficient mapping in Voice::control): a state-variable filter with damping
// k = 1/Q, where resonance 0..1 maps exponentially from Q 0.71 (1.31 for the first
// stage of the 24 dB types) to Q 40, the input is attenuated by kRel^(1/4) (up to
// -9 dB) as resonance rises, and the 24 dB types add a fixed Q 0.54 second stage.
float FilterDisplay::responseDb (float hz) const
{
    const int type = juce::roundToInt (val (0));
    const float w = hz / juce::jmax (1.0f, val (1));
    const float w2 = w * w;
    const bool twoStage = type == fLP24 || type == fHP24;
    const float kMax = twoStage ? 0.7654f : 1.4142f, kMin = 0.025f, k2 = 1.0f / 0.5412f;
    const float kRel = std::pow (kMin / kMax, juce::jlimit (0.0f, 1.0f, val (2)));
    const float k = kMax * kRel;
    const auto den = [w, w2] (float kk) { return std::sqrt ((1.0f - w2) * (1.0f - w2) + (kk * w) * (kk * w)); };

    float mag = 1.0f;
    switch (type)
    {
        case fLP24:  mag = 1.0f / (den (k) * den (k2)); break;
        case fLP12:  mag = 1.0f / den (k); break;
        case fBP12:  mag = 1.19f * std::sqrt (k) * w / den (k); break;
        case fHP12:  mag = w2 / den (k); break;
        case fHP24:  mag = w2 * w2 / (den (k) * den (k2)); break;
        default:     mag = std::abs (1.0f - w2) / den (k); break;      // notch
    }
    return juce::Decibels::gainToDecibels (mag * std::sqrt (std::sqrt (kRel)), -120.0f);
}

void FilterDisplay::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto r = paintFrame (g, false);

    // frequency grid: decades strong, the rest faint — real chart paper
    for (float dec = 10.0f; dec < kMaxHz; dec *= 10.0f)
        for (int k = 1; k <= 9; ++k)
        {
            const float hz = dec * (float) k;
            if (hz <= kMinHz || hz >= kMaxHz) continue;
            g.setColour (k == 1 ? t.gridInk.withMultipliedAlpha (2.2f) : t.gridInk);
            g.fillRect (std::floor (xForHz (hz)), r.getY(), 1.0f, r.getHeight());
        }
    const auto yForDb = [r] (float db) { return juce::jmap (db, kTopDb, kBotDb, r.getY(), r.getBottom()); };
    g.setColour (t.gridInk);
    for (float db = -36.0f; db < kTopDb; db += 12.0f)
        g.fillRect (r.getX(), std::floor (yForDb (db)), r.getWidth(), 1.0f);
    g.setColour (t.textDim.withAlpha (0.55f));
    g.fillRect (r.getX(), std::floor (yForDb (0.0f)), r.getWidth(), 1.0f);

    juce::Path p;
    const int n = juce::jmax (8, (int) r.getWidth());
    for (int i = 0; i <= n; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) n;
        const float hz = kMinHz * std::pow (kMaxHz / kMinHz, (float) i / (float) n);
        const float y = juce::jlimit (r.getY() - 4.0f, r.getBottom() + 4.0f, yForDb (responseDb (hz)));
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    paintCurve (g, p, r, r.getBottom() + 4.0f);

    // envelope sweep: where the filter env will push the cutoff (±10 oct at full amount)
    const float fc = val (1), env = val (3);
    const float cx = xForHz (fc);
    if (std::abs (env) > 0.005f)
    {
        const float ex = xForHz (fc * std::pow (2.0f, env * 10.0f));
        const float y = r.getBottom() - 4.0f;
        g.setColour (t.light ? t.accent2 : t.accent2.withAlpha (0.9f));
        g.fillRect (juce::jmin (cx, ex), y - 1.0f, std::abs (ex - cx), 2.0f);
        g.fillRect (ex - 1.0f, y - 4.0f, 2.0f, 8.0f);
    }

    // cutoff marker
    const float cy = juce::jlimit (r.getY() + 4.0f, r.getBottom() - 4.0f, yForDb (responseDb (fc)));
    g.setColour (t.textDim.withAlpha (0.6f));
    g.fillRect (cx - 0.5f, r.getY(), 1.0f, r.getHeight());
    g.setColour (t.display);
    g.fillEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    g.setColour (t.light ? t.curve : inkColour());
    g.drawEllipse (cx - 4.0f, cy - 4.0f, 8.0f, 8.0f, dragging || isMouseOver() ? 2.2f : 1.5f);
}

void FilterDisplay::mouseDown (const juce::MouseEvent& e)
{
    cutoff.beginGesture(); reso.beginGesture();
    dragging = true;
    startReso = val (2);
    mouseDrag (e);
}

void FilterDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const auto r = plot();
    const float tx = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
    cutoff.setValueAsPartOfGesture (kMinHz * std::pow (kMaxHz / kMinHz, tx));
    reso.setValueAsPartOfGesture (juce::jlimit (0.0f, 1.0f, startReso - (float) e.getDistanceFromDragStartY() / r.getHeight()));
}

void FilterDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging) { cutoff.endGesture(); reso.endGesture(); dragging = false; }
    repaint();
}

void FilterDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    cutoff.setValueAsCompleteGesture (cutoffP.convertFrom0to1 (cutoffP.getDefaultValue()));
    reso.setValueAsCompleteGesture (resoP.convertFrom0to1 (resoP.getDefaultValue()));
}

// ===========================================================================
EnvDisplay::EnvDisplay (APVTS& s, P a, P d, P su, P r) : ids { a, d, su, r }
{
    watch (s, { a, d, su, r });
    for (int i = 0; i < 4; ++i)
    {
        params[i] = s.getParameter (pid (ids[i]));
        att[i] = std::make_unique<juce::ParameterAttachment> (*params[i], [] (float) {});
    }
}

EnvDisplay::~EnvDisplay() { endDrag(); }

EnvDisplay::Geometry EnvDisplay::geometry() const
{
    Geometry gm;
    gm.area  = getLocalBounds().toFloat().reduced (7.0f, 7.0f);
    gm.segW  = gm.area.getWidth() * 0.27f;
    gm.holdW = gm.area.getWidth() - 3.0f * gm.segW;
    const auto n = [this] (int i) { return params[i]->convertTo0to1 (val (i)); };
    const float top = gm.area.getY(), bot = gm.area.getBottom(), h = gm.area.getHeight();
    gm.p[0] = { gm.area.getX(), bot };
    gm.p[1] = { gm.p[0].x + juce::jmax (1.5f, n (0) * gm.segW), top };
    gm.p[2] = { gm.p[1].x + juce::jmax (1.5f, n (1) * gm.segW), top + (1.0f - val (2)) * h };
    gm.p[3] = { gm.p[2].x + gm.holdW, gm.p[2].y };
    gm.p[4] = { gm.p[3].x + juce::jmax (1.5f, n (3) * gm.segW), bot };
    return gm;
}

juce::Point<float> EnvDisplay::handlePosition (int handle) const
{
    const int idx[] = { 1, 2, 4 };
    return geometry().p[idx[juce::jlimit (0, 2, handle)]];
}

int EnvDisplay::handleAt (juce::Point<float> pos) const
{
    const auto gm = geometry();
    const int idx[] = { 1, 2, 4 };
    int best = -1; float bestD = 26.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = pos.getDistanceFrom (gm.p[idx[i]]);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void EnvDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1 && drag < 0) { hover = -1; repaint(); }
}

void EnvDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = handleAt (e.position);
    if (drag < 0) return;
    hover = drag;
    att[drag == 0 ? 0 : drag == 1 ? 1 : 3]->beginGesture();
    if (drag == 1) att[2]->beginGesture();
    repaint();
}

void EnvDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    const auto gm = geometry();
    const auto setN = [this] (int i, float n)
    {
        att[i]->setValueAsPartOfGesture (params[i]->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
    };
    if (drag == 0)      setN (0, (e.position.x - gm.p[0].x) / gm.segW);
    else if (drag == 2) setN (3, (e.position.x - gm.p[3].x) / gm.segW);
    else
    {
        setN (1, (e.position.x - gm.p[1].x) / gm.segW);
        setN (2, 1.0f - (e.position.y - gm.area.getY()) / gm.area.getHeight());
    }
}

void EnvDisplay::mouseUp (const juce::MouseEvent&) { endDrag(); }

void EnvDisplay::endDrag()
{
    if (drag < 0) return;
    att[drag == 0 ? 0 : drag == 1 ? 1 : 3]->endGesture();
    if (drag == 1) att[2]->endGesture();
    drag = -1;
    repaint();
}

void EnvDisplay::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto frame = paintFrame (g, true);
    const auto gm = geometry();

    // The engine's segment shapes (dsp/Voice.h, Env): the attack rises exponentially
    // towards 1.3 and is cut off at full level; decay and release fall exponentially,
    // their times measured to -60 dB (ln 1000 = 6.9078 time constants).
    const float bot = gm.area.getBottom(), h = gm.area.getHeight(), sus = juce::jlimit (0.0f, 1.0f, val (2));
    const float atkLog = std::log (1.3f / 0.3f);
    const int steps = 24;
    const auto segment = [&] (juce::Path& path, juce::Point<float> from, juce::Point<float> to, const std::function<float (float)>& level)
    {
        for (int i = 1; i <= steps; ++i)
        {
            const float x = (float) i / (float) steps;
            path.lineTo (from.x + (to.x - from.x) * x, bot - level (x) * h);
        }
    };

    juce::Path p;
    p.startNewSubPath (gm.p[0]);
    segment (p, gm.p[0], gm.p[1], [atkLog] (float x) { return juce::jmin (1.0f, 1.3f * (1.0f - std::exp (-atkLog * x))); });
    segment (p, gm.p[1], gm.p[2], [sus] (float x) { return sus + (1.0f - sus) * std::exp (-6.9078f * x); });
    p.lineTo (gm.p[3]);
    segment (p, gm.p[3], gm.p[4], [sus] (float x) { return sus * std::exp (-6.9078f * x); });
    paintCurve (g, p, frame, gm.area.getBottom());

    // sustain hold is "as long as the key": dashed to say it isn't a time
    g.setColour (t.display.withAlpha (0.75f));
    for (float x = gm.p[2].x + 4.0f; x < gm.p[3].x - 3.0f; x += 7.0f)
        g.fillRect (x, gm.p[2].y - 1.5f, 3.0f, 3.0f);

    const int idx[] = { 1, 2, 4 };
    for (int i = 0; i < 3; ++i)
    {
        const auto c = gm.p[idx[i]];
        const bool hot = (i == hover || i == drag) && ! dimmed;
        const float s = hot ? 4.5f : 3.2f;
        const auto box = juce::Rectangle<float> (c.x - s, c.y - s, s * 2.0f, s * 2.0f);
        g.setColour (hot ? inkColour() : t.display);
        if (t.light) g.fillRect (box); else g.fillEllipse (box);
        g.setColour ((t.light ? t.curve : inkColour()).withMultipliedAlpha (fade()));
        if (t.light) g.drawRect (box, 1.3f); else g.drawEllipse (box, 1.4f);
    }

    const int active = drag >= 0 ? drag : hover;
    if (active >= 0)
    {
        const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        const auto txt = active == 0 ? "A " + formatParam (ids[0], val (0))
                       : active == 1 ? "D " + formatParam (ids[1], val (1)) + dot + "S " + formatParam (ids[2], val (2))
                                     : "R " + formatParam (ids[3], val (3));
        g.setFont (themeFont (9.5f, true));
        auto box = frame.reduced (4.0f, 3.0f).removeFromTop (12.0f);
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 8.0f;
        const auto pill = box.removeFromRight (w);
        g.setColour (t.display.withAlpha (0.85f));
        g.fillRect (pill);
        g.setColour (t.text);
        g.drawText (txt, pill, juce::Justification::centred, false);
    }
}

// ===========================================================================
ScopeDisplay::ScopeDisplay (WhyshoSynthAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void ScopeDisplay::tick()
{
    proc.readScope (buf.data(), (int) buf.size());
    float peak = 0.0f;
    for (auto s : buf) peak = juce::jmax (peak, std::abs (s));
    const bool nowSilent = peak < 1.0e-4f;
    if (nowSilent && silent) return;        // nothing moved: keep the last frame
    silent = nowSilent;

    start = 0;
    for (int i = 1; i < (int) buf.size() / 2; ++i)
        if (buf[(size_t) i - 1] < 0.0f && buf[(size_t) i] >= 0.0f) { start = i; break; }
    repaint();
}

void ScopeDisplay::paint (juce::Graphics& g)
{
    const auto area = paintFrame (g, true);
    const auto r = area.reduced (0.0f, 4.0f);
    const float mid = r.getCentreY();

    g.setColour (theme().textDim.withAlpha (0.5f));
    g.fillRect (area.getX(), mid - 0.5f, area.getWidth(), 1.0f);
    if (silent) return;

    float peak = 0.0f;
    for (auto s : buf) peak = juce::jmax (peak, std::abs (s));
    const float gain = 0.92f / juce::jmax (0.3f, peak);       // quiet patches still read, loud ones never clip the box

    juce::Path p;
    const int n = juce::jmax (8, (int) r.getWidth());
    const int span = (int) buf.size() / 2 - 1;
    for (int i = 0; i <= n; ++i)
    {
        const float s = buf[(size_t) (start + i * span / n)] * gain;
        const float x = r.getX() + r.getWidth() * (float) i / (float) n;
        const float y = mid - juce::jlimit (-1.0f, 1.0f, s) * r.getHeight() * 0.5f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    paintCurve (g, p, area, mid);
}

// ===========================================================================
Meter::Meter (WhyshoSynthAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (false, false);
}

void Meter::tick()
{
    bool dirty = false;
    for (int c = 0; c < 2; ++c)
    {
        const float pk = proc.outputPeak (c);
        const float n = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (pk, -60.0f) + 48.0f) / 48.0f);
        if (std::abs (n - level[c]) > 0.004f) { level[c] = n; dirty = true; }
        if (pk > 1.0f) clipHold[c] = 30;
        const bool nowClip = clipHold[c] > 0;
        if (clipHold[c] > 0) --clipHold[c];
        if (nowClip != clip[c]) { clip[c] = nowClip; dirty = true; }
    }
    if (dirty) repaint();
}

void Meter::paint (juce::Graphics& g)
{
    const auto& t = theme();
    auto b = getLocalBounds().toFloat();
    const float gap = 2.0f, h = (b.getHeight() - gap) * 0.5f;
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (b.getX(), b.getY() + (float) c * (h + gap), b.getWidth(), h);
        g.setColour (t.light ? t.grid.withAlpha (0.7f) : t.panel2);
        g.fillRect (bar);
        const float hot = bar.getWidth() * (42.0f / 48.0f);       // -6 dB
        const float w = bar.getWidth() * level[c];
        g.setColour (clip[c] ? t.bad : (t.light ? t.text : t.accent));
        g.fillRect (bar.withWidth (juce::jmin (w, hot)));
        if (w > hot)
        {
            g.setColour (clip[c] ? t.bad : t.warn);
            g.fillRect (bar.withLeft (bar.getX() + hot).withRight (bar.getX() + w));
        }
    }
}

} // namespace wsho::gui
