// NX Player — MIT License
#include "StreamingProviders.h"
#include "Http.h"
#include "../Audio/AudioDecoder.h"
#include <juce_cryptography/juce_cryptography.h>

#if JUCE_LINUX || JUCE_MAC
 #include <sys/stat.h>
#endif

namespace nx
{

namespace
{
    juce::String base64Url (const void* data, size_t n)
    {
        return juce::Base64::toBase64 (data, n).replaceCharacter ('+', '-').replaceCharacter ('/', '_').trimCharactersAtEnd ("=");
    }

    bool httpGetBinary (const juce::String& url, juce::MemoryBlock& out, const juce::String& headers = {})
    {
        juce::WebInputStream web { juce::URL (url), false };
        if (headers.isNotEmpty()) web.withExtraHeaders (headers);
        web.withConnectionTimeout (15000);
        if (! web.connect (nullptr) || web.getStatusCode() < 200 || web.getStatusCode() >= 300) return false;
        out.reset();
        juce::MemoryOutputStream mo (out, false);
        mo.writeFromInputStream (web, -1);
        return ! web.isError();
    }

    juce::String randomToken (int length)
    {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
        juce::Random r;
        r.setSeedRandomly();
        juce::String s;
        for (int i = 0; i < length; ++i) s << chars[r.nextInt (66)];
        return s;
    }

    juce::String firstArtist (const juce::var& artists)
    {
        juce::StringArray names;
        if (auto* a = artists.getArray())
            for (auto& x : *a) names.add (x.getProperty ("name", {}).toString());
        return names.joinIntoString (", ");
    }

    /** FLAC in fMP4 (Tidal Max): downloads the init segment then each media segment and writes a
        plain FLAC stream ("fLaC" + metadata blocks from dfLa + the frames from every mdat). */
    class DashFlacStream final : public ProgressiveStream
    {
    public:
        DashFlacStream (TidalProvider::DashManifest m) : ProgressiveStream ("NX dash"), manifest (std::move (m)) { begin(); }
        ~DashFlacStream() override { stopThread (5000); }

    private:
        bool produce() override
        {
            juce::MemoryBlock init;
            if (! httpGetBinary (manifest.initUrl, init)) { fail ("segment d'initialisation DASH introuvable"_t); return false; }
            const auto header = TidalProvider::flacHeaderFromInitSegment (init);
            if (header.getSize() < 42) { fail ("pas de configuration FLAC (dfLa) dans le flux"_t); return false; }
            append (header.getData(), header.getSize());
            for (auto& url : manifest.segmentUrls)
            {
                if (threadShouldExit()) return false;
                juce::MemoryBlock seg;
                if (! httpGetBinary (url, seg)) { fail ("segment DASH manquant"_t); return false; }
                const auto frames = TidalProvider::mdatPayload (seg);
                if (! append (frames.getData(), frames.getSize())) return false;
            }
            return true;
        }
        TidalProvider::DashManifest manifest;
    };
}

namespace
{
    // Accepted sockets inherit the listener's non-blocking mode on Windows: wait for readiness explicitly.
    juce::String readRequestHead (juce::StreamingSocket& c, int timeoutMs)
    {
        juce::MemoryBlock data;
        for (int waited = 0; waited < timeoutMs;)
        {
            const int ready = c.waitUntilReady (true, 50);
            if (ready < 0) break;
            if (ready == 0) { waited += 50; continue; }
            char buf[4096];
            const int n = c.read (buf, sizeof (buf), false);
            if (n <= 0) break;
            data.append (buf, (size_t) n);
            if (data.toString().contains ("\r\n\r\n") || data.getSize() > 65536) break;
        }
        return data.toString();
    }

    void writeAll (juce::StreamingSocket& c, const juce::String& text)
    {
        const char* p = text.toRawUTF8();
        int left = (int) text.getNumBytesAsUTF8();
        for (int stalls = 0; left > 0 && stalls < 100;)
        {
            const int w = c.write (p, left);
            if (w > 0) { p += w; left -= w; stalls = 0; continue; }
            if (c.waitUntilReady (false, 50) < 0) break;
            ++stalls;
        }
    }

    void respond (juce::StreamingSocket& c, int status, const juce::String& title, const juce::String& message)
    {
        const juce::String html = "<!doctype html><html><head><meta charset='utf-8'><title>NX Player</title></head>"
                                  "<body style='font-family:-apple-system,Segoe UI,sans-serif;background:#0A0C0E;color:#EEF3F1;"
                                  "display:flex;align-items:center;justify-content:center;height:100vh;margin:0'>"
                                  "<div style='text-align:center'><h2 style='color:#00F5A0'>" + title + "</h2><p>" + message + "</p></div></body></html>";
        const juce::String head = "HTTP/1.1 " + juce::String (status) + (status == 200 ? " OK" : " Not Found")
                                  + "\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "
                                  + juce::String ((int) html.getNumBytesAsUTF8()) + "\r\n\r\n";
        writeAll (c, head + html);
        // Give the browser time to read the page before the socket closes.
        for (int i = 0; i < 20 && c.waitUntilReady (true, 50) == 0; ++i) {}
    }
}

bool OAuthLoopback::open (int port, juce::String& error)
{
    listener.close();
    for (int attempt = 0; attempt < 10; ++attempt)   // a previous attempt may still hold the port briefly
    {
        if (listener.createListener (port, "127.0.0.1")) return true;
        juce::Thread::sleep (200);
    }
    error = "le port "_t + juce::String (port) + " est déjà utilisé (une autre connexion est en cours ?)"_t;
    return false;
}

juce::String OAuthLoopback::waitForCode (int timeoutMs, juce::String& error)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < deadline && ! juce::Thread::currentThreadShouldExit())
    {
        const int ready = listener.waitUntilReady (true, 250);
        if (ready < 0) { error = "écoute interrompue"_t; return {}; }
        if (ready == 0) continue;
        std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
        if (client == nullptr) continue;

        const auto request = readRequestHead (*client, 3000);
        const auto target = request.fromFirstOccurrenceOf ("GET ", false, false).upToFirstOccurrenceOf (" ", false, false);
        if (! target.startsWith ("/callback"))
        {
            // Browser preconnects, /favicon.ico...: answer and keep listening.
            if (target.isNotEmpty()) respond (*client, 404, "NX Player", "");
            continue;
        }

        const juce::URL u ("http://127.0.0.1" + target);
        juce::String code, err;
        for (int i = 0; i < u.getParameterNames().size(); ++i)
        {
            if (u.getParameterNames()[i] == "code")  code = u.getParameterValues()[i];
            if (u.getParameterNames()[i] == "error") err = u.getParameterValues()[i];
        }
        if (code.isNotEmpty())
        {
            respond (*client, 200, "NX Player est connect&eacute;", "Vous pouvez fermer cet onglet et revenir dans NX Player.");
            listener.close();
            return code;
        }
        if (err.isNotEmpty())
        {
            respond (*client, 200, "Connexion annul&eacute;e", juce::String ("Le service a r&eacute;pondu : ") + err);
            listener.close();
            error = err == "access_denied" ? juce::String ("autorisation refusée"_t) : err;
            return {};
        }
        respond (*client, 404, "NX Player", "Adresse de retour incompl&egrave;te.");
    }
    listener.close();
    if (error.isEmpty()) error = "délai dépassé : aucune réponse du navigateur"_t;
    return {};
}

juce::String waitForOAuthRedirect (int port, int timeoutMs, juce::String& error)
{
    OAuthLoopback loop;
    if (! loop.open (port, error)) return {};
    return loop.waitForCode (timeoutMs, error);
}

//==============================================================================
// Qobuz
//==============================================================================
bool QobuzProvider::isConfigured() const
{
    return creds.get ("qobuz", "app_id").isNotEmpty() && creds.get ("qobuz", "app_secret").isNotEmpty();
}

bool QobuzProvider::isSignedIn() const { return creds.get ("qobuz", "user_auth_token").isNotEmpty(); }

juce::String QobuzProvider::requestSignature (const juce::String& trackId, int formatId, juce::int64 ts, const juce::String& secret)
{
    const auto s = "trackgetFileUrlformat_id" + juce::String (formatId) + "intentstreamtrack_id" + trackId + juce::String (ts) + secret;
    return juce::MD5 (s.toUTF8()).toHexString();
}

HttpResponse QobuzProvider::call (const juce::String& endpoint, const juce::StringPairArray& params)
{
    juce::URL url ("https://www.qobuz.com/api.json/0.2/" + endpoint);
    for (int i = 0; i < params.size(); ++i) url = url.withParameter (params.getAllKeys()[i], params.getAllValues()[i]);
    url = url.withParameter ("app_id", creds.get ("qobuz", "app_id"));
    juce::StringPairArray h;
    h.set ("X-App-Id", creds.get ("qobuz", "app_id"));
    if (isSignedIn()) h.set ("X-User-Auth-Token", creds.get ("qobuz", "user_auth_token"));
    auto r = httpRequest ("GET", url, h);
    if (! r.ok()) lastError = "Qobuz " + endpoint + " : HTTP " + juce::String (r.status) + " " + r.json().getProperty ("message", {}).toString();
    return r;
}

bool QobuzProvider::signIn (const juce::String& email, const juce::String& password, juce::String& error)
{
    if (! isConfigured()) { error = "renseignez app_id / app_secret (accès partenaire Qobuz)"_t; return false; }
    juce::StringPairArray p;
    p.set ("email", email);
    p.set ("password", juce::MD5 (password.toUTF8()).toHexString());
    auto r = call ("user/login", p);
    const auto token = r.json().getProperty ("user_auth_token", {}).toString();
    if (token.isEmpty()) { error = lastError.isNotEmpty() ? lastError : juce::String ("identifiants refusés"_t); return false; }
    creds.set ("qobuz", "user_auth_token", token);
    creds.set ("qobuz", "email", email);
    return true;
}

TrackInfo QobuzProvider::parseTrack (const juce::var& j)
{
    TrackInfo t;
    t.source = "qobuz";
    t.externalId = j.getProperty ("id", {}).toString();
    t.title = j.getProperty ("title", {}).toString();
    const auto version = j.getProperty ("version", {}).toString();
    if (version.isNotEmpty()) t.title << " (" << version << ")";
    t.artist = j.getProperty ("performer", {}).getProperty ("name", {}).toString();
    const auto album = j.getProperty ("album", {});
    t.album = album.getProperty ("title", {}).toString();
    t.albumArtist = album.getProperty ("artist", {}).getProperty ("name", {}).toString();
    if (t.artist.isEmpty()) t.artist = t.albumArtist;
    t.artworkKey = album.getProperty ("image", {}).getProperty ("large", {}).toString();
    t.genre = album.getProperty ("genre", {}).getProperty ("name", {}).toString();
    t.year = album.getProperty ("release_date_original", {}).toString().substring (0, 4).getIntValue();
    t.composer = j.getProperty ("composer", {}).getProperty ("name", {}).toString();
    t.duration = (double) j.getProperty ("duration", 0);
    t.trackNumber = (int) j.getProperty ("track_number", 0);
    t.discNumber = (int) j.getProperty ("media_number", 1);
    t.codec = "FLAC";
    t.bitsPerSample = (int) j.getProperty ("maximum_bit_depth", 16);
    t.sampleRate = (double) j.getProperty ("maximum_sampling_rate", 44.1) * 1000.0;
    t.channels = 2;
    const auto isrc = j.getProperty ("isrc", {}).toString();
    if (isrc.isNotEmpty()) t.credits = "ISRC : " + isrc;
    return t;
}

std::vector<TrackInfo> QobuzProvider::searchTracks (const juce::String& query, int limit)
{
    juce::StringPairArray p;
    p.set ("query", query);
    p.set ("type", "tracks");
    p.set ("limit", juce::String (limit));
    auto r = call ("catalog/search", p);
    std::vector<TrackInfo> out;
    if (auto* items = r.json().getProperty ("tracks", {}).getProperty ("items", {}).getArray())
        for (auto& it : *items) out.push_back (parseTrack (it));
    return out;
}

std::vector<PlaylistInfo> QobuzProvider::getPlaylists()
{
    juce::StringPairArray p;
    p.set ("limit", "200");
    auto r = call ("playlist/getUserPlaylists", p);
    std::vector<PlaylistInfo> out;
    if (auto* items = r.json().getProperty ("playlists", {}).getProperty ("items", {}).getArray())
        for (auto& it : *items)
            out.push_back ({ 0, it.getProperty ("name", {}).toString(), "qobuz", it.getProperty ("id", {}).toString(),
                             (int) it.getProperty ("tracks_count", 0) });
    return out;
}

std::vector<TrackInfo> QobuzProvider::getPlaylistTracks (const juce::String& id)
{
    juce::StringPairArray p;
    p.set ("playlist_id", id);
    p.set ("extra", "tracks");
    p.set ("limit", "500");
    auto r = call ("playlist/get", p);
    std::vector<TrackInfo> out;
    if (auto* items = r.json().getProperty ("tracks", {}).getProperty ("items", {}).getArray())
        for (auto& it : *items) out.push_back (parseTrack (it));
    return out;
}

std::unique_ptr<juce::InputStream> QobuzProvider::openStream (const TrackInfo& track, juce::String& error)
{
    if (! isSignedIn()) { error = "Qobuz : non connecté"_t; return nullptr; }
    for (int fmt : { 27, 7, 6 })
    {
        if (fmt > maxFormatId) continue;
        const auto ts = juce::Time::currentTimeMillis() / 1000;
        juce::StringPairArray p;
        p.set ("track_id", track.externalId);
        p.set ("format_id", juce::String (fmt));
        p.set ("intent", "stream");
        p.set ("request_ts", juce::String (ts));
        p.set ("request_sig", requestSignature (track.externalId, fmt, ts, creds.get ("qobuz", "app_secret")));
        auto r = call ("track/getFileUrl", p);
        const auto url = r.json().getProperty ("url", {}).toString();
        if (url.isNotEmpty()) return std::make_unique<HttpStream> (juce::URL (url));
    }
    error = lastError.isNotEmpty() ? lastError : juce::String ("Qobuz : flux indisponible"_t);
    return nullptr;
}

//==============================================================================
// Tidal
//==============================================================================
bool TidalProvider::isConfigured() const { return creds.get ("tidal", "client_id").isNotEmpty(); }
bool TidalProvider::isSignedIn() const   { return creds.get ("tidal", "refresh_token").isNotEmpty(); }

bool TidalProvider::beginDeviceLogin (DeviceLogin& out, juce::String& error)
{
    if (! isConfigured()) { error = "renseignez le client_id fourni par Tidal"_t; return false; }
    juce::StringPairArray f;
    f.set ("client_id", creds.get ("tidal", "client_id"));
    f.set ("scope", "r_usr w_usr");
    juce::StringPairArray h;
    h.set ("Content-Type", "application/x-www-form-urlencoded");
    auto r = httpRequest ("POST", juce::URL ("https://auth.tidal.com/v1/oauth2/device_authorization"), h, formEncode (f));
    const auto j = r.json();
    out.deviceCode = j.getProperty ("deviceCode", {}).toString();
    out.userCode = j.getProperty ("userCode", {}).toString();
    out.verificationUri = j.getProperty ("verificationUriComplete", j.getProperty ("verificationUri", {})).toString();
    if (! out.verificationUri.startsWith ("http")) out.verificationUri = "https://" + out.verificationUri;
    out.interval = juce::jmax (2, (int) j.getProperty ("interval", 5));
    out.expiresIn = (int) j.getProperty ("expiresIn", 300);
    if (out.deviceCode.isEmpty()) { error = "Tidal : HTTP " + juce::String (r.status); return false; }
    return true;
}

bool TidalProvider::pollDeviceLogin (const DeviceLogin& login, juce::String& error)
{
    juce::StringPairArray f;
    f.set ("client_id", creds.get ("tidal", "client_id"));
    if (creds.get ("tidal", "client_secret").isNotEmpty()) f.set ("client_secret", creds.get ("tidal", "client_secret"));
    f.set ("device_code", login.deviceCode);
    f.set ("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    f.set ("scope", "r_usr w_usr");
    juce::StringPairArray h;
    h.set ("Content-Type", "application/x-www-form-urlencoded");
    auto r = httpRequest ("POST", juce::URL ("https://auth.tidal.com/v1/oauth2/token"), h, formEncode (f));
    const auto j = r.json();
    if (! r.ok()) { error = j.getProperty ("error", "authorization_pending").toString(); return false; }
    creds.set ("tidal", "access_token", j.getProperty ("access_token", {}).toString());
    creds.set ("tidal", "refresh_token", j.getProperty ("refresh_token", {}).toString());
    creds.set ("tidal", "user_id", j.getProperty ("user", {}).getProperty ("userId", {}).toString());
    creds.set ("tidal", "country", j.getProperty ("user", {}).getProperty ("countryCode", "FR").toString());
    return true;
}

bool TidalProvider::refreshToken()
{
    juce::StringPairArray f;
    f.set ("client_id", creds.get ("tidal", "client_id"));
    if (creds.get ("tidal", "client_secret").isNotEmpty()) f.set ("client_secret", creds.get ("tidal", "client_secret"));
    f.set ("refresh_token", creds.get ("tidal", "refresh_token"));
    f.set ("grant_type", "refresh_token");
    juce::StringPairArray h;
    h.set ("Content-Type", "application/x-www-form-urlencoded");
    auto r = httpRequest ("POST", juce::URL ("https://auth.tidal.com/v1/oauth2/token"), h, formEncode (f));
    const auto token = r.json().getProperty ("access_token", {}).toString();
    if (token.isEmpty()) return false;
    creds.set ("tidal", "access_token", token);
    return true;
}

HttpResponse TidalProvider::api (const juce::String& path, juce::StringPairArray params)
{
    params.set ("countryCode", creds.get ("tidal", "country").isNotEmpty() ? creds.get ("tidal", "country") : juce::String ("FR"));
    juce::URL url ("https://api.tidal.com/v1/" + path);
    for (int i = 0; i < params.size(); ++i) url = url.withParameter (params.getAllKeys()[i], params.getAllValues()[i]);
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        juce::StringPairArray h;
        h.set ("Authorization", "Bearer " + creds.get ("tidal", "access_token"));
        auto r = httpRequest ("GET", url, h);
        if (r.status == 401 && attempt == 0 && refreshToken()) continue;
        if (! r.ok()) lastError = "Tidal " + path + " : HTTP " + juce::String (r.status);
        return r;
    }
    return {};
}

TrackInfo TidalProvider::parseTrack (const juce::var& j)
{
    TrackInfo t;
    t.source = "tidal";
    t.externalId = j.getProperty ("id", {}).toString();
    t.title = j.getProperty ("title", {}).toString();
    const auto version = j.getProperty ("version", {}).toString();
    if (version.isNotEmpty()) t.title << " (" << version << ")";
    t.artist = firstArtist (j.getProperty ("artists", {}));
    if (t.artist.isEmpty()) t.artist = j.getProperty ("artist", {}).getProperty ("name", {}).toString();
    const auto album = j.getProperty ("album", {});
    t.album = album.getProperty ("title", {}).toString();
    const auto cover = album.getProperty ("cover", {}).toString();
    if (cover.isNotEmpty()) t.artworkKey = "https://resources.tidal.com/images/" + cover.replaceCharacter ('-', '/') + "/640x640.jpg";
    t.duration = (double) j.getProperty ("duration", 0);
    t.trackNumber = (int) j.getProperty ("trackNumber", 0);
    t.discNumber = (int) j.getProperty ("volumeNumber", 1);
    t.codec = "FLAC";
    const auto q = j.getProperty ("audioQuality", {}).toString();
    const auto tags = j.getProperty ("mediaMetadata", {}).getProperty ("tags", {});
    bool hiRes = q == "HI_RES_LOSSLESS";
    if (auto* a = tags.getArray()) for (auto& x : *a) hiRes |= x.toString() == "HIRES_LOSSLESS";
    t.bitsPerSample = hiRes ? 24 : 16;
    t.sampleRate = hiRes ? 96000.0 : 44100.0;   // exact rate is known only once the stream is opened
    t.channels = 2;
    return t;
}

std::vector<TrackInfo> TidalProvider::searchTracks (const juce::String& query, int limit)
{
    juce::StringPairArray p;
    p.set ("query", query);
    p.set ("types", "TRACKS");
    p.set ("limit", juce::String (limit));
    auto r = api ("search", p);
    std::vector<TrackInfo> out;
    if (auto* items = r.json().getProperty ("tracks", {}).getProperty ("items", {}).getArray())
        for (auto& it : *items) out.push_back (parseTrack (it));
    return out;
}

std::vector<PlaylistInfo> TidalProvider::getPlaylists()
{
    auto r = api ("users/" + creds.get ("tidal", "user_id") + "/playlists", {});
    std::vector<PlaylistInfo> out;
    if (auto* items = r.json().getProperty ("items", {}).getArray())
        for (auto& it : *items)
            out.push_back ({ 0, it.getProperty ("title", {}).toString(), "tidal", it.getProperty ("uuid", {}).toString(),
                             (int) it.getProperty ("numberOfTracks", 0) });
    return out;
}

std::vector<TrackInfo> TidalProvider::getPlaylistTracks (const juce::String& id)
{
    juce::StringPairArray p;
    p.set ("limit", "500");
    auto r = api ("playlists/" + id + "/items", p);
    std::vector<TrackInfo> out;
    if (auto* items = r.json().getProperty ("items", {}).getArray())
        for (auto& it : *items)
            if (it.getProperty ("type", "track").toString() == "track") out.push_back (parseTrack (it.getProperty ("item", {})));
    return out;
}

bool TidalProvider::parseMpd (const juce::String& mpd, DashManifest& out)
{
    auto xml = juce::XmlDocument::parse (mpd);
    if (xml == nullptr) return false;
    const juce::XmlElement* tmpl = nullptr;
    const juce::XmlElement* rep = nullptr;
    std::function<void (const juce::XmlElement&, const juce::XmlElement*)> walk = [&] (const juce::XmlElement& e, const juce::XmlElement* currentRep)
    {
        for (auto* c : e.getChildIterator())
        {
            const auto name = c->getTagNameWithoutNamespace();
            const juce::XmlElement* r = name == "Representation" ? c : currentRep;
            if (name == "SegmentTemplate" && tmpl == nullptr) { tmpl = c; rep = r; }
            walk (*c, r);
        }
    };
    walk (*xml, nullptr);
    if (tmpl == nullptr) return false;

    const auto repId = rep != nullptr ? rep->getStringAttribute ("id") : juce::String();
    const auto bandwidth = rep != nullptr ? rep->getStringAttribute ("bandwidth") : juce::String();
    out.codecs = rep != nullptr ? rep->getStringAttribute ("codecs") : juce::String();
    auto subst = [&] (juce::String s, int number)
    {
        s = s.replace ("$RepresentationID$", repId).replace ("$Bandwidth$", bandwidth);
        // $Number$ and $Number%0Nd$
        const int at = s.indexOf ("$Number");
        if (at >= 0)
        {
            const int end = s.indexOf (at + 1, "$");
            const auto fmt = s.substring (at + 7, end);
            juce::String num (number);
            if (fmt.startsWith ("%0")) num = num.paddedLeft ('0', fmt.substring (2).getIntValue());
            s = s.substring (0, at) + num + s.substring (end + 1);
        }
        return s;
    };

    out.initUrl = subst (tmpl->getStringAttribute ("initialization"), 0);
    const auto media = tmpl->getStringAttribute ("media");
    const int startNumber = tmpl->getIntAttribute ("startNumber", 1);
    int count = 0;
    for (auto* tl : tmpl->getChildIterator())
        if (tl->getTagNameWithoutNamespace() == "SegmentTimeline")
            for (auto* s : tl->getChildIterator())
                if (s->getTagNameWithoutNamespace() == "S") count += 1 + juce::jmax (0, s->getIntAttribute ("r", 0));
    for (int i = 0; i < count; ++i) out.segmentUrls.add (subst (media, startNumber + i));
    return out.initUrl.isNotEmpty() && ! out.segmentUrls.isEmpty();
}

juce::MemoryBlock TidalProvider::flacHeaderFromInitSegment (const juce::MemoryBlock& init)
{
    const auto* p = static_cast<const uint8_t*> (init.getData());
    const size_t n = init.getSize();
    for (size_t i = 4; i + 8 <= n; ++i)
    {
        if (std::memcmp (p + i, "dfLa", 4) != 0) continue;
        const size_t size = bytes::be32 (p + i - 4);
        if (size < 12 || i - 4 + size > n) continue;
        juce::MemoryBlock out;
        out.append ("fLaC", 4);
        out.append (p + i + 8, size - 12);      // after box header (8) + version/flags (4)
        return out;
    }
    return {};
}

juce::MemoryBlock TidalProvider::mdatPayload (const juce::MemoryBlock& seg)
{
    juce::MemoryBlock out;
    const auto* p = static_cast<const uint8_t*> (seg.getData());
    const size_t n = seg.getSize();
    size_t pos = 0;
    while (pos + 8 <= n)
    {
        uint64_t size = bytes::be32 (p + pos);
        size_t header = 8;
        if (size == 1 && pos + 16 <= n) { size = bytes::be64 (p + pos + 8); header = 16; }
        if (size == 0) size = n - pos;
        if (size < header || pos + size > n) break;
        if (std::memcmp (p + pos + 4, "mdat", 4) == 0) out.append (p + pos + header, (size_t) size - header);
        pos += (size_t) size;
    }
    return out;
}

std::unique_ptr<juce::InputStream> TidalProvider::openStream (const TrackInfo& track, juce::String& error)
{
    if (! isSignedIn()) { error = "Tidal : non connecté"_t; return nullptr; }
    juce::StringPairArray p;
    p.set ("audioquality", "HI_RES_LOSSLESS");
    p.set ("playbackmode", "STREAM");
    p.set ("assetpresentation", "FULL");
    auto r = api ("tracks/" + track.externalId + "/playbackinfo", p);
    const auto j = r.json();
    const auto mime = j.getProperty ("manifestMimeType", {}).toString();
    juce::MemoryOutputStream decoded;
    juce::Base64::convertFromBase64 (decoded, j.getProperty ("manifest", {}).toString());
    const auto manifest = decoded.toString();

    if (mime.contains ("vnd.tidal.bts"))
    {
        const auto m = juce::JSON::parse (manifest);
        const auto enc = m.getProperty ("encryptionType", "NONE").toString();
        if (enc != "NONE") { error = "Tidal : flux chiffré (DRM), lecture impossible hors de l'application Tidal"_t; return nullptr; }
        if (auto* urls = m.getProperty ("urls", {}).getArray())
            if (! urls->isEmpty()) return std::make_unique<HttpStream> (juce::URL (urls->getFirst().toString()));
    }
    else if (mime.contains ("dash+xml"))
    {
        DashManifest dm;
        if (parseMpd (manifest, dm) && dm.codecs.containsIgnoreCase ("flac"))
            return std::make_unique<DashFlacStream> (dm);
        error = "Tidal : manifeste DASH non pris en charge (" + dm.codecs + ")";
        return nullptr;
    }
    error = lastError.isNotEmpty() ? lastError : "Tidal : manifeste inattendu ("_t + mime + ")";
    return nullptr;
}

//==============================================================================
// Spotify
//==============================================================================
juce::String SpotifyProvider::codeChallenge (const juce::String& verifier)
{
    const juce::SHA256 sha (verifier.toUTF8());
    const auto raw = sha.getRawData();
    return base64Url (raw.getData(), raw.getSize());
}

bool SpotifyProvider::signIn (juce::String& error)
{
    if (! isConfigured()) { error = "aucun client_id Spotify : cette version a été compilée sans NX_SPOTIFY_CLIENT_ID, renseignez le vôtre"_t; return false; }
    const int port = 43821;
    OAuthLoopback loopback;   // listening before the browser opens: an already-granted consent redirects instantly
    if (! loopback.open (port, error)) return false;
    const auto redirect = "http://127.0.0.1:" + juce::String (port) + "/callback";
    const auto verifier = randomToken (64);
    juce::URL auth ("https://accounts.spotify.com/authorize");
    auth = auth.withParameter ("client_id", creds.get ("spotify", "client_id"))
               .withParameter ("response_type", "code")
               .withParameter ("redirect_uri", redirect)
               .withParameter ("code_challenge_method", "S256")
               .withParameter ("code_challenge", codeChallenge (verifier))
               .withParameter ("scope", "user-library-read playlist-read-private user-read-playback-state user-modify-playback-state");
    if (! auth.launchInDefaultBrowser()) { error = "impossible d'ouvrir le navigateur"_t; return false; }

    const auto code = loopback.waitForCode (300000, error);
    if (code.isEmpty()) return false;

    juce::StringPairArray f;
    f.set ("grant_type", "authorization_code");
    f.set ("code", code);
    f.set ("redirect_uri", redirect);
    f.set ("client_id", creds.get ("spotify", "client_id"));
    f.set ("code_verifier", verifier);
    juce::StringPairArray h;
    h.set ("Content-Type", "application/x-www-form-urlencoded");
    auto r = httpRequest ("POST", juce::URL ("https://accounts.spotify.com/api/token"), h, formEncode (f));
    const auto j = r.json();
    if (j.getProperty ("access_token", {}).toString().isEmpty())
    {
        const auto desc = j.getProperty ("error_description", {}).toString();
        error = desc.isNotEmpty() ? desc : r.body.substring (0, 200);
        if (error.containsIgnoreCase ("client") || error.containsIgnoreCase ("redirect"))
            error << " — vérifiez que l'application Spotify déclare l'adresse de redirection "_t << redirect;
        return false;
    }
    creds.set ("spotify", "access_token", j.getProperty ("access_token", {}).toString());
    creds.set ("spotify", "refresh_token", j.getProperty ("refresh_token", {}).toString());
    return true;
}

bool SpotifyProvider::refreshToken()
{
    juce::StringPairArray f;
    f.set ("grant_type", "refresh_token");
    f.set ("refresh_token", creds.get ("spotify", "refresh_token"));
    f.set ("client_id", creds.get ("spotify", "client_id"));
    juce::StringPairArray h;
    h.set ("Content-Type", "application/x-www-form-urlencoded");
    auto r = httpRequest ("POST", juce::URL ("https://accounts.spotify.com/api/token"), h, formEncode (f));
    const auto j = r.json();
    const auto token = j.getProperty ("access_token", {}).toString();
    if (token.isEmpty()) return false;
    creds.set ("spotify", "access_token", token);
    if (j.getProperty ("refresh_token", {}).toString().isNotEmpty()) creds.set ("spotify", "refresh_token", j.getProperty ("refresh_token", {}).toString());
    return true;
}

HttpResponse SpotifyProvider::api (const juce::String& method, const juce::String& path, const juce::String& body)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        juce::StringPairArray h;
        h.set ("Authorization", "Bearer " + creds.get ("spotify", "access_token"));
        if (body.isNotEmpty()) h.set ("Content-Type", "application/json");
        auto r = httpRequest (method, juce::URL ("https://api.spotify.com/v1/" + path), h, body);
        if (r.status == 401 && attempt == 0 && refreshToken()) continue;
        if (! r.ok()) lastError = "Spotify " + path + " : HTTP " + juce::String (r.status);
        return r;
    }
    return {};
}

TrackInfo SpotifyProvider::parseTrack (const juce::var& j)
{
    TrackInfo t;
    t.source = "spotify";
    t.externalId = j.getProperty ("id", {}).toString();
    t.uri = j.getProperty ("uri", {}).toString();
    t.title = j.getProperty ("name", {}).toString();
    t.artist = firstArtist (j.getProperty ("artists", {}));
    const auto album = j.getProperty ("album", {});
    t.album = album.getProperty ("name", {}).toString();
    t.albumArtist = firstArtist (album.getProperty ("artists", {}));
    t.year = album.getProperty ("release_date", {}).toString().substring (0, 4).getIntValue();
    if (auto* imgs = album.getProperty ("images", {}).getArray())
        if (! imgs->isEmpty()) t.artworkKey = imgs->getFirst().getProperty ("url", {}).toString();
    t.duration = (double) j.getProperty ("duration_ms", 0) / 1000.0;
    t.trackNumber = (int) j.getProperty ("track_number", 0);
    t.discNumber = (int) j.getProperty ("disc_number", 1);
    t.codec = "Spotify";
    t.lossy = true;
    return t;
}

std::vector<TrackInfo> SpotifyProvider::searchTracks (const juce::String& query, int limit)
{
    // Since February 2026 the search limit is 10 per page (development mode): page with offset.
    std::vector<TrackInfo> out;
    const int wanted = juce::jlimit (1, 30, limit);
    for (int offset = 0; offset < wanted; offset += 10)
    {
        auto r = api ("GET", "search?type=track&limit=" + juce::String (juce::jmin (10, wanted - offset)) + "&offset=" + juce::String (offset)
                                 + "&q=" + juce::URL::addEscapeChars (query, true));
        auto* items = r.json().getProperty ("tracks", {}).getProperty ("items", {}).getArray();
        if (items == nullptr || items->isEmpty()) break;
        for (auto& it : *items) out.push_back (parseTrack (it));
        if (items->size() < 10) break;
    }
    return out;
}

std::vector<PlaylistInfo> SpotifyProvider::getPlaylists()
{
    auto r = api ("GET", "me/playlists?limit=50");
    std::vector<PlaylistInfo> out;
    if (auto* items = r.json().getProperty ("items", {}).getArray())
        for (auto& it : *items)
            out.push_back ({ 0, it.getProperty ("name", {}).toString(), "spotify", it.getProperty ("id", {}).toString(),
                             (int) (it.getProperty ("items", {}).isObject() ? it.getProperty ("items", {}) : it.getProperty ("tracks", {})).getProperty ("total", 0) });
    return out;
}

std::vector<TrackInfo> SpotifyProvider::getPlaylistTracks (const juce::String& id)
{
    std::vector<TrackInfo> out;
    juce::String path = "playlists/" + id + "/items?limit=100";   // "/tracks" was removed in February 2026
    for (int page = 0; page < 20 && path.isNotEmpty(); ++page)
    {
        auto r = api ("GET", path);
        const auto j = r.json();
        if (auto* items = j.getProperty ("items", {}).getArray())
            for (auto& it : *items)
            {
                const auto entry = it.getProperty ("item", {}).isObject() ? it.getProperty ("item", {}) : it.getProperty ("track", {});
                if (entry.isObject() && entry.getProperty ("type", "track").toString() == "track") out.push_back (parseTrack (entry));
            }
        path = j.getProperty ("next", {}).toString().fromFirstOccurrenceOf ("/v1/", false, false);
    }
    return out;
}

bool SpotifyProvider::playExternally (const TrackInfo& track, juce::String& error)
{
    const auto uri = track.uri.startsWith ("spotify:") ? track.uri : "spotify:track:" + track.externalId;
    auto r = api ("PUT", "me/player/play", "{\"uris\":[\"" + uri + "\"]}");
    if (r.ok()) return true;
    // No active Spotify Connect device: open the Spotify app on this track.
    if (juce::URL (uri).launchInDefaultBrowser()) return true;
    error = lastError;
    return false;
}

//==============================================================================
// YouTube Music
//==============================================================================
HttpResponse YouTubeMusicProvider::api (const juce::String& path, juce::StringPairArray params)
{
    juce::URL url ("https://www.googleapis.com/youtube/v3/" + path);
    const auto key = creds.get ("youtube", "api_key");
    if (key.isNotEmpty()) params.set ("key", key);
    for (int i = 0; i < params.size(); ++i) url = url.withParameter (params.getAllKeys()[i], params.getAllValues()[i]);
    juce::StringPairArray h;
    if (creds.get ("youtube", "access_token").isNotEmpty()) h.set ("Authorization", "Bearer " + creds.get ("youtube", "access_token"));
    auto r = httpRequest ("GET", url, h);
    if (! r.ok()) lastError = "YouTube " + path + " : HTTP " + juce::String (r.status);
    return r;
}

TrackInfo YouTubeMusicProvider::parseVideo (const juce::var& item)
{
    TrackInfo t;
    t.source = "youtube";
    const auto id = item.getProperty ("id", {});
    t.externalId = id.isObject() ? id.getProperty ("videoId", {}).toString()
                                 : item.getProperty ("snippet", {}).getProperty ("resourceId", {}).getProperty ("videoId", id).toString();
    const auto sn = item.getProperty ("snippet", {});
    auto title = juce::String (sn.getProperty ("title", {}).toString());
    auto channel = sn.getProperty ("videoOwnerChannelTitle", sn.getProperty ("channelTitle", {})).toString().replace (" - Topic", "");
    // "Artist - Title" is the usual convention for music videos
    if (title.contains (" - ") && channel.isEmpty()) { channel = title.upToFirstOccurrenceOf (" - ", false, false); title = title.fromFirstOccurrenceOf (" - ", false, false); }
    t.title = title;
    t.artist = channel;
    t.uri = "https://music.youtube.com/watch?v=" + t.externalId;
    t.artworkKey = sn.getProperty ("thumbnails", {}).getProperty ("high", {}).getProperty ("url", {}).toString();
    t.codec = "YouTube";
    t.lossy = true;
    return t;
}

std::vector<TrackInfo> YouTubeMusicProvider::searchTracks (const juce::String& query, int limit)
{
    juce::StringPairArray p;
    p.set ("part", "snippet");
    p.set ("type", "video");
    p.set ("videoCategoryId", "10");
    p.set ("maxResults", juce::String (juce::jlimit (1, 50, limit)));
    p.set ("q", query);
    auto r = api ("search", p);
    std::vector<TrackInfo> out;
    if (auto* items = r.json().getProperty ("items", {}).getArray())
        for (auto& it : *items) out.push_back (parseVideo (it));
    return out;
}

std::vector<PlaylistInfo> YouTubeMusicProvider::getPlaylists()
{
    std::vector<PlaylistInfo> out;
    // With an OAuth token: the user's playlists; otherwise the public playlists the user added by id.
    if (creds.get ("youtube", "access_token").isNotEmpty())
    {
        juce::StringPairArray p;
        p.set ("part", "snippet,contentDetails");
        p.set ("mine", "true");
        p.set ("maxResults", "50");
        auto r = api ("playlists", p);
        if (auto* items = r.json().getProperty ("items", {}).getArray())
            for (auto& it : *items)
                out.push_back ({ 0, it.getProperty ("snippet", {}).getProperty ("title", {}).toString(), "youtube",
                                 it.getProperty ("id", {}).toString(), (int) it.getProperty ("contentDetails", {}).getProperty ("itemCount", 0) });
    }
    for (auto& id : juce::StringArray::fromTokens (creds.get ("youtube", "playlists"), ",", {}))
        if (id.trim().isNotEmpty()) out.push_back ({ 0, "Playlist " + id.trim(), "youtube", id.trim(), 0 });
    return out;
}

std::vector<TrackInfo> YouTubeMusicProvider::getPlaylistTracks (const juce::String& id)
{
    std::vector<TrackInfo> out;
    juce::String pageToken;
    for (int page = 0; page < 20; ++page)
    {
        juce::StringPairArray p;
        p.set ("part", "snippet");
        p.set ("playlistId", id);
        p.set ("maxResults", "50");
        if (pageToken.isNotEmpty()) p.set ("pageToken", pageToken);
        auto r = api ("playlistItems", p);
        const auto j = r.json();
        if (auto* items = j.getProperty ("items", {}).getArray())
            for (auto& it : *items) out.push_back (parseVideo (it));
        pageToken = j.getProperty ("nextPageToken", {}).toString();
        if (pageToken.isEmpty()) break;
    }
    return out;
}

bool YouTubeMusicProvider::playExternally (const TrackInfo& track, juce::String& error)
{
    if (juce::URL ("https://music.youtube.com/watch?v=" + track.externalId).launchInDefaultBrowser()) return true;
    error = "navigateur introuvable"_t;
    return false;
}

} // namespace nx
