#include "PresetManager.h"
// STUB — replaced by the real preset manager.
namespace wsho
{
struct PresetManager::Impl { std::vector<Bank> banks { Bank { "Factory", "whysho", true, {}, {} } }; };
PresetManager::PresetManager (juce::AudioProcessorValueTreeState&) : impl (std::make_unique<Impl>()) {}
PresetManager::~PresetManager() = default;
const juce::StringArray& PresetManager::categories() { static const juce::StringArray c { "808", "Bass", "Lead", "Pluck", "Keys", "Bell", "Pad", "Brass", "FX" }; return c; }
int PresetManager::numBanks() const { return (int) impl->banks.size(); }
const Bank& PresetManager::bank (int i) const { return impl->banks[(size_t) i]; }
void PresetManager::rescan() {}
juce::File PresetManager::userDir() const { return {}; }
void PresetManager::load (int, int) {}
void PresetManager::loadInit() {}
void PresetManager::step (int) {}
int PresetManager::currentBank() const { return -1; }
int PresetManager::currentPreset() const { return -1; }
juce::String PresetManager::currentName() const { return "Init"; }
bool PresetManager::isModified() const { return false; }
Preset PresetManager::captureCurrent() const { return {}; }
int PresetManager::createBank (const juce::String&, const juce::String&) { return -1; }
bool PresetManager::deleteBank (int) { return false; }
bool PresetManager::renameBank (int, const juce::String&) { return false; }
bool PresetManager::savePreset (int, const Preset&) { return false; }
bool PresetManager::deletePreset (int, int) { return false; }
int PresetManager::importFile (const juce::File&, juce::String&) { return -1; }
bool PresetManager::exportBank (int, const juce::File&) const { return false; }
bool PresetManager::exportPreset (const Preset&, const juce::File&) const { return false; }
bool PresetManager::isFavourite (int, int) const { return false; }
void PresetManager::setFavourite (int, int, bool) {}
void PresetManager::saveSessionState (juce::ValueTree&) const {}
void PresetManager::restoreSessionState (const juce::ValueTree&) {}
}
