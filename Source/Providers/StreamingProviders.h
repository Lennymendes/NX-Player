// NX Player — MIT License
// Streaming services, through their official APIs and with credentials the user (or the
// distributor of the build) obtains from each service. Nothing is extracted from the services'
// own apps, and no protection is bypassed:
//
//  Qobuz   : partner API (app_id / app_secret issued by Qobuz) -> FLAC up to 24 / 192 into the engine
//  Tidal   : OAuth device login with a client id issued by Tidal -> FLAC (BTS) or DASH / fMP4 FLAC
//            (Max, up to 24 / 192) re-wrapped into a plain FLAC stream for the engine
//  Spotify : Web API (PKCE, user's own client id) for search / library / playlists; playback is
//            handed to the user's Spotify Connect device (Spotify does not license raw streams)
//  YouTube Music : YouTube Data API v3 (API key / OAuth) for search and playlists; playback opens
//            music.youtube.com (the YouTube terms forbid extracting the audio)
#pragma once

#include "MediaProvider.h"
#include "Http.h"

namespace nx
{

class QobuzProvider final : public MediaProvider
{
public:
    explicit QobuzProvider (CredentialStore& c) : creds (c) {}
    juce::String getId() const override          { return "qobuz"; }
    juce::String getDisplayName() const override { return "Qobuz"; }
    bool isConfigured() const override;
    bool isSignedIn() const override;

    bool signIn (const juce::String& email, const juce::String& password, juce::String& error);
    void signOut() { creds.set ("qobuz", "user_auth_token", {}); }

    std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) override;
    std::vector<PlaylistInfo> getPlaylists() override;
    std::vector<TrackInfo> getPlaylistTracks (const juce::String& playlistId) override;
    std::unique_ptr<juce::InputStream> openStream (const TrackInfo& track, juce::String& error) override;

    /** 27 = FLAC 24 bits jusqu'à 192 kHz, 7 = 24 / 96, 6 = 16 / 44,1. */
    int maxFormatId = 27;

    static juce::String requestSignature (const juce::String& trackId, int formatId, juce::int64 timestamp, const juce::String& appSecret);
    static TrackInfo parseTrack (const juce::var& json);

private:
    HttpResponse call (const juce::String& endpoint, const juce::StringPairArray& params);
    CredentialStore& creds;
};

class TidalProvider final : public MediaProvider
{
public:
    explicit TidalProvider (CredentialStore& c) : creds (c) {}
    juce::String getId() const override          { return "tidal"; }
    juce::String getDisplayName() const override { return "Tidal"; }
    bool isConfigured() const override;
    bool isSignedIn() const override;

    struct DeviceLogin { juce::String deviceCode, userCode, verificationUri; int interval = 5, expiresIn = 300; };
    /** Step 1: shows a code the user enters on link.tidal.com. */
    bool beginDeviceLogin (DeviceLogin& out, juce::String& error);
    /** Step 2 (poll every interval seconds): true once the user has approved. */
    bool pollDeviceLogin (const DeviceLogin& login, juce::String& error);
    void signOut() { creds.clear ("tidal"); }

    std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) override;
    std::vector<PlaylistInfo> getPlaylists() override;
    std::vector<TrackInfo> getPlaylistTracks (const juce::String& playlistId) override;
    std::unique_ptr<juce::InputStream> openStream (const TrackInfo& track, juce::String& error) override;

    static TrackInfo parseTrack (const juce::var& json);

    // DASH / fMP4 helpers (tests)
    struct DashManifest { juce::String initUrl; juce::StringArray segmentUrls; juce::String codecs; };
    static bool parseMpd (const juce::String& mpd, DashManifest& out);
    /** dfLa box of an fMP4 init segment -> "fLaC" + metadata blocks. */
    static juce::MemoryBlock flacHeaderFromInitSegment (const juce::MemoryBlock& init);
    /** Concatenated mdat payloads of a media segment (FLAC frames). */
    static juce::MemoryBlock mdatPayload (const juce::MemoryBlock& segment);

private:
    bool refreshToken();
    HttpResponse api (const juce::String& path, juce::StringPairArray params);
    CredentialStore& creds;
};

class SpotifyProvider final : public MediaProvider
{
public:
    explicit SpotifyProvider (CredentialStore& c) : creds (c) {}
    juce::String getId() const override          { return "spotify"; }
    juce::String getDisplayName() const override { return "Spotify"; }
    PlaybackRoute getRoute() const override      { return PlaybackRoute::External; }
    bool isConfigured() const override           { return creds.get ("spotify", "client_id").isNotEmpty(); }
    bool isSignedIn() const override             { return creds.get ("spotify", "refresh_token").isNotEmpty(); }

    /** Opens the browser on Spotify's consent page and waits for the loopback redirect (blocking, ≤ 3 min). */
    bool signIn (juce::String& error);
    void signOut() { creds.set ("spotify", "refresh_token", {}); creds.set ("spotify", "access_token", {}); }

    std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) override;
    std::vector<PlaylistInfo> getPlaylists() override;
    std::vector<TrackInfo> getPlaylistTracks (const juce::String& playlistId) override;
    bool playExternally (const TrackInfo& track, juce::String& error) override;

    static juce::String codeChallenge (const juce::String& verifier);
    static TrackInfo parseTrack (const juce::var& json);

private:
    bool refreshToken();
    HttpResponse api (const juce::String& method, const juce::String& path, const juce::String& body = {});
    CredentialStore& creds;
};

class YouTubeMusicProvider final : public MediaProvider
{
public:
    explicit YouTubeMusicProvider (CredentialStore& c) : creds (c) {}
    juce::String getId() const override          { return "youtube"; }
    juce::String getDisplayName() const override { return "YouTube Music"; }
    PlaybackRoute getRoute() const override      { return PlaybackRoute::External; }
    bool isConfigured() const override           { return creds.get ("youtube", "api_key").isNotEmpty() || creds.get ("youtube", "client_id").isNotEmpty(); }
    bool isSignedIn() const override             { return isConfigured(); }

    std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) override;
    std::vector<PlaylistInfo> getPlaylists() override;
    std::vector<TrackInfo> getPlaylistTracks (const juce::String& playlistId) override;
    bool playExternally (const TrackInfo& track, juce::String& error) override;

    static TrackInfo parseVideo (const juce::var& item);

private:
    HttpResponse api (const juce::String& path, juce::StringPairArray params);
    CredentialStore& creds;
};

/** Loopback HTTP listener for OAuth redirects (http://127.0.0.1:<port>/callback?code=...).
    open() it BEFORE launching the browser: when consent was already given, the provider redirects at once. */
class OAuthLoopback
{
public:
    bool open (int port, juce::String& error);
    /** Serves every connection (preconnects, favicon...) until /callback brings a code or an error. */
    juce::String waitForCode (int timeoutMs, juce::String& error);
    void close() { listener.close(); }

private:
    juce::StreamingSocket listener;
};

/** Convenience: open + wait (the listener opens late, prefer OAuthLoopback). */
juce::String waitForOAuthRedirect (int port, int timeoutMs, juce::String& error);

} // namespace nx
