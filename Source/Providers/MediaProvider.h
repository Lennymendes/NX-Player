// NX Player — MIT License
// Sources of music other than the local library, and the UnifiedMediaProvider that merges them.
//
//  Engine route   : the provider hands an InputStream to NX Player's own decoders (UPnP / DLNA,
//                   Qobuz, Tidal) -> bit-perfect path, automatic DAC rate switching.
//  External route : the service's licence only allows its own player (Spotify, YouTube Music):
//                   NX Player browses / searches with the official API and starts playback in the
//                   service's player (Spotify Connect, music.youtube.com). The Signal Path says so.
#pragma once

#include "../Library/Library.h"
#include "../Engine/PlaybackEngine.h"
#include <juce_events/juce_events.h>
#include <memory>
#include <vector>

namespace nx
{

enum class PlaybackRoute { Engine, External };

/** Small JSON file in the user's data folder with account settings and tokens (chmod 600). */
class CredentialStore
{
public:
    explicit CredentialStore (juce::File file);
    /** Stored value, or the credential compiled into this build (see NX_SPOTIFY_CLIENT_ID... in CMake). */
    juce::String get (const juce::String& provider, const juce::String& key) const;
    /** Credential compiled into this build, empty if none. */
    static juce::String builtIn (const juce::String& provider, const juce::String& key);
    void set (const juce::String& provider, const juce::String& key, const juce::String& value);
    void clear (const juce::String& provider);
    juce::File getFile() const { return file; }

private:
    void save() const;
    juce::File file;
    juce::var root;
    mutable juce::CriticalSection lock;
};

class MediaProvider
{
public:
    virtual ~MediaProvider() = default;

    virtual juce::String getId() const = 0;              // "qobuz", "tidal", "spotify", "youtube", "upnp"
    virtual juce::String getDisplayName() const = 0;
    virtual PlaybackRoute getRoute() const { return PlaybackRoute::Engine; }

    /** Credentials present (partner app id, client id...). */
    virtual bool isConfigured() const = 0;
    virtual bool isSignedIn() const = 0;
    virtual juce::String getStatusText() const;

    // Blocking calls: UnifiedMediaProvider runs them on its thread pool.
    virtual std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) = 0;
    virtual std::vector<PlaylistInfo> getPlaylists() { return {}; }
    virtual std::vector<TrackInfo> getPlaylistTracks (const juce::String& externalPlaylistId) { juce::ignoreUnused (externalPlaylistId); return {}; }

    /** Engine route: the audio bytes (decoder thread). */
    virtual std::unique_ptr<juce::InputStream> openStream (const TrackInfo& track, juce::String& error)
    {
        juce::ignoreUnused (track);
        error = "lecture directe non disponible"_t;
        return nullptr;
    }

    /** External route: start playback in the service's own player. */
    virtual bool playExternally (const TrackInfo& track, juce::String& error)
    {
        juce::ignoreUnused (track);
        error = "non pris en charge"_t;
        return false;
    }

    juce::String lastError;
};

class UnifiedMediaProvider
{
public:
    UnifiedMediaProvider (Library& library, CredentialStore& credentials);
    ~UnifiedMediaProvider();

    void addProvider (std::unique_ptr<MediaProvider> p);
    MediaProvider* getProvider (const juce::String& id) const;
    const std::vector<std::unique_ptr<MediaProvider>>& getProviders() const noexcept { return providers; }
    CredentialStore& getCredentials() noexcept { return credentials; }

    /** Library + every signed-in service, merged and deduplicated; callback on the message thread. */
    void search (const juce::String& query, std::function<void (std::vector<TrackInfo>)> callback, int limitPerSource = 50);
    std::vector<TrackInfo> searchBlocking (const juce::String& query, int limitPerSource = 50);

    /** Copies a service playlist into the library (tracks + playlist rows). Background, callback on the message thread. */
    void importPlaylist (const juce::String& providerId, const PlaylistInfo& playlist, std::function<void (int)> done);

    /** What the engine plays for a library row. */
    PlayableItem toPlayable (const TrackInfo& t) const;
    /** Stream resolver installed on the PlaybackEngine. */
    std::unique_ptr<juce::InputStream> openStream (const PlayableItem& item);
    PlaybackRoute routeFor (const TrackInfo& t) const;

    static juce::String dedupeKey (const TrackInfo& t);
    static std::vector<TrackInfo> merge (std::vector<std::vector<TrackInfo>> lists);

private:
    Library& library;
    CredentialStore& credentials;
    std::vector<std::unique_ptr<MediaProvider>> providers;
    juce::ThreadPool pool { 4 };
};

} // namespace nx
