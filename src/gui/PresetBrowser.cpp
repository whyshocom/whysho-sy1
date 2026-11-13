#include "PresetBrowser.h"
#include "WhyshoLookAndFeel.h"
#include "Controls.h"
#include "UiSettings.h"

namespace wsho::gui
{
namespace
{
const juce::String kFavourites ("*");
juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* const kDash = " \xe2\x80\x94 ";     // em dash
const char* const kDot  = " \xc2\xb7 ";         // middle dot
} // namespace

// ===========================================================================
// A single-select list column (banks, categories) with counts.
class PresetBrowser::Column : public juce::Component
{
public:
    struct Row { juce::String text; int count = 0; bool star = false; juce::String tag; bool faint = false; };

    std::function<void (int)> onSelect;

    void setRows (std::vector<Row> r, int sel)
    {
        rows = std::move (r);
        selected = sel;
        scroll = juce::jlimit (0, maxScroll(), scroll);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto r = juce::Rectangle<int> (0, i * rowH - scroll, getWidth(), rowH);
            if (r.getBottom() < 0 || r.getY() > getHeight()) continue;
            const auto& row = rows[(size_t) i];
            const bool sel = i == selected;
            if (sel)
            {
                g.setColour (t.light ? t.text : t.accent);
                g.fillRoundedRectangle (r.toFloat().reduced (0.0f, 0.5f), juce::jmin (t.radius, 3.0f));
            }
            else if (i == hover)
            {
                g.setColour (t.text.withAlpha (0.07f));
                g.fillRect (r);
            }
            const auto fg = sel ? (t.light ? t.bg : t.onAccent) : (row.faint ? t.textDim.withAlpha (0.75f) : t.text);
            auto inner = r.reduced (7, 0);

            g.setFont (themeFont (10.0f));
            g.setColour (sel ? fg.withAlpha (0.8f) : t.textDim.withAlpha (row.faint ? 0.6f : 1.0f));
            g.drawText (juce::String (row.count), inner.removeFromRight (26), juce::Justification::centredRight, false);

            if (row.tag.isNotEmpty())
            {
                g.setFont (themeFont (8.5f, true));
                const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), row.tag) + 4;
                g.drawText (row.tag, inner.removeFromRight (tw), juce::Justification::centredRight, false);
            }
            if (row.star)
            {
                g.setColour (sel ? fg : t.accentText);
                g.fillPath (starPath (inner.removeFromLeft (14).toFloat().withSizeKeepingCentre (11.0f, 11.0f)));
                inner.removeFromLeft (3);
            }
            g.setColour (fg);
            g.setFont (themeFont (11.5f, sel));
            g.drawText (row.text, inner, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.y);
        if (i >= 0 && onSelect) onSelect (i);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scroll = juce::jlimit (0, maxScroll(), scroll - juce::roundToInt (w.deltaY * 120.0f));
        repaint();
    }
    void resized() override { scroll = juce::jlimit (0, maxScroll(), scroll); }

    static constexpr int rowH = 23;

private:
    int rowAt (int y) const
    {
        const int i = (y + scroll) / rowH;
        return i >= 0 && i < (int) rows.size() ? i : -1;
    }
    int maxScroll() const { return juce::jmax (0, (int) rows.size() * rowH - getHeight()); }
    void setHover (int h) { if (h != hover) { hover = h; repaint(); } }

    std::vector<Row> rows;
    int selected = -1, hover = -1, scroll = 0;
};

// ===========================================================================
// The tone list: row-major grid of presets, like a hardware synth's tone
// browser but with stars and search.
class PresetBrowser::Grid : public juce::Component
{
public:
    explicit Grid (PresetBrowser& o) : owner (o) {}

    static constexpr int rowH = 23, minColW = 184;

    int cols() const { return juce::jmax (1, getWidth() / minColW); }

    void layoutFor (int width)
    {
        const int c = juce::jmax (1, width / minColW);
        const int n = (int) owner.items.size();
        setSize (width, juce::jmax (rowH, ((n + c - 1) / c) * rowH));
        repaint();
    }

    juce::Rectangle<int> cell (int i) const
    {
        const int c = cols(), cw = getWidth() / c;
        return { (i % c) * cw, (i / c) * rowH, cw, rowH };
    }

    int itemAt (juce::Point<int> p) const
    {
        const int c = cols(), cw = getWidth() / c;
        const int col = p.x / juce::jmax (1, cw);
        if (col >= c || p.y < 0) return -1;
        const int i = (p.y / rowH) * c + col;
        return i < (int) owner.items.size() ? i : -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = theme();
        const auto clip = g.getClipBounds();
        const auto& pm = owner.pm;
        const int cb = pm.currentBank(), cp = pm.currentPreset();

        for (int i = 0; i < (int) owner.items.size(); ++i)
        {
            const auto r = cell (i);
            if (! r.intersects (clip)) continue;
            const auto& it = owner.items[(size_t) i];
            const auto& p = pm.bank (it.bank).presets[(size_t) it.preset];
            const bool cur = it.bank == cb && it.preset == cp;
            const bool hov = i == hover;
            const bool fav = pm.isFavourite (it.bank, it.preset);

            auto body = r.reduced (1, 0).withTrimmedBottom (1);
            if (cur)
            {
                g.setColour (t.accent);
                g.fillRoundedRectangle (body.toFloat(), juce::jmin (t.radius, 3.0f));
            }
            else if (hov)
            {
                g.setColour (t.text.withAlpha (0.08f));
                g.fillRect (body);
            }
            // ledger rule under every line
            g.setColour (t.grid.withAlpha (t.light ? 0.8f : 0.5f));
            g.fillRect (r.getX() + 1, r.getBottom() - 1, r.getWidth() - 2, 1);

            const auto fg = cur ? t.onAccent : t.text;
            auto inner = body.reduced (4, 0);
            const auto starBox = inner.removeFromLeft (16).toFloat().withSizeKeepingCentre (11.0f, 11.0f);
            if (fav)
            {
                g.setColour (cur ? fg : t.accentText);
                g.fillPath (starPath (starBox));
            }
            else if (hov)
            {
                g.setColour (fg.withAlpha (0.45f));
                g.strokePath (starPath (starBox), juce::PathStrokeType (1.0f));
            }
            inner.removeFromLeft (3);

            if (p.category.isNotEmpty())
            {
                g.setFont (themeFont (8.5f, true));
                const auto tag = caps (p.category);
                const int tw = juce::jmin (64, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tag) + 6);
                g.setColour (cur ? fg.withAlpha (0.8f) : t.textDim);
                g.drawText (tag, inner.removeFromRight (tw), juce::Justification::centredRight, false);
            }
            g.setColour (fg);
            g.setFont (themeFont (11.5f, cur));
            g.drawText (p.name, inner, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (itemAt (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = itemAt (e.getPosition());
        if (i < 0) return;
        const auto it = owner.items[(size_t) i];
        if (e.mods.isPopupMenu())                { owner.presetMenu (it); return; }
        if (e.x - cell (i).getX() < 22)          // the star
        {
            owner.pm.setFavourite (it.bank, it.preset, ! owner.pm.isFavourite (it.bank, it.preset));
            repaint();
            return;
        }
        owner.load (it);
    }

    void setHover (int h)
    {
        if (h == hover) return;
        hover = h;
        owner.setHover (h);
        repaint();
    }

private:
    PresetBrowser& owner;
    int hover = -1;
};

// ===========================================================================
PresetBrowser::PresetBrowser (PresetManager& m) : pm (m)
{
    setWantsKeyboardFocus (true);

    bankList = std::make_unique<Column>();
    catList  = std::make_unique<Column>();
    grid     = std::make_unique<Grid> (*this);
    addAndMakeVisible (*bankList);
    addAndMakeVisible (*catList);
    gridView.setViewedComponent (grid.get(), false);
    gridView.setScrollBarsShown (true, false);
    gridView.setScrollBarThickness (9);
    addAndMakeVisible (gridView);

    bankList->onSelect = [this] (int row) { selectBank (row - 1); message.clear(); rebuild(); };
    catList->onSelect = [this] (int row)
    {
        catSel = row == 0 ? juce::String() : row == 1 ? kFavourites : cats[row - 2];
        message.clear();
        rebuild();
    };

    searchBox.setTextToShowWhenEmpty ("Search name, category, author", theme().textDim);
    searchBox.onTextChange = [this] { search = searchBox.getText().trim(); message.clear(); rebuildItems(); repaint(); };
    searchBox.addKeyListener (this);
    addAndMakeVisible (searchBox);

    clearBtn.onClick  = [this] { setSearch ({}); };
    closeBtn.onClick  = [this] { if (onClose) onClose(); };
    initBtn.onClick   = [this] { if (onInit) onInit(); };
    newBtn.onClick    = [this] { newBank(); };
    importBtn.onClick = [this] { chooseImport(); };
    exportBtn.onClick = [this] { exportBank(); };
    renameBtn.onClick = [this] { renameBank(); };
    deleteBtn.onClick = [this] { deleteBank(); };
    folderBtn.onClick = [this] { openFolder(); };

    initBtn.setTooltip ("Reset every parameter to the default patch");
    newBtn.setTooltip ("Create an empty user bank");
    importBtn.setTooltip ("Import .sy1bank / .sy1preset files (or just drop them on the window)");
    exportBtn.setTooltip ("Export the selected bank as one shareable .sy1bank file");
    renameBtn.setTooltip ("Rename the selected user bank");
    deleteBtn.setTooltip ("Delete the selected user bank");
    folderBtn.setTooltip ("Open the folder where user banks live");

    for (auto* b : { &clearBtn, &closeBtn, &initBtn, &newBtn, &importBtn, &exportBtn, &renameBtn, &deleteBtn, &folderBtn })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    pm.addChangeListener (this);
    lookAndFeelChanged();
    rebuild();
}

PresetBrowser::~PresetBrowser()
{
    pm.removeChangeListener (this);
    searchBox.removeKeyListener (this);
}

void PresetBrowser::lookAndFeelChanged()
{
    styleTextEditor (searchBox, 12.5f);
    searchBox.setTextToShowWhenEmpty ("Search name, category, author", theme().textDim);
    repaint();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible()) rebuild();
}

void PresetBrowser::opened()
{
    rebuild();
    if (isShowing()) searchBox.grabKeyboardFocus();
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].bank == pm.currentBank() && items[(size_t) i].preset == pm.currentPreset())
        {
            const auto c = grid->cell (i);
            gridView.setViewPosition (0, juce::jmax (0, c.getCentreY() - gridView.getHeight() / 2));
            break;
        }
}

void PresetBrowser::setSearch (const juce::String& s)
{
    searchBox.setText (s, true);
    search = s.trim();
    rebuildItems();
    repaint();
}

void PresetBrowser::showMessage (const juce::String& m, bool isError)
{
    message = m;
    messageIsError = isError;
    repaint();
}

// ---------------------------------------------------------------------------
void PresetBrowser::rebuild()
{
    // Bank indices shift whenever the library changes, so the selection is
    // remembered by name and resolved again on every rebuild.
    const int nb = pm.numBanks();
    bankSel = -1;
    for (int i = 0; i < nb && bankName.isNotEmpty(); ++i)
        if (pm.bank (i).name == bankName) { bankSel = i; break; }
    if (bankSel < 0) bankName.clear();

    // Files the scan had to skip: say so once, inline, instead of hiding them.
    const auto skipped = pm.scanErrors().joinIntoString ("; ");
    if (skipped != lastScanErrors)
    {
        lastScanErrors = skipped;
        if (skipped.isNotEmpty()) { message = "Skipped " + skipped; messageIsError = true; }
    }

    std::vector<Column::Row> bankRows;
    int total = 0;
    for (int i = 0; i < nb; ++i) total += (int) pm.bank (i).presets.size();
    bankRows.push_back ({ "All banks", total, false, {}, false });
    for (int i = 0; i < nb; ++i)
    {
        const auto& b = pm.bank (i);
        bankRows.push_back ({ b.name, (int) b.presets.size(), false, b.factory ? juce::String ("ROM") : juce::String(), false });
    }
    bankList->setRows (std::move (bankRows), bankSel + 1);

    // categories: the fixed list, plus anything unusual an imported bank brought along
    cats = PresetManager::categories();
    std::map<juce::String, int> counts;
    int favs = 0;
    scopeCount = 0;
    for (int b = 0; b < nb; ++b)
    {
        if (bankSel >= 0 && b != bankSel) continue;
        const auto& bk = pm.bank (b);
        for (int p = 0; p < (int) bk.presets.size(); ++p)
        {
            const auto& c = bk.presets[(size_t) p].category;
            ++scopeCount;
            ++counts[c];
            if (c.isNotEmpty() && ! cats.contains (c)) cats.add (c);
            if (pm.isFavourite (b, p)) ++favs;
        }
    }
    if (catSel.isNotEmpty() && catSel != kFavourites && ! cats.contains (catSel)) catSel.clear();

    std::vector<Column::Row> catRows;
    catRows.push_back ({ "All", scopeCount, false, {}, false });
    catRows.push_back ({ "Favourites", favs, true, {}, favs == 0 });
    for (auto& c : cats) catRows.push_back ({ c, counts[c], false, {}, counts[c] == 0 });
    catList->setRows (std::move (catRows), catSel.isEmpty() ? 0 : catSel == kFavourites ? 1 : 2 + cats.indexOf (catSel));

    rebuildItems();
    updateButtons();
    repaint();
}

void PresetBrowser::rebuildItems()
{
    items.clear();
    juce::StringArray tokens;
    tokens.addTokens (search.toLowerCase(), " ", "");
    tokens.removeEmptyStrings();

    for (int b = 0; b < pm.numBanks(); ++b)
    {
        if (bankSel >= 0 && b != bankSel) continue;
        const auto& bk = pm.bank (b);
        for (int p = 0; p < (int) bk.presets.size(); ++p)
        {
            const auto& pr = bk.presets[(size_t) p];
            if (catSel == kFavourites) { if (! pm.isFavourite (b, p)) continue; }
            else if (catSel.isNotEmpty() && pr.category != catSel) continue;

            if (! tokens.isEmpty())
            {
                const auto hay = (pr.name + " " + pr.category + " " + pr.author).toLowerCase();
                bool all = true;
                for (auto& tk : tokens) all = all && hay.contains (tk);
                if (! all) continue;
            }
            items.push_back ({ b, p });
        }
    }

    hoverItem = -1;
    grid->setHover (-1);
    gridView.setVisible (! items.empty());
    grid->layoutFor (juce::jmax (Grid::minColW, gridView.getWidth() - gridView.getScrollBarThickness()));
    clearBtn.setVisible (search.isNotEmpty());
}

void PresetBrowser::selectBank (int index)
{
    bankSel = index >= 0 && index < pm.numBanks() ? index : -1;
    bankName = bankSel >= 0 ? pm.bank (bankSel).name : juce::String();
}

void PresetBrowser::updateButtons()
{
    const bool one  = bankSel >= 0 && bankSel < pm.numBanks();
    const bool user = one && ! pm.bank (bankSel).factory;
    exportBtn.setEnabled (one);
    renameBtn.setEnabled (user);
    deleteBtn.setEnabled (user);
}

void PresetBrowser::setHover (int itemIndex)
{
    if (hoverItem != itemIndex) { hoverItem = itemIndex; repaint (footerArea); }
}

juce::String PresetBrowser::describe (const Item& it) const
{
    if (it.bank < 0 || it.bank >= pm.numBanks()) return {};
    const auto& bk = pm.bank (it.bank);
    if (it.preset < 0 || it.preset >= (int) bk.presets.size()) return {};
    const auto& p = bk.presets[(size_t) it.preset];
    auto s = p.name;
    if (p.category.isNotEmpty()) s << utf8 (kDot) << p.category;
    if (p.author.isNotEmpty())   s << utf8 (kDot) << "by " << p.author;
    s << utf8 (kDot) << bk.name;
    if (p.notes.isNotEmpty())    s << utf8 (kDash) << p.notes;
    return s;
}

// ---------------------------------------------------------------------------
void PresetBrowser::load (const Item& it)
{
    message.clear();
    pm.load (it.bank, it.preset);
    grid->repaint();
    repaint (footerArea);
}

void PresetBrowser::navigate (int dx, int dy)
{
    if (items.empty()) return;
    int idx = -1;
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].bank == pm.currentBank() && items[(size_t) i].preset == pm.currentPreset()) { idx = i; break; }

    if (idx < 0) idx = 0;
    else
    {
        const int next = idx + dx + dy * grid->cols();
        if (next < 0 || next >= (int) items.size()) return;
        idx = next;
    }
    load (items[(size_t) idx]);

    const auto c = grid->cell (idx);
    const int top = gridView.getViewPositionY();
    if (c.getY() < top)                                   gridView.setViewPosition (0, c.getY());
    else if (c.getBottom() > top + gridView.getHeight())  gridView.setViewPosition (0, c.getBottom() - gridView.getHeight());
}

bool PresetBrowser::handleNavKey (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::upKey)    { navigate (0, -1); return true; }
    if (k == juce::KeyPress::downKey)  { navigate (0, 1);  return true; }
    if (k == juce::KeyPress::leftKey)  { navigate (-1, 0); return true; }
    if (k == juce::KeyPress::rightKey) { navigate (1, 0);  return true; }
    return false;
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (handleNavKey (k)) return true;
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0)) { searchBox.grabKeyboardFocus(); return true; }
    return false;       // everything else falls through to the editor → computer-keyboard notes
}

bool PresetBrowser::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    // Search box has focus: up/down always walk the list; left/right only when there's no text to edit.
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) return handleNavKey (k);
    if (searchBox.isEmpty() && (k == juce::KeyPress::leftKey || k == juce::KeyPress::rightKey)) return handleNavKey (k);
    if (k == juce::KeyPress::escapeKey)
    {
        if (searchBox.isEmpty()) { if (onClose) onClose(); }
        else setSearch ({});
        return true;
    }
    if (k == juce::KeyPress::returnKey) { grabKeyboardFocus(); return true; }   // done typing: keys play notes again
    return false;
}

void PresetBrowser::presetMenu (const Item& it)
{
    const auto& bk = pm.bank (it.bank);
    const bool fav = pm.isFavourite (it.bank, it.preset);
    juce::PopupMenu m;
    m.addItem (1, fav ? "Remove from favourites" : "Add to favourites");
    m.addItem (2, "Export preset...");
    m.addSeparator();
    m.addItem (4, "Rename preset...", ! bk.factory);
    m.addItem (3, "Delete preset", ! bk.factory);

    juce::Component::SafePointer<PresetBrowser> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, it, fav] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pm = safe->pm;
        if (it.bank >= pm.numBanks() || it.preset >= (int) pm.bank (it.bank).presets.size()) return;
        const auto preset = pm.bank (it.bank).presets[(size_t) it.preset];
        if (r == 1) { pm.setFavourite (it.bank, it.preset, ! fav); safe->rebuild(); }
        else if (r == 2) safe->exportPresetToFile (preset);
        else if (r == 4)
        {
            PromptSpec s;
            s.kicker = "Rename preset";
            s.title = preset.name;
            s.message = "New name for this preset:";
            s.okText = "Rename";
            s.wantsText = true;
            s.initialText = preset.name;
            s.onOk = [safe, it, old = preset.name] (const juce::String& name)
            {
                if (safe == nullptr) return;
                auto& mgr = safe->pm;
                if (it.bank < mgr.numBanks() && it.preset < (int) mgr.bank (it.bank).presets.size()
                    && mgr.bank (it.bank).presets[(size_t) it.preset].name == old)
                {
                    if (! mgr.renamePreset (it.bank, it.preset, name))
                        safe->showMessage ("Couldn't rename " + old + " (is that name already taken?).", true);
                    safe->rebuild();
                }
            };
            safe->prompt (std::move (s));
        }
        else if (r == 3)
        {
            PromptSpec s;
            s.kicker = "Delete preset";
            s.title = preset.name;
            s.message = "Delete this preset from " + pm.bank (it.bank).name + "? This can't be undone.";
            s.okText = "Delete";
            s.onOk = [safe, it, name = preset.name] (const juce::String&)
            {
                if (safe == nullptr) return;
                auto& mgr = safe->pm;
                // re-check: the library may have changed while the question was up
                if (it.bank < mgr.numBanks() && it.preset < (int) mgr.bank (it.bank).presets.size()
                    && mgr.bank (it.bank).presets[(size_t) it.preset].name == name)
                {
                    if (! mgr.deletePreset (it.bank, it.preset))
                        safe->showMessage ("Couldn't delete " + name + ".", true);
                    safe->rebuild();
                }
            };
            safe->prompt (std::move (s));
        }
    });
}

// ---------------------------------------------------------------------------
void PresetBrowser::newBank()
{
    PromptSpec s;
    s.kicker = "New bank";
    s.title = "Name the bank";
    s.message = "A bank is one file you can share. Saved sounds go into the bank you pick.";
    s.okText = "Create";
    s.wantsText = true;
    s.initialText = "My Sounds";
    juce::Component::SafePointer<PresetBrowser> safe (this);
    s.onOk = [safe] (const juce::String& name)
    {
        if (safe == nullptr) return;
        const int idx = safe->pm.createBank (name, settings::get ("author"));
        if (idx < 0) safe->showMessage ("Couldn't create the bank. Check that the presets folder is writable.", true);
        else { safe->selectBank (idx); safe->catSel.clear(); safe->showMessage ("Created " + safe->bankName, false); }
        safe->rebuild();
    };
    prompt (std::move (s));
}

void PresetBrowser::renameBank()
{
    if (bankSel < 0 || bankSel >= pm.numBanks() || pm.bank (bankSel).factory) return;
    PromptSpec s;
    s.kicker = "Rename bank";
    s.title = pm.bank (bankSel).name;
    s.message = "New name for this bank:";
    s.okText = "Rename";
    s.wantsText = true;
    s.initialText = pm.bank (bankSel).name;
    juce::Component::SafePointer<PresetBrowser> safe (this);
    s.onOk = [safe, idx = bankSel, old = pm.bank (bankSel).name] (const juce::String& name)
    {
        if (safe == nullptr || idx >= safe->pm.numBanks() || safe->pm.bank (idx).name != old) return;
        if (safe->pm.renameBank (idx, name)) safe->bankName = name;     // keep it selected under its new name
        else safe->showMessage ("Couldn't rename the bank.", true);
        safe->rebuild();
    };
    prompt (std::move (s));
}

void PresetBrowser::deleteBank()
{
    if (bankSel < 0 || bankSel >= pm.numBanks() || pm.bank (bankSel).factory) return;
    const auto& b = pm.bank (bankSel);
    PromptSpec s;
    s.kicker = "Delete bank";
    s.title = b.name;
    s.message = "Delete this bank and its " + juce::String ((int) b.presets.size())
              + " preset(s) from disk? This can't be undone. Export it first if you want a backup.";
    s.okText = "Delete bank";
    juce::Component::SafePointer<PresetBrowser> safe (this);
    s.onOk = [safe, idx = bankSel, old = b.name] (const juce::String&)
    {
        if (safe == nullptr || idx >= safe->pm.numBanks() || safe->pm.bank (idx).name != old) return;
        if (safe->pm.deleteBank (idx)) { safe->selectBank (-1); safe->showMessage ("Deleted " + old, false); }
        else safe->showMessage ("Couldn't delete the bank.", true);
        safe->rebuild();
    };
    prompt (std::move (s));
}

void PresetBrowser::exportBank()
{
    if (bankSel < 0 || bankSel >= pm.numBanks()) return;
    const auto name = pm.bank (bankSel).name;
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (juce::File::createLegalFileName (name) + PresetManager::bankExtension);
    chooser = std::make_unique<juce::FileChooser> ("Export bank", start, juce::String ("*") + PresetManager::bankExtension);
    juce::Component::SafePointer<PresetBrowser> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, idx = bankSel, name] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (safe == nullptr || f == juce::File()) return;
        if (idx >= safe->pm.numBanks() || safe->pm.bank (idx).name != name) return;
        f = f.withFileExtension (PresetManager::bankExtension);
        if (safe->pm.exportBank (idx, f)) safe->showMessage ("Exported " + f.getFileName(), false);
        else safe->showMessage ("Couldn't write " + f.getFileName() + ".", true);
    });
}

void PresetBrowser::exportPresetToFile (const Preset& p)
{
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (juce::File::createLegalFileName (p.name) + PresetManager::presetExtension);
    chooser = std::make_unique<juce::FileChooser> ("Export preset", start, juce::String ("*") + PresetManager::presetExtension);
    juce::Component::SafePointer<PresetBrowser> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, p] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (safe == nullptr || f == juce::File()) return;
        f = f.withFileExtension (PresetManager::presetExtension);
        if (safe->pm.exportPreset (p, f)) safe->showMessage ("Exported " + f.getFileName(), false);
        else safe->showMessage ("Couldn't write " + f.getFileName() + ".", true);
    });
}

void PresetBrowser::chooseImport()
{
    chooser = std::make_unique<juce::FileChooser> ("Import banks or presets", juce::File(),
                                                   juce::String ("*") + PresetManager::bankExtension + ";*" + PresetManager::presetExtension);
    juce::Component::SafePointer<PresetBrowser> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && ! fc.getResults().isEmpty()) safe->importFiles (fc.getResults());
    });
}

void PresetBrowser::importFiles (const juce::Array<juce::File>& files)
{
    int ok = 0, lastBank = -1;
    juce::StringArray problems;
    for (auto& f : files)
    {
        juce::String err;
        const int idx = pm.importFile (f, err);
        if (idx >= 0) { ++ok; lastBank = idx; }
        else problems.add (f.getFileName() + ": " + (err.isNotEmpty() ? err : juce::String ("not a readable SY1 bank or preset")));
    }
    if (lastBank >= 0) { selectBank (lastBank); catSel.clear(); setSearch ({}); }

    if (problems.isEmpty())
        showMessage (ok == 1 ? "Imported " + files[0].getFileName() : "Imported " + juce::String (ok) + " files", false);
    else
        showMessage ((ok > 0 ? "Imported " + juce::String (ok) + ", but " : juce::String ("Couldn't import "))
                         + problems.joinIntoString ("; "), true);
    rebuild();
}

void PresetBrowser::openFolder()
{
    auto d = pm.userDir();
    if (d == juce::File()) { showMessage ("The presets folder isn't available.", true); return; }
    d.createDirectory();
    d.startAsProcess();
}

// ---------------------------------------------------------------------------
void PresetBrowser::resized()
{
    auto b = getLocalBounds().reduced (7, 0).withTrimmedTop (7).withTrimmedBottom (7);
    auto inner = b.reduced (12, 0);

    headArea = inner.removeFromTop (46);
    {
        auto h = headArea.withTrimmedTop (10).withTrimmedBottom (8);
        closeBtn.setBounds (h.removeFromRight (64));
        h.removeFromRight (6);
        initBtn.setBounds (h.removeFromRight (86));
        h.removeFromLeft (118);                 // title
        auto sb = h.removeFromLeft (juce::jmin (340, h.getWidth() - 80));
        searchBox.setBounds (sb);
        h.removeFromLeft (6);
        clearBtn.setBounds (h.removeFromLeft (54));
    }

    footerArea = inner.removeFromBottom (28);
    inner.removeFromBottom (2);

    bankArea = inner.removeFromLeft (182);
    inner.removeFromLeft (13);
    catArea = inner.removeFromLeft (150);
    inner.removeFromLeft (13);
    mainArea = inner;

    {
        auto a = bankArea.withTrimmedTop (20);
        auto btns = a.removeFromBottom (3 * 24 + 4);
        a.removeFromBottom (6);
        bankList->setBounds (a);
        juce::TextButton* order[] = { &newBtn, &importBtn, &renameBtn, &exportBtn, &deleteBtn, &folderBtn };
        for (int i = 0; i < 6; ++i)
        {
            auto row = btns.withY (btns.getY() + (i / 2) * 26).withHeight (22);
            const int half = (row.getWidth() - 4) / 2;
            order[i]->setBounds (i % 2 == 0 ? row.withWidth (half) : row.withLeft (row.getRight() - half));
        }
    }
    catList->setBounds (catArea.withTrimmedTop (20));
    gridView.setBounds (mainArea.withTrimmedTop (20));
    grid->layoutFor (juce::jmax (Grid::minColW, gridView.getWidth() - gridView.getScrollBarThickness()));
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto& t = theme();
    g.fillAll (t.bg);
    const auto b = getLocalBounds().reduced (7, 0).withTrimmedTop (7).withTrimmedBottom (7).toFloat();

    g.setColour (t.panel);
    g.fillRoundedRectangle (b, t.radius);
    if (t.light) { g.setColour (t.rule); g.fillRect (b.withHeight (1.5f)); }
    else         { g.setColour (t.accent.withAlpha (0.45f)); g.drawRoundedRectangle (b.reduced (0.5f), t.radius, 1.0f); }

    // title
    g.setFont (displayFont (24.0f));
    g.setColour (t.text);
    g.drawText ("Sounds", headArea.withWidth (112), juce::Justification::centredLeft, false);

    // hairlines: under the header, between columns, above the footer
    g.setColour (t.light ? t.textDim.withAlpha (0.55f) : t.grid);
    g.fillRect (headArea.getX(), headArea.getBottom(), headArea.getWidth(), 1);
    g.fillRect (footerArea.getX(), footerArea.getY(), footerArea.getWidth(), 1);
    g.fillRect (bankArea.getRight() + 6, bankArea.getY() + 6, 1, bankArea.getHeight() - 10);
    g.fillRect (catArea.getRight() + 6, catArea.getY() + 6, 1, catArea.getHeight() - 10);

    // column kickers
    g.setFont (kickerFont (9.5f));
    g.setColour (t.textDim);
    g.drawText ("BANKS", bankArea.withHeight (20).withTrimmedLeft (7), juce::Justification::centredLeft, false);
    g.drawText ("CATEGORY", catArea.withHeight (20).withTrimmedLeft (7), juce::Justification::centredLeft, false);
    auto ph = mainArea.withHeight (20).withTrimmedLeft (5);
    g.drawText ("PRESETS", ph, juce::Justification::centredLeft, false);
    g.setFont (themeFont (10.0f));
    g.drawText (juce::String ((int) items.size()) + (search.isNotEmpty() || catSel.isNotEmpty() ? " of " + juce::String (scopeCount) : juce::String()),
                ph.withTrimmedRight (10), juce::Justification::centredRight, false);

    // empty states
    if (items.empty())
    {
        juce::String head, body;
        if (search.isNotEmpty())
        {
            head = "Nothing matches " + utf8 ("\xe2\x80\x9c") + search + utf8 ("\xe2\x80\x9d");
            body = "Try fewer letters, another category, or All banks.";
        }
        else if (catSel == kFavourites)
        {
            head = "No favourites yet";
            body = "Click the star next to a preset to keep it here.";
        }
        else if (catSel.isNotEmpty())
        {
            head = "No " + catSel + " presets here";
            body = "Pick another category or bank.";
        }
        else if (bankSel >= 0 && bankSel < pm.numBanks() && ! pm.bank (bankSel).factory)
        {
            head = "No presets in this bank yet";
            body = "Hit Save to add your first sound.";
        }
        else
        {
            head = "No presets yet";
            body = "Hit Save to keep your first sound, or drop a .sy1bank file anywhere on the window.";
        }
        auto area = mainArea.withTrimmedTop (20).reduced (20);
        auto box = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 460), 70).translated (0, -20);
        g.setFont (displayFont (22.0f));
        g.setColour (t.text);
        g.drawText (head, box.removeFromTop (30), juce::Justification::centred, true);
        g.setFont (themeFont (12.0f));
        g.setColour (t.textDim);
        g.drawFittedText (body, box, juce::Justification::centredTop, 2);
    }

    // footer: message, else what's under the mouse, else the loaded preset
    auto f = footerArea.withTrimmedTop (1).reduced (7, 0);
    g.setFont (themeFont (11.0f));
    if (message.isNotEmpty())
    {
        g.setColour (messageIsError ? t.bad : t.good);
        g.fillRect (f.removeFromLeft (3).reduced (0, 7));
        f.removeFromLeft (7);
        g.setColour (t.text);
        g.drawText (message, f, juce::Justification::centredLeft, true);
    }
    else
    {
        Item it { pm.currentBank(), pm.currentPreset() };
        if (hoverItem >= 0 && hoverItem < (int) items.size()) it = items[(size_t) hoverItem];
        const auto d = describe (it);
        g.setColour (t.textDim);
        g.drawText (d.isNotEmpty() ? d : juce::String ("Click a preset to load it. Arrow keys step through the list, right-click for more."),
                    f, juce::Justification::centredLeft, true);
    }
}

} // namespace wsho::gui
