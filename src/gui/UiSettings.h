#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace wsho::gui
{
// Per-user UI preferences shared by every SY1 instance (last author name,
// theme for brand-new instances). Opened on demand — nothing static, so
// there is nothing to tear down when the host unloads the plugin.
namespace settings
{
inline juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "ui";
    o.filenameSuffix      = ".settings";
    o.folderName          = juce::String ("Whysho") + juce::File::getSeparatorString() + "SY1";
    o.osxLibrarySubFolder = "Application Support";
    return o;
}

inline juce::String get (const juce::String& key, const juce::String& fallback = {})
{
    juce::PropertiesFile f (options());
    return f.getValue (key, fallback);
}

inline void set (const juce::String& key, const juce::String& value)
{
    juce::PropertiesFile f (options());
    if (f.getValue (key) != value)
    {
        f.setValue (key, value);
        f.saveIfNeeded();
    }
}
} // namespace settings
} // namespace wsho::gui
