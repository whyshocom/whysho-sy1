#include "Account.h"
// STUB — replaced by the real account client.
namespace wsho::lic
{
AccountClient::AccountClient() = default;
AccountClient::Result AccountClient::login (const juce::String&, const juce::String&) { return {}; }
AccountClient::BrowserLogin AccountClient::startBrowserLogin() { return {}; }
AccountClient::Poll AccountClient::pollBrowserLogin (const juce::String&) { return Poll::Error; }
AccountClient::Result AccountClient::checkLicense() { return {}; }
void AccountClient::logout() {}
bool AccountClient::isSignedIn() const { return false; }
juce::String AccountClient::email() const { return {}; }
LicenseInfo AccountClient::cachedLicense() const { return {}; }
LicenseInfo resolveLicense (LicenseManager& keys, const AccountClient&) { return keys.refresh(); }
}
