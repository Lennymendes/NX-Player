// NX Player — MIT License
#include "RemoteServer.h"
#include "../Core/Text.h"
#include "NXPlayerData.h"
#include <juce_events/juce_events.h>

namespace nx
{

RemoteServer::RemoteServer() : juce::Thread ("NX Remote")
{
    webApp = juce::String::fromUTF8 (NXData::index_html, NXData::index_htmlSize);
    regenerateToken();
}

RemoteServer::~RemoteServer() { stop(); }

void RemoteServer::regenerateToken()
{
    auto& r = juce::Random::getSystemRandom();
    juce::String t;
    for (int i = 0; i < 4; ++i) t << juce::String::toHexString ((juce::int64) (juce::uint32) r.nextInt()).paddedLeft ('0', 8);
    { std::lock_guard<std::mutex> l (lock); token = t; }
    if (onTokenChanged) onTokenChanged();
}

void RemoteServer::setToken (const juce::String& saved)
{
    if (saved.length() < 16 || ! saved.containsOnly ("0123456789abcdefABCDEF")) return;
    std::lock_guard<std::mutex> l (lock);
    token = saved;
}

juce::String RemoteServer::getToken() const { std::lock_guard<std::mutex> l (lock); return token; }

void RemoteServer::setState (const juce::String& json) { std::lock_guard<std::mutex> l (lock); state = json; }
juce::String RemoteServer::getState() const { std::lock_guard<std::mutex> l (lock); return state; }

juce::String RemoteServer::localIPv4()
{
    juce::String fallback;
    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        const auto s = a.toString();
        if (s.startsWith ("127.") || s.startsWith ("169.254.") || s == "0.0.0.0") continue;
        if (s.startsWith ("192.168.") || s.startsWith ("10.") || s.startsWith ("172.")) return s;
        if (fallback.isEmpty()) fallback = s;
    }
    return fallback.isNotEmpty() ? fallback : juce::String ("127.0.0.1");
}

juce::String RemoteServer::getUrl() const
{
    return "http://" + localIPv4() + ":" + juce::String (boundPort.load()) + "/?k=" + getToken();
}

bool RemoteServer::start (int port, juce::String& error)
{
    if (running.load()) return true;
    listener = std::make_unique<juce::StreamingSocket>();
    bool ok = false;
    if (port > 0) ok = listener->createListener (port);
    if (! ok)
    {
        if (port > 0) listener = std::make_unique<juce::StreamingSocket>();
        for (int p = 8765; p < 8775 && ! ok; ++p)
        {
            ok = listener->createListener (p);
            if (! ok) listener = std::make_unique<juce::StreamingSocket>();
        }
        if (! ok) ok = listener->createListener (0);
    }
    if (! ok)
    {
        error = "NX Remote : impossible d'ouvrir un port réseau"_t;
        listener.reset();
        return false;
    }
    boundPort = listener->getBoundPort();
    running = true;
    startThread();
    return true;
}

void RemoteServer::stop()
{
    if (! running.exchange (false)) return;
    signalThreadShouldExit();
    if (listener != nullptr) listener->close();
    stopThread (3000);
    listener.reset();
    boundPort = 0;
}

void RemoteServer::run()
{
    while (! threadShouldExit() && listener != nullptr)
    {
        std::unique_ptr<juce::StreamingSocket> client (listener->waitForNextConnection());
        if (client == nullptr) { if (threadShouldExit()) break; juce::Thread::sleep (5); continue; }
        if (threadShouldExit()) break;
        handle (*client);
        client->close();
    }
}

void RemoteServer::send (juce::StreamingSocket& s, int status, const juce::String& mime, const void* data, size_t size, const char* cacheControl)
{
    const char* text = status == 200 ? "OK" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : "Bad Request";
    juce::String head;
    head << "HTTP/1.1 " << status << " " << text << "\r\n"
         << "Content-Type: " << mime << "\r\n"
         << "Content-Length: " << (juce::int64) size << "\r\n"
         << "Cache-Control: " << cacheControl << "\r\n"
         << "Connection: close\r\n\r\n";
    s.write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
    if (size > 0) s.write (data, (int) size);
}

void RemoteServer::handle (juce::StreamingSocket& s)
{
    // Read the request (headers, then the body announced by Content-Length).
    juce::MemoryBlock buf;
    char tmp[4096];
    int headerEnd = -1;
    while (headerEnd < 0 && buf.getSize() < 32768)
    {
        if (s.waitUntilReady (true, 3000) != 1) return;
        const int n = s.read (tmp, (int) sizeof (tmp), false);
        if (n <= 0) return;
        buf.append (tmp, (size_t) n);
        const auto str = juce::String::fromUTF8 ((const char*) buf.getData(), (int) buf.getSize());
        headerEnd = str.indexOf ("\r\n\r\n");
    }
    if (headerEnd < 0) return;
    const auto all = juce::String::fromUTF8 ((const char*) buf.getData(), (int) buf.getSize());
    const auto head = all.substring (0, headerEnd);
    auto body = all.substring (headerEnd + 4);
    const auto lines = juce::StringArray::fromLines (head);
    const auto requestLine = juce::StringArray::fromTokens (lines[0], " ", {});
    const auto method = requestLine[0];
    const auto target = requestLine[1];
    int contentLength = 0;
    for (auto& l : lines) if (l.startsWithIgnoreCase ("content-length:")) contentLength = l.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
    while (body.getNumBytesAsUTF8() < (size_t) juce::jlimit (0, 65536, contentLength))
    {
        if (s.waitUntilReady (true, 2000) != 1) break;
        const int n = s.read (tmp, (int) sizeof (tmp), false);
        if (n <= 0) break;
        body += juce::String::fromUTF8 (tmp, n);
    }
    ++requests;

    // Parameters: query string + form body
    juce::StringPairArray params;
    auto addPairs = [&params] (const juce::String& q)
    {
        for (auto& pair : juce::StringArray::fromTokens (q, "&", {}))
        {
            if (pair.isEmpty()) continue;
            const auto k = juce::URL::removeEscapeChars (pair.upToFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' '));
            const auto v = juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false).replaceCharacter ('+', ' '));
            params.set (k, v);
        }
    };
    const auto path = target.upToFirstOccurrenceOf ("?", false, false);
    addPairs (target.fromFirstOccurrenceOf ("?", false, false));
    if (method == "POST" && ! body.trimStart().startsWith ("{")) addPairs (body.trim());

    auto sendText = [&s] (int status, const juce::String& mime, const juce::String& text)
    {
        send (s, status, mime, text.toRawUTF8(), text.getNumBytesAsUTF8());
    };

    if (path == "/" || path == "/index.html")
    {
        sendText (200, "text/html; charset=utf-8", webApp);
        return;
    }
    if (path == "/manifest.json")
    {
        sendText (200, "application/manifest+json", R"({"name":"NX Remote","short_name":"NX Remote","display":"standalone","background_color":"#0A0C0E","theme_color":"#0A0C0E","start_url":"/"})");
        return;
    }
    if (! path.startsWith ("/api/")) { sendText (404, "text/plain", "not found"); return; }

    // Token check (constant time)
    const auto expected = getToken();
    const auto given = params["k"];
    int diff = (int) (given.length() ^ expected.length());
    for (int i = 0; i < juce::jmin (given.length(), expected.length()); ++i) diff |= (int) (given[i] ^ expected[i]);
    if (diff != 0 || expected.isEmpty()) { sendText (403, "application/json", R"({"error":"token"})"); return; }

    if (path == "/api/state") { sendText (200, "application/json", getState()); return; }
    if (path == "/api/cmd")
    {
        const auto command = params["c"];
        if (command.isEmpty()) { sendText (400, "application/json", R"({"error":"c"})"); return; }
        if (onCommand)
        {
            auto fn = onCommand;
            juce::MessageManager::callAsync ([fn, command, params] { fn (command, params); });
        }
        sendText (200, "application/json", R"({"ok":true})");
        return;
    }
    if (path == "/api/library")
    {
        sendText (200, "application/json", onLibraryQuery ? onLibraryQuery (params) : juce::String ("{}"));
        return;
    }
    if (path == "/api/art")
    {
        juce::String mime = "image/jpeg";
        const auto data = getArtwork ? getArtwork (params["key"], juce::jlimit (48, 600, params["s"].isEmpty() ? 600 : params["s"].getIntValue()), mime) : juce::MemoryBlock();
        if (data.getSize() == 0) { sendText (404, "text/plain", "no artwork"); return; }
        // the page puts the track / cover in v=: a new picture is a new URL, so it can be cached for good
        send (s, 200, mime, data.getData(), data.getSize(), params["v"].isNotEmpty() ? "private, max-age=31536000, immutable" : "no-store");
        return;
    }
    sendText (404, "application/json", R"({"error":"route"})");
}

} // namespace nx
