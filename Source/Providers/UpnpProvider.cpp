// NX Player — MIT License
#include "UpnpProvider.h"
#include "Http.h"

namespace nx
{

namespace
{
    juce::String childText (const juce::XmlElement& e, const juce::String& localName)
    {
        for (auto* c : e.getChildIterator())
            if (c->getTagNameWithoutNamespace() == localName) return c->getAllSubText().trim();
        return {};
    }

    juce::String resolveUrl (const juce::String& base, const juce::String& rel)
    {
        if (rel.startsWith ("http://") || rel.startsWith ("https://")) return rel;
        juce::URL u (base);
        const auto origin = u.getScheme() + "://" + u.getDomain() + (u.getPort() > 0 ? ":" + juce::String (u.getPort()) : juce::String());
        if (rel.startsWithChar ('/')) return origin + rel;
        const auto path = base.fromFirstOccurrenceOf (origin, false, false).upToLastOccurrenceOf ("/", true, false);
        return origin + (path.isEmpty() ? "/" : path) + rel;
    }

    juce::String codecFromMime (const juce::String& mime, bool& lossy)
    {
        const auto m = mime.toLowerCase();
        lossy = false;
        if (m.contains ("flac")) return "FLAC";
        if (m.contains ("wav")) return "WAV";
        if (m.contains ("aiff")) return "AIFF";
        if (m.contains ("dsf")) return "DSF";
        if (m.contains ("dff") || m.contains ("dsd")) return "DFF";
        if (m.contains ("mp4") || m.contains ("m4a") || m.contains ("alac")) return "ALAC";
        lossy = true;
        if (m.contains ("mpeg") || m.contains ("mp3")) return "MP3";
        if (m.contains ("ogg") || m.contains ("vorbis")) return "Vorbis";
        return mime.fromLastOccurrenceOf ("/", false, false).toUpperCase();
    }

    int resScore (const juce::String& mime)
    {
        const auto m = mime.toLowerCase();
        if (m.contains ("dsf") || m.contains ("dff") || m.contains ("dsd")) return 6;
        if (m.contains ("flac")) return 5;
        if (m.contains ("wav") || m.contains ("aiff")) return 4;
        if (m.contains ("mp4") || m.contains ("alac")) return 3;
        if (m.contains ("l16")) return -1;     // raw PCM without a header: not decodable as a file
        return 1;
    }
}

double UpnpProvider::parseDuration (const juce::String& s)
{
    auto parts = juce::StringArray::fromTokens (s.trim(), ":", {});
    double secs = 0.0;
    for (auto& p : parts) secs = secs * 60.0 + p.getDoubleValue();
    return secs;
}

juce::String UpnpProvider::parseSsdpLocation (const juce::String& response)
{
    for (auto line : juce::StringArray::fromLines (response))
        if (line.trim().startsWithIgnoreCase ("location:"))
            return line.fromFirstOccurrenceOf (":", false, false).trim();
    return {};
}

bool UpnpProvider::parseDeviceDescription (const juce::String& xmlText, const juce::String& location, UpnpServer& out)
{
    auto xml = juce::XmlDocument::parse (xmlText);
    if (xml == nullptr) return false;
    auto base = childText (*xml, "URLBase");
    if (base.isEmpty()) base = location;

    std::function<bool (const juce::XmlElement&)> findDevice = [&] (const juce::XmlElement& e) -> bool
    {
        for (auto* d : e.getChildIterator())
        {
            if (d->getTagNameWithoutNamespace() == "device")
            {
                for (auto* sl : d->getChildIterator())
                    if (sl->getTagNameWithoutNamespace() == "serviceList")
                        for (auto* svc : sl->getChildIterator())
                            if (childText (*svc, "serviceType").contains ("ContentDirectory"))
                            {
                                out.udn = childText (*d, "UDN");
                                out.name = childText (*d, "friendlyName");
                                out.controlUrl = resolveUrl (base, childText (*svc, "controlURL"));
                                out.location = location;
                                for (auto* il : d->getChildIterator())
                                    if (il->getTagNameWithoutNamespace() == "iconList")
                                        for (auto* icon : il->getChildIterator())
                                            if (out.iconUrl.isEmpty()) out.iconUrl = resolveUrl (base, childText (*icon, "url"));
                                return true;
                            }
                for (auto* sub : d->getChildIterator())
                    if (sub->getTagNameWithoutNamespace() == "deviceList" && findDevice (*sub)) return true;
            }
        }
        return false;
    };
    return findDevice (*xml);
}

std::vector<UpnpEntry> UpnpProvider::parseDidl (const juce::String& didl, const juce::String& udn)
{
    std::vector<UpnpEntry> out;
    auto xml = juce::XmlDocument::parse (didl);
    if (xml == nullptr) return out;
    for (auto* e : xml->getChildIterator())
    {
        UpnpEntry entry;
        entry.id = e->getStringAttribute ("id");
        entry.parentId = e->getStringAttribute ("parentID");
        entry.title = childText (*e, "title");
        if (e->getTagNameWithoutNamespace() == "container")
        {
            entry.isContainer = true;
            entry.childCount = e->getIntAttribute ("childCount");
            out.push_back (entry);
            continue;
        }
        if (e->getTagNameWithoutNamespace() != "item") continue;

        // pick the best <res>
        const juce::XmlElement* best = nullptr;
        int bestScore = -100;
        for (auto* c : e->getChildIterator())
            if (c->getTagNameWithoutNamespace() == "res")
            {
                const auto mime = c->getStringAttribute ("protocolInfo").upToLastOccurrenceOf (":", false, false).fromLastOccurrenceOf (":", false, false);
                const int score = resScore (mime);
                if (score > bestScore) { bestScore = score; best = c; }
            }
        if (best == nullptr || bestScore < 0) continue;

        auto& t = entry.track;
        t.source = "upnp:" + udn;
        t.externalId = entry.id;
        t.uri = best->getAllSubText().trim();
        t.title = entry.title;
        t.artist = childText (*e, "artist");
        if (t.artist.isEmpty()) t.artist = childText (*e, "creator");
        t.album = childText (*e, "album");
        t.genre = childText (*e, "genre");
        t.trackNumber = childText (*e, "originalTrackNumber").getIntValue();
        t.year = childText (*e, "date").substring (0, 4).getIntValue();
        t.artworkKey = childText (*e, "albumArtURI");
        const auto mime = best->getStringAttribute ("protocolInfo").upToLastOccurrenceOf (":", false, false).fromLastOccurrenceOf (":", false, false);
        t.codec = codecFromMime (mime, t.lossy);
        t.duration = parseDuration (best->getStringAttribute ("duration"));
        t.sampleRate = best->getDoubleAttribute ("sampleFrequency");
        t.bitsPerSample = best->getIntAttribute ("bitsPerSample");
        t.channels = best->getIntAttribute ("nrAudioChannels", 2);
        t.fileSize = (juce::int64) best->getStringAttribute ("size").getLargeIntValue();
        t.isDsd = t.codec == "DSF" || t.codec == "DFF";
        if (t.isDsd && t.sampleRate < 1.0e6) t.sampleRate = 2822400.0;
        if (t.isDsd) t.bitsPerSample = 1;
        out.push_back (entry);
    }
    return out;
}

//==============================================================================
bool UpnpProvider::isSignedIn() const
{
    const juce::ScopedLock l (lock);
    return ! servers.empty();
}

juce::String UpnpProvider::getStatusText() const
{
    const juce::ScopedLock l (lock);
    return servers.empty() ? "Aucun serveur trouvé"_t : juce::String ((int) servers.size()) + " serveur(s)";
}

std::vector<UpnpServer> UpnpProvider::getServers() const
{
    const juce::ScopedLock l (lock);
    return servers;
}

void UpnpProvider::addServerFromLocation (const juce::String& location)
{
    auto r = httpRequest ("GET", juce::URL (location), {}, {}, 4000);
    UpnpServer s;
    if (! r.ok() || ! parseDeviceDescription (r.body, location, s)) return;
    const juce::ScopedLock l (lock);
    for (auto& existing : servers)
        if (existing.udn == s.udn) { existing = s; return; }
    servers.push_back (s);
}

std::vector<UpnpServer> UpnpProvider::discover (int timeoutMs)
{
    juce::DatagramSocket socket (true);
    socket.bindToPort (0);
    const juce::String msg = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                             "ST: urn:schemas-upnp-org:service:ContentDirectory:1\r\n\r\n";
    for (int i = 0; i < 2; ++i)
        socket.write ("239.255.255.250", 1900, msg.toRawUTF8(), (int) msg.getNumBytesAsUTF8());

    juce::StringArray locations;
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    char buf[4096];
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (socket.waitUntilReady (true, 200) != 1) continue;
        const int n = socket.read (buf, sizeof (buf) - 1, false);
        if (n <= 0) continue;
        buf[n] = 0;
        const auto loc = parseSsdpLocation (juce::String::fromUTF8 (buf, n));
        if (loc.isNotEmpty()) locations.addIfNotAlreadyThere (loc);
    }
    for (auto& loc : locations) addServerFromLocation (loc);
    return getServers();
}

std::vector<UpnpEntry> UpnpProvider::soapCall (const UpnpServer& server, const juce::String& action, const juce::String& args)
{
    const juce::String body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:" + action + " xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">" + args
        + "</u:" + action + "></s:Body></s:Envelope>";
    juce::StringPairArray h;
    h.set ("Content-Type", "text/xml; charset=\"utf-8\"");
    h.set ("SOAPACTION", "\"urn:schemas-upnp-org:service:ContentDirectory:1#" + action + "\"");
    auto r = httpRequest ("POST", juce::URL (server.controlUrl), h, body, 10000);
    if (! r.ok()) { lastError = "UPnP " + action + " : HTTP " + juce::String (r.status); return {}; }
    auto xml = juce::XmlDocument::parse (r.body);
    if (xml == nullptr) return {};
    juce::String result;
    std::function<void (const juce::XmlElement&)> find = [&] (const juce::XmlElement& e)
    {
        for (auto* c : e.getChildIterator())
        {
            if (c->getTagNameWithoutNamespace() == "Result") { result = c->getAllSubText(); return; }
            find (*c);
        }
    };
    find (*xml);
    return parseDidl (result, server.udn);
}

std::vector<UpnpEntry> UpnpProvider::browse (const UpnpServer& server, const juce::String& objectId, int start, int count)
{
    return soapCall (server, "Browse",
                     "<ObjectID>" + juce::String (objectId).replace ("&", "&amp;").replace ("<", "&lt;") + "</ObjectID>"
                     "<BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter><StartingIndex>" + juce::String (start)
                     + "</StartingIndex><RequestedCount>" + juce::String (count) + "</RequestedCount><SortCriteria></SortCriteria>");
}

std::vector<TrackInfo> UpnpProvider::searchTracks (const juce::String& query, int limit)
{
    std::vector<TrackInfo> out;
    const auto q = query.replace ("\"", "").replace ("&", "&amp;").replace ("<", "&lt;");
    for (auto& s : getServers())
    {
        auto entries = soapCall (s, "Search",
            "<ContainerID>0</ContainerID><SearchCriteria>upnp:class derivedfrom \"object.item.audioItem\" and (dc:title contains \""
            + q + "\" or upnp:artist contains \"" + q + "\" or upnp:album contains \"" + q + "\")</SearchCriteria>"
            "<Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>" + juce::String (limit) + "</RequestedCount><SortCriteria></SortCriteria>");
        for (auto& e : entries) if (! e.isContainer) out.push_back (e.track);
    }
    return out;
}

int UpnpProvider::importContainer (Library& library, const UpnpServer& server, const juce::String& objectId, int maxTracks)
{
    int imported = 0;
    std::vector<juce::String> stack { objectId };
    while (! stack.empty() && imported < maxTracks)
    {
        const auto id = stack.back();
        stack.pop_back();
        for (int start = 0;; start += 500)
        {
            auto entries = browse (server, id, start, 500);
            for (auto& e : entries)
            {
                if (e.isContainer) stack.push_back (e.id);
                else { library.upsert (e.track); ++imported; }
            }
            if (entries.size() < 500) break;
        }
    }
    if (imported > 0) library.notifyChanged();
    return imported;
}

} // namespace nx
