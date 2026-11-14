#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <vector>

// Small blocking JSON-over-HTTP helper shared by the licensing clients.
// Requests are cancellable from another thread, so a dialog or plugin being
// destroyed never has to wait out a network timeout before joining a worker.
namespace wsho::lic::http
{
class Canceller
{
public:
    // While aborting, in-flight requests using this canceller are cut short
    // and new ones fail immediately. Calls nest (begin/end must balance).
    void beginAbort();
    void endAbort();
    bool aborting() const noexcept { return depth.load() > 0; }

private:
    friend struct ScopedStream;
    std::mutex lock;
    std::vector<juce::WebInputStream*> live;
    std::atomic<int> depth { 0 };
};

struct Response
{
    int status = -1;             // HTTP status, or -1 when the server was not reached
    juce::String body;
    juce::var json;              // parsed body when it is a JSON object, else void

    bool reached() const noexcept { return status > 0; }
    juce::String field (const char* name) const
    {
        return json.isObject() ? json.getProperty (name, juce::var()).toString() : juce::String();
    }
};

// method is "GET" or "POST". jsonBody is sent as application/json for POST.
// bearer (optional) is sent as "Authorization: Bearer <token>".
Response request (const juce::String& method, const juce::String& url,
                  const juce::String& jsonBody, const juce::String& bearer,
                  Canceller* canceller, int timeoutMs = 8000);

} // namespace wsho::lic::http
