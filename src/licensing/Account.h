#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include "License.h"

namespace wsho::lic
{
namespace http { class Canceller; }

// whysho.com account sign-in (the website's /api/plugin/* JSON API).
// Licensing there is account-based: the plugin is unlocked while it is signed
// in to an account that owns the product. All network calls BLOCK — call them
// off the message thread. PUBLIC API IS A CONTRACT with the GUI/processor.
//
// The session (bearer token, email, last ownership answer) is shared by every
// AccountClient in the process and stored machine-bound on disk, so several
// plugin instances agree and a restart stays signed in. The accessors
// (isSignedIn / email / cachedLicense / lastCheckedAt) are thread-safe and
// never touch the network.
class AccountClient
{
public:
    struct Result { bool ok = false; juce::String error; };

    struct BrowserLogin
    {
        bool ok = false;
        juce::String deviceCode, userCode, verificationUrl, error;
        int intervalSeconds = 5, expiresInSeconds = 600;
    };
    enum class Poll { Pending, Approved, Expired, Error };

    // How long a successful "owned" answer keeps the plugin unlocked without
    // reaching whysho.com again (the token itself lasts a year server-side).
    static constexpr int kOfflineGraceDays = 14;

    AccountClient();
    ~AccountClient();
    AccountClient (const AccountClient&);
    AccountClient& operator= (const AccountClient&);

    Result       login (const juce::String& email, const juce::String& password);  // POST /api/plugin/login
    BrowserLogin startBrowserLogin();                                              // POST /api/plugin/login/start
    Poll         pollBrowserLogin (const juce::String& deviceCode);                // POST /api/plugin/login/poll

    // GET /api/plugin/license?product=sy1 — refreshes the cached ownership.
    // ok means the server answered (owned or not: read cachedLicense()); a
    // failure leaves the cache as it was, except invalid_token which signs out.
    Result       checkLicense();
    void         logout();           // POST /api/plugin/logout + forget the local session

    bool         isSignedIn() const;
    juce::String email() const;

    // Offline view of the last successful checkLicense(): Licensed while the
    // account owned the product and the check is within the offline grace
    // window; otherwise Unlicensed / Expired with a reason.
    LicenseInfo  cachedLicense() const;

    // Unix seconds of the last successful checkLicense(), 0 = never.
    juce::int64  lastCheckedAt() const;

    // Cut short this client's in-flight requests and make new ones fail fast
    // until endAbort(). Lets an owner join its worker threads without waiting
    // for a network timeout. Calls nest.
    void beginAbort();
    void endAbort();

    juce::File sessionFile() const;

private:
    struct Shared;
    std::shared_ptr<Shared> shared;
    std::shared_ptr<http::Canceller> canceller;
};

// The one place that decides the effective license: signed license-key token,
// else signed-in account that owns the product, else running trial.
LicenseInfo resolveLicense (LicenseManager& keys, const AccountClient& account);

} // namespace wsho::lic
