#include "Account.h"
#include "Http.h"
#include "MachineId.h"
#include "ObfString.h"
#include <juce_cryptography/juce_cryptography.h>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace wsho::lic
{
namespace
{
constexpr juce::int64 kDay = 86400;
constexpr juce::int64 kGraceSeconds     = (juce::int64) AccountClient::kOfflineGraceDays * kDay;
constexpr juce::int64 kRecheckAfter     = 6 * 3600;   // background re-check once the answer is this old
constexpr juce::int64 kRecheckRetry     = 30 * 60;    // ...but no more often than this when it keeps failing
constexpr juce::int64 kClockSlack       = kDay;       // tolerated backwards clock jump (time zones, NTP)

juce::int64 nowSecs()
{
    auto t = juce::Time::currentTimeMillis() / 1000;
   #if WSHO_LIC_TESTING   // test builds only: lets the suite travel in time
    t += juce::SystemStats::getEnvironmentVariable ("WHYSHO_TEST_CLOCK_OFFSET", "0").getLargeIntValue();
   #endif
    return t;
}

juce::String productSlug() { return WSHO_JS ("sy1"); }

juce::String serverBase()
{
    // Obfuscated at rest; WHYSHO_ACCOUNT_SERVER points a dev build at a local site.
    auto base = juce::SystemStats::getEnvironmentVariable ("WHYSHO_ACCOUNT_SERVER", {}).trim();
    if (base.isEmpty()) base = WSHO_JS ("https://whysho.com");
    while (base.endsWithChar ('/')) base = base.dropLastCharacters (1);
    return base;
}

juce::String deviceLabel()
{
    auto name = juce::SystemStats::getComputerName().trim();
    return name.isNotEmpty() ? name.substring (0, 100) : juce::String ("whysho SY1");
}

juce::String jsonOf (std::initializer_list<std::pair<const char*, juce::String>> fields)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    for (auto& f : fields) o->setProperty (f.first, f.second);
    return juce::JSON::toString (juce::var (o.get()), true);
}

// Short, friendly text for everything the API (or the network) can answer.
juce::String friendlyError (const http::Response& r)
{
    if (! r.reached())
        return "Can't reach whysho.com. Check your connection and try again.";

    const auto code = r.field ("error");
    if (code == "invalid_credentials")   return "Wrong email or password.";
    if (code == "use_browser_login")     return "This account signs in with Google or Apple. Use \"Sign in with browser\".";
    if (code == "too_many_attempts")     return "Too many attempts. Wait a few minutes and try again.";
    if (code == "authorization_pending") return "Waiting for you to approve the code in your browser.";
    if (code == "expired_token")         return "That code has expired. Start again.";
    if (code == "invalid_token")         return "Your sign-in is no longer valid. Please sign in again.";
    if (code == "unknown_product")       return "SY1 isn't listed on whysho.com yet. Try again later.";
    if (code == "invalid_request")       return "Enter your email and password.";

    if (r.status >= 500) return "whysho.com is having trouble right now. Try again later.";
    return "Unexpected answer from whysho.com (HTTP " + juce::String (r.status) + "). Try again later.";
}

// --- machine-bound sealing of the session file -----------------------------
juce::MemoryBlock sha256 (const juce::MemoryBlock& in)
{
    return juce::SHA256 (in.getData(), in.getSize()).getRawData();
}

juce::MemoryBlock sha256 (const juce::String& in)
{
    return juce::SHA256 (in.toRawUTF8(), in.getNumBytesAsUTF8()).getRawData();
}

juce::String hmacSha256Hex (const juce::MemoryBlock& key, const juce::String& message)
{
    juce::MemoryBlock inner (64, true), outer (64, true);
    inner.copyFrom (key.getData(), 0, juce::jmin ((size_t) 64, key.getSize()));
    outer.copyFrom (key.getData(), 0, juce::jmin ((size_t) 64, key.getSize()));
    for (size_t i = 0; i < 64; ++i) { inner[i] = (char) (inner[i] ^ 0x36); outer[i] = (char) (outer[i] ^ 0x5c); }

    inner.append (message.toRawUTF8(), message.getNumBytesAsUTF8());
    outer.append (sha256 (inner).getData(), 32);
    return juce::String::toHexString (sha256 (outer).getData(), 32, 0);
}

// XOR with a SHA-256 counter keystream derived from the machine id + a nonce.
// Keeps the token out of plain sight and useless on another computer; it is
// obfuscation, not a vault (the key material is on the machine by necessity).
void applyKeystream (juce::MemoryBlock& data, const juce::String& machine, const juce::String& nonce)
{
    const juce::String seed = machine + "|" + WSHO_JS ("wsho-acct-seal-v1") + "|" + nonce + "|";
    for (size_t offset = 0, block = 0; offset < data.getSize(); offset += 32, ++block)
    {
        const auto ks = sha256 (seed + juce::String ((juce::int64) block));
        for (size_t i = 0; i < 32 && offset + i < data.getSize(); ++i)
            data[offset + i] = (char) (data[offset + i] ^ ks[i]);
    }
}

juce::MemoryBlock macKey (const juce::String& machine)
{
    return sha256 (machine + "|" + WSHO_JS ("wsho-acct-hmac-v1"));
}

juce::String seal (const juce::String& plain, const juce::String& machine)
{
    juce::uint8 raw[16];
    juce::Random rng;
    rng.setSeedRandomly();
    for (auto& b : raw) b = (juce::uint8) rng.nextInt (256);
    const auto nonce = juce::String::toHexString (raw, 16, 0);

    juce::MemoryBlock data (plain.toRawUTF8(), plain.getNumBytesAsUTF8());
    applyKeystream (data, machine, nonce);
    const auto body = base64UrlEncode (data);
    return "1\n" + nonce + "\n" + body + "\n" + hmacSha256Hex (macKey (machine), "1|" + nonce + "|" + body) + "\n";
}

bool unseal (const juce::String& text, const juce::String& machine, juce::String& plain)
{
    const auto lines = juce::StringArray::fromLines (text.trim());
    if (lines.size() != 4 || lines[0] != "1") return false;
    if (lines[3] != hmacSha256Hex (macKey (machine), "1|" + lines[1] + "|" + lines[2])) return false;

    auto data = base64UrlDecode (lines[2]);
    applyKeystream (data, machine, lines[1]);
    plain = juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize());
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
//  Process-wide session state + the background re-check.
// ---------------------------------------------------------------------------
struct AccountClient::Shared
{
    struct Session
    {
        juce::String token, email;
        bool owned = false;
        juce::int64 checkedAt = 0;   // last successful license check (0 = never)
        juce::int64 lastSeen = 0;    // latest clock reading we trusted (rollback guard)
    };

    static std::shared_ptr<Shared> get()
    {
        static std::mutex m;
        static std::weak_ptr<Shared> instance;
        const std::lock_guard<std::mutex> g (m);
        auto s = instance.lock();
        if (s == nullptr) { s = std::make_shared<Shared>(); instance = s; }
        return s;
    }

    Shared()
        : machine (getMachineId()),
          file (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile ("Whysho").getChildFile ("SY1").getChildFile (".wsacct"))
    {
        loadLocked();

        // Remember that time moved on, so winding the clock back later
        // doesn't stretch the offline grace window.
        if (session.token.isNotEmpty() && nowSecs() > session.lastSeen + kDay)
            saveLocked();

        worker = std::thread ([this] { backgroundLoop(); });
    }

    ~Shared()
    {
        { const std::lock_guard<std::mutex> g (wakeLock); quitting = true; }
        backgroundCanceller.beginAbort();
        wake.notify_all();
        if (worker.joinable()) worker.join();
    }

    // --- persistence (call with `lock` held) -------------------------------
    void loadLocked()
    {
        session = {};
        note.clear();
        stamp = file.getLastModificationTime();
        if (! file.existsAsFile()) return;

        juce::String plain;
        juce::var v;
        if (unseal (file.loadFileAsString(), machine, plain)) v = juce::JSON::parse (plain);

        if (! v.isObject() || v.getProperty ("p", "").toString() != productSlug()
            || v.getProperty ("t", "").toString().isEmpty())
        {
            // Hand-edited, corrupt, or copied from another computer.
            note = "The saved sign-in isn't valid on this computer. Please sign in again.";
            return;
        }

        session.token     = v.getProperty ("t", "").toString();
        session.email     = v.getProperty ("e", "").toString();
        session.owned     = (bool) v.getProperty ("o", false);
        session.checkedAt = (juce::int64) (double) v.getProperty ("c", 0.0);
        session.lastSeen  = (juce::int64) (double) v.getProperty ("s", 0.0);
    }

    void saveLocked()
    {
        if (session.token.isEmpty())
        {
            file.deleteFile();
        }
        else
        {
            session.lastSeen = juce::jmax (session.lastSeen, nowSecs());

            juce::DynamicObject::Ptr o = new juce::DynamicObject();
            o->setProperty ("p", productSlug());
            o->setProperty ("t", session.token);
            o->setProperty ("e", session.email);
            o->setProperty ("o", session.owned);
            o->setProperty ("c", (double) session.checkedAt);
            o->setProperty ("s", (double) session.lastSeen);

            file.getParentDirectory().createDirectory();
            file.replaceWithText (seal (juce::JSON::toString (juce::var (o.get()), true), machine),
                                  false, false, "\n");
        }
        stamp = file.getLastModificationTime();
    }

    // Another plugin process (or a sandboxed host's second instance) may have
    // signed in or out: pick that up, but stat the file at most every 2 s.
    void syncFromDiskLocked()
    {
        const auto ms = juce::Time::getMillisecondCounter();
        if (lastSyncMs != 0 && ms - lastSyncMs < 2000) return;
        lastSyncMs = ms;
        if (file.getLastModificationTime() != stamp) loadLocked();
    }

    // The clock reading used for the grace window: never earlier than the
    // latest time we have already seen.
    juce::int64 trustedNowLocked() const { return juce::jmax (nowSecs(), session.lastSeen); }

    // --- license check ------------------------------------------------------
    Result checkLicense (http::Canceller* canceller)
    {
        juce::String token;
        {
            const std::lock_guard<std::mutex> g (lock);
            syncFromDiskLocked();
            token = session.token;
            lastAttempt = nowSecs();
        }
        if (token.isEmpty()) return { false, "Sign in first." };

        const auto r = http::request ("GET", serverBase() + WSHO_JS ("/api/plugin/license?product=") + productSlug(),
                                      {}, token, canceller);

        const std::lock_guard<std::mutex> g (lock);
        if (session.token != token) return { false, "Signed out." };   // logged out while we were asking

        if (r.status == 200 && r.json.isObject() && r.json.hasProperty ("owned")
            && r.field ("product") == productSlug())
        {
            session.owned = (bool) r.json.getProperty ("owned", false);
            session.checkedAt = nowSecs();
            // The server just answered, so the present is trustworthy again
            // (heals a machine whose clock was once set far in the future).
            session.lastSeen = session.checkedAt;
            if (const auto e = r.field ("email"); e.isNotEmpty()) session.email = e;
            saveLocked();
            return { true, {} };
        }

        if (r.status == 401)
        {
            // The token was revoked or ran out: the account no longer vouches for this machine.
            session = {};
            saveLocked();
            return { false, friendlyError (r) };
        }

        if (r.status == 200) return { false, "Unexpected answer from whysho.com. Try again later." };
        return { false, friendlyError (r) };
    }

    void backgroundLoop()
    {
        std::unique_lock<std::mutex> l (wakeLock);
        // A short pause first: plugin scanners create and destroy instances
        // within moments and shouldn't cause any network traffic.
        wake.wait_for (l, std::chrono::seconds (5), [this] { return quitting; });

        while (! quitting)
        {
            l.unlock();
            bool due = false;
            {
                const std::lock_guard<std::mutex> g (lock);
                syncFromDiskLocked();
                const auto now = nowSecs();
                due = session.token.isNotEmpty()
                      && std::abs (now - session.checkedAt) > kRecheckAfter
                      && std::abs (now - lastAttempt) > kRecheckRetry;
            }
            if (due) checkLicense (&backgroundCanceller);
            l.lock();
            wake.wait_for (l, std::chrono::minutes (10), [this] { return quitting; });
        }
    }

    std::mutex lock;                 // guards everything below up to `lastAttempt`
    Session session;
    juce::String note;               // why a saved session was rejected, if it was
    const juce::String machine;
    const juce::File file;
    juce::Time stamp;
    juce::uint32 lastSyncMs = 0;
    juce::int64 lastAttempt = 0;

    http::Canceller backgroundCanceller;
    std::mutex wakeLock;
    std::condition_variable wake;
    bool quitting = false;
    std::thread worker;
};

// ---------------------------------------------------------------------------
AccountClient::AccountClient()
    : shared (Shared::get()), canceller (std::make_shared<http::Canceller>()) {}

AccountClient::~AccountClient() = default;

// Copies share the session but cancel independently.
AccountClient::AccountClient (const AccountClient& other)
    : shared (other.shared), canceller (std::make_shared<http::Canceller>()) {}

AccountClient& AccountClient::operator= (const AccountClient& other)
{
    shared = other.shared;
    return *this;
}

void AccountClient::beginAbort() { canceller->beginAbort(); }
void AccountClient::endAbort()   { canceller->endAbort(); }

juce::File AccountClient::sessionFile() const { return shared->file; }

AccountClient::Result AccountClient::login (const juce::String& emailIn, const juce::String& password)
{
    const auto mail = emailIn.trim();
    if (mail.isEmpty() || password.isEmpty()) return { false, "Enter your email and password." };

    const auto r = http::request ("POST", serverBase() + WSHO_JS ("/api/plugin/login"),
                                  jsonOf ({ { "email", mail }, { "password", password }, { "device", deviceLabel() } }),
                                  {}, canceller.get());

    const auto token = r.field ("token");
    if (r.status != 200 || token.isEmpty())
        return { false, r.status == 200 ? juce::String ("Unexpected answer from whysho.com. Try again later.")
                                        : friendlyError (r) };

    const std::lock_guard<std::mutex> g (shared->lock);
    shared->session = {};
    shared->session.token = token;
    shared->session.email = r.field ("email").isNotEmpty() ? r.field ("email") : mail;
    shared->saveLocked();
    return { true, {} };
}

AccountClient::BrowserLogin AccountClient::startBrowserLogin()
{
    BrowserLogin b;
    const auto base = serverBase();
    const auto r = http::request ("POST", base + WSHO_JS ("/api/plugin/login/start"),
                                  jsonOf ({ { "device", deviceLabel() } }), {}, canceller.get());

    if (r.status != 200) { b.error = friendlyError (r); return b; }

    b.deviceCode      = r.field ("device_code");
    b.userCode        = r.field ("user_code");
    b.verificationUrl = r.field ("verification_url");

    // The URL is handed to the system browser, so only ever accept a web link
    // (plain http only when a developer pointed the plugin at a local site).
    const bool webLink = b.verificationUrl.startsWithIgnoreCase ("https://")
                         || (base.startsWithIgnoreCase ("http://") && b.verificationUrl.startsWithIgnoreCase ("http://"));

    if (b.deviceCode.isEmpty() || b.userCode.isEmpty() || ! webLink)
    {
        b = {};
        b.error = "Unexpected answer from whysho.com. Try again later.";
        return b;
    }

    b.intervalSeconds  = juce::jlimit (2, 30,   (int) r.json.getProperty ("interval", 5));
    b.expiresInSeconds = juce::jlimit (30, 1800, (int) r.json.getProperty ("expires_in", 600));
    b.ok = true;
    return b;
}

AccountClient::Poll AccountClient::pollBrowserLogin (const juce::String& deviceCode)
{
    if (deviceCode.isEmpty()) return Poll::Error;

    const auto r = http::request ("POST", serverBase() + WSHO_JS ("/api/plugin/login/poll"),
                                  jsonOf ({ { "device_code", deviceCode } }), {}, canceller.get());

    const auto code = r.field ("error");
    if (r.status == 400 && code == "authorization_pending") return Poll::Pending;
    if (r.status == 400 && code == "expired_token")         return Poll::Expired;

    const auto token = r.field ("token");
    if (r.status != 200 || token.isEmpty()) return Poll::Error;   // transient: the caller may keep polling

    const std::lock_guard<std::mutex> g (shared->lock);
    shared->session = {};
    shared->session.token = token;
    shared->session.email = r.field ("email");
    shared->saveLocked();
    return Poll::Approved;
}

AccountClient::Result AccountClient::checkLicense() { return shared->checkLicense (canceller.get()); }

void AccountClient::logout()
{
    juce::String token;
    {
        // Forget locally first so signing out works offline too.
        const std::lock_guard<std::mutex> g (shared->lock);
        token = shared->session.token;
        shared->session = {};
        shared->note.clear();
        shared->saveLocked();
    }
    if (token.isNotEmpty())   // best effort: the server-side token also expires on its own
        http::request ("POST", serverBase() + WSHO_JS ("/api/plugin/logout"), {}, token, canceller.get());
}

bool AccountClient::isSignedIn() const
{
    const std::lock_guard<std::mutex> g (shared->lock);
    shared->syncFromDiskLocked();
    return shared->session.token.isNotEmpty();
}

juce::String AccountClient::email() const
{
    const std::lock_guard<std::mutex> g (shared->lock);
    shared->syncFromDiskLocked();
    return shared->session.email;
}

juce::int64 AccountClient::lastCheckedAt() const
{
    const std::lock_guard<std::mutex> g (shared->lock);
    return shared->session.checkedAt;
}

LicenseInfo AccountClient::cachedLicense() const
{
    const std::lock_guard<std::mutex> g (shared->lock);
    shared->syncFromDiskLocked();
    const auto& s = shared->session;

    LicenseInfo out;
    if (s.token.isEmpty())
    {
        out.reason = shared->note.isNotEmpty() ? shared->note : juce::String ("Not signed in.");
        return out;
    }

    out.email = s.email;
    out.edition = "account";

    if (s.checkedAt == 0) { out.reason = "Signed in, but ownership hasn't been checked yet."; return out; }
    if (! s.owned)        { out.reason = "Signed in, but this account doesn't own SY1."; return out; }

    out.issuedAt  = s.checkedAt;
    out.expiresAt = s.checkedAt + kGraceSeconds;

    // Too old, or dated in the future (the clock was wound back after the check).
    const auto now = shared->trustedNowLocked();
    if (now > out.expiresAt || s.checkedAt > now + kClockSlack)
    {
        out.state = LicenseState::Expired;
        out.reason = "Couldn't confirm your account for " + juce::String (kOfflineGraceDays)
                     + " days. Go online and refresh to unlock SY1.";
        return out;
    }

    out.state = LicenseState::Licensed;
    out.features = juce::StringArray { "synth" };
    out.reason = "OK";
    return out;
}

// ---------------------------------------------------------------------------
LicenseInfo resolveLicense (LicenseManager& keys, const AccountClient& account)
{
    // Key token first; refresh() itself falls back to the local trial.
    auto info = keys.refresh();
    if (info.state == LicenseState::Licensed) return info;

    const auto acct = account.cachedLicense();
    if (acct.state == LicenseState::Licensed) return acct;

    // Neither unlocks: report the trial state, and when the trial is no use
    // either, say what the account side found so the UI can explain it.
    if (! info.isUsable() && account.isSignedIn())
    {
        info.email = acct.email;
        info.reason = (info.reason.isNotEmpty() ? info.reason + " " : juce::String()) + acct.reason;
    }
    return info;
}

} // namespace wsho::lic
