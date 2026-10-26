// NX Player — MIT License
#include "Lyrics.h"
#include "Library.h"
#include "TagReader.h"
#include <algorithm>

namespace nx
{

int Lyrics::indexAt (double t) const noexcept
{
    if (! synced || lines.empty()) return -1;
    auto it = std::upper_bound (lines.begin(), lines.end(), t, [] (double v, const LyricLine& l) { return v < l.time; });
    return (int) (it - lines.begin()) - 1;
}

namespace
{
    /** "mm:ss.xx" / "mm:ss:xx" / "mm:ss" -> seconds, or -1. */
    double parseTimeTag (const juce::String& tag)
    {
        const auto mm = tag.upToFirstOccurrenceOf (":", false, false);
        auto rest = tag.fromFirstOccurrenceOf (":", false, false);
        if (mm.isEmpty() || ! mm.containsOnly ("0123456789") || rest.isEmpty()) return -1.0;
        rest = rest.replaceCharacter (':', '.');
        if (! rest.containsOnly ("0123456789.")) return -1.0;
        return mm.getIntValue() * 60.0 + rest.getDoubleValue();
    }

    juce::String stripWordTags (const juce::String& s)
    {
        juce::String out;
        int i = 0;
        while (i < s.length())
        {
            if (s[i] == '<')
            {
                const int close = s.indexOfChar (i, '>');
                if (close > i && parseTimeTag (s.substring (i + 1, close)) >= 0.0) { i = close + 1; continue; }
            }
            out << s[i];
            ++i;
        }
        return out.trim();
    }
}

Lyrics parseLrc (const juce::String& text)
{
    Lyrics l;
    double offset = 0.0;
    juce::StringArray plainLines;

    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        std::vector<double> times;
        while (line.startsWithChar ('['))
        {
            const int close = line.indexOfChar (']');
            if (close < 0) break;
            const auto tag = line.substring (1, close);
            const double t = parseTimeTag (tag);
            if (t >= 0.0) times.push_back (t);
            else if (tag.startsWithIgnoreCase ("offset:")) offset = tag.fromFirstOccurrenceOf (":", false, false).trim().getDoubleValue() / 1000.0;
            else if (times.empty()) { line = {}; break; }   // [ar:], [ti:]... metadata line
            line = line.substring (close + 1).trimStart();
        }
        const auto words = stripWordTags (line);
        if (times.empty()) { if (words.isNotEmpty()) plainLines.add (words); continue; }
        for (auto t : times) l.lines.push_back ({ juce::jmax (0.0, t - offset), words });
    }

    std::stable_sort (l.lines.begin(), l.lines.end(), [] (const LyricLine& a, const LyricLine& b) { return a.time < b.time; });
    l.synced = ! l.lines.empty();
    l.plain = l.synced ? juce::String() : plainLines.joinIntoString ("\n");
    return l;
}

Lyrics loadLocalLyrics (const TrackInfo& track)
{
    if (track.lyricsPath.isNotEmpty())
    {
        auto l = parseLrc (juce::File (track.lyricsPath).loadFileAsString());
        l.source = "Fichier .lrc"_t;
        if (! l.isEmpty()) return l;
    }
    if (track.source == "local")
    {
        juce::File f (track.externalId);
        auto lrc = f.withFileExtension ("lrc");
        if (lrc.existsAsFile())
        {
            auto l = parseLrc (lrc.loadFileAsString());
            l.source = "Fichier .lrc"_t;
            if (! l.isEmpty()) return l;
        }
        TagData tags;
        if (readTags (f, tags) && tags.get ("lyrics").isNotEmpty())
        {
            auto l = parseLrc (tags.get ("lyrics"));
            l.source = "Tags";
            return l;
        }
    }
    return {};
}

std::optional<Lyrics> fetchLrclib (const juce::String& artist, const juce::String& title, const juce::String& album, double duration)
{
    juce::URL url ("https://lrclib.net/api/get");
    url = url.withParameter ("artist_name", artist).withParameter ("track_name", title);
    if (album.isNotEmpty()) url = url.withParameter ("album_name", album);
    if (duration > 0) url = url.withParameter ("duration", juce::String ((int) std::lround (duration)));

    int status = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (6000)
                                             .withExtraHeaders ("User-Agent: NX Player/" NX_VERSION " (https://github.com/)")
                                             .withStatusCode (&status));
    if (stream == nullptr || status != 200) return std::nullopt;
    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    const auto synced = json.getProperty ("syncedLyrics", {}).toString();
    const auto plain = json.getProperty ("plainLyrics", {}).toString();
    Lyrics l = synced.isNotEmpty() ? parseLrc (synced) : parseLrc (plain);
    l.source = "LRCLIB";
    if (l.isEmpty()) return std::nullopt;
    return l;
}

namespace
{
    juce::String getText (const juce::URL& url, int& status)
    {
        status = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withConnectionTimeoutMs (6000)
                                                 .withExtraHeaders ("User-Agent: NX Player/" NX_VERSION " (https://github.com/Lennymendes/NX-Player)")
                                                 .withStatusCode (&status));
        return stream != nullptr ? stream->readEntireStreamAsString() : juce::String();
    }
}

juce::String cleanTitleForLyrics (const juce::String& title)
{
    auto t = title;
    for (auto open : { " (", " [", " - " })
    {
        const auto lower = t.toLowerCase();
        const int i = lower.indexOf (open);
        if (i <= 0) continue;
        const auto rest = lower.substring (i);
        for (auto w : { "remaster", "live", "feat", "ft.", "version", "mono", "stereo", "edit", "mix", "deluxe", "bonus", "hi-res", "24" })
            if (rest.contains (w)) { t = t.substring (0, i); break; }
    }
    return t.trim();
}

juce::String mainArtistForLyrics (const juce::String& artist)
{
    auto a = artist;
    for (auto sep : { " feat.", " feat ", " ft.", " & ", ", ", "; ", " x ", " / " })
    {
        const int i = a.toLowerCase().indexOf (sep);
        if (i > 0) a = a.substring (0, i);
    }
    return a.trim();
}

std::optional<Lyrics> searchLrclib (const juce::String& artist, const juce::String& title, double duration)
{
    juce::URL url ("https://lrclib.net/api/search");
    url = url.withParameter ("track_name", title).withParameter ("artist_name", artist);
    int status = 0;
    const auto json = juce::JSON::parse (getText (url, status));
    auto* results = json.getArray();
    if (status != 200 || results == nullptr) return std::nullopt;

    const juce::var* best = nullptr;
    double bestScore = 1.0e9;
    for (auto& r : *results)
    {
        const bool synced = r.getProperty ("syncedLyrics", {}).toString().isNotEmpty();
        if (! synced && r.getProperty ("plainLyrics", {}).toString().isEmpty()) continue;
        const double d = (double) r.getProperty ("duration", 0.0);
        if (duration > 0 && d > 0 && std::abs (d - duration) > 8.0) continue;   // another version of the song
        const double score = (synced ? 0.0 : 100.0) + (duration > 0 && d > 0 ? std::abs (d - duration) : 50.0);
        if (score < bestScore) { bestScore = score; best = &r; }
    }
    if (best == nullptr) return std::nullopt;
    const auto synced = best->getProperty ("syncedLyrics", {}).toString();
    Lyrics l = parseLrc (synced.isNotEmpty() ? synced : best->getProperty ("plainLyrics", {}).toString());
    l.source = "LRCLIB";
    if (l.isEmpty()) return std::nullopt;
    return l;
}

std::optional<Lyrics> fetchLyricsOvh (const juce::String& artist, const juce::String& title)
{
    if (artist.isEmpty() || title.isEmpty()) return std::nullopt;
    const juce::URL url ("https://api.lyrics.ovh/v1/" + juce::URL::addEscapeChars (artist, false) + "/" + juce::URL::addEscapeChars (title, false));
    int status = 0;
    const auto json = juce::JSON::parse (getText (url, status));
    auto text = json.getProperty ("lyrics", {}).toString().replace ("\r\n", "\n").trim();
    if (status != 200 || text.isEmpty()) return std::nullopt;
    // lyrics.ovh sometimes prefixes "Paroles de la chanson X par Y" on the first line
    if (text.upToFirstOccurrenceOf ("\n", false, false).startsWithIgnoreCase ("Paroles de la chanson"))
        text = text.fromFirstOccurrenceOf ("\n", false, false).trim();
    Lyrics l;
    l.plain = text;
    l.synced = false;
    l.source = "lyrics.ovh";
    return l;
}

std::optional<Lyrics> fetchOnlineLyrics (const juce::String& artist, const juce::String& title, const juce::String& album, double duration)
{
    if (auto l = fetchLrclib (artist, title, album, duration)) return l;
    const auto cleanTitle = cleanTitleForLyrics (title), mainArtist = mainArtistForLyrics (artist);
    if (cleanTitle != title || mainArtist != artist)
        if (auto l = fetchLrclib (mainArtist, cleanTitle, {}, duration)) return l;
    if (auto l = searchLrclib (mainArtist, cleanTitle, duration)) return l;
    if (auto l = fetchLyricsOvh (mainArtist, cleanTitle)) return l;
    return std::nullopt;
}

} // namespace nx
