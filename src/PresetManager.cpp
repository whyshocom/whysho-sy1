#include "PresetManager.h"
#include "presets/BankFormat.h"
#include "presets/FactoryBank.h"
#include <set>

namespace wsho
{
namespace
{
constexpr int   kMaxUserBanks    = 512;
constexpr int   kMaxFavourites   = 20000;
constexpr float kModifiedEpsilon = 1.0e-6f;     // normalised; far below one knob step

const juce::Identifier kSessionNode ("PRESET");
const juce::Identifier kBankProp ("bank"), kNameProp ("name"), kCategoryProp ("category"),
                       kAuthorProp ("author"), kNotesProp ("notes"), kModifiedProp ("modified");

const char* const kUserBankName  = "User";
const char* const kFavouritesFile = "favourites.json";
const char* const kFavouritesFormat = "whysho-sy1-favourites";

juce::String favKey (const juce::String& bankName, const juce::String& presetName)
{
    return bankName + "\n" + presetName;       // cleaned names never contain a newline
}

int presetIndexByName (const Bank& b, const juce::String& name)
{
    for (size_t i = 0; i < b.presets.size(); ++i)
        if (b.presets[i].name.equalsIgnoreCase (name)) return (int) i;
    return -1;
}
} // namespace

// ===========================================================================
struct PresetManager::Impl
{
    explicit Impl (juce::AudioProcessorValueTreeState& s)
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            params[(size_t) i] = s.getParameter (pid (i));
            jassert (params[(size_t) i] != nullptr);
        }

        meta.name = "Init";
        markClean();
        scan();
    }

    // ---- live parameters ---------------------------------------------------
    void apply (const bankio::ValueTable& values)
    {
        for (int i = 0; i < kNumParams; ++i)
            if (auto* p = params[(size_t) i])
            {
                // No gesture: a preset change is not a "touch", and hosts that
                // record automation would otherwise write 100 lanes.
                const float norm = p->convertTo0to1 (values[(size_t) i]);
                if (! juce::exactlyEqual (p->getValue(), norm)) p->setValueNotifyingHost (norm);
            }
        markClean();
    }

    // The reference `isModified()` compares against = what the parameters hold now.
    void markClean()
    {
        for (int i = 0; i < kNumParams; ++i)
            reference[(size_t) i] = params[(size_t) i] != nullptr ? params[(size_t) i]->getValue() : 0.0f;
        forcedModified = false;
    }

    // ... or what a preset would make them hold, without applying it.
    void setReference (const bankio::ValueTable& values)
    {
        for (int i = 0; i < kNumParams; ++i)
            reference[(size_t) i] = params[(size_t) i] != nullptr ? params[(size_t) i]->convertTo0to1 (values[(size_t) i]) : 0.0f;
        forcedModified = false;
    }

    bool modified() const
    {
        if (forcedModified) return true;
        for (int i = 0; i < kNumParams; ++i)
            if (auto* p = params[(size_t) i])
                if (std::abs (p->getValue() - reference[(size_t) i]) > kModifiedEpsilon) return true;
        return false;
    }

    // ---- library -----------------------------------------------------------
    static juce::File dir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Whysho").getChildFile ("SY1").getChildFile ("Banks");
    }

    static juce::Array<juce::File> bankFiles()
    {
        auto files = dir().findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false,
                                           juce::String ("*") + bankExtension);
        files.sort();
        return files;
    }

    // Cheap fingerprint of the folder, to notice other plugin instances (or the
    // user) changing it behind our back.
    static juce::String diskSignature()
    {
        juce::String s;
        auto add = [&s] (const juce::File& f)
        {
            s << f.getFileName() << '|' << f.getLastModificationTime().toMilliseconds() << '|' << f.getSize() << '\n';
        };
        for (const auto& f : bankFiles()) add (f);
        const auto fav = dir().getChildFile (kFavouritesFile);
        if (fav.existsAsFile()) add (fav);
        return s;
    }

    bool nameTaken (const juce::String& name, int exceptBank = -1) const
    {
        for (size_t i = 0; i < banks.size(); ++i)
            if ((int) i != exceptBank && banks[i].name.equalsIgnoreCase (name)) return true;
        return false;
    }

    int bankIndexByName (const juce::String& name) const
    {
        for (size_t i = 0; i < banks.size(); ++i)
            if (banks[i].name.equalsIgnoreCase (name)) return (int) i;
        return -1;
    }

    // A file name in the bank folder that nothing uses yet.
    static juce::File freeFile (const juce::String& bankName)
    {
        const auto stem = bankio::fileStem (bankName);
        const auto d = dir();
        auto f = d.getChildFile (stem + bankExtension);
        for (int n = 2; f.exists(); ++n)
            f = d.getChildFile (stem + " " + juce::String (n) + bankExtension);
        return f;
    }

    void scan()
    {
        std::vector<Bank> user;
        Bank userBank;
        bool haveUserBank = false;
        errors.clear();

        banks.clear();
        banks.push_back (makeFactoryBank());

        const auto files = bankFiles();
        for (const auto& f : files)
        {
            if ((int) user.size() + 1 >= kMaxUserBanks)
            {
                errors.add ("Too many bank files; only the first " + juce::String (kMaxUserBanks) + " are shown.");
                break;
            }

            bankio::Parsed parsed;
            const auto result = bankio::readFile (f, parsed);
            if (result.failed() || ! parsed.isBank)
            {
                errors.add (f.getFileName() + ": " + (result.failed() ? result.getErrorMessage()
                                                                      : juce::String ("This is a single preset, not a bank.")));
                continue;
            }

            auto& b = parsed.bank;
            b.factory = false;
            b.file = f;

            if (! haveUserBank && b.name.equalsIgnoreCase (kUserBankName))
            {
                b.name = kUserBankName;
                userBank = std::move (b);
                haveUserBank = true;
                continue;
            }

            // two files can claim the same name; names are our keys, so keep them apart
            b.name = bankio::uniqueName (b.name, [&] (const juce::String& n)
            {
                if (n.equalsIgnoreCase (kUserBankName) || nameTaken (n)) return true;
                for (const auto& other : user) if (other.name.equalsIgnoreCase (n)) return true;
                return false;
            });
            user.push_back (std::move (b));
        }

        if (! haveUserBank)
        {
            userBank.name = kUserBankName;
            userBank.file = freeFile (kUserBankName);    // written on first save
        }

        std::sort (user.begin(), user.end(), [] (const Bank& a, const Bank& b) { return a.name.compareNatural (b.name) < 0; });

        banks.push_back (std::move (userBank));
        for (auto& b : user) banks.push_back (std::move (b));

        loadFavourites();
        signature = diskSignature();
        resolveCurrent();
    }

    // Re-read the folder if it changed on disk. Indices are re-resolved by name;
    // false = the bank / preset the caller meant is gone.
    bool sync (int* bankIndex = nullptr, int* presetIndex = nullptr)
    {
        juce::String bankName, presetName;
        if (bankIndex != nullptr)
        {
            if (! juce::isPositiveAndBelow (*bankIndex, (int) banks.size())) return false;
            const auto& b = banks[(size_t) *bankIndex];
            bankName = b.name;

            if (presetIndex != nullptr)
            {
                if (! juce::isPositiveAndBelow (*presetIndex, (int) b.presets.size())) return false;
                presetName = b.presets[(size_t) *presetIndex].name;
            }
        }

        if (diskSignature() == signature) return true;
        scan();

        if (bankIndex != nullptr)
        {
            *bankIndex = bankIndexByName (bankName);
            if (*bankIndex < 0) return false;
            if (presetIndex != nullptr)
            {
                *presetIndex = presetIndexByName (banks[(size_t) *bankIndex], presetName);
                if (*presetIndex < 0) return false;
            }
        }
        return true;
    }

    bool writeBank (const Bank& b)
    {
        const bool ok = bankio::writeFileAtomic (b.file, bankio::bankToJson (b));
        signature = diskSignature();
        return ok;
    }

    bool isUserBank (int index) const
    {
        return juce::isPositiveAndBelow (index, (int) banks.size()) && ! banks[(size_t) index].factory;
    }

    // ---- current preset ----------------------------------------------------
    void resolveCurrent()
    {
        curBank = curBankName.isEmpty() ? -1 : bankIndexByName (curBankName);
        curPreset = curBank < 0 ? -1 : presetIndexByName (banks[(size_t) curBank], meta.name);
        if (curPreset < 0) curBank = -1;
    }

    void setCurrent (int bankIndex, const Preset& p)
    {
        curBankName = banks[(size_t) bankIndex].name;
        meta.name = p.name;  meta.category = p.category;  meta.author = p.author;  meta.notes = p.notes;
        resolveCurrent();
    }

    // The current preset no longer exists in a bank: keep showing its name, as unsaved.
    void detachCurrent()
    {
        curBankName.clear();
        curBank = curPreset = -1;
        forcedModified = true;
    }

    // ---- favourites --------------------------------------------------------
    void loadFavourites()
    {
        favourites.clear();
        const auto f = dir().getChildFile (kFavouritesFile);
        if (! f.existsAsFile() || f.getSize() > bankio::kMaxFileBytes) return;

        juce::var root;
        if (juce::JSON::parse (f.loadFileAsString(), root).failed()) return;
        if (root["format"].toString() != kFavouritesFormat) return;

        if (const auto* list = root["favourites"].getArray())
            for (const auto& item : *list)
            {
                if ((int) favourites.size() >= kMaxFavourites) break;
                const auto b = bankio::cleanText (item["bank"].toString(),   bankio::kMaxNameLen);
                const auto p = bankio::cleanText (item["preset"].toString(), bankio::kMaxNameLen);
                if (b.isNotEmpty() && p.isNotEmpty()) favourites.insert (favKey (b, p));
            }
    }

    bool saveFavourites()
    {
        juce::String o;
        o << "{\n  \"format\": \"" << kFavouritesFormat << "\",\n  \"version\": 1,\n  \"favourites\": [";
        bool first = true;
        for (const auto& key : favourites)
        {
            o << (first ? "\n" : ",\n")
              << "    { \"bank\": \""   << juce::JSON::escapeString (key.upToFirstOccurrenceOf ("\n", false, false))
              << "\", \"preset\": \""   << juce::JSON::escapeString (key.fromFirstOccurrenceOf ("\n", false, false)) << "\" }";
            first = false;
        }
        o << (first ? "]\n}\n" : "\n  ]\n}\n");

        const bool ok = bankio::writeFileAtomic (dir().getChildFile (kFavouritesFile), o);
        signature = diskSignature();
        return ok;
    }

    // Move or drop the favourites of one bank (empty `to` = drop). True if any changed.
    bool retargetFavourites (const juce::String& fromBank, const juce::String& toBank)
    {
        const auto prefix = fromBank + "\n";
        std::set<juce::String> next;
        bool changed = false;
        for (const auto& key : favourites)
        {
            if (! key.startsWith (prefix)) { next.insert (key); continue; }
            changed = true;
            if (toBank.isNotEmpty()) next.insert (favKey (toBank, key.substring (prefix.length())));
        }
        favourites.swap (next);
        return changed;
    }

    // -----------------------------------------------------------------------
    std::array<juce::RangedAudioParameter*, (size_t) kNumParams> params {};

    std::vector<Bank> banks;                 // [0] Factory, [1] User, then by name
    std::set<juce::String> favourites;
    juce::StringArray errors;
    juce::String signature;

    juce::String curBankName;                // empty = not from a bank
    Preset meta;                             // name / category / author / notes of the current sound
    int curBank = -1, curPreset = -1;        // derived from the names after every scan
    std::array<float, (size_t) kNumParams> reference {};   // normalised values of the unmodified preset
    bool forcedModified = false;             // modified in a way the reference can't express
};

// ===========================================================================
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : impl (std::make_unique<Impl> (s)) {}
PresetManager::~PresetManager() = default;

const juce::StringArray& PresetManager::categories()
{
    static const juce::StringArray c { "808", "Bass", "Lead", "Pluck", "Keys", "Piano", "Bell", "Synth",
                                         "Pad", "Pulse", "Strings", "Brass", "FX" };
    return c;
}

// ---- library ----
int PresetManager::numBanks() const { return (int) impl->banks.size(); }

const Bank& PresetManager::bank (int i) const
{
    static const Bank none;                  // a stale index must not crash the browser
    return juce::isPositiveAndBelow (i, (int) impl->banks.size()) ? impl->banks[(size_t) i] : none;
}

void PresetManager::rescan()
{
    impl->scan();
    sendChangeMessage();
}

juce::File PresetManager::userDir() const            { return Impl::dir(); }
int PresetManager::userBankIndex() const             { return 1; }
juce::StringArray PresetManager::scanErrors() const  { return impl->errors; }

// ---- current preset ----
void PresetManager::load (int bankIndex, int presetIndex)
{
    const auto& b = bank (bankIndex);
    if (! juce::isPositiveAndBelow (presetIndex, (int) b.presets.size())) return;

    const auto p = b.presets[(size_t) presetIndex];    // copy: apply() may re-enter via host callbacks
    impl->apply (bankio::resolve (p));
    impl->setCurrent (bankIndex, p);
    sendChangeMessage();
}

void PresetManager::loadInit()
{
    impl->apply (bankio::defaultValues());
    impl->meta = {};
    impl->meta.name = "Init";
    impl->curBankName.clear();
    impl->resolveCurrent();
    sendChangeMessage();
}

void PresetManager::step (int delta)
{
    if (delta == 0) return;

    // from Init / an unsaved sound, start at either end of the factory bank
    const int b = impl->curBank >= 0 ? impl->curBank : 0;
    const int n = (int) bank (b).presets.size();
    if (n == 0) return;

    const int from = impl->curBank >= 0 ? impl->curPreset : (delta > 0 ? -1 : n);
    load (b, (((from + delta) % n) + n) % n);
}

int PresetManager::currentBank() const          { return impl->curBank; }
int PresetManager::currentPreset() const        { return impl->curPreset; }
juce::String PresetManager::currentName() const { return impl->meta.name; }
bool PresetManager::isModified() const          { return impl->modified(); }

Preset PresetManager::captureCurrent() const
{
    Preset p = impl->meta;
    p.values.clear();
    for (int i = 0; i < kNumParams; ++i)
        if (auto* param = impl->params[(size_t) i])
            p.values.emplace_back (pid (i), bankio::sanitiseValue (i, (double) param->convertFrom0to1 (param->getValue())));
    return p;
}

// ---- editing user banks ----
int PresetManager::createBank (const juce::String& name, const juce::String& author)
{
    impl->sync();
    if (numBanks() >= kMaxUserBanks) return -1;

    Bank b;
    b.name = bankio::cleanText (name, bankio::kMaxNameLen);
    if (b.name.isEmpty()) b.name = "New Bank";
    b.name = bankio::uniqueName (b.name, [this] (const juce::String& n) { return impl->nameTaken (n); });
    b.author = bankio::cleanText (author, bankio::kMaxNameLen);
    b.file = Impl::freeFile (b.name);

    if (! impl->writeBank (b)) return -1;

    const auto finalName = b.name;
    impl->scan();
    sendChangeMessage();
    return impl->bankIndexByName (finalName);
}

bool PresetManager::deleteBank (int bankIndex)
{
    if (! impl->sync (&bankIndex) || ! impl->isUserBank (bankIndex)) return false;

    const auto b = impl->banks[(size_t) bankIndex];
    if (b.file.existsAsFile() && ! b.file.moveToTrash() && ! b.file.deleteFile()) return false;

    if (impl->retargetFavourites (b.name, {})) impl->saveFavourites();
    if (impl->curBank == bankIndex) impl->detachCurrent();

    impl->scan();            // deleting "User" leaves a fresh, empty one
    sendChangeMessage();
    return true;
}

bool PresetManager::renameBank (int bankIndex, const juce::String& newName)
{
    if (! impl->sync (&bankIndex) || ! impl->isUserBank (bankIndex)) return false;

    const auto name = bankio::cleanText (newName, bankio::kMaxNameLen);
    if (name.isEmpty() || impl->nameTaken (name, bankIndex)) return false;

    const auto old = impl->banks[(size_t) bankIndex];
    if (name == old.name) return true;

    // the file follows the name, so the folder stays readable for the user
    auto renamed = old;
    renamed.name = name;
    if (bankio::fileStem (name) != old.file.getFileNameWithoutExtension()) renamed.file = Impl::freeFile (name);
    if (! impl->writeBank (renamed)) return false;
    if (renamed.file != old.file) old.file.deleteFile();

    if (impl->retargetFavourites (old.name, name)) impl->saveFavourites();
    if (impl->curBank == bankIndex) impl->curBankName = name;

    impl->scan();
    sendChangeMessage();
    return true;
}

bool PresetManager::savePreset (int bankIndex, const Preset& p)
{
    if (! impl->sync (&bankIndex) || ! impl->isUserBank (bankIndex)) return false;

    const auto clean = bankio::sanitisePreset (p);
    auto updated = impl->banks[(size_t) bankIndex];
    const int existing = presetIndexByName (updated, clean.name);

    if (existing >= 0)                                                   updated.presets[(size_t) existing] = clean;
    else if ((int) updated.presets.size() < bankio::kMaxPresetsPerBank)  updated.presets.push_back (clean);
    else                                                                 return false;

    if (! impl->writeBank (updated)) return false;
    impl->banks[(size_t) bankIndex] = std::move (updated);

    // Current, but not applied: the caller normally saves what is already playing.
    impl->setCurrent (bankIndex, clean);
    impl->setReference (bankio::resolve (clean));
    sendChangeMessage();
    return true;
}

bool PresetManager::deletePreset (int bankIndex, int presetIndex)
{
    if (! impl->sync (&bankIndex, &presetIndex) || ! impl->isUserBank (bankIndex)) return false;

    auto updated = impl->banks[(size_t) bankIndex];
    const auto name = updated.presets[(size_t) presetIndex].name;
    updated.presets.erase (updated.presets.begin() + presetIndex);
    if (! impl->writeBank (updated)) return false;

    const bool wasCurrent = impl->curBank == bankIndex && impl->curPreset == presetIndex;
    impl->banks[(size_t) bankIndex] = std::move (updated);

    if (impl->favourites.erase (favKey (impl->banks[(size_t) bankIndex].name, name)) > 0) impl->saveFavourites();
    if (wasCurrent) impl->detachCurrent(); else impl->resolveCurrent();
    sendChangeMessage();
    return true;
}

bool PresetManager::renamePreset (int bankIndex, int presetIndex, const juce::String& newName)
{
    if (! impl->sync (&bankIndex, &presetIndex) || ! impl->isUserBank (bankIndex)) return false;

    const auto name = bankio::cleanText (newName, bankio::kMaxNameLen);
    auto updated = impl->banks[(size_t) bankIndex];
    const int clash = presetIndexByName (updated, name);
    if (name.isEmpty() || (clash >= 0 && clash != presetIndex)) return false;

    const auto oldName = updated.presets[(size_t) presetIndex].name;
    if (name == oldName) return true;

    updated.presets[(size_t) presetIndex].name = name;
    if (! impl->writeBank (updated)) return false;

    const bool wasCurrent = impl->curBank == bankIndex && impl->curPreset == presetIndex;
    impl->banks[(size_t) bankIndex] = std::move (updated);
    const auto& bankName = impl->banks[(size_t) bankIndex].name;

    if (impl->favourites.erase (favKey (bankName, oldName)) > 0)
    {
        impl->favourites.insert (favKey (bankName, name));
        impl->saveFavourites();
    }
    if (wasCurrent) impl->meta.name = name;
    impl->resolveCurrent();
    sendChangeMessage();
    return true;
}

// ---- sharing ----
int PresetManager::importFile (const juce::File& f, juce::String& error)
{
    error.clear();

    bankio::Parsed parsed;
    if (const auto result = bankio::readFile (f, parsed); result.failed())
    {
        error = result.getErrorMessage();
        return -1;
    }

    impl->sync();

    if (! parsed.isBank)
    {
        // a lone preset joins the "User" bank; never silently replaces one of the same name
        const int user = userBankIndex();
        auto updated = impl->banks[(size_t) user];
        if ((int) updated.presets.size() >= bankio::kMaxPresetsPerBank)
        {
            error = "The User bank is full.";
            return -1;
        }

        auto p = parsed.bank.presets.front();
        p.name = bankio::uniqueName (p.name, [&updated] (const juce::String& n) { return presetIndexByName (updated, n) >= 0; });
        updated.presets.push_back (std::move (p));

        if (! impl->writeBank (updated))
        {
            error = "Could not write to " + updated.file.getFullPathName();
            return -1;
        }
        impl->banks[(size_t) user] = std::move (updated);
        impl->resolveCurrent();
        sendChangeMessage();
        return user;
    }

    // already in the library folder (dropped there by hand): nothing to copy
    if (f.getParentDirectory() == userDir())
        for (int i = 0; i < numBanks(); ++i)
            if (impl->banks[(size_t) i].file == f) { sendChangeMessage(); return i; }

    if (numBanks() >= kMaxUserBanks)
    {
        error = "The library is full (" + juce::String (kMaxUserBanks) + " banks).";
        return -1;
    }

    // Re-written from the parsed data rather than copied, so only clean,
    // in-range content ever lands in the library.
    auto b = std::move (parsed.bank);
    b.factory = false;
    b.name = bankio::uniqueName (b.name, [this] (const juce::String& n) { return impl->nameTaken (n); });
    b.file = Impl::freeFile (b.name);

    if (! impl->writeBank (b))
    {
        error = "Could not write to " + b.file.getFullPathName();
        return -1;
    }

    const auto finalName = b.name;
    impl->scan();
    sendChangeMessage();
    return impl->bankIndexByName (finalName);
}

bool PresetManager::exportBank (int bankIndex, const juce::File& dest) const
{
    if (! juce::isPositiveAndBelow (bankIndex, numBanks())) return false;
    return bankio::writeFileAtomic (dest, bankio::bankToJson (impl->banks[(size_t) bankIndex]));
}

bool PresetManager::exportPreset (const Preset& p, const juce::File& dest) const
{
    return bankio::writeFileAtomic (dest, bankio::presetToJson (p));
}

// ---- favourites ----
bool PresetManager::isFavourite (int bankIndex, int presetIndex) const
{
    const auto& b = bank (bankIndex);
    return juce::isPositiveAndBelow (presetIndex, (int) b.presets.size())
        && impl->favourites.count (favKey (b.name, b.presets[(size_t) presetIndex].name)) > 0;
}

void PresetManager::setFavourite (int bankIndex, int presetIndex, bool fav)
{
    if (! impl->sync (&bankIndex, &presetIndex)) return;

    const auto& b = impl->banks[(size_t) bankIndex];
    const auto key = favKey (b.name, b.presets[(size_t) presetIndex].name);

    const bool changed = fav ? ((int) impl->favourites.size() < kMaxFavourites && impl->favourites.insert (key).second)
                             : impl->favourites.erase (key) > 0;
    if (! changed) return;

    impl->saveFavourites();
    sendChangeMessage();
}

// ---- DAW session state ----
void PresetManager::saveSessionState (juce::ValueTree& state) const
{
    if (! state.isValid()) return;

    auto node = state.getOrCreateChildWithName (kSessionNode, nullptr);
    node.removeAllProperties (nullptr);
    node.setProperty (kBankProp,     impl->curBankName,   nullptr);   // kept even if that bank isn't installed here
    node.setProperty (kNameProp,     impl->meta.name,     nullptr);
    node.setProperty (kCategoryProp, impl->meta.category, nullptr);
    node.setProperty (kAuthorProp,   impl->meta.author,   nullptr);
    node.setProperty (kNotesProp,    impl->meta.notes,    nullptr);
    node.setProperty (kModifiedProp, isModified(),        nullptr);
}

void PresetManager::restoreSessionState (const juce::ValueTree& state)
{
    // The parameters already hold the project's values; this only restores which
    // preset they came from. Nothing is applied.
    const auto node = state.getChildWithName (kSessionNode);

    impl->meta = {};
    impl->meta.name     = bankio::cleanText (node[kNameProp].toString(),     bankio::kMaxNameLen);
    impl->meta.category = bankio::cleanText (node[kCategoryProp].toString(), bankio::kMaxCategoryLen);
    impl->meta.author   = bankio::cleanText (node[kAuthorProp].toString(),   bankio::kMaxNameLen);
    impl->meta.notes    = bankio::cleanText (node[kNotesProp].toString(),    bankio::kMaxNotesLen);
    if (impl->meta.name.isEmpty()) impl->meta.name = "Init";

    impl->curBankName = bankio::cleanText (node[kBankProp].toString(), bankio::kMaxNameLen);
    impl->resolveCurrent();

    const bool wasModified = (bool) node[kModifiedProp];
    if (! wasModified)
    {
        impl->markClean();
    }
    else if (impl->curBank >= 0)
    {
        // compare against the stored preset, so undoing the edit clears the flag
        impl->setReference (bankio::resolve (impl->banks[(size_t) impl->curBank].presets[(size_t) impl->curPreset]));
    }
    else
    {
        impl->markClean();
        impl->forcedModified = true;        // the bank isn't on this machine; just remember it was edited
    }

    sendChangeMessage();
}
} // namespace wsho
