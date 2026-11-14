#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <vector>

namespace wsho::gui
{
// A complete, swappable palette. Field names match the old `col::` tokens so
// every component simply reads `theme().<name>`.
struct Theme
{
    juce::String name;
    bool   light      = false;   // paper-style rendering (black curve, fills)
    bool   curveGlow  = true;    // neon glow under the curve (dark themes)
    bool   mono       = false;   // monospace editorial typography

    juce::Colour bg, panel, panel2, grid, text, textDim,
                 accent, accent2, good, warn, bad, curve, specPre, specPost;

    std::array<juce::Colour, 8> nodePalette; // per-band node colours (SY1: osc 1/2/3 inks first)

    // ---- SY1 additions. Left transparent / zero in a theme literal they are
    //      derived from the fields above by `finished()`, so EQ-style theme
    //      definitions keep working unchanged.
    juce::Colour accentText;     // accent tuned to be legible as small text on bg / panel
    juce::Colour onAccent;       // text / glyphs drawn on top of an accent fill
    juce::Colour rule;           // strong hairline that opens a section
    juce::Colour gridInk;        // chart-paper log-grid ink (already alpha'd)
    juce::Colour display;        // background of scopes / curve displays
    juce::Colour keyWhite, keyBlack;
    float radius = -1.0f;        // corner radius of panels / fields (paper = sharp)
};

namespace detail
{
inline float luminance (juce::Colour c)
{
    auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
}
inline float contrast (juce::Colour a, juce::Colour b)
{
    const float la = luminance (a), lb = luminance (b);
    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}

// Fill in the derived SY1 fields a theme literal left unset.
inline Theme finished (Theme t)
{
    const auto unset = [] (juce::Colour c) { return c.isTransparent(); };

    if (unset (t.accentText))
    {
        // Walk the accent towards the text colour until it reads on the panel.
        t.accentText = t.accent;
        for (int i = 0; i < 12 && contrast (t.accentText, t.panel) < 4.6f; ++i)
            t.accentText = t.accentText.interpolatedWith (t.text, 0.12f);
    }
    if (unset (t.onAccent))
    {
        const auto dark = t.light ? t.text : t.bg, lite = t.light ? t.panel2 : t.text;
        t.onAccent = contrast (dark, t.accent) >= contrast (lite, t.accent) ? dark : lite;
    }
    if (unset (t.rule))     t.rule     = t.light ? t.text : t.grid.brighter (0.25f);
    if (unset (t.gridInk))  t.gridInk  = (t.light ? t.text : t.accent).withAlpha (t.light ? 0.10f : 0.13f);
    if (unset (t.display))  t.display  = t.light ? t.panel2 : t.bg;
    if (unset (t.keyWhite)) t.keyWhite = t.light ? t.panel2 : t.text.interpolatedWith (t.bg, 0.12f);
    if (unset (t.keyBlack)) t.keyBlack = t.light ? t.text   : t.bg;
    if (t.radius < 0.0f)    t.radius   = t.light ? 1.5f : 5.0f;
    return t;
}
} // namespace detail

// ---- The theme catalogue --------------------------------------------------
// Indices 0..2 are shared with EQ1 (same order, same colours) so a saved
// `uiTheme` means the same thing across the suite. New themes are appended.
inline const std::vector<Theme>& themes()
{
    using C = juce::Colour;
    // Positional like the EQ1 literals; the SY1 fields stay unset → derived.
    static const auto make = [] (const char* name, bool light, bool glow, bool mono,
                                 C bg, C panel, C panel2, C grid, C text, C textDim,
                                 C accent, C accent2, C good, C warn, C bad, C curve, C specPre, C specPost,
                                 std::array<C, 8> palette)
    {
        Theme t;
        t.name = name; t.light = light; t.curveGlow = glow; t.mono = mono;
        t.bg = bg; t.panel = panel; t.panel2 = panel2; t.grid = grid; t.text = text; t.textDim = textDim;
        t.accent = accent; t.accent2 = accent2; t.good = good; t.warn = warn; t.bad = bad;
        t.curve = curve; t.specPre = specPre; t.specPost = specPost; t.nodePalette = palette;
        return t;
    };
    static const std::vector<Theme> list = [] {
        std::vector<Theme> v;

        // 0 — Paper (default): warm editorial light look
        v.push_back (make (
            "Paper", /*light*/ true, /*glow*/ false, /*mono*/ true,
            C (0xffF2EEE3), // bg
            C (0xffEAE5D7), // panel
            C (0xffFBF9F2), // panel2 (fields)
            C (0xffCBC4B0), // grid
            C (0xff1A1813), // text
            C (0xff6E6857), // textDim
            C (0xffC5542F), // accent (terracotta)
            C (0xff8E6FD6), // accent2 (purple)
            C (0xff3F8F56), // good
            C (0xffC27C2B), // warn
            C (0xffC0392B), // bad
            C (0xff14110B), // curve (near-black)
            C (0x3C5B86B4), // specPre
            C (0x665B86B4), // specPost (blue fill)
            { C (0xffE4572E), C (0xffF2932C), C (0xff3FA34D), C (0xff8E6FD6),
              C (0xff3C78C8), C (0xff2BA6A0), C (0xffD7599B), C (0xffC9A227) } ));

        // 1 — Midnight: dark cyan (the original)
        v.push_back (make (
            "Midnight", false, true, false,
            C (0xff0E0F13), C (0xff15171E), C (0xff1C1F29),
            C (0xff2A2E3A), C (0xffE6E9F0), C (0xff8A90A0),
            C (0xff4CC2FF), C (0xffB46BFF), C (0xff57D9A3),
            C (0xffFFB454), C (0xffFF6B6B), C (0xff4CC2FF),
            C (0x5563718A), C (0x884CC2FF),
            { C (0xff4CC2FF), C (0xff6BE3B8), C (0xffB46BFF), C (0xffFFB454),
              C (0xffFF6B9D), C (0xff7CF2E0), C (0xffFFD166), C (0xff8AA8FF) } ));

        // 2 — Graphite: dark neutral, amber accent
        v.push_back (make (
            "Graphite", false, false, false,
            C (0xff17171A), C (0xff1F1F23), C (0xff27272C),
            C (0xff343439), C (0xffEDEDED), C (0xff908F96),
            C (0xffF2A33C), C (0xffCE6BF2), C (0xff5BD98C),
            C (0xffF2C14E), C (0xffF2655B), C (0xffF2A33C),
            C (0x55707078), C (0x88F2A33C),
            { C (0xffF2A33C), C (0xff5BD98C), C (0xffCE6BF2), C (0xff5BB8D9),
              C (0xffF2655B), C (0xffE0C341), C (0xffCE6BF2), C (0xff9AD95B) } ));

        // 3 — Riso: the website's "signal paper" — fluorescent pink + riso blue on chart paper
        {
            Theme t = make ("Riso", true, false, true,
                C (0xffF1ECE0), C (0xffF8F4EA), C (0xffFFFDF7),
                C (0xffD0CABC), C (0xff1B1813), C (0xff5A5243),
                C (0xffFF48B0), C (0xff0078BF), C (0xff00804A),
                C (0xffC2500F), C (0xffD0283A), C (0xff1B1813),
                C (0x330078BF), C (0x55FF48B0),
                { C (0xffFF48B0), C (0xff0078BF), C (0xffF5B800), C (0xff00A95C),
                  C (0xff765BA7), C (0xffFF6C2F), C (0xffF15060), C (0xff1B1813) } );
            t.accentText = C (0xffB0006A);          // the site's --pink-text
            t.onAccent   = C (0xff1B1813);
            t.gridInk    = C (0xffFF48B0).withAlpha (0.16f);
            t.radius     = 0.0f;
            v.push_back (t);
        }

        // 4 — Ultraviolet: deep purple, neon violet + cyan
        {
            Theme t = make ("Ultraviolet", false, true, false,
                C (0xff0C0716), C (0xff150D26), C (0xff1F1438),
                C (0xff37265E), C (0xffF0EAFF), C (0xffA296C6),
                C (0xffC04DFF), C (0xff2DE2E6), C (0xff4DF0A0),
                C (0xffFFC857), C (0xffFF5C7A), C (0xffD9A0FF),
                C (0x55504080), C (0x88C04DFF),
                { C (0xffC04DFF), C (0xff2DE2E6), C (0xffFF4FB8), C (0xffFFC857),
                  C (0xff4DF0A0), C (0xff8AA8FF), C (0xffFF8A5C), C (0xffE0D0FF) } );
            t.accentText = C (0xffD28AFF);
            t.onAccent   = C (0xff12061F);
            v.push_back (t);
        }

        // 5 — Acid: black with acid green
        {
            Theme t = make ("Acid", false, true, false,
                C (0xff080907), C (0xff10120E), C (0xff191C15),
                C (0xff2D3226), C (0xffEBF2DE), C (0xff929C85),
                C (0xffB6FF2E), C (0xff39E6C3), C (0xffB6FF2E),
                C (0xffFFD23F), C (0xffFF5A4F), C (0xffB6FF2E),
                C (0x55566048), C (0x77B6FF2E),
                { C (0xffB6FF2E), C (0xff39E6C3), C (0xffF2FF3A), C (0xffFF7AD9),
                  C (0xff7AB8FF), C (0xffFFD23F), C (0xffFF8A5C), C (0xffDDE8C8) } );
            t.onAccent = C (0xff0A0C06);
            v.push_back (t);
        }

        // 6 — Blood: near-black with arterial red
        {
            Theme t = make ("Blood", false, false, false,
                C (0xff0B0808), C (0xff151011), C (0xff1F1819),
                C (0xff36282B), C (0xffF4EAEA), C (0xffA39193),
                C (0xffFF2B3F), C (0xffFF8A3D), C (0xff6FD99A),
                C (0xffFFB454), C (0xffFF2B3F), C (0xffFF4A58),
                C (0x55604044), C (0x77FF2B3F),
                { C (0xffFF2B3F), C (0xffFF8A3D), C (0xffF4EAEA), C (0xffFFC857),
                  C (0xffFF6FA0), C (0xffC0A0FF), C (0xff6FD99A), C (0xff8AB8FF) } );
            t.accentText = C (0xffFF5C68);
            t.onAccent   = C (0xff160507);
            v.push_back (t);
        }

        for (auto& t : v) t = detail::finished (t);
        return v;
    }();
    return list;
}

// ---- Active theme ---------------------------------------------------------
namespace detail { inline int& activeThemeIndex() { static int i = 0; return i; } }

inline const Theme& theme()
{
    const auto& list = themes();
    int i = detail::activeThemeIndex();
    if (i < 0 || i >= (int) list.size()) i = 0;
    return list[(size_t) i];
}

inline int  currentThemeIndex() { return detail::activeThemeIndex(); }
inline void setThemeIndex (int i) { detail::activeThemeIndex() = (i >= 0 && i < (int) themes().size()) ? i : 0; }

inline juce::StringArray themeNames()
{
    juce::StringArray n;
    for (auto& t : themes()) n.add (t.name);
    return n;
}

inline juce::Colour bandColour (int band)
{
    const auto& pal = theme().nodePalette;
    return pal[(size_t) (band % (int) pal.size())];
}

// Typography: monospace editorial face for Paper, clean sans for dark themes.
inline juce::Font themeFont (float height, bool bold = false)
{
    const int style = bold ? juce::Font::bold : juce::Font::plain;
    if (theme().mono)
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, style));
    return juce::Font (juce::FontOptions (height, style));
}

// Small tracked label face for section kickers ("OSC 1", "FILTER").
inline juce::Font kickerFont (float height = 10.0f)
{
    return themeFont (height, true).withExtraKerningFactor (theme().mono ? 0.06f : 0.11f);
}

// Big condensed display face for product codes / preset names. Always sans:
// the site sets "EQ1" / "SY1" in a heavy grotesque even on paper.
inline juce::Font displayFont (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold))
               .withHorizontalScale (0.92f).withExtraKerningFactor (-0.035f);
}

// All-caps label with light tracking (used for section / header text).
inline juce::String caps (const juce::String& s) { return s.toUpperCase(); }

// Chart-recorder paper: log-spaced verticals (one decade per `decadeW`) and
// evenly spaced horizontals, in the theme's grid ink.
inline void drawLogGrid (juce::Graphics& g, juce::Rectangle<float> r, float decadeW, int rows)
{
    const auto ink = theme().gridInk;
    for (float x0 = r.getX(); x0 < r.getRight(); x0 += decadeW)
        for (int k = 1; k <= 9; ++k)
        {
            const float x = x0 + decadeW * std::log10 ((float) k);
            if (x >= r.getRight()) break;
            g.setColour (k == 1 ? ink.withMultipliedAlpha (2.0f) : ink);
            g.fillRect (x, r.getY(), 1.0f, r.getHeight());
        }
    g.setColour (ink);
    for (int i = 1; i < rows; ++i)
        g.fillRect (r.getX(), r.getY() + r.getHeight() * (float) i / (float) rows, r.getWidth(), 1.0f);
}

} // namespace wsho::gui
