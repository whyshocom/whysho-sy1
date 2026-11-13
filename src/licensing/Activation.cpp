#include "Activation.h"
#include "Http.h"
#include "MachineId.h"
#include "ObfString.h"

namespace wsho::lic
{
juce::String ActivationClient::serverBase() const
{
    if (serverOverride.isNotEmpty()) return serverOverride;
    // Obfuscated at rest; override via WHYSHO_LICENSE_SERVER env for self-host.
    if (auto env = juce::SystemStats::getEnvironmentVariable ("WHYSHO_LICENSE_SERVER", {}); env.isNotEmpty())
        return env;
    return WSHO_JS ("https://license.whysho.com");
}

ActivationClient::ActivationClient() : canceller (std::make_shared<http::Canceller>()) {}

void ActivationClient::beginAbort() { canceller->beginAbort(); }
void ActivationClient::endAbort()   { canceller->endAbort(); }

static juce::var postJson (const juce::String& url, const juce::var& body, int& statusCode, http::Canceller* canceller)
{
    const auto r = http::request ("POST", url, juce::JSON::toString (body, true), {}, canceller, 6000);
    statusCode = r.status;   // -1 when the server could not be reached
    return r.json;
}

// The server answers errors as { error: <code>, message: <text> }.
static juce::String activationError (const juce::var& resp, int status)
{
    const auto code = resp.getProperty ("error", "").toString();
    if (code == "wrong-product") return "This license server doesn't issue SY1 licenses yet.";
    if (code == "unknown-key")   return "That license key wasn't found. Check it and try again.";
    if (code == "seat-limit")    return "This key is already active on its maximum number of computers.";
    if (code == "revoked")       return "This license has been revoked.";
    if (code == "expired")       return "This license has expired.";

    const auto message = resp.getProperty ("message", "").toString();
    if (message.isNotEmpty()) return message;
    return "Activation failed (HTTP " + juce::String (status) + ").";
}

ActivationClient::Result ActivationClient::activate (const juce::String& licenseKey)
{
    Result r;
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("licenseKey", licenseKey.trim());
    body->setProperty ("machineId", getMachineId());
    body->setProperty ("productId", juce::String (WSHO_JS ("whysho-sy1")));

    int status = 0;
    auto resp = postJson (serverBase() + WSHO_JS ("/api/activate"), juce::var (body.get()), status, canceller.get());

    if (status == -1) { r.error = "Could not reach the activation server. Check your connection."; return r; }
    if (status != 200)
    {
        r.error = activationError (resp, status);
        return r;
    }
    r.token = resp.getProperty ("token", "").toString();
    r.ok = r.token.isNotEmpty();
    if (! r.ok) r.error = "Server did not return a license token.";
    return r;
}

bool ActivationClient::deactivate (const juce::String& licenseKey)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("licenseKey", licenseKey.trim());
    body->setProperty ("machineId", getMachineId());
    int status = 0;
    postJson (serverBase() + WSHO_JS ("/api/deactivate"), juce::var (body.get()), status, canceller.get());
    return status == 200;
}

bool ActivationClient::validateOnline (const juce::String& token)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty ("token", token);
    int status = 0;
    auto resp = postJson (serverBase() + WSHO_JS ("/api/validate"), juce::var (body.get()), status, canceller.get());
    if (status != 200) return true; // network issue -> don't punish the user (offline grace)
    return (bool) resp.getProperty ("valid", true);
}

} // namespace wsho::lic
