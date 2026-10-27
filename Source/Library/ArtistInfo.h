// NX Player — MIT License
// Artist pages: photo, banner and an up-to-date biography from open services, cached in the library.
//   1. TheAudioDB (free public key) : photo, banner / fanart, biography (French when available)
//   2. Wikipedia (REST summary)     : biography and photo when TheAudioDB has nothing
// Requests run on one background thread, one at a time, throttled (≈ 1 per second). Results are kept for
// 30 days; "not found" answers too, so that a library of 2 000 artists never hammers the services.
#pragma once

#include "Library.h"
#include <juce_events/juce_events.h>
#include <deque>
#include <mutex>

namespace nx
{

class ArtistInfoService : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    explicit ArtistInfoService (Library& library);
    ~ArtistInfoService() override;

    /** Cached information (may be stale or empty). Queues a fetch when missing / older than 30 days. */
    std::optional<ArtistMeta> get (const juce::String& artist, bool fetchIfMissing = true);
    /** Queues fetches for several artists (artists page). */
    void prefetch (const juce::StringArray& artists);
    /** Blocking fetch, cache updated (tests, worker thread). */
    ArtistMeta fetchNow (const juce::String& artist);

    void setEnabled (bool on) { enabled = on; }
    bool isEnabled() const noexcept { return enabled.load(); }
    int getPendingCount() const;

    // Services (overridden by the tests with a local server). {lang} is replaced by the language.
    juce::String audioDbBase { "https://www.theaudiodb.com/api/v1/json/2" };
    juce::String wikipediaBase { "https://{lang}.wikipedia.org/api/rest_v1/page/summary/" };
    juce::String language { "fr" };
    int throttleMs = 1100;

    static ArtistMeta parseAudioDb (const juce::var& json, const juce::String& artist, const juce::String& language);
    static void mergeWikipedia (ArtistMeta& meta, const juce::var& json);
    /** Biography cleaned for display (paragraphs kept, at most maxChars, cut at a sentence). */
    static juce::String tidyBio (const juce::String& text, int maxChars = 1600);

private:
    void run() override;

    Library& library;
    std::atomic<bool> enabled { true };
    mutable std::mutex lock;
    std::deque<juce::String> queue;
    juce::StringArray queued;
    juce::WaitableEvent wake;
};

} // namespace nx
