// NX Player — MIT License
#include "Library.h"
#include "../Audio/AudioDecoder.h"
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
        INSERT OR IGNORE INTO meta (key, value) VALUES ('schema', '1');
    )SQL";
    if (! db.exec (schema)) { error = db.lastError(); return false; }

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
            uri = excluded.uri, title = excluded.title, artist = excluded.artist, album_artist = excluded.album_artist,
            album = excluded.album, genre = excluded.genre, composer = excluded.composer, year = excluded.year,
            track_no = excluded.track_no, disc_no = excluded.disc_no, duration = excluded.duration, codec = excluded.codec,
            sample_rate = excluded.sample_rate, bits = excluded.bits, channels = excluded.channels, is_dsd = excluded.is_dsd,
            lossy = excluded.lossy, file_size = excluded.file_size, mtime = excluded.mtime, artwork = excluded.artwork,
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

std::vector<AlbumInfo> Library::getAlbums (const juce::String& orderBy, int limit, const juce::String& sourceFilter)
{
    std::lock_guard<std::recursive_mutex> l (db.getMutex());
    juce::String sql = R"SQL(
        SELECT album, COALESCE(NULLIF(album_artist, ''), artist) AS a, MAX(year), COUNT(*), MAX(sample_rate), MAX(bits),
               MAX(is_dsd), MIN(lossy), SUM(duration), MAX(added), MAX(artwork), MIN(source)
        FROM tracks WHERE album IS NOT NULL AND album != '')SQL";
    if (sourceFilter.isNotEmpty()) sql << " AND source LIKE ?2";
    sql << " GROUP BY a, album ORDER BY ";
    sql << (orderBy == "added" ? "MAX(added) DESC" : orderBy == "artist" ? "a COLLATE NOCASE, MAX(year)" : orderBy == "year" ? "MAX(year) DESC" : "album COLLATE NOCASE");
    sql << " LIMIT ?1";
    SqliteStatement s (db, sql.toRawUTF8());
    s.bind (1, limit);
    if (sourceFilter.isNotEmpty()) s.bind (2, sourceFilter + "%");
    std::vector<AlbumInfo> out;
    while (s.step())
    {
        AlbumInfo a;
        a.title = s.getText (0); a.artist = s.getText (1); a.year = s.getInt (2); a.trackCount = s.getInt (3);
        a.maxRate = s.getDouble (4); a.maxBits = s.getInt (5); a.hasDsd = s.getInt (6) != 0; a.lossy = s.getInt (7) != 0;
        a.duration = s.getDouble (8); a.added = s.getInt64 (9); a.artworkKey = s.getText (10); a.source = s.getText (11);
        out.push_back (a);
    }
    return out;
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
    const auto ext = mime.containsIgnoreCase ("png") ? ".png" : ".jpg";
    const auto key = hash + ext;
    auto f = artworkDir.getChildFile (key);
    if (! f.existsAsFile()) f.replaceWithData (image.getData(), image.getSize());
    return key;
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
        for (auto name : { "cover.jpg", "folder.jpg", "front.jpg", "cover.png", "folder.png", "Cover.jpg", "Folder.jpg", "AlbumArt.jpg" })
        {
            auto img = file.getParentDirectory().getChildFile (name);
            if (img.existsAsFile())
            {
                juce::MemoryBlock mb;
                img.loadFileAsData (mb);
                t.artworkKey = library.storeArtwork (mb, img.getFileExtension().containsIgnoreCase ("png") ? "image/png" : "image/jpeg");
                break;
            }
        }
    }

    auto lrc = file.withFileExtension ("lrc");
    t.lyricsPath = lrc.existsAsFile() ? lrc.getFullPathName() : juce::String();
    return true;
}

} // namespace nx
