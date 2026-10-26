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

juce::String UpnpProvider::parseSsdpHeader (const juce::String& response, const juce::String& name)
{
    for (auto line : juce::StringArray::fromLines (response))
        if (line.trim().startsWithIgnoreCase (name + ":"))
            return line.fromFirstOccurrenceOf (":", false, false).trim();
    return {};
}

juce::String UpnpProvider::parseSsdpLocation (const juce::String& response) { return parseSsdpHeader (response, "location"); }

bool UpnpProvider::isMediaServerAnnouncement (const juce::String& response)
{
    const auto first = response.upToFirstOccurrenceOf ("\n", false, false);
    const bool notify = first.startsWithIgnoreCase ("NOTIFY");
    if (notify && ! parseSsdpHeader (response, "nts").containsIgnoreCase ("ssdp:alive")) return false;
    if (parseSsdpLocation (response).isEmpty()) return false;
    // An answer to our own M-SEARCH: keep it whatever the ST it echoes (some servers answer with their device
    // type or uuid); the device description tells later whether it has a ContentDirectory.
    if (! notify) return true;
    const auto target = parseSsdpHeader (response, "nt");
    return target.containsIgnoreCase ("MediaServer") || target.containsIgnoreCase ("ContentDirectory");
}

juce::String UpnpProvider::mSearch (const juce::String& st, const juce::String& host)
{
    return "M-SEARCH * HTTP/1.1\r\nHOST: " + host + "\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: " + st
         + "\r\nUSER-AGENT: NXPlayer/" NX_VERSION " UPnP/1.1\r\n\r\n";
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
            entry.childCount = e->getIntAttribute ("childCount", -1);
            entry.track.artist = childText (*e, "artist");
            if (entry.track.artist.isEmpty()) entry.track.artist = childText (*e, "creator");
            entry.track.album = entry.title;
            entry.track.artworkKey = childText (*e, "albumArtURI");
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

juce::StringArray UpnpProvider::getKnownLocations() const
{
    const juce::ScopedLock l (lock);
    juce::StringArray out;
    for (auto& s : servers) out.add (s.location);
    return out;
}

bool UpnpProvider::addServerFromLocation (const juce::String& location)
{
    const Busy guard (busy);
    auto r = httpRequest ("GET", juce::URL (location), {}, {}, 4000);
    UpnpServer s;
    if (! r.ok()) { lastError = "UPnP : description introuvable ("_t + location + ", HTTP " + juce::String (r.status) + ")"; return false; }
    if (! parseDeviceDescription (r.body, location, s)) { lastError = "UPnP : "_t + location + " n'est pas un serveur de médias"_t; return false; }
    const juce::ScopedLock l (lock);
    for (auto& existing : servers)
        if (existing.udn == s.udn) { existing = s; return true; }
    servers.push_back (s);
    return true;
}

void UpnpProvider::collectLocations (juce::DatagramSocket& socket, juce::StringArray& locations, int waitMs)
{
    if (socket.waitUntilReady (true, waitMs) != 1) return;
    char buf[8192];
    for (int k = 0; k < 64; ++k)   // drain what is queued
    {
        const int n = socket.read (buf, sizeof (buf) - 1, false);
        if (n <= 0) break;
        const auto text = juce::String::fromUTF8 (buf, n);
        if (isMediaServerAnnouncement (text)) locations.addIfNotAlreadyThere (parseSsdpLocation (text));
        if (socket.waitUntilReady (true, 0) != 1) break;
    }
}

std::vector<UpnpServer> UpnpProvider::discover (int timeoutMs)
{
    const Busy guard (busy);
    // 1) Passive: servers announce themselves (ssdp:alive) on 239.255.255.250:1900.
    juce::DatagramSocket listener (false);
    listener.setEnablePortReuse (true);
    const bool listening = listener.bindToPort (1900) && listener.joinMulticast ("239.255.255.250");

    // 2) Active: M-SEARCH from an ephemeral port; answers come back unicast.
    juce::DatagramSocket socket (true);
    socket.bindToPort (0);
    const juce::StringArray targets { "urn:schemas-upnp-org:device:MediaServer:1", "urn:schemas-upnp-org:service:ContentDirectory:1" };

    juce::StringArray locations;
    const auto t0 = juce::Time::getMillisecondCounter();
    const int sendAt[] = { 0, 400, 1200 };   // UDP is lossy: repeat
    int sent = 0;
    while ((int) (juce::Time::getMillisecondCounter() - t0) < timeoutMs)
    {
        if (sent < 3 && (int) (juce::Time::getMillisecondCounter() - t0) >= sendAt[sent])
        {
            for (auto& st : targets)
            {
                const auto msg = mSearch (st);
                socket.write ("239.255.255.250", 1900, msg.toRawUTF8(), (int) msg.getNumBytesAsUTF8());
            }
            ++sent;
        }
        collectLocations (socket, locations, 40);
        if (listening) collectLocations (listener, locations, 10);
    }
    if (listening) listener.leaveMulticast ("239.255.255.250");

    if (locations.isEmpty() && getServers().empty())
        lastError = "Aucune réponse SSDP : vérifiez que le NAS est sur le même réseau, ou ajoutez son adresse IP."_t;
    for (auto& loc : locations) addServerFromLocation (loc);
    return getServers();
}

std::vector<UpnpServer> UpnpProvider::discoverHost (const juce::String& host, int port, int timeoutMs)
{
    const Busy guard (busy);
    juce::DatagramSocket socket (false);
    socket.bindToPort (0);
    juce::StringArray locations;
    const auto t0 = juce::Time::getMillisecondCounter();
    int sent = 0;
    while ((int) (juce::Time::getMillisecondCounter() - t0) < timeoutMs)
    {
        if (sent < 3 && (int) (juce::Time::getMillisecondCounter() - t0) >= sent * 500)
        {
            for (auto st : { "urn:schemas-upnp-org:device:MediaServer:1", "urn:schemas-upnp-org:service:ContentDirectory:1" })
            {
                const auto msg = mSearch (st, host + ":" + juce::String (port));
                socket.write (host, port, msg.toRawUTF8(), (int) msg.getNumBytesAsUTF8());
            }
            ++sent;
        }
        collectLocations (socket, locations, 50);
        if (! locations.isEmpty() && sent >= 2) break;
    }
    if (locations.isEmpty()) lastError = "UPnP : aucune réponse de "_t + host;
    for (auto& loc : locations) addServerFromLocation (loc);
    return getServers();
}

UpnpBrowsePage UpnpProvider::soapCall (const UpnpServer& server, const juce::String& action, const juce::String& args)
{
    const Busy guard (busy);
    UpnpBrowsePage page;
    const juce::String body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:" + action + " xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">" + args
        + "</u:" + action + "></s:Body></s:Envelope>";
    juce::StringPairArray h;
    h.set ("Content-Type", "text/xml; charset=\"utf-8\"");
    h.set ("SOAPACTION", "\"urn:schemas-upnp-org:service:ContentDirectory:1#" + action + "\"");
    auto r = httpRequest ("POST", juce::URL (server.controlUrl), h, body, 15000);
    auto xml = juce::XmlDocument::parse (r.body);

    std::function<juce::String (const juce::XmlElement&, const juce::String&)> find = [&] (const juce::XmlElement& e, const juce::String& tag) -> juce::String
    {
        for (auto* c : e.getChildIterator())
        {
            if (c->getTagNameWithoutNamespace() == tag) return c->getAllSubText();
            const auto sub = find (*c, tag);
            if (sub.isNotEmpty()) return sub;
        }
        return {};
    };

    if (! r.ok())
    {
        const auto desc = xml != nullptr ? find (*xml, "errorDescription").trim() : juce::String();
        page.error = "UPnP " + action + " : " + (desc.isNotEmpty() ? desc : (r.status == 0 ? juce::String ("serveur injoignable"_t) : "HTTP " + juce::String (r.status)));
        lastError = page.error;
        return page;
    }
    if (xml == nullptr) { page.error = lastError = "UPnP " + action + " : réponse illisible"_t; return page; }
    page.ok = true;
    page.entries = parseDidl (find (*xml, "Result"), server.udn);
    const auto nr = find (*xml, "NumberReturned").trim();
    const auto tm = find (*xml, "TotalMatches").trim();
    page.numberReturned = nr.isNotEmpty() ? nr.getIntValue() : (int) page.entries.size();
    page.totalMatches = tm.isNotEmpty() ? tm.getIntValue() : -1;
    return page;
}

UpnpBrowsePage UpnpProvider::browsePage (const UpnpServer& server, const juce::String& objectId, int start, int count)
{
    return soapCall (server, "Browse",
                     "<ObjectID>" + objectId.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;") + "</ObjectID>"
                     "<BrowseFlag>BrowseDirectChildren</BrowseFlag><Filter>*</Filter><StartingIndex>" + juce::String (start)
                     + "</StartingIndex><RequestedCount>" + juce::String (count) + "</RequestedCount><SortCriteria></SortCriteria>");
}

std::vector<UpnpEntry> UpnpProvider::browseAll (const UpnpServer& server, const juce::String& objectId, int maxEntries)
{
    std::vector<UpnpEntry> out;
    constexpr int pageSize = 200;
    for (int start = 0; (int) out.size() < maxEntries;)
    {
        auto page = browsePage (server, objectId, start, pageSize);
        if (! page.ok) break;
        out.insert (out.end(), page.entries.begin(), page.entries.end());
        if (page.numberReturned <= 0) break;
        start += page.numberReturned;
        if (page.totalMatches > 0 ? start >= page.totalMatches : page.numberReturned < pageSize) break;
    }
    return out;
}

std::vector<TrackInfo> UpnpProvider::collectTracks (const UpnpServer& server, const juce::String& objectId, int maxTracks)
{
    std::vector<TrackInfo> out;
    std::vector<juce::String> stack { objectId };
    juce::StringArray seen;
    while (! stack.empty() && (int) out.size() < maxTracks)
    {
        const auto id = stack.back();
        stack.pop_back();
        if (seen.contains (id)) continue;   // some servers expose the same container twice
        seen.add (id);
        auto entries = browseAll (server, id, maxTracks);
        // keep the server's order: containers are visited depth-first in order
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            if (it->isContainer) stack.push_back (it->id);
        for (auto& e : entries)
            if (! e.isContainer && (int) out.size() < maxTracks) out.push_back (e.track);
    }
    return out;
}

std::vector<TrackInfo> UpnpProvider::searchTracks (const juce::String& query, int limit)
{
    std::vector<TrackInfo> out;
    const auto q = query.replace ("\"", "").replace ("&", "&amp;").replace ("<", "&lt;");
    for (auto& s : getServers())
    {
        auto page = soapCall (s, "Search",
            "<ContainerID>0</ContainerID><SearchCriteria>upnp:class derivedfrom \"object.item.audioItem\" and (dc:title contains \""
            + q + "\" or upnp:artist contains \"" + q + "\" or upnp:album contains \"" + q + "\")</SearchCriteria>"
            "<Filter>*</Filter><StartingIndex>0</StartingIndex><RequestedCount>" + juce::String (limit) + "</RequestedCount><SortCriteria></SortCriteria>");
        for (auto& e : page.entries) if (! e.isContainer) out.push_back (e.track);
    }
    return out;
}

int UpnpProvider::importContainer (Library& library, const UpnpServer& server, const juce::String& objectId, int maxTracks)
{
    auto tracks = collectTracks (server, objectId, maxTracks);
    for (auto& t : tracks) library.upsert (t);
    if (! tracks.empty()) library.notifyChanged();
    return (int) tracks.size();
}

} // namespace nx
