#include "BankFormat.h"
#include <cmath>
#include <cstdio>
#include <map>

namespace wsho::bankio
{
namespace
{
constexpr int kMaxJsonDepth     = 12;      // a bank needs 4
constexpr int kMaxObjectMembers = 2048;    // JUCE's object parser is quadratic in the member count
constexpr int kMaxArrayItems    = 65536;

const std::map<juce::String, int>& idMap()
{
    static const std::map<juce::String, int> m = []
    {
        std::map<juce::String, int> r;
        for (int i = 0; i < kNumParams; ++i) r[paramDef (i).id] = i;
        return r;
    }();
    return m;
}

int indexOfId (const juce::String& id)
{
    const auto it = idMap().find (id);
    return it == idMap().end() ? -1 : it->second;
}

// juce::JSON recurses per nesting level and de-duplicates object keys with a
// linear search, so bound both before handing it text from the internet.
juce::Result checkShape (const juce::String& text)
{
    struct Level { bool object; int items; };
    Level stack[kMaxJsonDepth + 1] {};
    int depth = 0;
    juce::juce_wchar quote = 0;

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (quote != 0)
        {
            if (c == '\\')       { if (! p.isEmpty()) ++p; }
            else if (c == quote) quote = 0;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
        }
        else if (c == '{' || c == '[')
        {
            if (++depth > kMaxJsonDepth)
                return juce::Result::fail ("The file is nested too deeply to be a bank.");
            stack[depth] = { c == '{', 0 };
        }
        else if (c == '}' || c == ']')
        {
            if (depth > 0) --depth;
        }
        else if (c == ',' && depth > 0)
        {
            auto& l = stack[depth];
            if (++l.items >= (l.object ? kMaxObjectMembers : kMaxArrayItems))
                return juce::Result::fail ("The file has too many entries to be a bank.");
        }
    }
    return juce::Result::ok();
}

juce::String stringField (const juce::var& obj, const char* key, int maxLen)
{
    const auto& v = obj[key];
    return v.isString() ? cleanText (v.toString(), maxLen) : juce::String();
}

// A parameter value from JSON: number or bool; a Choice may also be given by name.
bool readValue (int index, const juce::var& v, float& out)
{
    if (v.isBool())                               { out = sanitiseValue (index, (bool) v ? 1.0 : 0.0); return true; }
    if (v.isInt() || v.isInt64() || v.isDouble()) { out = sanitiseValue (index, (double) v);           return true; }

    if (v.isString() && paramDef (index).kind == ParamDef::Choice)
    {
        const auto choice = paramChoices ((P) index).indexOf (v.toString().substring (0, 64).trim(), true);
        if (choice >= 0) { out = (float) choice; return true; }
    }
    return false;   // wrong type → the caller keeps the default
}

Preset readPreset (const juce::var& v)
{
    Preset p;
    p.name     = stringField (v, "name",     kMaxNameLen);
    p.category = stringField (v, "category", kMaxCategoryLen);
    p.author   = stringField (v, "author",   kMaxNameLen);
    p.notes    = stringField (v, "notes",    kMaxNotesLen);

    auto values = defaultValues();
    if (auto* params = v["params"].getDynamicObject())
        for (const auto& prop : params->getProperties())
        {
            const int i = indexOfId (prop.name.toString());
            if (i >= 0) readValue (i, prop.value, values[(size_t) i]);
        }

    for (int i = 0; i < kNumParams; ++i)
        p.values.emplace_back (paramDef (i).id, values[(size_t) i]);
    return sanitisePreset (p);
}

juce::String quoted (const juce::String& s) { return "\"" + juce::JSON::escapeString (s) + "\""; }

// Shortest decimal text that reads back as exactly the same float.
juce::String numberText (int index, float v)
{
    if (paramDef (index).kind != ParamDef::Float || (juce::exactlyEqual (v, std::floor (v)) && std::abs (v) < 1.0e6f))
        return juce::String ((int) std::lround (v));

    char buf[40];
    for (int digits = 1; digits <= 9; ++digits)
    {
        std::snprintf (buf, sizeof (buf), "%.*g", digits, (double) v);
        const auto t = juce::String (buf).replaceCharacter (',', '.');   // a host may have set a comma locale
        if (juce::exactlyEqual ((float) t.getDoubleValue(), v)) return t;
    }
    return juce::String ((double) v, 9);
}

void writePresetBody (juce::String& o, const Preset& raw, const juce::String& in)
{
    const auto p = sanitisePreset (raw);
    o << in << "\"name\": "     << quoted (p.name)     << ",\n"
      << in << "\"category\": " << quoted (p.category) << ",\n"
      << in << "\"author\": "   << quoted (p.author)   << ",\n"
      << in << "\"notes\": "    << quoted (p.notes)    << ",\n"
      << in << "\"params\": {\n";

    for (int i = 0; i < kNumParams; ++i)
        o << in << "  \"" << paramDef (i).id << "\": " << numberText (i, p.values[(size_t) i].second)
          << (i + 1 < kNumParams ? ",\n" : "\n");

    o << in << "}\n";
}
} // namespace

// ---------------------------------------------------------------------------
float sanitiseValue (int index, double v)
{
    const auto& d = paramDef (index);
    if (std::isnan (v)) return d.def;            // +-inf clamps like any other out-of-range number

    v = juce::jlimit ((double) d.min, (double) d.max, v);
    if (d.kind != ParamDef::Float) v = std::round (v);
    return (float) v;
}

ValueTable defaultValues()
{
    ValueTable t {};
    for (int i = 0; i < kNumParams; ++i) t[(size_t) i] = paramDef (i).def;
    return t;
}

ValueTable resolve (const Preset& p)
{
    auto t = defaultValues();
    for (const auto& [id, value] : p.values)
    {
        const int i = indexOfId (id);
        if (i >= 0) t[(size_t) i] = sanitiseValue (i, (double) value);
    }
    return t;
}

juce::String cleanText (const juce::String& s, int maxLen)
{
    juce::String out;
    bool pendingSpace = false;
    int length = 0;

    for (auto p = s.getCharPointer(); ! p.isEmpty() && length < maxLen;)
    {
        const auto c = p.getAndAdvance();
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xa0 || c == 0x2028 || c == 0x2029;
        // control characters and bidi overrides could garble or spoof the browser list
        const bool junk = c < 32 || c == 127 || (c >= 0x80 && c < 0xa0) || (c >= 0x200b && c <= 0x200f)
                          || (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069) || c == 0xfeff;

        if (space)      { pendingSpace = length > 0; continue; }
        if (junk)       continue;
        if (pendingSpace && length + 1 < maxLen) { out << ' '; ++length; }
        pendingSpace = false;
        out += c;
        ++length;
    }
    return out;
}

juce::String fileStem (const juce::String& bankName)
{
    auto s = cleanText (bankName, kMaxNameLen)
                 .replaceCharacters ("<>:\"/\\|?*", "_________");
    while (s.startsWithChar ('.') || s.startsWithChar (' ')) s = s.substring (1);
    while (s.endsWithChar ('.')   || s.endsWithChar (' '))   s = s.dropLastCharacters (1);
    if (s.isEmpty()) s = "Bank";

    // device names Windows refuses as file names, whatever the extension
    static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
                                              "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
                                              "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    if (reserved.contains (s.upToFirstOccurrenceOf (".", false, false).trim(), true)) s = "_" + s;
    return s;
}

juce::String uniqueName (const juce::String& base, const std::function<bool (const juce::String&)>& taken)
{
    if (! taken (base)) return base;
    for (int n = 2;; ++n)
    {
        const auto suffix = " " + juce::String (n);
        const auto candidate = base.substring (0, kMaxNameLen - suffix.length()).trimEnd() + suffix;
        if (! taken (candidate)) return candidate;
    }
}

Preset sanitisePreset (const Preset& p)
{
    Preset r;
    r.name     = cleanText (p.name,     kMaxNameLen);
    r.category = cleanText (p.category, kMaxCategoryLen);
    r.author   = cleanText (p.author,   kMaxNameLen);
    r.notes    = cleanText (p.notes,    kMaxNotesLen);
    if (r.name.isEmpty()) r.name = "Untitled";

    const auto& cats = PresetManager::categories();
    const int known = cats.indexOf (r.category, true);
    if (known >= 0) r.category = cats[known];

    const auto t = resolve (p);
    r.values.reserve ((size_t) kNumParams);
    for (int i = 0; i < kNumParams; ++i)
        r.values.emplace_back (paramDef (i).id, t[(size_t) i]);
    return r;
}

// ---------------------------------------------------------------------------
juce::String bankToJson (const Bank& b)
{
    juce::String o;
    o.preallocateBytes (512 + b.presets.size() * 4096);
    o << "{\n"
      << "  \"format\": \""  << kBankFormat << "\",\n"
      << "  \"version\": "   << kFormatVersion << ",\n"
      << "  \"name\": "      << quoted (cleanText (b.name, kMaxNameLen))   << ",\n"
      << "  \"author\": "    << quoted (cleanText (b.author, kMaxNameLen)) << ",\n"
      << "  \"presets\": [";

    for (size_t i = 0; i < b.presets.size(); ++i)
    {
        o << (i == 0 ? "\n" : ",\n") << "    {\n";
        writePresetBody (o, b.presets[i], "      ");
        o << "    }";
    }

    o << (b.presets.empty() ? "]\n" : "\n  ]\n") << "}\n";
    return o;
}

juce::String presetToJson (const Preset& p)
{
    juce::String o;
    o << "{\n"
      << "  \"format\": \"" << kPresetFormat << "\",\n"
      << "  \"version\": "  << kFormatVersion << ",\n";
    writePresetBody (o, p, "  ");
    o << "}\n";
    return o;
}

juce::Result parse (const juce::String& json, Parsed& out, const juce::String& fallbackName)
{
    out = {};

    if (const auto shape = checkShape (json); shape.failed())
        return shape;

    juce::var root;
    if (juce::JSON::parse (json, root).failed() || root.getDynamicObject() == nullptr)
        return juce::Result::fail ("Not a whysho SY1 bank: the file is damaged or is not JSON.");

    const auto format = root["format"].isString() ? root["format"].toString().substring (0, 64) : juce::String();
    if (format != kBankFormat && format != kPresetFormat)
        return juce::Result::fail ("Not a whysho SY1 bank or preset file.");

    // A newer version loads best-effort: fields we don't know are simply not read.
    const auto& version = root["version"];
    out.version = (version.isInt() || version.isInt64() || version.isDouble())
                      ? (int) juce::jlimit (1.0, 1.0e6, (double) version) : kFormatVersion;

    if (format == kPresetFormat)
    {
        out.bank.presets.push_back (readPreset (root));
        return juce::Result::ok();
    }

    out.isBank = true;
    out.bank.name   = stringField (root, "name",   kMaxNameLen);
    out.bank.author = stringField (root, "author", kMaxNameLen);
    if (out.bank.name.isEmpty()) out.bank.name = cleanText (fallbackName, kMaxNameLen);
    if (out.bank.name.isEmpty()) out.bank.name = "Bank";

    const auto& presets = root["presets"];
    if (presets.isVoid()) return juce::Result::ok();          // an empty bank is fine

    const auto* list = presets.getArray();
    if (list == nullptr)
        return juce::Result::fail ("Damaged bank: \"presets\" is not a list.");
    if (list->size() > kMaxPresetsPerBank)
        return juce::Result::fail ("This bank holds " + juce::String (list->size()) + " presets; the limit is "
                                   + juce::String (kMaxPresetsPerBank) + ".");

    auto& dest = out.bank.presets;
    for (const auto& item : *list)
    {
        if (item.getDynamicObject() == nullptr) continue;     // skip junk entries, keep the rest

        auto p = readPreset (item);
        // names are the key for favourites and session recall, so keep them unique
        p.name = uniqueName (p.name, [&dest] (const juce::String& n)
        {
            for (const auto& other : dest) if (other.name.equalsIgnoreCase (n)) return true;
            return false;
        });
        dest.push_back (std::move (p));
    }
    return juce::Result::ok();
}

juce::Result readFile (const juce::File& f, Parsed& out)
{
    out = {};
    if (! f.existsAsFile())            return juce::Result::fail ("The file could not be found.");
    if (f.getSize() > kMaxFileBytes)   return juce::Result::fail ("The file is too large to be a bank (limit 8 MB).");

    juce::MemoryBlock data;
    if (! f.loadFileAsData (data))     return juce::Result::fail ("The file could not be read.");
    if (data.getSize() > (size_t) kMaxFileBytes)                       // it may have grown since the check
        return juce::Result::fail ("The file is too large to be a bank (limit 8 MB).");

    auto* bytes = static_cast<const char*> (data.getData());
    auto size = (int) data.getSize();
    if (size >= 3 && (juce::uint8) bytes[0] == 0xef && (juce::uint8) bytes[1] == 0xbb && (juce::uint8) bytes[2] == 0xbf)
    {
        bytes += 3;   // UTF-8 byte order mark
        size -= 3;
    }

    if (! juce::CharPointer_UTF8::isValidString (bytes, size))
        return juce::Result::fail ("Not a whysho SY1 bank: the file is not UTF-8 text.");

    return parse (juce::String::fromUTF8 (bytes, size), out, f.getFileNameWithoutExtension());
}

bool writeFileAtomic (const juce::File& dest, const juce::String& text)
{
    if (dest == juce::File() || ! dest.getParentDirectory().createDirectory()) return false;

    // not the bank extension: a scan must never pick up a half-written temp file
    const auto tmpName = "." + dest.getFileName() + "." + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".tmp";
    juce::TemporaryFile tmp (dest, dest.getSiblingFile (tmpName));

    return tmp.getFile().replaceWithText (text, false, false, "\n")
        && tmp.overwriteTargetFileWithTemporary();
}
} // namespace wsho::bankio
