#include "Http.h"
#include <algorithm>

namespace wsho::lic::http
{
void Canceller::beginAbort()
{
    const std::lock_guard<std::mutex> g (lock);
    ++depth;
    for (auto* s : live) s->cancel();
}

void Canceller::endAbort() { --depth; }

// Registers a stream with its canceller for the lifetime of one request.
struct ScopedStream
{
    ScopedStream (Canceller* c, juce::WebInputStream& s) : canceller (c), stream (s)
    {
        if (canceller == nullptr) return;
        const std::lock_guard<std::mutex> g (canceller->lock);
        canceller->live.push_back (&stream);
        // Checked under the same lock beginAbort() takes, so an abort can
        // never slip in between "may I start?" and "I am registered".
        refused = canceller->depth.load() > 0;
    }

    ~ScopedStream()
    {
        if (canceller == nullptr) return;
        const std::lock_guard<std::mutex> g (canceller->lock);
        auto& v = canceller->live;
        v.erase (std::remove (v.begin(), v.end(), &stream), v.end());
    }

    Canceller* canceller;
    juce::WebInputStream& stream;
    bool refused = false;
};

Response request (const juce::String& method, const juce::String& url,
                  const juce::String& jsonBody, const juce::String& bearer,
                  Canceller* canceller, int timeoutMs)
{
    Response r;
    const bool post = method == "POST";

    juce::URL u (url);
    if (post) u = u.withPOSTData (jsonBody.isNotEmpty() ? jsonBody : juce::String ("{}"));

    juce::String headers ("Accept: application/json");
    if (post)                 headers << "\r\nContent-Type: application/json";
    if (bearer.isNotEmpty())  headers << "\r\nAuthorization: Bearer " << bearer;

    juce::WebInputStream stream (u, post);
    stream.withExtraHeaders (headers)
          .withConnectionTimeout (timeoutMs)
          .withNumRedirectsToFollow (3);

    const ScopedStream scope (canceller, stream);
    if (scope.refused) return r;

    if (! stream.connect (nullptr)) return r;
    if (canceller != nullptr && canceller->aborting()) return r;

    const int status = stream.getStatusCode();
    if (status <= 0) return r;

    // Bounded read: these endpoints answer with a few hundred bytes, so a
    // huge or endless body is never worth buffering.
    juce::MemoryOutputStream out;
    out.writeFromInputStream (stream, 64 * 1024);

    r.status = status;
    r.body = out.toUTF8();
    auto parsed = juce::JSON::parse (r.body);
    if (parsed.isObject()) r.json = parsed;
    return r;
}

} // namespace wsho::lic::http
