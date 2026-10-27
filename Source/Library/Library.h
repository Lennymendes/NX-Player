// NX Player — MIT License
// Unified library (SQLite): local files, NAS / UPnP servers and streaming services live in the same
// tables, each row tagged with its source. Albums and artists are derived views of the tracks.
#pragma once

#include "Sqlite.h"
#include "TagReader.h"
#include "../Core/Format.h"
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <optional>
#include <vector>

namespace nx
{

struct TrackInfo
{
    juce::int64 id = 0;
    juce::String source = "local";   // "local", "upnp:<uuid>", "qobuz", "tidal", "spotify", "youtube"
    juce::String externalId;         // path for local files, provider id otherwise
    juce::String uri;                // file path or stream URL (may be resolved later)
    juce::String title, artist, albumArtist, album, genre, composer;
    int year = 0, trackNumber = 0, discNumber = 0;
    double duration = 0.0;
    juce::String codec;
    double sampleRate = 0.0;
    int bitsPerSample = 0, channels = 0;
    bool isDsd = false, lossy = false;
    juce::int64 fileSize = 0, modified = 0, added = 0;
    juce::String artworkKey;         // file name in the artwork cache, or an http(s) URL
    juce::String lyricsPath;         // sidecar .lrc
    juce::String credits;            // newline separated "Rôle : Nom"
    int playCount = 0;
    bool favourite = false;

    juce::String albumKey() const { return (albumArtist.isNotEmpty() ? albumArtist : artist) + "\x1f" + album; }
    juce::String resolutionBadge() const;     // "24/192", "DSD128", "16/44,1", "MP3"
    juce::String displayArtist() const { return artist.isNotEmpty() ? artist : albumArtist; }
};

struct AlbumInfo
{
    juce::String title, artist, artworkKey, source;
    int year = 0, trackCount = 0;
    double maxRate = 0.0;
    int maxBits = 0;
    bool hasDsd = false, lossy = false;
    double duration = 0.0;
    juce::int64 added = 0;
    juce::String nasSource;            // "upnp:<uuid>" when (part of) the album comes from a NAS / Jellyfin server
    bool favourite = false;
    juce::String genre;
    juce::String badge() const;
    bool fromNas() const { return nasSource.isNotEmpty(); }
};

struct ArtistInfo { juce::String name; int albumCount = 0, trackCount = 0; juce::String artworkKey; };

/** Online artist information (photo, banner, biography), cached in the database. */
struct ArtistMeta
{
    juce::String name, imageUrl, bannerUrl, bio, bioSource, country, genre;
    juce::int64 fetched = 0;                 // ms since epoch, 0 = never
    bool found = false;
};

struct GenreInfo { juce::String name; int albumCount = 0, trackCount = 0; juce::String artworkKey; };

/** Quality filters of the library pills (Audirvana style). */
enum class QualityFilter { Any, Dxd, Dsd, HiRes, Cd };

struct PlaylistInfo { juce::int64 id = 0; juce::String name, source, externalId; int trackCount = 0; };

class Library
{
public:
    Library();
    ~Library();

    /** Opens (creates) the database. dataDir also holds the artwork cache. */
    bool open (const juce::File& dataDir, juce::String& error);
    bool openInMemory (const juce::File& artworkDir, juce::String& error);
    juce::File getArtworkDir() const { return artworkDir; }

    //== Tracks ===============================================================
    /** Insert or update by (source, externalId). Returns the row id. */
    juce::int64 upsert (TrackInfo& t);
    bool remove (const juce::String& source, const juce::String& externalId);
    int removeMissingUnder (const juce::String& folderPath, const juce::StringArray& stillPresent);
    juce::StringArray getLocalPathsInDirectory (const juce::File& dir);
    std::optional<TrackInfo> getTrack (juce::int64 id);
    std::optional<TrackInfo> findTrack (const juce::String& source, const juce::String& externalId);
    std::vector<TrackInfo> getTracks (const juce::String& orderBy = "title", int limit = 5000, const juce::String& sourceFilter = {});
    std::vector<TrackInfo> getAlbumTracks (const juce::String& artist, const juce::String& album);
    std::vector<TrackInfo> getArtistTracks (const juce::String& artist);
    std::vector<TrackInfo> getRecentlyAdded (int limit = 50);
    std::vector<TrackInfo> search (const juce::String& query, int limit = 200);
    int countTracks (const juce::String& sourceFilter = {});

    //== Derived views ========================================================
    std::vector<AlbumInfo> getAlbums (const juce::String& orderBy = "added", int limit = 2000, const juce::String& sourceFilter = {});
    std::vector<ArtistInfo> getArtists (int limit = 5000);
    /** Albums with at least one track matching the quality filter (and the genre, if given). */
    std::vector<AlbumInfo> getAlbumsFiltered (QualityFilter q, const juce::String& genre = {}, const juce::String& orderBy = "artist", int limit = 5000);
    std::vector<TrackInfo> getTracksFiltered (QualityFilter q, const juce::String& genre = {}, int limit = 20000);
    int countTracksFiltered (QualityFilter q);
    std::vector<GenreInfo> getGenres();
    static juce::String qualityCondition (QualityFilter q);   // SQL condition on the tracks table

    //== Favourites ===========================================================
    std::vector<TrackInfo> getFavouriteTracks();
    std::vector<AlbumInfo> getFavouriteAlbums();
    void setAlbumFavourite (const juce::String& artist, const juce::String& album, bool fav);
    bool isAlbumFavourite (const juce::String& artist, const juce::String& album);

    //== Removal (NAS clean-up) ===============================================
    /** Removes an album's tracks from the library (only those whose source starts with sourcePrefix, if given). */
    int removeAlbum (const juce::String& artist, const juce::String& album, const juce::String& sourcePrefix = {});
    /** Removes every track whose source starts with prefix ("upnp" = everything imported from NAS servers). */
    int removeSourcePrefix (const juce::String& prefix);
    /** Removes the tracks of the NAS servers ("upnp:<udn>") that are not in available. */
    int removeNasSourcesExcept (const juce::StringArray& availableSources);
    juce::StringArray getSources (const juce::String& prefix);
    int countAlbumsFromSourcePrefix (const juce::String& prefix);

    //== Artist information ===================================================
    std::optional<ArtistMeta> getArtistMeta (const juce::String& name);
    void setArtistMeta (const ArtistMeta& meta);

    //== Playlists ============================================================
    juce::int64 createPlaylist (const juce::String& name, const juce::String& source = "local", const juce::String& externalId = {});
    void setPlaylistTracks (juce::int64 playlistId, const std::vector<juce::int64>& trackIds);
    std::vector<PlaylistInfo> getPlaylists();
    std::vector<TrackInfo> getPlaylistTracks (juce::int64 playlistId);
    /** Appends tracks (ids) at the end of a playlist. Returns how many were added. */
    int appendToPlaylist (juce::int64 playlistId, const std::vector<juce::int64>& trackIds);
    void removeFromPlaylist (juce::int64 playlistId, int position);
    void deletePlaylist (juce::int64 playlistId);
    void renamePlaylist (juce::int64 playlistId, const juce::String& name);

    //== Folders & history ====================================================
    void addFolder (const juce::File& folder, const juce::String& kind = "local");
    void removeFolder (const juce::File& folder);
    juce::StringArray getFolders();
    void markPlayed (juce::int64 trackId);
    void setFavourite (juce::int64 trackId, bool fav);

    //== Offline copies (tracks downloaded from a NAS / Jellyfin server) ====
    void markOffline (const juce::String& localPath, const TrackInfo& origin);
    /** A local track that was downloaded from a server ("hors-ligne" badge). */
    bool isOfflineCopy (const TrackInfo& localTrack) const;
    /** Path of the local copy of a remote track, empty if it was not downloaded (or the file is gone). */
    juce::String getOfflineCopy (const TrackInfo& remoteTrack) const;

    /** Stores artwork bytes in the cache (content-addressed) and returns its key. */
    juce::String storeArtwork (const juce::MemoryBlock& image, const juce::String& mime);
    juce::File getArtworkFile (const juce::String& key) const;

    /** Message-thread notification after scans / imports change the tables. */
    std::function<void()> onChanged;
    void notifyChanged();

    SqliteDb& getDb() noexcept { return db; }

private:
    bool createSchema (juce::String& error);
    void loadOfflineIndex();
    mutable std::mutex offlineLock;
    std::set<juce::String> offlinePaths;
    std::map<juce::String, juce::String> offlineOrigins;   // "source|id" and "uri|url" -> local path
    static TrackInfo rowToTrack (SqliteStatement& s);
    static const char* trackColumns();

    SqliteDb db;
    juce::File artworkDir;
    std::shared_ptr<int> alive = std::make_shared<int> (0);   // async notifications check it
    bool hasFts = false;
};

/** Fills a TrackInfo from a local file: format from the decoder, tags, artwork (embedded or folder.jpg). */
bool readLocalTrack (const juce::File& file, Library& library, TrackInfo& out);

} // namespace nx
