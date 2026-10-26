// NX Player — MIT License
// Offline copies: downloads the original audio file of NAS / Jellyfin (UPnP) tracks in the background into
// the download folder (Artist / Album / NN Title.ext), then adds it to the library as a local track marked
// "hors-ligne". One download at a time, progress reported through change messages.
#pragma once

#include "Library.h"
#include <deque>
#include <map>
#include <mutex>

namespace nx
{

class DownloadManager : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    enum class State { None, Queued, Downloading, Done, Failed };

    explicit DownloadManager (Library& library);
    ~DownloadManager() override;

    void setFolder (const juce::File& folder);
    juce::File getFolder() const;

    static bool canDownload (const TrackInfo& t) { return t.source.startsWith ("upnp") && t.uri.startsWith ("http"); }
    /** Queues the tracks that can be downloaded and are not already available offline. Returns how many. */
    int enqueue (const std::vector<TrackInfo>& tracks);

    State getState (const TrackInfo& t) const;
    float getProgress (const TrackInfo& t) const;          // 0..1 while downloading
    int getPendingCount() const;                          // queued + current
    int getSessionTotal() const;                          // since the queue was last empty
    float getCurrentProgress() const;
    juce::String getCurrentTitle() const;

    /** Message thread: one line for the toast ("Téléchargé : …", "Échec : …"). */
    std::function<void (const juce::String&)> onMessage;
    /** Blocks until the queue is empty (tests). */
    bool waitUntilIdle (int timeoutMs);

    // Helpers (tests)
    static juce::String extensionFor (const TrackInfo& t);
    static juce::File destinationFor (const juce::File& folder, const TrackInfo& t);

private:
    void run() override;
    bool downloadOne (const TrackInfo& t, juce::String& error);
    static juce::String key (const TrackInfo& t) { return t.source + "|" + t.externalId; }
    void post (const juce::String& message);

    Library& library;
    mutable std::mutex lock;
    juce::File folder;
    std::deque<TrackInfo> queue;
    std::map<juce::String, State> states;
    juce::String currentKey, currentTitle;
    float currentProgress = 0.0f;
    int sessionTotal = 0;
    juce::WaitableEvent wake;
    std::shared_ptr<int> alive = std::make_shared<int> (0);
};

} // namespace nx
