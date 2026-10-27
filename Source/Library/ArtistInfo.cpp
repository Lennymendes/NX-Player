// NX Player — MIT License
#include "ArtistInfo.h"
#include "../Providers/Http.h"

namespace nx
{

namespace
{
    constexpr juce::int64 maxAgeMs = (juce::int64) 30 * 24 * 3600 * 1000;

    juce::String str (const juce::var& o, const char* key)
    {
        const auto v = o.getProperty (key, {});
        return v.isVoid() ? juce::String() : v.toString().trim();
    }
}

ArtistInfoService::ArtistInfoService (Library& lib) : juce::Thread ("NX artist info"), library (lib)
{
    startThread (juce::Thread::Priority::low);
}

ArtistInfoService::~ArtistInfoService()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
}

int ArtistInfoService::getPendingCount() const
{
    std::lock_guard<std::mutex> l (lock);
    return (int) queue.size();
}

std::optional<ArtistMeta> ArtistInfoService::get (const juce::String& artist, bool fetchIfMissing)
{
    if (artist.trim().isEmpty()) return std::nullopt;
    auto meta = library.getArtistMeta (artist);
    const bool stale = ! meta.has_value() || juce::Time::currentTimeMillis() - meta->fetched > maxAgeMs;
    if (stale && fetchIfMissing && enabled.load()) prefetch ({ artist });
    return meta;
}

void ArtistInfoService::prefetch (const juce::StringArray& artists)
{
    if (! enabled.load()) return;
    {
        std::lock_guard<std::mutex> l (lock);
        for (auto& a : artists)
        {
            if (a.trim().isEmpty() || queued.contains (a)) continue;
            if (auto m = library.getArtistMeta (a); m && juce::Time::currentTimeMillis() - m->fetched <= maxAgeMs) continue;
            queue.push_back (a);
            queued.add (a);
        }
    }
    wake.signal();
}

juce::String ArtistInfoService::tidyBio (const juce::String& text, int maxChars)
{
    auto t = text.replace ("\r\n", "\n").trim();
    while (t.contains ("\n\n\n")) t = t.replace ("\n\n\n", "\n\n");
    if (t.length() <= maxChars) return t;
    auto cut = t.substring (0, maxChars);
    const int dot = juce::jmax (cut.lastIndexOf (". "), cut.lastIndexOf (".\n"));
    return (dot > maxChars / 2 ? cut.substring (0, dot + 1) : cut.trimEnd() + juce::String::fromUTF8 ("\xe2\x80\xa6"));
}

ArtistMeta ArtistInfoService::parseAudioDb (const juce::var& json, const juce::String& artist, const juce::String& lang)
{
    ArtistMeta m;
    m.name = artist;
    const auto* list = json.getProperty ("artists", {}).getArray();
    if (list == nullptr || list->isEmpty()) return m;

    // Prefer the exact name (TheAudioDB may return several artists).
    juce::var best = list->getReference (0);
    for (auto& a : *list)
        if (str (a, "strArtist").equalsIgnoreCase (artist)) { best = a; break; }

    m.found = true;
    m.imageUrl = str (best, "strArtistThumb");
    m.bannerUrl = str (best, "strArtistFanart");
    if (m.bannerUrl.isEmpty()) m.bannerUrl = str (best, "strArtistWideThumb");
    if (m.bannerUrl.isEmpty()) m.bannerUrl = str (best, "strArtistBanner");
    m.country = str (best, "strCountry");
    m.genre = str (best, "strGenre");
    const auto key = "strBiography" + lang.toUpperCase().substring (0, 2);
    m.bio = str (best, key.toRawUTF8());
    if (m.bio.isEmpty()) m.bio = str (best, "strBiographyEN");
    if (m.bio.isNotEmpty()) m.bioSource = "TheAudioDB";
    m.bio = tidyBio (m.bio);
    return m;
}

void ArtistInfoService::mergeWikipedia (ArtistMeta& m, const juce::var& json)
{
    if (! json.isObject() || str (json, "type") == "disambiguation") return;
    const auto extract = str (json, "extract");
    if (extract.isNotEmpty() && m.bio.isEmpty())
    {
        m.bio = tidyBio (extract);
        m.bioSource = "Wikipedia";
        m.found = true;
    }
    if (m.imageUrl.isEmpty())
    {
        auto img = json.getProperty ("thumbnail", {}).getProperty ("source", {}).toString();
        if (img.isEmpty()) img = json.getProperty ("originalimage", {}).getProperty ("source", {}).toString();
        if (img.isNotEmpty()) { m.imageUrl = img; m.found = true; }
    }
}

ArtistMeta ArtistInfoService::fetchNow (const juce::String& artist)
{
    ArtistMeta m;
    m.name = artist;
    const auto adb = httpRequest ("GET", juce::URL (audioDbBase + "/search.php").withParameter ("s", artist), {}, {}, 10000);
    if (adb.ok()) m = parseAudioDb (adb.json(), artist, language);
    m.name = artist;

    if (m.bio.isEmpty() || m.imageUrl.isEmpty())
    {
        const auto base = wikipediaBase.replace ("{lang}", language.isNotEmpty() ? language.substring (0, 2) : juce::String ("fr"));
        const auto title = juce::URL::addEscapeChars (artist.replaceCharacter (' ', '_'), false);
        juce::StringPairArray headers;
        headers.set ("Accept", "application/json");
        auto wiki = httpRequest ("GET", juce::URL (base + title), headers, {}, 10000);
        if (wiki.ok()) mergeWikipedia (m, wiki.json());
        if (m.bio.isEmpty() && language != "en" && wikipediaBase.contains ("{lang}"))
        {
            wiki = httpRequest ("GET", juce::URL (wikipediaBase.replace ("{lang}", "en") + title), headers, {}, 10000);
            if (wiki.ok()) mergeWikipedia (m, wiki.json());
        }
    }
    m.fetched = juce::Time::currentTimeMillis();
    library.setArtistMeta (m);
    return m;
}

void ArtistInfoService::run()
{
    while (! threadShouldExit())
    {
        juce::String next;
        {
            std::lock_guard<std::mutex> l (lock);
            if (! queue.empty()) { next = queue.front(); queue.pop_front(); }
        }
        if (next.isEmpty()) { wake.wait (2000); continue; }

        if (enabled.load()) fetchNow (next);
        {
            std::lock_guard<std::mutex> l (lock);
            queued.removeString (next);
        }
        sendChangeMessage();
        for (int waited = 0; waited < throttleMs && ! threadShouldExit(); waited += 50) juce::Thread::sleep (50);   // ≈ 1 request / s
    }
}

} // namespace nx
