#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace wsho
{
// One sound. `values` holds { parameter id, raw value }; ids missing from a
// preset fall back to the parameter default, unknown ids are ignored — so old
// banks keep loading as the synth grows.
struct Preset
{
    juce::String name, category, author, notes;
    std::vector<std::pair<juce::String, float>> values;
};

// A bank = one shareable file (*.sy1bank, JSON) holding any number of presets.
struct Bank
{
    juce::String name, author;
    bool factory = false;      // built-in, read-only
    juce::File file;           // empty for factory banks
    std::vector<Preset> presets;
};

// Factory banks + user banks on disk + import / export + favourites.
// PUBLIC API IS A CONTRACT with the editor — extend, don't break.
// Message-thread only. Broadcasts a change whenever banks, the current preset
// or favourites change.
class PresetManager : public juce::ChangeBroadcaster
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s);
    ~PresetManager() override;

    // Fixed category list shown in the browser ("808", "Bass", "Lead", ...).
    static const juce::StringArray& categories();
    static constexpr const char* bankExtension   = ".sy1bank";
    static constexpr const char* presetExtension = ".sy1preset";

    // ---- library ----
    int numBanks() const;
    const Bank& bank (int bankIndex) const;
    void rescan();                                   // re-read user banks from disk
    juce::File userDir() const;                      // <appdata>/Whysho/SY1/Banks

    // ---- current preset ----
    void load (int bankIndex, int presetIndex);      // applies to the parameters
    void loadInit();                                 // every parameter to default
    void step (int delta);                           // prev / next inside current bank (wraps)
    int  currentBank() const;                        // -1 = none (init / unsaved)
    int  currentPreset() const;
    juce::String currentName() const;
    bool isModified() const;                         // parameters edited since load
    Preset captureCurrent() const;                   // snapshot of the live parameters (+ current meta)

    // ---- editing user banks (all return false on factory banks / io error) ----
    int  createBank (const juce::String& name, const juce::String& author = {});   // returns index or -1
    bool deleteBank (int bankIndex);
    bool renameBank (int bankIndex, const juce::String& newName);
    // Saves `p` into a user bank; replaces a preset of the same name, else appends.
    // Becomes the current preset.
    bool savePreset (int bankIndex, const Preset& p);
    bool deletePreset (int bankIndex, int presetIndex);

    // ---- sharing ----
    // Import a *.sy1bank (copied into userDir, name de-duplicated) or a
    // *.sy1preset (appended to the "User" bank). Returns the bank index or -1;
    // `error` gets a human-readable reason.
    int  importFile (const juce::File& f, juce::String& error);
    bool exportBank (int bankIndex, const juce::File& dest) const;      // works for factory banks too
    bool exportPreset (const Preset& p, const juce::File& dest) const;

    // ---- favourites (persisted per user, keyed by bank name + preset name) ----
    bool isFavourite (int bankIndex, int presetIndex) const;
    void setFavourite (int bankIndex, int presetIndex, bool fav);

    // ---- DAW session state (stored alongside the parameter state) ----
    void saveSessionState (juce::ValueTree& state) const;   // writes current bank/preset name
    void restoreSessionState (const juce::ValueTree& state);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace wsho
