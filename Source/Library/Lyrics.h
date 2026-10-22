// NX Player — MIT License
// Time-synced lyrics: LRC parser (multiple time tags per line, [offset:], enhanced <mm:ss.xx> word tags
// stripped), local sources (sidecar .lrc, embedded tags) and the open LRCLIB service (opt-in).
#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace nx
{

struct TrackInfo;

struct LyricLine
{
    double time = 0.0;      // seconds
    juce::String text;
};

struct Lyrics
{
    bool synced = false;
    std::vector<LyricLine> lines;   // sorted by time when synced
    juce::String plain;
    juce::String source;            // "Fichier .lrc", "Tags", "LRCLIB"

    bool isEmpty() const noexcept { return lines.empty() && plain.isEmpty(); }
    /** Index of the line being sung at time t (-1 before the first line). */
    int indexAt (double t) const noexcept;
};

Lyrics parseLrc (const juce::String& text);
Lyrics loadLocalLyrics (const TrackInfo& track);

/** https://lrclib.net — free, no key. Blocking: call from a background thread. */
std::optional<Lyrics> fetchLrclib (const juce::String& artist, const juce::String& title,
                                   const juce::String& album, double durationSeconds);

} // namespace nx
