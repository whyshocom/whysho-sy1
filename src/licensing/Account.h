#pragma once
#include <juce_core/juce_core.h>
#include "License.h"

namespace wsho::lic
{
// whysho.com account sign-in (the website's /api/plugin/* JSON API).
// Licensing there is account-based: the plugin is unlocked while it is signed
// in to an account that owns the product. All network calls BLOCK — call them
// off the message thread. PUBLIC API IS A CONTRACT with the GUI/processor.
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

    AccountClient();

    Result       login (const juce::String& email, const juce::String& password);  // POST /api/plugin/login
    BrowserLogin startBrowserLogin();                                              // POST /api/plugin/login/start
    Poll         pollBrowserLogin (const juce::String& deviceCode);                // POST /api/plugin/login/poll
    Result       checkLicense();     // GET /api/plugin/license?product=sy1 — refreshes the cached ownership
    void         logout();           // POST /api/plugin/logout + forget the local session

    bool         isSignedIn() const;
    juce::String email() const;

    // Offline view of the last successful checkLicense(): Licensed while the
    // account owned the product and the check is within the offline grace
    // window; otherwise Unlicensed with a reason.
    LicenseInfo  cachedLicense() const;
};

// The one place that decides the effective license: signed license-key token,
// else signed-in account that owns the product, else running trial.
LicenseInfo resolveLicense (LicenseManager& keys, const AccountClient& account);

} // namespace wsho::lic
