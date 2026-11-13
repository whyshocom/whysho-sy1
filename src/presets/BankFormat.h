#pragma once
#include <array>
#include <functional>
#include "../PresetManager.h"

// ===========================================================================
//  Bank / preset files (*.sy1bank, *.sy1preset) — JSON, see docs/BANKS.md.
//  Banks get passed around online, so everything read here is treated as
//  untrusted: sizes are capped, text is cleaned, every value is clamped into
//  its parameter range. Nothing in this file touches the live parameters.
// ===========================================================================
namespace wsho::bankio
{
constexpr int kFormatVersion     = 1;
constexpr int kMaxFileBytes      = 8 * 1024 * 1024;
constexpr int kMaxPresetsPerBank = 1024;
constexpr int kMaxNameLen        = 64;
constexpr int kMaxCategoryLen    = 32;
constexpr int kMaxNotesLen       = 512;

inline constexpr const char* kBankFormat   = "whysho-sy1-bank";
inline constexpr const char* kPresetFormat = "whysho-sy1-preset";

using ValueTable = std::array<float, (size_t) kNumParams>;

// ---- values ----
// Clamp into the parameter's range, snap Int / Choice / Bool; NaN → default.
float sanitiseValue (int paramIndex, double v);
ValueTable defaultValues();
// Full raw value table of a preset: defaults, overridden by the known ids.
ValueTable resolve (const Preset& p);

// ---- text ----
// Printable, single-line, whitespace-collapsed, at most `maxLen` characters.
juce::String cleanText (const juce::String& s, int maxLen);
// A bank name → safe file name stem (no separators, dots or reserved names).
juce::String fileStem (const juce::String& bankName);
// `base`, or "base 2", "base 3" ... — the first one `taken` rejects.
juce::String uniqueName (const juce::String& base, const std::function<bool (const juce::String&)>& taken);

// Cleaned meta + every parameter, in table order. Known categories get their
// canonical spelling; an empty name becomes "Untitled".
Preset sanitisePreset (const Preset& p);

// ---- (de)serialisation ----
juce::String bankToJson (const Bank& b);
juce::String presetToJson (const Preset& p);

struct Parsed
{
    bool isBank = false;     // false: a single preset, stored as bank.presets[0]
    int  version = kFormatVersion;
    Bank bank;               // sanitised; preset names unique inside the bank
};

// `fallbackName` names a bank whose file carries no usable name.
juce::Result parse (const juce::String& json, Parsed& out, const juce::String& fallbackName = {});
juce::Result readFile (const juce::File& f, Parsed& out);

// Temp file + rename, so a crash or a second plugin instance never sees half a file.
bool writeFileAtomic (const juce::File& dest, const juce::String& text);
} // namespace wsho::bankio
