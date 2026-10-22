// NX Player — MIT License
#include "MediaProvider.h"
#include "Http.h"
#include <map>
#if JUCE_LINUX || JUCE_MAC
 #include <sys/stat.h>
#endif

namespace nx
{

//==============================================================================
CredentialStore::CredentialStore (juce::File f) : file (std::move (f))
{
    root = juce::JSON::parse (file);
    if (! root.isObject()) root = juce::var (new juce::DynamicObject());
}

juce::String CredentialStore::get (const juce::String& provider, const juce::String& key) const
{
    const juce::ScopedLock l (lock);
    return root.getProperty (provider, {}).getProperty (key, {}).toString();
}

void CredentialStore::set (const juce::String& provider, const juce::String& key, const juce::String& value)
{
    {
        const juce::ScopedLock l (lock);
        auto* obj = root.getDynamicObject();
        auto section = obj->getProperty (provider);
        if (! section.isObject()) { section = juce::var (new juce::DynamicObject()); obj->setProperty (provider, section); }
        section.getDynamicObject()->setProperty (key, value);
    }
    save();
}

void CredentialStore::clear (const juce::String& provider)
{
    {
        const juce::ScopedLock l (lock);
        root.getDynamicObject()->removeProperty (provider);
    }
    save();
}

void CredentialStore::save() const
{
    const juce::ScopedLock l (lock);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (root));
   #if JUCE_LINUX || JUCE_MAC
    ::chmod (file.getFullPathName().toRawUTF8(), 0600);
   #endif
}

juce::String MediaProvider::getStatusText() const
{
    if (! isConfigured()) return "Non configuré"_t;
    return isSignedIn() ? "Connecté"_t : "Non connecté"_t;
}

//==============================================================================
UnifiedMediaProvider::UnifiedMediaProvider (Library& l, CredentialStore& c) : library (l), credentials (c) {}

UnifiedMediaProvider::~UnifiedMediaProvider() { pool.removeAllJobs (true, 10000); }

void UnifiedMediaProvider::addProvider (std::unique_ptr<MediaProvider> p) { providers.push_back (std::move (p)); }

MediaProvider* UnifiedMediaProvider::getProvider (const juce::String& id) const
{
    for (auto& p : providers)
        if (id == p->getId() || id.startsWith (p->getId() + ":")) return p.get();
    return nullptr;
}

juce::String UnifiedMediaProvider::dedupeKey (const TrackInfo& t)
{
    auto norm = [] (juce::String s)
    {
        s = s.toLowerCase();
        for (auto cut : { " (remaster", " - remaster", " [remaster", " (live", " (feat", " feat.", " (bonus" })
            if (s.contains (cut)) s = s.upToFirstOccurrenceOf (cut, false, false);
        juce::String out;
        for (auto c : s)
            if (juce::CharacterFunctions::isLetterOrDigit (c)) out << juce::CharacterFunctions::toLowerCase (c);
        return out;
    };
    const auto artist = t.artist.upToFirstOccurrenceOf (",", false, false).upToFirstOccurrenceOf (" & ", false, false);
    return norm (t.title) + "|" + norm (artist);
}

std::vector<TrackInfo> UnifiedMediaProvider::merge (std::vector<std::vector<TrackInfo>> lists)
{
    auto rank = [] (const TrackInfo& t)
    {
        if (t.source == "local") return 0;
        if (t.source.startsWith ("upnp")) return 1;
        if (t.source == "qobuz") return 2;
        if (t.source == "tidal") return 3;
        if (t.source == "spotify") return 4;
        return 5;
    };
    auto quality = [] (const TrackInfo& t) { return t.lossy ? 0.0 : (t.isDsd ? 1.0e9 : t.sampleRate * juce::jmax (16, t.bitsPerSample)); };

    std::vector<TrackInfo> out;
    std::map<juce::String, size_t> index;
    for (auto& list : lists)
        for (auto& t : list)
        {
            const auto key = dedupeKey (t);
            auto it = index.find (key);
            if (it == index.end()) { index[key] = out.size(); out.push_back (t); continue; }
            auto& kept = out[it->second];
            if (quality (t) > quality (kept) || (quality (t) == quality (kept) && rank (t) < rank (kept)))
                kept = t;
        }
    return out;
}

std::vector<TrackInfo> UnifiedMediaProvider::searchBlocking (const juce::String& query, int limit)
{
    std::vector<std::vector<TrackInfo>> lists;
    lists.push_back (library.search (query, limit));
    for (auto& p : providers)
        if (p->isConfigured() && p->isSignedIn())
            lists.push_back (p->searchTracks (query, limit));
    return merge (std::move (lists));
}

void UnifiedMediaProvider::search (const juce::String& query, std::function<void (std::vector<TrackInfo>)> callback, int limit)
{
    pool.addJob ([this, query, callback, limit]
    {
        auto results = searchBlocking (query, limit);
        juce::MessageManager::callAsync ([callback, results = std::move (results)]() mutable { callback (std::move (results)); });
    });
}

void UnifiedMediaProvider::importPlaylist (const juce::String& providerId, const PlaylistInfo& playlist, std::function<void (int)> done)
{
    pool.addJob ([this, providerId, playlist, done]
    {
        int count = 0;
        if (auto* p = getProvider (providerId))
        {
            auto tracks = p->getPlaylistTracks (playlist.externalId);
            std::vector<juce::int64> ids;
            for (auto& t : tracks) ids.push_back (library.upsert (t));
            const auto pid = library.createPlaylist (playlist.name, providerId, playlist.externalId);
            library.setPlaylistTracks (pid, ids);
            count = (int) ids.size();
            library.notifyChanged();
        }
        juce::MessageManager::callAsync ([done, count] { if (done) done (count); });
    });
}

PlaybackRoute UnifiedMediaProvider::routeFor (const TrackInfo& t) const
{
    if (t.source == "local" || t.source.startsWith ("upnp")) return PlaybackRoute::Engine;
    if (auto* p = getProvider (t.source)) return p->getRoute();
    return PlaybackRoute::Engine;
}

PlayableItem UnifiedMediaProvider::toPlayable (const TrackInfo& t) const
{
    PlayableItem item;
    item.id = t.source + ":" + t.externalId;
    item.uri = t.uri.isNotEmpty() ? t.uri : t.externalId;
    item.title = t.title;
    item.artist = t.displayArtist();
    item.album = t.album;
    item.durationHint = t.duration;
    if (t.source == "local") item.sourceName = "Fichier local"_t;
    else if (t.source.startsWith ("upnp")) item.sourceName = "NAS (UPnP / DLNA)";
    else if (auto* p = getProvider (t.source)) item.sourceName = p->getDisplayName();
    return item;
}

std::unique_ptr<juce::InputStream> UnifiedMediaProvider::openStream (const PlayableItem& item)
{
    const auto source = item.id.upToFirstOccurrenceOf (":", false, false);
    if (source == "local" || source.isEmpty()) return nullptr;   // the engine opens files itself
    if (source == "upnp")
        return item.uri.startsWith ("http") ? std::make_unique<HttpStream> (juce::URL (item.uri)) : nullptr;

    if (auto* p = getProvider (source))
    {
        TrackInfo t;
        t.source = source;
        t.externalId = item.id.fromFirstOccurrenceOf (":", false, false);
        t.uri = item.uri;
        t.title = item.title;
        juce::String error;
        auto s = p->openStream (t, error);
        if (s == nullptr) p->lastError = error;
        return s;
    }
    return nullptr;
}

} // namespace nx
