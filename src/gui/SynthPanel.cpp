#include "SynthPanel.h"
#include "../PluginProcessor.h"

namespace wsho::gui
{
namespace
{
constexpr int kKnobH = 54;      // dial + label cell height at 1x

using Comps = std::initializer_list<juce::Component*>;

// Spread components evenly across a row.
void spread (juce::Rectangle<int> r, Comps items)
{
    const float w = (float) r.getWidth() / (float) juce::jmax (1, (int) items.size());
    int i = 0;
    for (auto* c : items)
    {
        const int x0 = r.getX() + juce::roundToInt (w * (float) i), x1 = r.getX() + juce::roundToInt (w * (float) (i + 1));
        if (c != nullptr) c->setBounds (x0, r.getY(), x1 - x0, r.getHeight());
        ++i;
    }
}

// A row of knobs: evenly spread, never taller than a knob wants to be.
void knobRow (juce::Rectangle<int> r, Comps items, int maxH = kKnobH + 8)
{
    spread (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), maxH)), items);
}

juce::String shapeLabel (int wave)
{
    switch (wave)
    {
        case wSquare:   return "Width";
        case wSuperSaw: return "Detune";
        case wNoise:    return "Shape";
        default:        return "Fold";
    }
}
} // namespace

// ===========================================================================
void Caption::paint (juce::Graphics& g)
{
    g.setFont (themeFont (9.5f));
    g.setColour (theme().textDim.withMultipliedAlpha (dimmed ? 0.5f : 1.0f));
    g.drawText (caps (text), getLocalBounds(), just, false);
}

// ===========================================================================
Section::Section (const juce::String& t) : title (t) {}

void Section::setDimmed (bool d)
{
    if (dimmed == d) return;
    dimmed = d;
    for (auto* c : owned)
        if (c != power)
            if (auto* dm = dynamic_cast<Dimmable*> (c))
                dm->setDimmed (d);
    repaint();
}

void Section::tickDisplays()
{
    for (auto* c : owned)
        if (auto* d = dynamic_cast<ParamDisplay*> (c))
            d->tick();
}

void Section::paint (juce::Graphics& g)
{
    const auto& t = theme();
    const auto b = getLocalBounds().toFloat();

    g.setColour (t.panel);
    g.fillRoundedRectangle (b, t.radius);
    if (t.light)
    {
        // the site's sheet rule: one firm ink line opening each block
        g.setColour (t.rule);
        g.fillRect (b.withHeight (1.5f));
    }
    else
    {
        g.setColour (t.grid.withAlpha (0.55f));
        g.drawRoundedRectangle (b.reduced (0.5f), t.radius, 1.0f);
    }

    g.setFont (kickerFont (10.0f));
    g.setColour (dimmed ? t.textDim : t.text);
    g.drawText (caps (title), power != nullptr ? 24 : 8, 2, getWidth() - 30, headerH - 2,
                juce::Justification::centredLeft, false);
}

void Section::resized()
{
    if (power != nullptr) power->setBounds (6, 4, 15, 15);
    if (onLayout) onLayout (getLocalBounds().withTrimmedTop (headerH + 2).reduced (7, 0).withTrimmedBottom (5));
}

// ===========================================================================
SynthPanel::SynthPanel (WhyshoSynthAudioProcessor& p) : proc (p), state (p.state())
{
    for (int i = 0; i < kNumParams; ++i)
        rawValues[(size_t) i] = state.getRawParameterValue (pid (i));

    const auto row = [this] (int r, int weight) { rows[r].push_back (sections.getLast()); rowWidths[r].push_back (weight); };

    for (int i = 0; i < kNumOscs; ++i) { buildOsc (i); row (0, 205); }
    buildMix();                              row (0, 128);
    buildFilter();                           row (0, 325);

    buildEnv ("Amp Env", P::ampA, 0);        row (1, 230);
    buildEnv ("Filter Env", P::fltA, 1);     row (1, 230);
    buildPitch();                            row (1, 96);
    for (int i = 0; i < kNumLfos; ++i) { buildLfo (i); row (1, 168); }
    buildVoice();                            row (1, 176);

    buildMod();                              row (2, 292);
    buildFx();                               // adds four sections
    for (int i = 0; i < 4; ++i)
    {
        rows[2].push_back (sections[sections.size() - 4 + i]);
        rowWidths[2].push_back (i == 0 ? 118 : i == 2 ? 196 : 140);
    }
    buildOut();                              row (2, 182);

    tick();
}

Section& SynthPanel::section (const juce::String& title)
{
    auto* s = sections.add (new Section (title));
    addAndMakeVisible (s);
    return *s;
}

void SynthPanel::buildOsc (int i)
{
    auto& s = section ("Osc " + juce::String (i + 1));
    const auto id = [i] (P base) { return oscParam (i, base); };

    auto& on = s.add<ToggleChip> (state, id (P::osc1On), "On", ToggleChip::Style::power);
    on.setInk (i);
    s.setPower (on);

    auto* wave = &s.add<ChoiceStrip> (state, id (P::osc1Wave), juce::StringArray(), drawOscWaveIcon);
    auto* view = &s.add<WavePreview> (state, i);
    wave->setInk (i);
    view->setInk (i);

    const std::pair<P, const char*> defs[] = { { P::osc1Oct, "Oct" }, { P::osc1Semi, "Semi" }, { P::osc1Fine, "Fine" },
                                               { P::osc1Level, "Level" }, { P::osc1Pan, "Pan" }, { P::osc1Shape, "Fold" } };
    std::array<Knob*, 6> k {};
    for (size_t n = 0; n < 6; ++n)
    {
        k[n] = &s.add<Knob> (state, id (defs[n].first), defs[n].second);
        k[n]->setInk (i);
    }
    osc[i] = { &s, k[5] };

    s.onLayout = [wave, view, k] (juce::Rectangle<int> b)
    {
        wave->setBounds (b.removeFromTop (20));
        b.removeFromTop (4);
        const int kh = juce::jmin (kKnobH + 6, (b.getHeight() - 30) / 2);
        auto r2 = b.removeFromBottom (kh), r1 = b.removeFromBottom (kh);
        knobRow (r1, { k[0], k[1], k[2] });
        knobRow (r2, { k[3], k[4], k[5] });
        view->setBounds (b.withTrimmedBottom (4));
    };
}

void SynthPanel::buildMix()
{
    auto& s = section ("Mix");
    auto* sub   = &s.add<Knob> (state, P::subLevel, "Sub");
    auto* wave  = &s.add<ChoiceStrip> (state, P::subWave, juce::StringArray(), drawSubWaveIcon);
    auto* oct   = &s.add<ChoiceStrip> (state, P::subOct, juce::StringArray { "-1", "-2" });
    auto* noise = &s.add<Knob> (state, P::noiseLevel, "Noise");
    auto* fm    = &s.add<Knob> (state, P::oscFm, "FM 2>1");
    fm->setInk (1);

    s.onLayout = [sub, wave, oct, noise, fm] (juce::Rectangle<int> b)
    {
        const int kh = juce::jmin (kKnobH + 6, b.getHeight() / 2 - 4);
        const int pad = (b.getHeight() - 2 * kh) / 3;
        auto top = b.withY (b.getY() + pad).withHeight (kh);
        auto bot = b.withY (top.getBottom() + pad).withHeight (kh);
        sub->setBounds (top.removeFromLeft (top.getWidth() / 2));
        auto side = top.withSizeKeepingCentre (top.getWidth() - 2, 40);
        wave->setBounds (side.removeFromTop (19));
        oct->setBounds (side.removeFromBottom (19));
        knobRow (bot, { noise, fm });
    };
}

void SynthPanel::buildFilter()
{
    auto& s = section ("Filter");
    auto* type = &s.add<ChoiceStrip> (state, P::fltType, juce::StringArray { "LP 24", "LP 12", "BP", "HP 12", "HP 24", "Notch" });
    auto* view = &s.add<FilterDisplay> (state);
    std::array<Knob*, 5> k { &s.add<Knob> (state, P::fltCutoff, "Cutoff"), &s.add<Knob> (state, P::fltReso, "Reso"),
                             &s.add<Knob> (state, P::fltDrive, "Drive"),   &s.add<Knob> (state, P::fltEnv, "Env"),
                             &s.add<Knob> (state, P::fltKey, "Key") };
    k[3]->setInk (1);       // matches the env-sweep marker family (second ink)
    view->setTooltip ("Drag: cutoff / resonance");

    s.onLayout = [type, view, k] (juce::Rectangle<int> b)
    {
        type->setBounds (b.removeFromTop (20));
        b.removeFromTop (4);
        knobRow (b.removeFromBottom (juce::jmin (kKnobH + 6, b.getHeight() / 2)), { k[0], k[1], k[2], k[3], k[4] });
        view->setBounds (b.withTrimmedBottom (4));
    };
}

void SynthPanel::buildEnv (const juce::String& title, P a, int ink)
{
    auto& s = section (title);
    const auto id = [a] (int n) { return (P) ((int) a + n); };
    auto* view = &s.add<EnvDisplay> (state, id (0), id (1), id (2), id (3));
    view->setInk (ink);
    const char* names[] = { "Attack", "Decay", "Sustain", "Release" };
    std::array<Knob*, 4> k {};
    for (int n = 0; n < 4; ++n)
    {
        k[(size_t) n] = &s.add<Knob> (state, id (n), names[n]);
        k[(size_t) n]->setInk (ink);
    }

    s.onLayout = [view, k] (juce::Rectangle<int> b)
    {
        knobRow (b.removeFromBottom (juce::jmin (kKnobH + 6, b.getHeight() / 2)), { k[0], k[1], k[2], k[3] });
        view->setBounds (b.withTrimmedBottom (4));
    };
}

void SynthPanel::buildPitch()
{
    auto& s = section ("Pitch Env");
    auto* amt = &s.add<Knob> (state, P::pitchAmt, "Amount");
    auto* dec = &s.add<Knob> (state, P::pitchDec, "Decay");
    s.onLayout = [amt, dec] (juce::Rectangle<int> b)
    {
        const int kh = juce::jmin (kKnobH + 8, b.getHeight() / 2 - 4);
        const int pad = (b.getHeight() - 2 * kh) / 3;
        amt->setBounds (b.withY (b.getY() + pad).withHeight (kh));
        dec->setBounds (b.withY (b.getY() + 2 * pad + kh).withHeight (kh));
    };
}

void SynthPanel::buildLfo (int i)
{
    auto& s = section ("LFO " + juce::String (i + 1));
    const auto id = [i] (P base) { return lfoParam (i, base); };
    auto* wave  = &s.add<ChoiceStrip> (state, id (P::lfo1Wave), juce::StringArray(), drawLfoWaveIcon);
    auto* view  = &s.add<LfoPreview> (state, i);
    auto* rate  = &s.add<Knob> (state, id (P::lfo1Rate), "Rate");
    auto* div   = &s.add<ParamCombo> (state, id (P::lfo1Div));
    auto* cap   = &s.add<Caption> ("Rate");
    auto* sync  = &s.add<ToggleChip> (state, id (P::lfo1Sync), "Sync", ToggleChip::Style::text);
    auto* retr  = &s.add<ToggleChip> (state, id (P::lfo1Retrig), "Retrig", ToggleChip::Style::text);
    wave->setInk (3 + i); view->setInk (3 + i); rate->setInk (3 + i); sync->setInk (3 + i); retr->setInk (3 + i);
    sync->setTooltip ("Sync the LFO rate to the host tempo");
    retr->setTooltip ("Restart the LFO on every note");
    syncs.push_back ({ rate, div, cap, id (P::lfo1Sync) });

    s.onLayout = [wave, view, rate, div, cap, sync, retr] (juce::Rectangle<int> b)
    {
        wave->setBounds (b.removeFromTop (20));
        b.removeFromTop (4);
        auto bottom = b.removeFromBottom (juce::jmin (kKnobH + 6, b.getHeight() / 2));
        view->setBounds (b.withTrimmedBottom (4));

        auto chips = bottom.removeFromRight (bottom.getWidth() / 2).withSizeKeepingCentre (bottom.getWidth() - 4, 42);
        sync->setBounds (chips.removeFromTop (19));
        retr->setBounds (chips.removeFromBottom (19));
        rate->setBounds (bottom);
        auto cell = bottom.withSizeKeepingCentre (bottom.getWidth() - 6, 38);
        div->setBounds (cell.removeFromTop (22));
        cap->setBounds (cell.removeFromBottom (12));
    };
}

void SynthPanel::buildVoice()
{
    auto& s = section ("Voice");
    auto* mode = &s.add<ChoiceStrip> (state, P::voiceMode);
    glide     = &s.add<Knob> (state, P::glide, "Glide");
    auto* uni = &s.add<Knob> (state, P::unison, "Unison");
    uniDetune = &s.add<Knob> (state, P::uniDetune, "Detune");
    uniSpread = &s.add<Knob> (state, P::uniSpread, "Spread");
    auto* bend = &s.add<Knob> (state, P::bendRange, "Bend");
    auto* vel  = &s.add<Knob> (state, P::velSens, "Velocity");

    s.onLayout = [mode, uni, bend, vel, g = glide, d = uniDetune, sp = uniSpread] (juce::Rectangle<int> b)
    {
        mode->setBounds (b.removeFromTop (20));
        b.removeFromTop (2);
        auto r1 = b.removeFromTop (b.getHeight() / 2);
        knobRow (r1, { g, bend, vel });
        knobRow (b, { uni, d, sp });
    };
}

void SynthPanel::buildMod()
{
    auto& s = section ("Mod Matrix");
    struct Row { Caption* n; ParamCombo* src; ParamCombo* dst; Knob* amt; };
    std::array<Row, kNumModSlots> r {};
    for (int i = 0; i < kNumModSlots; ++i)
    {
        auto& row = r[(size_t) i];
        row.n   = &s.add<Caption> (juce::String (i + 1));
        row.src = &s.add<ParamCombo> (state, modParam (i, P::mod1Src));
        row.dst = &s.add<ParamCombo> (state, modParam (i, P::mod1Dst));
        row.amt = &s.add<Knob> (state, modParam (i, P::mod1Amt), "Amount", Knob::Style::bar);
        row.amt->setInk (1);
        modAmt[i] = row.amt;
    }
    auto* h1 = &s.add<Caption> ("Source", juce::Justification::centredLeft);
    auto* h2 = &s.add<Caption> ("Target", juce::Justification::centredLeft);
    auto* h3 = &s.add<Caption> ("Amount", juce::Justification::centredLeft);

    s.onLayout = [r, h1, h2, h3] (juce::Rectangle<int> b)
    {
        auto head = b.removeFromTop (12);
        const int numW = 14, gap = 5;
        const int w = b.getWidth() - numW;
        const int srcW = w * 30 / 100, dstW = w * 34 / 100;
        head.removeFromLeft (numW);
        h1->setBounds (head.removeFromLeft (srcW));
        h2->setBounds (head.removeFromLeft (dstW));
        h3->setBounds (head);

        const int rh = juce::jmin (32, b.getHeight() / kNumModSlots);
        const int pad = (b.getHeight() - rh * kNumModSlots) / (kNumModSlots + 1);
        for (int i = 0; i < kNumModSlots; ++i)
        {
            auto line = juce::Rectangle<int> (b.getX(), b.getY() + pad + i * (rh + pad), b.getWidth(), rh)
                            .withSizeKeepingCentre (b.getWidth(), juce::jmin (rh - 4, 22));
            const auto& row = r[(size_t) i];
            row.n->setBounds (line.removeFromLeft (numW).withTrimmedRight (4));
            row.src->setBounds (line.removeFromLeft (srcW).withTrimmedRight (gap));
            row.dst->setBounds (line.removeFromLeft (dstW).withTrimmedRight (gap));
            row.amt->setBounds (line);
        }
    };
}

void SynthPanel::buildFx()
{
    {   // DRIVE
        auto& s = section ("Drive");
        s.setPower (s.add<ToggleChip> (state, P::drvOn, "On", ToggleChip::Style::power));
        auto* type = &s.add<ParamCombo> (state, P::drvType);
        auto* amt  = &s.add<Knob> (state, P::drvAmt, "Amount");
        auto* mix  = &s.add<Knob> (state, P::drvMix, "Mix");
        fx.push_back ({ &s, P::drvOn });
        s.onLayout = [type, amt, mix] (juce::Rectangle<int> b)
        {
            b.removeFromTop (2);
            type->setBounds (b.removeFromTop (22));
            knobRow (b, { amt, mix });
        };
    }
    {   // CHORUS
        auto& s = section ("Chorus");
        s.setPower (s.add<ToggleChip> (state, P::choOn, "On", ToggleChip::Style::power));
        auto* a = &s.add<Knob> (state, P::choRate, "Rate");
        auto* b2 = &s.add<Knob> (state, P::choDepth, "Depth");
        auto* c = &s.add<Knob> (state, P::choMix, "Mix");
        fx.push_back ({ &s, P::choOn });
        s.onLayout = [a, b2, c] (juce::Rectangle<int> b) { knobRow (b, { a, b2, c }); };
    }
    {   // DELAY
        auto& s = section ("Delay");
        s.setPower (s.add<ToggleChip> (state, P::dlyOn, "On", ToggleChip::Style::power));
        auto* sync = &s.add<ToggleChip> (state, P::dlySync, "Sync", ToggleChip::Style::text);
        auto* ping = &s.add<ToggleChip> (state, P::dlyPing, "Ping-pong", ToggleChip::Style::text);
        auto* time = &s.add<Knob> (state, P::dlyTime, "Time");
        auto* div  = &s.add<ParamCombo> (state, P::dlyDiv);
        auto* cap  = &s.add<Caption> ("Time");
        auto* fb   = &s.add<Knob> (state, P::dlyFb, "Feedback");
        auto* mix  = &s.add<Knob> (state, P::dlyMix, "Mix");
        sync->setTooltip ("Sync the delay time to the host tempo");
        fx.push_back ({ &s, P::dlyOn });
        syncs.push_back ({ time, div, cap, P::dlySync });
        s.onLayout = [sync, ping, time, div, cap, fb, mix] (juce::Rectangle<int> b)
        {
            b.removeFromTop (2);
            auto chips = b.removeFromTop (19);
            sync->setBounds (chips.removeFromLeft (chips.getWidth() * 38 / 100).withTrimmedRight (4));
            ping->setBounds (chips);
            auto r = b.withSizeKeepingCentre (b.getWidth(), juce::jmin (b.getHeight(), kKnobH + 8));
            auto first = r.removeFromLeft (r.getWidth() / 3);
            time->setBounds (first);
            auto cell = first.withSizeKeepingCentre (first.getWidth() - 2, 38);
            div->setBounds (cell.removeFromTop (22));
            cap->setBounds (cell.removeFromBottom (12));
            spread (r, { fb, mix });
        };
    }
    {   // REVERB
        auto& s = section ("Reverb");
        s.setPower (s.add<ToggleChip> (state, P::revOn, "On", ToggleChip::Style::power));
        auto* a = &s.add<Knob> (state, P::revSize, "Size");
        auto* b2 = &s.add<Knob> (state, P::revDamp, "Damp");
        auto* c = &s.add<Knob> (state, P::revMix, "Mix");
        fx.push_back ({ &s, P::revOn });
        s.onLayout = [a, b2, c] (juce::Rectangle<int> b) { knobRow (b, { a, b2, c }); };
    }
}

void SynthPanel::buildOut()
{
    auto& s = section ("Out");
    auto* scope = &s.add<ScopeDisplay> (proc);
    auto* gain  = &s.add<Knob> (state, P::masterGain, "Master");
    meter       = &s.add<Meter> (proc);
    auto* cap   = &s.add<Caption> ("Level L / R", juce::Justification::centredLeft);

    s.onLayout = [scope, gain, cap, m = meter] (juce::Rectangle<int> b)
    {
        auto bottom = b.removeFromBottom (juce::jmin (kKnobH + 6, b.getHeight() / 2));
        scope->setBounds (b.withTrimmedBottom (4));
        gain->setBounds (bottom.removeFromLeft (62));
        bottom.removeFromLeft (6);
        auto col = bottom.withSizeKeepingCentre (bottom.getWidth(), 30);
        cap->setBounds (col.removeFromTop (12));
        m->setBounds (col.removeFromBottom (13));
    };
}

// ===========================================================================
void SynthPanel::tick()
{
    for (auto* s : sections) s->tickDisplays();
    if (meter != nullptr) meter->tick();

    for (int i = 0; i < kNumOscs; ++i)
    {
        osc[i].sec->setDimmed (raw (oscParam (i, P::osc1On)) < 0.5f);
        osc[i].shape->setLabel (shapeLabel (juce::roundToInt (raw (oscParam (i, P::osc1Wave)))));
    }
    for (auto& f : fx)
        f.sec->setDimmed (raw (f.on) < 0.5f);

    // tempo-synced controls show the division picker, free ones the Hz / time knob
    for (auto& s : syncs)
    {
        const bool synced = raw (s.sync) > 0.5f;
        s.free->setVisible (! synced);
        s.synced->setVisible (synced);
        s.syncedCaption->setVisible (synced);
    }

    for (int i = 0; i < kNumModSlots; ++i)
        modAmt[i]->setDimmed (raw (modParam (i, P::mod1Src)) < 0.5f || raw (modParam (i, P::mod1Dst)) < 0.5f);

    const bool solo = raw (P::unison) < 1.5f;
    uniDetune->setDimmed (solo);
    uniSpread->setDimmed (solo);
}

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme().bg);
    if (theme().light)      // chart paper shows through the gutters between the sheets
        drawLogGrid (g, getLocalBounds().toFloat(), 360.0f, juce::jmax (1, getHeight() / 36));
}

void SynthPanel::resized()
{
    const int margin = 7, gap = 6;
    auto area = getLocalBounds().reduced (margin, 0).withTrimmedTop (margin).withTrimmedBottom (margin);
    const int rowWeights[] = { 196, 186, 172 };
    const int totalH = area.getHeight() - 2 * gap;
    int y = area.getY();

    for (int r = 0; r < 3; ++r)
    {
        const int h = r == 2 ? area.getBottom() - y : totalH * rowWeights[r] / (196 + 186 + 172);
        const int n = (int) rows[r].size();
        int sum = 0;
        for (int w : rowWidths[r]) sum += w;
        const float unit = (float) (area.getWidth() - gap * (n - 1)) / (float) sum;

        float x = (float) area.getX();
        for (int i = 0; i < n; ++i)
        {
            const float w = unit * (float) rowWidths[r][(size_t) i];
            const int x0 = juce::roundToInt (x), x1 = i == n - 1 ? area.getRight() : juce::roundToInt (x + w);
            rows[r][(size_t) i]->setBounds (x0, y, x1 - x0, h);
            x += w + (float) gap;
        }
        y += h + gap;
    }
}

} // namespace wsho::gui
