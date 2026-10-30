// NX Player — MIT License
#include "Library.h"
#include "../Audio/AudioDecoder.h"
#include "../Audio/SacdIso.h"
#include "../Core/ImageDecode.h"
#include <juce_cryptography/juce_cryptography.h>

namespace nx
{

juce::String TrackInfo::resolutionBadge() const
{
    if (isDsd) return "DSD" + juce::String ((int) std::lround (sampleRate / 44100.0));
    if (lossy) return codec.isNotEmpty() ? codec : juce::String ("Lossy");
    if (sampleRate <= 0) return codec;
    const auto khz = sampleRate / 1000.0;
    const auto k = std::fmod (khz, 1.0) == 0.0 ? juce::String ((int) khz) : juce::String (khz, 1).replaceCharacter ('.', ',');
    return juce::String (bitsPerSample > 0 ? bitsPerSample : 16) + "/" + k;
}

juce::String AlbumInfo::badge() const
{
    TrackInfo t;
    t.isDsd = hasDsd;
    t.lossy = lossy;
    t.sampleRate = hasDsd ? maxRate : maxRate;
    t.bitsPerSample = maxBits;
    return t.resolutionBadge();
}

namespace
{
    int formatRank (const AlbumInfo& a)
    {
        if (a.lossy) return 5;
        if (a.hasDsd) return 2;
        if (a.maxRate >= 352000.0) return 1;                                  // DXD
        if (a.maxRate > 48000.0 || a.maxBits > 16) return 3;                  // Hi-Res PCM
        return 4;                                                             // CD
    }
}

void sortAlbums (std::vector<AlbumInfo>& v, const juce::String& mode)
{
    auto cmp = [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b, false) < 0; };
    if (mode == "title")
        std::stable_sort (v.begin(), v.end(), [&] (const AlbumInfo& a, const AlbumInfo& b) { return cmp (a.title, b.title); });
    else if (mode == "artist")
        std::stable_sort (v.begin(), v.end(), [&] (const AlbumInfo& a, const AlbumInfo& b)
                          { const int c = a.artist.compareNatural (b.artist, false); return c != 0 ? c < 0 : a.year < b.year; });
    else if (mode == "added")
        std::stable_sort (v.begin(), v.end(), [] (const AlbumInfo& a, const AlbumInfo& b) { return a.added > b.added; });
    else if (mode == "format")
        std::stable_sort (v.begin(), v.end(), [] (const AlbumInfo& a, const AlbumInfo& b)
                          { const int ra = formatRank (a), rb = formatRank (b); return ra != rb ? ra < rb : a.maxRate > b.maxRate; });
}

Library::Library() = default;
Library::~Library() = default;

const char* Library::trackColumns()
{
    return "id, source, external_id, uri, title, artist, album_artist, album, genre, composer, year, track_no, disc_no, duration, "
           "codec, sample_rate, bits, channels, is_dsd, lossy, file_size, mtime, added, artwork, lyrics_path, credits, play_count, favorite";
}

TrackInfo Library::rowToTrack (SqliteStatement& s)
{
    TrackInfo t;
    int c = 0;
    t.id = s.getInt64 (c++); t.source = s.getText (c++); t.externalId = s.getText (c++); t.uri = s.getText (c++);
    t.title = s.getText (c++); t.artist = s.getText (c++); t.albumArtist = s.getText (c++); t.album = s.getText (c++);
    t.genre = s.getText (c++); t.composer = s.getText (c++); t.year = s.getInt (c++); t.trackNumber = s.getInt (c++);
    t.discNumber = s.getInt (c++); t.duration = s.getDouble (c++); t.codec = s.getText (c++); t.sampleRate = s.getDouble (c++);
    t.bitsPerSample = s.getInt (c++); t.channels = s.getInt (c++); t.isDsd = s.getInt (c++) != 0; t.lossy = s.getInt (c++) != 0;
    t.fileSize = s.getInt64 (c++); t.modified = s.getInt64 (c++); t.added = s.getInt64 (c++); t.artworkKey = s.getText (c++);
    t.lyricsPath = s.getText (c++); t.credits = s.getText (c++); t.playCount = s.getInt (c++); t.favourite = s.getInt (c++) != 0;
    return t;
}

bool Library::open (const juce::File& dataDir, juce::String& error)
{
    artworkDir = dataDir.getChildFile ("Artwork");
    artworkDir.createDirectory();
    if (! db.open (dataDir.getChildFile ("library.sqlite"), error)) return false;
    return createSchema (error);
}

bool Library::openInMemory (const juce::File& artDir, juce::String& error)
{
    artworkDir = artDir;
    artworkDir.createDirectory();
    if (! db.openInMemory (error)) return false;
    return createSchema (error);
}

bool Library::createSchema (juce::String& error)
{
    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY, source TEXT NOT NULL, external_id TEXT NOT NULL, uri TEXT,
            title TEXT, artist TEXT, album_artist TEXT, album TEXT, genre TEXT, composer TEXT,
            year INTEGER DEFAULT 0, track_no INTEGER DEFAULT 0, disc_no INTEGER DEFAULT 0, duration REAL DEFAULT 0,
            codec TEXT, sample_rate REAL DEFAULT 0, bits INTEGER DEFAULT 0, channels INTEGER DEFAULT 0,
            is_dsd INTEGER DEFAULT 0, lossy INTEGER DEFAULT 0, file_size INTEGER DEFAULT 0, mtime INTEGER DEFAULT 0,
            added INTEGER DEFAULT 0, artwork TEXT, lyrics_path TEXT, credits TEXT,
            play_count INTEGER DEFAULT 0, last_played INTEGER DEFAULT 0, favorite INTEGER DEFAULT 0,
            UNIQUE (source, external_id));
        CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks (album_artist, album, disc_no, track_no);
        CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks (artist);
        CREATE INDEX IF NOT EXISTS idx_tracks_added ON tracks (added);
        CREATE TABLE IF NOT EXISTS folders (path TEXT PRIMARY KEY, kind TEXT, last_scan INTEGER DEFAULT 0);
        CREATE TABLE IF NOT EXISTS dir_state (path TEXT PRIMARY KEY, mtime INTEGER);
        CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY, name TEXT, source TEXT, external_id TEXT, created INTEGER);
        CREATE TABLE IF NOT EXISTS playlist_items (playlist_id INTEGER REFERENCES playlists(id) ON DELETE CASCADE,
                                                   position INTEGER, track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE);
        CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);
        CREATE TABLE IF NOT EXISTS offline (local_path TEXT PRIMARY KEY, origin_source TEXT, origin_id TEXT, origin_uri TEXT, added INTEGER);
        CREATE TABLE IF NOT EXISTS album_favorites (artist TEXT, album TEXT, added INTEGER, PRIMARY KEY (artist, album));
        CREATE TABLE IF NOT EXISTS artist_meta (name TEXT PRIMARY KEY COLLATE NOCASE, image TEXT, banner TEXT, bio TEXT, bio_source TEXT,
                                                country TEXT, genre TEXT, found INTEGER DEFAULT 0, fetched INTEGER DEFAULT 0);
        CREATE INDEX IF NOT EXISTS idx_tracks_source ON tracks (source);
        INSERT OR IGNORE INTO meta (key, value) VALUES ('schema', '1');
    )SQL";
    if (! db.exec (schema)) { error = db.lastError(); return false; }
    // Fields edited by hand (bits: 1 album, 2 album artist, 4 year, 8 cover): a rescan never overwrites them.
    db.exec ("ALTER TABLE tracks ADD COLUMN locked INTEGER DEFAULT 0");   // fails harmlessly when the column exists
    loadOfflineIndex();

    // Full-text search (FTS5) when this SQLite has it; LIKE otherwise.
    hasFts = db.exec (R"SQL(
        CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5 (title, artist, album, album_artist, composer,
            content='tracks', content_rowid='id', tokenize='unicode61 remove_diacritics 2');
        CREATE TRIGGER IF NOT EXISTS tracks_ai AFTER INSERT ON tracks BEGIN
            INSERT INTO tracks_fts (rowid, title, artist, album, album_artist, composer) VALUES (new.id, new.title, new.artist, new.album, new.album_artist, new.composer); END;
        CREATE TRIGGER IF NOT EXISTS tracks_ad AFTER DELETE ON tracks BEGIN
            INSERT INTO tracks_fts (tracks_fts, rowid, title, artist, album, album_artist, composer) VALUES ('delete', old.id, old.title, old.artist, old.album, old.album_artist, old.composer); END;
        CREATE TRIGGER IF NOT EXISTS tracks_au AFTER UPDATE ON tracks BEGIN
            INSERT INTO tracks_fts (tracks_fts, rowid, title, artist, album, album_artist, composer) VALUES ('delete', old.id, old.title, old.artist, old.album, old.album_artist, old.composer);
            INSERT INTO tracks_fts (rowid, title, artist, album, album_artist, composer) VALUES (new.id, new.title, new.artist, new.album, new.album_artist, new.composer); END;
    )SQL");
    return true;
}

juce::int64 Library::upsert (TrackInfo& t)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    if (t.added == 0) t.added = juce::Time::currentTimeMillis();
    SqliteStatement s (db, R"SQL(
        INSERT INTO tracks (source, external_id, uri, title, artist, album_artist, album, genre, composer, year, track_no, disc_no,
                            duration, codec, sample_rate, bits, channels, is_dsd, lossy, file_size, mtime, added, artwork, lyrics_path, credits)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24, ?25)
        ON CONFLICT (source, external_id) DO UPDATE SET
            uri = excluded.uri, title = excluded.title, artist = excluded.artist, album_artist = CASE WHEN tracks.locked & 2 THEN tracks.album_artist ELSE excluded.album_artist END,
            album = CASE WHEN tracks.locked & 1 THEN tracks.album ELSE excluded.album END, genre = excluded.genre, composer = excluded.composer,
            year = CASE WHEN tracks.locked & 4 THEN tracks.year ELSE excluded.year END,
            track_no = excluded.track_no, disc_no = excluded.disc_no, duration = excluded.duration, codec = excluded.codec,
            sample_rate = excluded.sample_rate, bits = excluded.bits, channels = excluded.channels, is_dsd = excluded.is_dsd,
            lossy = excluded.lossy, file_size = excluded.file_size, mtime = excluded.mtime,
            artwork = CASE WHEN tracks.locked & 8 THEN tracks.artwork ELSE excluded.artwork END,
            lyrics_path = excluded.lyrics_path, credits = excluded.credits
    )SQL");
    s.bind (1, t.source).bind (2, t.externalId).bind (3, t.uri).bind (4, t.title).bind (5, t.artist).bind (6, t.albumArtist)
     .bind (7, t.album).bind (8, t.genre).bind (9, t.composer).bind (10, t.year).bind (11, t.trackNumber).bind (12, t.discNumber)
     .bind (13, t.duration).bind (14, t.codec).bind (15, t.sampleRate).bind (16, t.bitsPerSample).bind (17, t.channels)
     .bind (18, t.isDsd ? 1 : 0).bind (19, t.lossy ? 1 : 0).bind (20, t.fileSize).bind (21, t.modified).bind (22, t.added)
     .bind (23, t.artworkKey).bind (24, t.lyricsPath).bind (25, t.credits);
    s.execute();

    SqliteStatement q (db, "SELECT id FROM tracks WHERE source = ?1 AND external_id = ?2");
    q.bind (1, t.source).bind (2, t.externalId);
    if (q.step()) t.id = q.getInt64 (0);
    return t.id;
}

bool Library::remove (const juce::String& source, const juce::String& externalId)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "DELETE FROM tracks WHERE source = ?1 AND external_id = ?2");
    s.bind (1, source).bind (2, externalId);
    return s.execute() && db.changes() > 0;
}

int Library::removeMissingUnder (const juce::String& folderPath, const juce::StringArray& present)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    std::vector<juce::String> toRemove;
    {
        SqliteStatement s (db, "SELECT external_id FROM tracks WHERE source = 'local' AND substr(external_id, 1, length(?1)) = ?1");
        s.bind (1, folderPath);
        while (s.step())
        {
            const auto path = s.getText (0);
            if (! present.contains (path)) toRemove.push_back (path);
        }
    }
    for (auto& p : toRemove) remove ("local", p);
    return (int) toRemove.size();
}

juce::StringArray Library::getLocalPathsInDirectory (const juce::File& dir)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    const auto prefix = dir.getFullPathName() + juce::File::getSeparatorString();
    SqliteStatement s (db, "SELECT external_id FROM tracks WHERE source = 'local' AND substr(external_id, 1, length(?1)) = ?1");
    s.bind (1, prefix);
    juce::StringArray out;
    while (s.step())
    {
        const auto path = s.getText (0);
        if (! path.substring (prefix.length()).containsChar (juce::File::getSeparatorChar())) out.add (path);
    }
    return out;
}

juce::StringArray Library::getLocalPathsWithPrefix (const juce::String& prefix)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT external_id FROM tracks WHERE source = 'local' AND substr(external_id, 1, length(?1)) = ?1");
    s.bind (1, prefix);
    juce::StringArray out;
    while (s.step()) out.add (s.getText (0));
    return out;
}

std::optional<TrackInfo> Library::getTrack (juce::int64 id)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, (juce::String ("SELECT ") + trackColumns() + " FROM tracks WHERE id = ?1").toRawUTF8());
    s.bind (1, id);
    if (s.step()) return rowToTrack (s);
    return std::nullopt;
}

std::optional<TrackInfo> Library::findTrack (const juce::String& source, const juce::String& externalId)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, (juce::String ("SELECT ") + trackColumns() + " FROM tracks WHERE source = ?1 AND external_id = ?2").toRawUTF8());
    s.bind (1, source).bind (2, externalId);
    if (s.step()) return rowToTrack (s);
    return std::nullopt;
}

namespace
{
    juce::String orderClause (const juce::String& orderBy)
    {
        if (orderBy == "added")  return "added DESC";
        if (orderBy == "artist") return "COALESCE(NULLIF(album_artist, ''), artist) COLLATE NOCASE, album COLLATE NOCASE, disc_no, track_no";
        if (orderBy == "album")  return "album COLLATE NOCASE, disc_no, track_no";
        if (orderBy == "year")   return "year DESC, album COLLATE NOCASE, disc_no, track_no";
        return "title COLLATE NOCASE";
    }
}

std::vector<TrackInfo> Library::getTracks (const juce::String& orderBy, int limit, const juce::String& sourceFilter)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = juce::String ("SELECT ") + trackColumns() + " FROM tracks";
    if (sourceFilter.isNotEmpty()) sql << " WHERE source LIKE ?2";
    sql << " ORDER BY " << orderClause (orderBy) << " LIMIT ?1";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, limit);
    if (sourceFilter.isNotEmpty()) s.bind (2, sourceFilter + "%");
    std::vector<TrackInfo> out;
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

std::vector<TrackInfo> Library::getAlbumTracks (const juce::String& artist, const juce::String& album)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, (juce::String ("SELECT ") + trackColumns()
                            + " FROM tracks WHERE album = ?1 AND COALESCE(NULLIF(album_artist, ''), artist) = ?2 ORDER BY disc_no, track_no, title").toRawUTF8());
    s.bind (1, album).bind (2, artist);
    std::vector<TrackInfo> out;
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

std::vector<TrackInfo> Library::getArtistTracks (const juce::String& artist)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, (juce::String ("SELECT ") + trackColumns()
                            + " FROM tracks WHERE artist = ?1 OR album_artist = ?1 ORDER BY year DESC, album, disc_no, track_no").toRawUTF8());
    s.bind (1, artist);
    std::vector<TrackInfo> out;
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

std::vector<TrackInfo> Library::getRecentlyAdded (int limit) { return getTracks ("added", limit); }

std::vector<TrackInfo> Library::search (const juce::String& query, int limit)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    std::vector<TrackInfo> out;
    const auto q = query.trim();
    if (q.isEmpty()) return out;

    if (hasFts)
    {
        // Each word becomes a prefix term: "pink flo" -> "pink"* "flo"*
        juce::StringArray words;
        words.addTokens (q, " ", "\"");
        words.removeEmptyStrings();
        juce::String match;
        for (auto& w : words) match << "\"" << w.removeCharacters ("\"*") << "\"* ";
        juce::String sql = "SELECT ";
        for (auto& col : juce::StringArray::fromTokens (trackColumns(), ",", {}))
            sql << "t." << col.trim() << ",";
        sql = sql.dropLastCharacters (1) + " FROM tracks_fts f JOIN tracks t ON t.id = f.rowid WHERE tracks_fts MATCH ?1 ORDER BY rank LIMIT ?2";
        SqliteStatement s (db, sql.toRawUTF8());
        if (s.isValid())
        {
            s.bind (1, match.trim()).bind (2, limit);
            while (s.step()) out.push_back (rowToTrack (s));
            return out;
        }
    }

    SqliteStatement s (db, (juce::String ("SELECT ") + trackColumns()
                            + " FROM tracks WHERE title LIKE ?1 OR artist LIKE ?1 OR album LIKE ?1 OR album_artist LIKE ?1 ORDER BY title LIMIT ?2").toRawUTF8());
    s.bind (1, "%" + q + "%").bind (2, limit);
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

int Library::countTracks (const juce::String& sourceFilter)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, sourceFilter.isEmpty() ? "SELECT COUNT(*) FROM tracks" : "SELECT COUNT(*) FROM tracks WHERE source LIKE ?1");
    if (sourceFilter.isNotEmpty()) s.bind (1, sourceFilter + "%");
    return s.step() ? s.getInt (0) : 0;
}

namespace
{
    const char* albumColumns()
    {
        return R"SQL(
        SELECT album, COALESCE(NULLIF(album_artist, ''), artist) AS a, MAX(year), COUNT(*), MAX(sample_rate), MAX(bits),
               MAX(is_dsd), MIN(lossy), SUM(duration), MAX(added), MAX(artwork), MIN(source),
               MAX(CASE WHEN source LIKE 'upnp%' THEN source END), MAX(genre), MAX(channels)
        FROM tracks)SQL";
    }

    AlbumInfo rowToAlbum (SqliteStatement& s)
    {
        AlbumInfo a;
        a.title = s.getText (0); a.artist = s.getText (1); a.year = s.getInt (2); a.trackCount = s.getInt (3);
        a.maxRate = s.getDouble (4); a.maxBits = s.getInt (5); a.hasDsd = s.getInt (6) != 0; a.lossy = s.getInt (7) != 0;
        a.duration = s.getDouble (8); a.added = s.getInt64 (9); a.artworkKey = s.getText (10); a.source = s.getText (11);
        a.nasSource = s.getText (12); a.genre = s.getText (13); a.maxChannels = s.getInt (14);
        return a;
    }

    juce::String albumOrder (const juce::String& orderBy)
    {
        return orderBy == "added" ? "MAX(added) DESC" : orderBy == "artist" ? "a COLLATE NOCASE, MAX(year)" : orderBy == "year" ? "MAX(year) DESC" : "album COLLATE NOCASE";
    }
}

std::vector<AlbumInfo> Library::getAlbums (const juce::String& orderBy, int limit, const juce::String& sourceFilter)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = albumColumns();
    sql << " WHERE album IS NOT NULL AND album != ''";
    if (sourceFilter.isNotEmpty()) sql << " AND source LIKE ?2";
    sql << " GROUP BY a, album ORDER BY " << albumOrder (orderBy) << " LIMIT ?1";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, limit);
    if (sourceFilter.isNotEmpty()) s.bind (2, sourceFilter + "%");
    std::vector<AlbumInfo> out;
    while (s.step()) out.push_back (rowToAlbum (s));
    return out;
}

juce::String Library::qualityCondition (QualityFilter q)
{
    switch (q)
    {
        case QualityFilter::Dxd:   return "(is_dsd = 0 AND lossy = 0 AND sample_rate >= 352000)";
        case QualityFilter::Dsd:   return "(is_dsd = 1)";
        case QualityFilter::HiRes: return "(is_dsd = 0 AND lossy = 0 AND (sample_rate > 48000 OR bits > 16))";
        case QualityFilter::Cd:    return "(is_dsd = 0 AND lossy = 0 AND sample_rate > 0 AND sample_rate <= 48000 AND bits <= 16)";
        case QualityFilter::Any:   break;
    }
    return "1";
}

std::vector<AlbumInfo> Library::getAlbumsFiltered (QualityFilter q, const juce::String& genre, const juce::String& orderBy, int limit)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = albumColumns();
    sql << " WHERE album IS NOT NULL AND album != '' AND " << qualityCondition (q);
    if (genre.isNotEmpty()) sql << " AND genre = ?2";
    sql << " GROUP BY a, album ORDER BY " << albumOrder (orderBy) << " LIMIT ?1";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, limit);
    if (genre.isNotEmpty()) s.bind (2, genre);
    std::vector<AlbumInfo> out;
    while (s.step()) out.push_back (rowToAlbum (s));
    return out;
}

std::vector<TrackInfo> Library::getTracksFiltered (QualityFilter q, const juce::String& genre, int limit)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = juce::String ("SELECT ") + trackColumns() + " FROM tracks WHERE " + qualityCondition (q);
    if (genre.isNotEmpty()) sql << " AND genre = ?2";
    sql << " ORDER BY " << orderClause ("artist") << " LIMIT ?1";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, limit);
    if (genre.isNotEmpty()) s.bind (2, genre);
    std::vector<TrackInfo> out;
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

int Library::countTracksFiltered (QualityFilter q)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, ("SELECT COUNT(*) FROM tracks WHERE " + qualityCondition (q)).toRawUTF8());
    return s.step() ? s.getInt (0) : 0;
}

std::vector<GenreInfo> Library::getGenres()
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, R"SQL(
        SELECT genre, COUNT(DISTINCT COALESCE(NULLIF(album_artist, ''), artist) || char(31) || album), COUNT(*), MAX(artwork)
        FROM tracks WHERE genre IS NOT NULL AND genre != '' GROUP BY genre ORDER BY genre COLLATE NOCASE)SQL");
    std::vector<GenreInfo> out;
    while (s.step()) out.push_back ({ s.getText (0), s.getInt (1), s.getInt (2), s.getText (3) });
    return out;
}

std::vector<TrackInfo> Library::getFavouriteTracks()
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, (juce::String ("SELECT ") + trackColumns() + " FROM tracks WHERE favorite = 1 ORDER BY " + orderClause ("artist")).toRawUTF8());
    std::vector<TrackInfo> out;
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

std::vector<AlbumInfo> Library::getFavouriteAlbums()
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = albumColumns();
    sql << " WHERE album IS NOT NULL AND album != '' AND EXISTS (SELECT 1 FROM album_favorites f WHERE f.album = tracks.album"
           " AND f.artist = COALESCE(NULLIF(tracks.album_artist, ''), tracks.artist))"
           " GROUP BY a, album ORDER BY a COLLATE NOCASE, MAX(year)";
    SqliteStatement s (db, sql.toRawUTF8());
    std::vector<AlbumInfo> out;
    while (s.step()) { out.push_back (rowToAlbum (s)); out.back().favourite = true; }
    return out;
}

void Library::setAlbumFavourite (const juce::String& artist, const juce::String& album, bool fav)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    if (fav)
    {
        SqliteStatement s (db, "INSERT OR IGNORE INTO album_favorites (artist, album, added) VALUES (?1, ?2, ?3)");
        s.bind (1, artist).bind (2, album).bind (3, (juce::int64) juce::Time::currentTimeMillis()).execute();
    }
    else
    {
        SqliteStatement s (db, "DELETE FROM album_favorites WHERE artist = ?1 AND album = ?2");
        s.bind (1, artist).bind (2, album).execute();
    }
}

bool Library::isAlbumFavourite (const juce::String& artist, const juce::String& album)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT 1 FROM album_favorites WHERE artist = ?1 AND album = ?2");
    s.bind (1, artist).bind (2, album);
    return s.step();
}

int Library::removeAlbum (const juce::String& artist, const juce::String& album, const juce::String& sourcePrefix)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = "DELETE FROM tracks WHERE album = ?1 AND COALESCE(NULLIF(album_artist, ''), artist) = ?2";
    if (sourcePrefix.isNotEmpty()) sql << " AND source LIKE ?3";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, album).bind (2, artist);
    if (sourcePrefix.isNotEmpty()) s.bind (3, sourcePrefix + "%");
    s.execute();
    return db.changes();
}

int Library::removeSourcePrefix (const juce::String& prefix)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "DELETE FROM tracks WHERE source LIKE ?1");
    s.bind (1, prefix + "%").execute();
    return db.changes();
}

juce::StringArray Library::getSources (const juce::String& prefix)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT DISTINCT source FROM tracks WHERE source LIKE ?1 ORDER BY source");
    s.bind (1, prefix + "%");
    juce::StringArray out;
    while (s.step()) out.add (s.getText (0));
    return out;
}

int Library::removeNasSourcesExcept (const juce::StringArray& available)
{
    int removed = 0;
    for (auto& src : getSources ("upnp"))
        if (! available.contains (src))
        {
            std::lock_guard<std::recursive_mutex> l (db.getMutex());
            SqliteStatement s (db, "DELETE FROM tracks WHERE source = ?1");
            s.bind (1, src).execute();
            removed += db.changes();
        }
    return removed;
}

int Library::countAlbumsFromSourcePrefix (const juce::String& prefix)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT COUNT(DISTINCT COALESCE(NULLIF(album_artist, ''), artist) || char(31) || album) FROM tracks WHERE source LIKE ?1");
    s.bind (1, prefix + "%");
    return s.step() ? s.getInt (0) : 0;
}

std::optional<ArtistMeta> Library::getArtistMeta (const juce::String& name)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT name, image, banner, bio, bio_source, country, genre, found, fetched FROM artist_meta WHERE name = ?1");
    s.bind (1, name);
    if (! s.step()) return std::nullopt;
    ArtistMeta m;
    m.name = s.getText (0); m.imageUrl = s.getText (1); m.bannerUrl = s.getText (2); m.bio = s.getText (3);
    m.bioSource = s.getText (4); m.country = s.getText (5); m.genre = s.getText (6); m.found = s.getInt (7) != 0; m.fetched = s.getInt64 (8);
    return m;
}

void Library::setArtistMeta (const ArtistMeta& m)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "INSERT OR REPLACE INTO artist_meta (name, image, banner, bio, bio_source, country, genre, found, fetched) "
                           "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    s.bind (1, m.name).bind (2, m.imageUrl).bind (3, m.bannerUrl).bind (4, m.bio).bind (5, m.bioSource).bind (6, m.country)
     .bind (7, m.genre).bind (8, m.found ? 1 : 0).bind (9, m.fetched > 0 ? m.fetched : (juce::int64) juce::Time::currentTimeMillis()).execute();
}

std::vector<ArtistInfo> Library::getArtists (int limit)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, R"SQL(
        SELECT COALESCE(NULLIF(album_artist, ''), artist) AS a, COUNT(DISTINCT album), COUNT(*), MAX(artwork)
        FROM tracks WHERE a IS NOT NULL AND a != '' GROUP BY a ORDER BY a COLLATE NOCASE LIMIT ?1)SQL");
    s.bind (1, limit);
    std::vector<ArtistInfo> out;
    while (s.step()) out.push_back ({ s.getText (0), s.getInt (1), s.getInt (2), s.getText (3) });
    return out;
}

juce::int64 Library::createPlaylist (const juce::String& name, const juce::String& source, const juce::String& externalId)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    if (externalId.isNotEmpty())
    {
        SqliteStatement f (db, "SELECT id FROM playlists WHERE source = ?1 AND external_id = ?2");
        f.bind (1, source).bind (2, externalId);
        if (f.step()) return f.getInt64 (0);
    }
    SqliteStatement s (db, "INSERT INTO playlists (name, source, external_id, created) VALUES (?1, ?2, ?3, ?4)");
    s.bind (1, name).bind (2, source).bind (3, externalId).bind (4, juce::Time::currentTimeMillis());
    s.execute();
    return db.lastInsertId();
}

void Library::setPlaylistTracks (juce::int64 playlistId, const std::vector<juce::int64>& ids)
{
    SqliteDb::Transaction tx (db);
    SqliteStatement d (db, "DELETE FROM playlist_items WHERE playlist_id = ?1");
    d.bind (1, playlistId).execute();
    SqliteStatement s (db, "INSERT INTO playlist_items (playlist_id, position, track_id) VALUES (?1, ?2, ?3)");
    for (size_t i = 0; i < ids.size(); ++i)
    {
        s.reset();
        s.bind (1, playlistId).bind (2, (int) i).bind (3, ids[i]).execute();
    }
    tx.commit();
}

std::vector<PlaylistInfo> Library::getPlaylists()
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT p.id, p.name, p.source, p.external_id, COUNT(i.track_id) FROM playlists p "
                           "LEFT JOIN playlist_items i ON i.playlist_id = p.id GROUP BY p.id ORDER BY p.name COLLATE NOCASE");
    std::vector<PlaylistInfo> out;
    while (s.step()) out.push_back ({ s.getInt64 (0), s.getText (1), s.getText (2), s.getText (3), s.getInt (4) });
    return out;
}

std::vector<TrackInfo> Library::getPlaylistTracks (juce::int64 playlistId)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = "SELECT ";
    for (auto& col : juce::StringArray::fromTokens (trackColumns(), ",", {})) sql << "t." << col.trim() << ",";
    sql = sql.dropLastCharacters (1) + " FROM playlist_items i JOIN tracks t ON t.id = i.track_id WHERE i.playlist_id = ?1 ORDER BY i.position";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, playlistId);
    std::vector<TrackInfo> out;
    while (s.step()) out.push_back (rowToTrack (s));
    return out;
}

int Library::appendToPlaylist (juce::int64 playlistId, const std::vector<juce::int64>& trackIds)
{
    SqliteDb::Transaction tx (db);
    int position = 0;
    {
        SqliteStatement p (db, "SELECT COALESCE(MAX(position), -1) + 1 FROM playlist_items WHERE playlist_id = ?1");
        p.bind (1, playlistId);
        if (p.step()) position = p.getInt (0);
    }
    SqliteStatement s (db, "INSERT INTO playlist_items (playlist_id, position, track_id) VALUES (?1, ?2, ?3)");
    int added = 0;
    for (auto id : trackIds)
    {
        if (id <= 0) continue;
        s.reset();
        s.bind (1, playlistId).bind (2, position++).bind (3, id).execute();
        ++added;
    }
    tx.commit();
    return added;
}

void Library::removeFromPlaylist (juce::int64 playlistId, int position)
{
    auto tracks = getPlaylistTracks (playlistId);
    if (position < 0 || position >= (int) tracks.size()) return;
    std::vector<juce::int64> ids;
    for (int i = 0; i < (int) tracks.size(); ++i) if (i != position) ids.push_back (tracks[(size_t) i].id);
    setPlaylistTracks (playlistId, ids);
}

void Library::deletePlaylist (juce::int64 playlistId)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement d (db, "DELETE FROM playlist_items WHERE playlist_id = ?1");
    d.bind (1, playlistId).execute();
    SqliteStatement s (db, "DELETE FROM playlists WHERE id = ?1");
    s.bind (1, playlistId).execute();
}

void Library::renamePlaylist (juce::int64 playlistId, const juce::String& name)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "UPDATE playlists SET name = ?2 WHERE id = ?1");
    s.bind (1, playlistId).bind (2, name).execute();
}

void Library::loadOfflineIndex()
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    const std::lock_guard<std::mutex> o (offlineLock);
    offlinePaths.clear();
    offlineOrigins.clear();
    SqliteStatement s (db, "SELECT local_path, origin_source, origin_id, origin_uri FROM offline");
    while (s.step())
    {
        offlinePaths.insert (s.getText (0));
        offlineOrigins[s.getText (1) + "|" + s.getText (2)] = s.getText (0);
        if (s.getText (3).isNotEmpty()) offlineOrigins["uri|" + s.getText (3)] = s.getText (0);
    }
}

void Library::markOffline (const juce::String& localPath, const TrackInfo& origin)
{
    {
        std::lock_guard<std::recursive_mutex> l (db.getMutex());
        SqliteStatement s (db, "INSERT OR REPLACE INTO offline (local_path, origin_source, origin_id, origin_uri, added) VALUES (?1, ?2, ?3, ?4, ?5)");
        s.bind (1, localPath).bind (2, origin.source).bind (3, origin.externalId).bind (4, origin.uri)
         .bind (5, (juce::int64) juce::Time::currentTimeMillis()).execute();
    }
    const std::lock_guard<std::mutex> o (offlineLock);
    offlinePaths.insert (localPath);
    offlineOrigins[origin.source + "|" + origin.externalId] = localPath;
    if (origin.uri.isNotEmpty()) offlineOrigins["uri|" + origin.uri] = localPath;
}

bool Library::isOfflineCopy (const TrackInfo& t) const
{
    const std::lock_guard<std::mutex> o (offlineLock);
    return t.source == "local" && offlinePaths.count (t.externalId) > 0;
}

juce::String Library::getOfflineCopy (const TrackInfo& remote) const
{
    const std::lock_guard<std::mutex> o (offlineLock);
    auto it = offlineOrigins.find (remote.source + "|" + remote.externalId);
    if (it == offlineOrigins.end() && remote.uri.isNotEmpty()) it = offlineOrigins.find ("uri|" + remote.uri);
    if (it == offlineOrigins.end() || ! juce::File (it->second).existsAsFile()) return {};
    return it->second;
}

void Library::addFolder (const juce::File& folder, const juce::String& kind)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "INSERT OR IGNORE INTO folders (path, kind) VALUES (?1, ?2)");
    s.bind (1, folder.getFullPathName()).bind (2, kind).execute();
}

void Library::removeFolder (const juce::File& folder)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "DELETE FROM folders WHERE path = ?1");
    s.bind (1, folder.getFullPathName()).execute();
    SqliteStatement t (db, "DELETE FROM tracks WHERE source = 'local' AND substr(external_id, 1, length(?1)) = ?1");
    t.bind (1, folder.getFullPathName() + juce::File::getSeparatorString()).execute();
}

juce::StringArray Library::getFolders()
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "SELECT path FROM folders ORDER BY path");
    juce::StringArray out;
    while (s.step()) out.add (s.getText (0));
    return out;
}

void Library::markPlayed (juce::int64 id)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "UPDATE tracks SET play_count = play_count + 1, last_played = ?2 WHERE id = ?1");
    s.bind (1, id).bind (2, juce::Time::currentTimeMillis()).execute();
}

void Library::setFavourite (juce::int64 id, bool fav)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement s (db, "UPDATE tracks SET favorite = ?2 WHERE id = ?1");
    s.bind (1, id).bind (2, fav ? 1 : 0).execute();
}

juce::String Library::storeArtwork (const juce::MemoryBlock& image, const juce::String& mime)
{
    if (image.getSize() < 16) return {};
    const auto hash = juce::SHA256 (image).toHexString().substring (0, 32);
    const bool standard = imagedec::isStandard (image.getData(), image.getSize());
    const auto ext = ! standard ? ".jpg" : mime.containsIgnoreCase ("png") ? ".png" : ".jpg";
    const auto key = hash + ext;
    auto f = artworkDir.getChildFile (key);
    if (! f.existsAsFile())
    {
        if (standard) f.replaceWithData (image.getData(), image.getSize());
        else
        {
            // progressive / CMYK JPEG, WebP, 16-bit PNG...: stored as a plain 8-bit RGB JPEG (player, NX Remote)
            const auto jpg = imagedec::toStandardJpeg (imagedec::decode (image.getData(), image.getSize()));
            if (jpg.getSize() > 0) f.replaceWithData (jpg.getData(), jpg.getSize());
            else f.replaceWithData (image.getData(), image.getSize());      // undecodable: kept as is
        }
    }
    return key;
}

juce::File Library::findFolderCover (const juce::File& folder)
{
    // by priority: cover, folder, front, albumart, album
    static const char* names[] = { "cover", "folder", "front", "albumart", "album" };
    juce::File best;
    int bestRank = 99;
    for (const auto& e : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFiles))
    {
        const auto f = e.getFile();
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".webp") continue;
        const auto base = f.getFileNameWithoutExtension().toLowerCase();
        for (int i = 0; i < 5; ++i)
            if (base == names[i] && i < bestRank) { best = f; bestRank = i; }
    }
    return best;
}

int Library::updateAlbum (const juce::String& artist, const juce::String& album, const juce::String& newAlbum,
                         const juce::String& newArtist, int newYear, const juce::File& coverFile)
{
    if (newAlbum.trim().isEmpty()) return 0;
    juce::String cover;
    if (coverFile.existsAsFile())
    {
        juce::MemoryBlock mb;
        if (coverFile.loadFileAsData (mb))
            cover = storeArtwork (mb, coverFile.getFileExtension().containsIgnoreCase ("png") ? "image/png" : "image/jpeg");
    }
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    SqliteStatement u (db, "UPDATE tracks SET album = ?3, album_artist = ?4, year = CASE WHEN ?5 > 0 THEN ?5 ELSE year END, "
                           "artwork = CASE WHEN ?6 != '' THEN ?6 ELSE artwork END, "
                           "locked = locked | 3 | (CASE WHEN ?5 > 0 THEN 4 ELSE 0 END) | (CASE WHEN ?6 != '' THEN 8 ELSE 0 END) "
                           "WHERE album = ?2 AND COALESCE(NULLIF(album_artist, ''), artist) = ?1");
    u.bind (1, artist).bind (2, album).bind (3, newAlbum.trim()).bind (4, newArtist.trim()).bind (5, newYear).bind (6, cover).execute();
    SqliteStatement c (db, "SELECT changes()");
    return c.step() ? c.getInt (0) : 0;
}

int Library::fillMissingArtwork()
{
    // rows grouped by parent folder, compared as File paths (a path may mix "/" and "\\" on Windows)
    std::map<juce::String, std::vector<juce::int64>> todo;
    {
        std::lock_guard<std::recursive_mutex> l (db.getMutex());
        SqliteStatement s (db, "SELECT id, external_id FROM tracks WHERE source = 'local' AND (artwork IS NULL OR artwork = '')");
        while (s.step())
        {
            const auto dir = juce::File (s.getText (1)).getParentDirectory().getFullPathName();
            if (coverChecked.count (dir) == 0) todo[dir].push_back (s.getInt64 (0));
        }
    }
    int updated = 0;
    for (auto& [d, ids] : todo)
    {
        coverChecked.insert (d);
        const auto cover = findFolderCover (juce::File (d));
        if (! cover.existsAsFile()) continue;
        juce::MemoryBlock mb;
        if (! cover.loadFileAsData (mb)) continue;
        const auto key = storeArtwork (mb, cover.getFileExtension().containsIgnoreCase ("png") ? "image/png" : "image/jpeg");
        if (key.isEmpty()) continue;
        std::lock_guard<std::recursive_mutex> l (db.getMutex());
        SqliteStatement u (db, "UPDATE tracks SET artwork = ?2 WHERE id = ?1 AND (artwork IS NULL OR artwork = '')");
        for (auto id : ids) u.bind (1, id).bind (2, key).execute();
        ++updated;
    }
    return updated;
}

juce::File Library::getArtworkFile (const juce::String& key) const
{
    if (key.isEmpty() || key.startsWith ("http")) return {};
    return artworkDir.getChildFile (key);
}

void Library::notifyChanged()
{
    std::weak_ptr<int> token = alive;
    juce::MessageManager::callAsync ([this, token] { if (! token.expired() && onChanged) onChanged(); });
}

//==============================================================================
bool readLocalTrack (const juce::File& file, Library& library, TrackInfo& t)
{
    juce::String err;
    auto decoder = openDecoder (file, err);
    if (decoder == nullptr) return false;
    const auto fmt = decoder->getFormat();

    t.source = "local";
    t.externalId = t.uri = file.getFullPathName();
    t.codec = fmt.codec;
    t.sampleRate = fmt.sampleRate;
    t.bitsPerSample = fmt.isDsd() ? 1 : fmt.bitsPerSample;
    t.channels = fmt.numChannels;
    t.isDsd = fmt.isDsd();
    t.lossy = fmt.lossy;
    t.duration = decoder->getDurationSeconds();
    t.fileSize = file.getSize();
    t.modified = file.getLastModificationTime().toMilliseconds();
    decoder.reset();

    TagData tags;
    readTags (file, tags);
    t.title = tags.get ("title").isNotEmpty() ? tags.get ("title") : file.getFileNameWithoutExtension();
    t.artist = tags.get ("artist");
    t.albumArtist = tags.get ("albumartist");
    t.album = tags.get ("album").isNotEmpty() ? tags.get ("album") : file.getParentDirectory().getFileName();
    t.genre = tags.get ("genre");
    t.composer = tags.get ("composer");
    t.year = tags.getInt ("year");
    t.trackNumber = tags.getInt ("tracknumber");
    t.discNumber = juce::jmax (1, tags.getInt ("discnumber"));

    juce::StringArray credits (tags.credits);
    for (auto key : { "composer", "conductor", "label", "isrc", "copyright" })
        if (tags.get (key).isNotEmpty() && ! credits.contains (juce::String (key)))
            credits.add (juce::String (key).substring (0, 1).toUpperCase() + juce::String (key).substring (1) + " : " + tags.get (key));
    t.credits = credits.joinIntoString ("\n");

    if (tags.picture.getSize() > 0)
        t.artworkKey = library.storeArtwork (tags.picture, tags.pictureMime);
    else
    {
        const auto img = Library::findFolderCover (file.getParentDirectory());
        if (img.existsAsFile())
        {
            juce::MemoryBlock mb;
            img.loadFileAsData (mb);
            t.artworkKey = library.storeArtwork (mb, img.getFileExtension().containsIgnoreCase ("png") ? "image/png" : "image/jpeg");
        }
    }

    auto lrc = file.withFileExtension ("lrc");
    t.lyricsPath = lrc.existsAsFile() ? lrc.getFullPathName() : juce::String();
    return true;
}

std::vector<TrackInfo> readSacdIso (const juce::File& iso, Library& library)
{
    std::vector<TrackInfo> out;
    sacd::Disc disc;
    juce::String err;
    if (! sacd::readDisc (iso, disc, err)) return out;

    juce::String artworkKey;
    const auto img = Library::findFolderCover (iso.getParentDirectory());
    if (img.existsAsFile())
    {
        juce::MemoryBlock mb;
        img.loadFileAsData (mb);
        artworkKey = library.storeArtwork (mb, img.getFileExtension().containsIgnoreCase ("png") ? "image/png" : "image/jpeg");
    }
    const auto album = disc.albumTitle.isNotEmpty() ? disc.albumTitle : disc.discTitle.isNotEmpty() ? disc.discTitle : iso.getFileNameWithoutExtension();
    const auto albumArtist = disc.albumArtist.isNotEmpty() ? disc.albumArtist : disc.discArtist;
    juce::StringArray credits;
    if (disc.publisher.isNotEmpty()) credits.add ("Label : " + disc.publisher);
    if (disc.copyright.isNotEmpty()) credits.add ("Copyright : " + disc.copyright);
    if (disc.catalog.isNotEmpty())   credits.add ("Catalogue : "_t + disc.catalog);

    for (auto& a : disc.areas)
        for (auto& tr : a.tracks)
        {
            TrackInfo t;
            t.source = "local";
            t.externalId = t.uri = sacd::virtualUri (iso, a.index, tr.number);
            t.codec = a.isDst() ? "DST" : "SACD";
            t.sampleRate = 2822400.0;
            t.bitsPerSample = 1;
            t.channels = a.channels;
            t.isDsd = true;
            t.duration = tr.durationSeconds();
            t.fileSize = iso.getSize();
            t.modified = iso.getLastModificationTime().toMilliseconds();
            t.title = tr.title.isNotEmpty() ? tr.title : "Piste "_t + juce::String (tr.number);
            t.artist = tr.performer.isNotEmpty() ? tr.performer : albumArtist;
            t.albumArtist = albumArtist;
            // a disc with both areas: two albums, clearly marked "(2ch)" and "(6ch)"
            t.album = disc.areas.size() > 1 ? album + " (" + juce::String (a.channels) + "ch)" : album;
            t.year = disc.year;
            t.trackNumber = tr.number;
            t.discNumber = 1;
            t.credits = credits.joinIntoString ("\n");
            t.artworkKey = artworkKey;
            out.push_back (t);
        }
    return out;
}

} // namespace nx
