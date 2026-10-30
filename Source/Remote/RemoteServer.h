// NX Player — MIT License
// NX Remote: a small HTTP server on the local network that serves a web app (phone / tablet) to control
// the player: now playing, transport, volume, A-B loop, device profiles, library. Started on demand only
// (settings or the remote button), stopped with the app. Every API call carries a random token (in the QR
// code's URL): a device that did not scan the code cannot drive the player.
//
//   GET  /                 the web app (Resources/remote/index.html)
//   GET  /api/state?k=     JSON snapshot (track, position, volume, loop, profiles...)
//   POST /api/cmd?k=&c=    play | pause | toggle | next | prev | seek&t= | volume&v= | loopA | loopB | loopClear
//                          | profile&i= | playAlbum&artist=&album= | playTrack&id=
//   GET  /api/library?k=&q=  albums (no q) or search results
//   GET  /api/art?k=&key=&s=&v=  cover as a baseline RGB JPEG, at most s x s (default 600); key = artwork key or "track:<id>"
//                          (v = cache-buster: the answer is cacheable because the URL changes with the picture)
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <mutex>

namespace nx
{

class RemoteServer : private juce::Thread
{
public:
    RemoteServer();
    ~RemoteServer() override;

    /** port > 0: that port if it is free, else (or port 0) the first free port of 8765…8774, else any. */
    bool start (int port, juce::String& error);
    void stop();
    bool isRunning() const noexcept { return running.load(); }
    int getPort() const noexcept { return boundPort.load(); }
    juce::String getToken() const;
    void regenerateToken();
    /** Restores a saved token (ignored when too short). */
    void setToken (const juce::String& saved);
    /** http://<LAN address>:<port>/?k=<token> — what the QR code holds. */
    juce::String getUrl() const;
    static juce::String localIPv4();
    int getRequestCount() const noexcept { return requests.load(); }

    /** Latest state (JSON), pushed by the app a few times per second while the server runs. */
    void setState (const juce::String& json);
    juce::String getState() const;

    // Hooks set by the app. onCommand runs on the message thread; the others on the server thread.
    std::function<void (const juce::String& command, const juce::StringPairArray& params)> onCommand;
    std::function<juce::String (const juce::StringPairArray& params)> onLibraryQuery;
    std::function<juce::MemoryBlock (const juce::String& key, int maxSize, juce::String& mime)> getArtwork;
    /** Called when the token changes (the app saves it: the QR code and the link stay the same between runs). */
    std::function<void()> onTokenChanged;
    juce::String webApp;     // index.html (defaults to the embedded one)

private:
    void run() override;
    void handle (juce::StreamingSocket& socket);
    static void send (juce::StreamingSocket& s, int status, const juce::String& mime, const void* data, size_t size, const char* cacheControl = "no-store");

    std::unique_ptr<juce::StreamingSocket> listener;
    std::atomic<bool> running { false };
    std::atomic<int> boundPort { 0 }, requests { 0 };
    mutable std::mutex lock;
    juce::String token, state { "{}" };
};

} // namespace nx
