#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PresetManager.h"
#include "Dialogs.h"

namespace wsho::gui
{
// The sound library: banks | categories | a dense multi-column preset list.
// Sits over the synth page (the keyboard below stays playable for auditioning).
class PresetBrowser : public juce::Component,
                      private juce::ChangeListener,
                      private juce::KeyListener
{
public:
    explicit PresetBrowser (PresetManager&);
    ~PresetBrowser() override;

    std::function<void()> onClose;
    std::function<void (PromptSpec)> onPrompt;      // the editor hosts dialogs above everything
    std::function<void()> onInit;

    void opened();                                  // focus search, reveal the current preset
    void setSearch (const juce::String&);
    void showMessage (const juce::String&, bool isError);
    void importFiles (const juce::Array<juce::File>&);
    void chooseImport();
    void exportPresetToFile (const Preset&);
    void openFolder();

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    struct Item { int bank, preset; };
    class Column;
    class Grid;
    friend class Grid;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;   // search box: arrows / esc

    void rebuild();
    void rebuildItems();
    void updateButtons();
    void selectBank (int index);
    void navigate (int dx, int dy);
    bool handleNavKey (const juce::KeyPress&);
    void load (const Item&);
    void presetMenu (const Item&);
    juce::String describe (const Item&) const;
    void setHover (int itemIndex);

    void newBank();
    void renameBank();
    void deleteBank();
    void exportBank();
    void prompt (PromptSpec s) { if (onPrompt) onPrompt (std::move (s)); }

    PresetManager& pm;
    std::vector<Item> items;
    juce::StringArray cats;                 // category names behind the category column rows 2..n
    int bankSel = -1;                       // -1 = all banks (resolved from bankName on every rebuild)
    juce::String bankName, lastScanErrors;
    juce::String catSel;                    // "" = all, "*" = favourites, else category
    juce::String search;
    int hoverItem = -1;
    juce::String message;
    bool messageIsError = false;
    int scopeCount = 0;

    std::unique_ptr<Column> bankList, catList;
    std::unique_ptr<Grid> grid;
    juce::Viewport gridView;
    juce::TextEditor searchBox;
    juce::TextButton closeBtn { "Close" }, initBtn { "Init patch" }, clearBtn { "Clear" };
    juce::TextButton newBtn { "New" }, importBtn { "Import..." }, exportBtn { "Export..." },
                     renameBtn { "Rename" }, deleteBtn { "Delete" }, folderBtn { "Folder" };
    juce::Rectangle<int> bankArea, catArea, mainArea, footerArea, headArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace wsho::gui
