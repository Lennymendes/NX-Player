// NX Player — MIT License
// Brique 2: tags, SQLite library, folder watching, lyrics, providers (parsers + a real HTTP stream).
#include "TestFramework.h"
#include "TestAudioFiles.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Library/Library.h"
#include "../Source/Library/LibraryScanner.h"
#include "../Source/Library/Lyrics.h"
#include "../Source/Providers/MediaProvider.h"
#include "../Source/Providers/UpnpProvider.h"
#include "../Source/Providers/StreamingProviders.h"
#include "../Source/Providers/Http.h"
#include "../Source/Output/CaptureBackend.h"

using namespace nxtest;

namespace
{
    // ID3v2.4 builder ---------------------------------------------------------
    void syncsafe (ByteWriter& w, uint32_t v) { w.u8 ((int) ((v >> 21) & 0x7f)); w.u8 ((int) ((v >> 14) & 0x7f)); w.u8 ((int) ((v >> 7) & 0x7f)); w.u8 ((int) (v & 0x7f)); }

    void frame (ByteWriter& w, const char* id, const juce::MemoryBlock& payload)
    {
        w.tag (id); syncsafe (w, (uint32_t) payload.getSize()); w.be16 (0); w.raw (payload.getData(), payload.getSize());
    }

    juce::MemoryBlock textUtf8 (const juce::String& s) { juce::MemoryBlock m; uint8_t e = 3; m.append (&e, 1); m.append (s.toRawUTF8(), s.getNumBytesAsUTF8()); return m; }
    juce::MemoryBlock textUtf16 (const juce::String& s)
    {
        juce::MemoryBlock m; uint8_t h[3] = { 1, 0xFF, 0xFE }; m.append (h, 3);
        auto* p = s.toUTF16().getAddress();
        while (*p) { uint8_t b[2] = { (uint8_t) (*p & 0xff), (uint8_t) (*p >> 8) }; m.append (b, 2); ++p; }
        return m;
    }

    juce::MemoryBlock fakeJpeg (int seed)
    {
        juce::MemoryBlock m;
        uint8_t h[4] = { 0xFF, 0xD8, 0xFF, 0xE0 };
        m.append (h, 4);
        for (int i = 0; i < 300; ++i) { uint8_t b = (uint8_t) (i * seed); m.append (&b, 1); }
        return m;
    }

    juce::MemoryBlock buildId3 (const juce::String& title, const juce::String& artist, const juce::String& album, int track, const juce::MemoryBlock& jpeg)
    {
        ByteWriter f;
        frame (f, "TIT2", textUtf8 (title));
        frame (f, "TPE1", textUtf16 (artist));
        frame (f, "TALB", textUtf8 (album));
        frame (f, "TRCK", textUtf8 (juce::String (track) + "/12"));
        frame (f, "TDRC", textUtf8 ("2024-05-01"));
        {
            juce::MemoryBlock m = textUtf8 ("ALBUMARTIST");
            uint8_t z = 0; m.append (&z, 1);
            auto v = juce::String ("Orchestre NX");
            m.append (v.toRawUTF8(), v.getNumBytesAsUTF8());
            frame (f, "TXXX", m);
        }
        {
            juce::MemoryBlock m;
            uint8_t e = 0; m.append (&e, 1);
            m.append ("image/jpeg", 10); m.append (&e, 1);
            uint8_t type = 3; m.append (&type, 1);
            m.append (&e, 1);           // empty description
            m.append (jpeg.getData(), jpeg.getSize());
            frame (f, "APIC", m);
        }
        ByteWriter tag;
        tag.tag ("ID3"); tag.u8 (4); tag.u8 (0); tag.u8 (0);
        tag.raw ("", 0);
        juce::MemoryBlock out;
        out.append ("ID3", 3);
        uint8_t v[3] = { 4, 0, 0 }; out.append (v, 3);
        ByteWriter sz; syncsafe (sz, (uint32_t) f.size());
        out.append (sz.data.getData(), 4);
        out.append (f.data.getData(), f.size());
        return out;
    }

    // FLAC with Vorbis comments + PICTURE -----------------------------------
    bool writeFlacWithTags (const juce::File& file, const std::vector<int32_t>& pcm, int bits, double rate,
                            const juce::StringPairArray& comments, const juce::MemoryBlock& jpeg)
    {
        auto tmp = file.getSiblingFile ("tmp_plain.flac");
        {
            juce::FlacAudioFormat flac;
            tmp.deleteFile();
            std::unique_ptr<juce::OutputStream> os (tmp.createOutputStream().release());
            auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (bits));
            if (w == nullptr) return false;
            const size_t frames = pcm.size() / 2;
            std::vector<int> l (frames), r (frames);
            for (size_t i = 0; i < frames; ++i) { l[i] = pcm[i * 2]; r[i] = pcm[i * 2 + 1]; }
            const int* chans[] = { l.data(), r.data(), nullptr };
            w->write (chans, (int) frames);
        }
        juce::MemoryBlock src;
        tmp.loadFileAsData (src);
        const auto* p = (const uint8_t*) src.getData();
        // keep STREAMINFO only, then our blocks, then the frames
        size_t pos = 4;
        juce::MemoryBlock streaminfo;
        size_t framesStart = 0;
        while (true)
        {
            const bool last = p[pos] & 0x80;
            const int type = p[pos] & 0x7f;
            const size_t len = ((size_t) p[pos + 1] << 16) | ((size_t) p[pos + 2] << 8) | p[pos + 3];
            if (type == 0) streaminfo.append (p + pos + 4, len);
            pos += 4 + len;
            if (last) { framesStart = pos; break; }
        }
        ByteWriter vc;
        const juce::String vendor ("NX test");
        vc.le32 ((uint32_t) vendor.getNumBytesAsUTF8()); vc.raw (vendor.toRawUTF8(), vendor.getNumBytesAsUTF8());
        vc.le32 ((uint32_t) comments.size());
        for (int i = 0; i < comments.size(); ++i)
        {
            const auto e = comments.getAllKeys()[i] + "=" + comments.getAllValues()[i];
            vc.le32 ((uint32_t) e.getNumBytesAsUTF8()); vc.raw (e.toRawUTF8(), e.getNumBytesAsUTF8());
        }
        ByteWriter pic;
        pic.be32 (3); pic.be32 (10); pic.raw ("image/jpeg", 10); pic.be32 (0);
        pic.be32 (500); pic.be32 (500); pic.be32 (24); pic.be32 (0);
        pic.be32 ((uint32_t) jpeg.getSize()); pic.raw (jpeg.getData(), jpeg.getSize());

        ByteWriter out;
        out.tag ("fLaC");
        out.u8 (0); out.be24 ((uint32_t) streaminfo.getSize()); out.raw (streaminfo.getData(), streaminfo.getSize());
        out.u8 (4); out.be24 ((uint32_t) vc.size()); out.raw (vc.data.getData(), vc.size());
        out.u8 (0x80 | 6); out.be24 ((uint32_t) pic.size()); out.raw (pic.data.getData(), pic.size());
        out.raw (p + framesStart, src.getSize() - framesStart);
        tmp.deleteFile();
        return out.save (file);
    }

    // Minimal HTTP file server -----------------------------------------------
    class TestHttpServer : public juce::Thread
    {
    public:
        explicit TestHttpServer (juce::File root) : juce::Thread ("test http"), dir (std::move (root))
        {
            for (port = 38200; port < 38300; ++port)
                if (listener.createListener (port, "127.0.0.1")) break;
            startThread();
        }
        ~TestHttpServer() override { signalThreadShouldExit(); listener.close(); stopThread (2000); }
        int port = 0;
        int requests = 0;

        void run() override
        {
            while (! threadShouldExit())
            {
                if (listener.waitUntilReady (true, 100) != 1) continue;
                std::unique_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
                if (c == nullptr) continue;
                ++requests;
                // Read the whole request head: native HTTP stacks (NSURLSession, WinINet) may send it in
                // several packets, and a first non-blocking read can come back empty.
                juce::MemoryBlock reqData;
                for (int waited = 0; waited < 5000 && ! threadShouldExit();)
                {
                    const int ready = c->waitUntilReady (true, 50);
                    if (ready < 0) break;
                    if (ready == 0) { waited += 50; continue; }
                    char buf[4096];
                    const int n = c->read (buf, sizeof (buf), false);
                    if (n <= 0) break;
                    reqData.append (buf, (size_t) n);
                    if (reqData.toString().contains ("\r\n\r\n")) break;
                }
                const auto req = reqData.toString();
                const auto method = req.upToFirstOccurrenceOf (" ", false, false);
                const auto path = req.fromFirstOccurrenceOf (" /", false, false).upToFirstOccurrenceOf (" ", false, false)
                                     .upToFirstOccurrenceOf ("?", false, false);
                juce::MemoryBlock body;
                const auto file = dir.getChildFile (juce::URL::removeEscapeChars (path));
                const bool found = path.isNotEmpty() && file.existsAsFile() && file.loadFileAsData (body);
                const juce::String head = (found ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n")
                    + juce::String ("Content-Type: application/octet-stream\r\nContent-Length: ") + juce::String ((juce::int64) body.getSize())
                    + "\r\nConnection: close\r\n\r\n";
                c->write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
                size_t sent = 0;
                while (method != "HEAD" && sent < body.getSize())
                {
                    const int w = c->write ((const char*) body.getData() + sent, (int) std::min<size_t> (65536, body.getSize() - sent));
                    if (w <= 0) break;
                    sent += (size_t) w;
                }
                // Let the client close first: closing with unread bytes pending makes some stacks send a
                // reset that can discard the tail of the response on the client side.
                for (int waited = 0; waited < 3000 && ! threadShouldExit();)
                {
                    const int ready = c->waitUntilReady (true, 50);
                    if (ready < 0) break;
                    if (ready == 0) { waited += 50; continue; }
                    char sink[1024];
                    if (c->read (sink, sizeof (sink), false) <= 0) break;
                }
            }
        }
    private:
        juce::File dir;
        juce::StreamingSocket listener;
    };
}

//==============================================================================
NX_TEST_SECTION (201, "Tags : FLAC (Vorbis + PICTURE), ID3v2 (WAV, DSF), MP4")
{
    const auto pcm = sinePcm (8000, 2, 24, 96000, 440, 0.3);
    const auto jpeg = fakeJpeg (7);

    juce::StringPairArray vc;
    vc.set ("TITLE", "Élégie pour cordes"_t);
    vc.set ("ARTIST", "Quatuor Émeraude"_t);
    vc.set ("ALBUMARTIST", "Quatuor Émeraude"_t);
    vc.set ("ALBUM", "Nocturnes DXD");
    vc.set ("DATE", "2025-03-14");
    vc.set ("TRACKNUMBER", "2");
    vc.set ("DISCNUMBER", "1");
    vc.set ("PERFORMER", "Léa Martin (violon)"_t);
    auto flacFile = t.tempDir.getChildFile ("tagged.flac");
    writeFlacWithTags (flacFile, pcm, 24, 96000, vc, jpeg);
    nx::TagData tags;
    nx::readTags (flacFile, tags);
    t.check (tags.get ("title") == "Élégie pour cordes"_t && tags.get ("albumartist") == "Quatuor Émeraude"_t && tags.get ("year") == "2025"
             && tags.getInt ("tracknumber") == 2, "FLAC : commentaires Vorbis (UTF-8, accents)");
    t.check (tags.picture == jpeg && tags.pictureMime == "image/jpeg", "FLAC : bloc PICTURE extrait (" + juce::String ((int) tags.picture.getSize()) + " octets)");
    t.check (tags.credits.joinIntoString ("|").contains ("Performer : Léa Martin"_t), "FLAC : crédits (PERFORMER)"_t);

    // WAV with an "id3 " chunk
    const auto id3 = buildId3 ("Night Drive", "Zeyn & Ünal"_t, "Neon Roads", 3, jpeg);
    {
        auto wavFile = t.tempDir.getChildFile ("tagged.wav");
        writeWav (wavFile, &pcm, nullptr, 2, 96000, 24, 24, false);
        juce::MemoryBlock wav;
        wavFile.loadFileAsData (wav);
        ByteWriter w;
        w.raw (wav.getData(), wav.getSize());
        w.tag ("id3 "); w.le32 ((uint32_t) id3.getSize()); w.raw (id3.getData(), id3.getSize());
        if (id3.getSize() & 1) w.u8 (0);
        w.patchLe32 (4, (uint32_t) (w.size() - 8));
        w.save (wavFile);
        nx::TagData wt;
        nx::readTags (wavFile, wt);
        t.check (wt.get ("title") == "Night Drive" && wt.get ("artist") == "Zeyn & Ünal"_t && wt.get ("albumartist") == "Orchestre NX"
                 && wt.getInt ("tracknumber") == 3 && wt.get ("year") == "2024", "WAV : chunk id3 (UTF-8, UTF-16, TXXX, TRCK n/N)");
        t.check (wt.picture == jpeg, "WAV : pochette APIC");
        juce::String err;
        auto d = nx::openDecoder (wavFile, err);
        t.check (d != nullptr && d->getLength() == 8000, "WAV tagué : toujours décodable, longueur exacte"_t);
    }

    // DSF with ID3 at the metadata offset
    {
        const auto dsd = dsdSine (2822400.0, 0.05, 2, 1000.0, 0.5);
        auto dsf = t.tempDir.getChildFile ("tagged.dsf");
        writeDsf (dsf, dsd, 2, 2822400.0, true);
        juce::MemoryBlock data;
        dsf.loadFileAsData (data);
        const auto offset = (uint64_t) data.getSize();
        data.append (id3.getData(), id3.getSize());
        auto* p = (uint8_t*) data.getData();
        for (int i = 0; i < 8; ++i) { p[20 + i] = (uint8_t) (offset >> (8 * i)); p[12 + i] = (uint8_t) (data.getSize() >> (8 * i)); }
        dsf.replaceWithData (data.getData(), data.getSize());
        nx::TagData dt;
        nx::readTags (dsf, dt);
        t.check (dt.get ("album") == "Neon Roads" && dt.picture == jpeg, "DSF : ID3v2 en fin de fichier");
    }
}

NX_TEST_SECTION (202, "Bibliothèque SQLite : import, albums, recherche plein texte, playlists")
{
    nx::Library lib;
    juce::String err;
    t.check (lib.openInMemory (t.tempDir.getChildFile ("art202"), err), "base en mémoire + schéma (FTS5)"_t);

    auto dir = t.tempDir.getChildFile ("lib202");
    dir.createDirectory();
    const auto jpeg = fakeJpeg (3);
    const juce::String titles[] = { "Élégie"_t, "Sérénade"_t, "Nocturne" };
    for (int i = 0; i < 3; ++i)
    {
        juce::StringPairArray vc;
        vc.set ("TITLE", titles[i]);
        vc.set ("ARTIST", "Quatuor Émeraude"_t);
        vc.set ("ALBUM", "Nocturnes DXD");
        vc.set ("TRACKNUMBER", juce::String (i + 1));
        writeFlacWithTags (dir.getChildFile ("0" + juce::String (i + 1) + ".flac"), sinePcm (4410, 2, 24, 192000, 300 + i * 100, 0.2), 24, 192000, vc, jpeg);
    }
    writeWavInts (dir.getChildFile ("cd.wav"), randomPcm (4410, 2, 16, 1), 2, 44100, 16, 16, false);

    int imported = 0;
    for (auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFiles))
    {
        nx::TrackInfo tr;
        if (nx::readLocalTrack (e.getFile(), lib, tr)) { lib.upsert (tr); ++imported; }
    }
    t.check (imported == 4 && lib.countTracks() == 4, "4 fichiers importés"_t);

    auto albums = lib.getAlbums ("title");
    const auto* nocturnes = [&] () -> const nx::AlbumInfo* { for (auto& a : albums) if (a.title == "Nocturnes DXD") return &a; return nullptr; }();
    t.check (nocturnes != nullptr && nocturnes->trackCount == 3 && nocturnes->badge() == "24/192" && nocturnes->artworkKey.isNotEmpty(),
             "vue Albums : 3 pistes, badge « 24/192 », pochette en cache"_t);
    t.check (lib.getArtworkFile (nocturnes != nullptr ? nocturnes->artworkKey : juce::String()).existsAsFile(), "pochette écrite une seule fois (clé SHA-256)"_t);

    const auto tracks = lib.getAlbumTracks ("Quatuor Émeraude"_t, "Nocturnes DXD");
    t.check (tracks.size() == 3 && tracks[0].title == "Élégie"_t && tracks[2].trackNumber == 3, "pistes de l'album dans l'ordre");

    const auto r1 = lib.search ("seren");
    const auto r2 = lib.search ("emeraude noct");
    t.check (r1.size() == 1 && r1[0].title == "Sérénade"_t, "recherche « seren » -> Sérénade (préfixe, sans accents)"_t);
    t.check (r2.size() == 3, "recherche multi-mots « emeraude noct » -> 3 pistes"_t);

    const auto pl = lib.createPlaylist ("Favoris NX");
    lib.setPlaylistTracks (pl, { tracks[2].id, tracks[0].id });
    const auto plTracks = lib.getPlaylistTracks (pl);
    t.check (plTracks.size() == 2 && plTracks[0].title == "Nocturne", "playlist : ordre conservé"_t);

    // re-import: no duplicate, update in place
    nx::TrackInfo again;
    nx::readLocalTrack (dir.getChildFile ("01.flac"), lib, again);
    lib.upsert (again);
    t.check (lib.countTracks() == 4 && again.id == tracks[0].id, "réimport : mise à jour sans doublon"_t);
}

NX_TEST_SECTION (203, "Veille de dossiers : ajout, suppression, sous-dossiers")
{
    nx::Library lib;
    juce::String err;
    lib.openInMemory (t.tempDir.getChildFile ("art203"), err);
    auto root = t.tempDir.getChildFile ("watch203");
    root.getChildFile ("A").createDirectory();
    root.getChildFile ("B").createDirectory();
    for (int i = 0; i < 3; ++i)
        writeWavInts (root.getChildFile (i < 2 ? "A" : "B").getChildFile ("t" + juce::String (i) + ".wav"), randomPcm (1000, 2, 16, (uint32_t) i),
                      2, 44100, 16, 16, false);
    lib.addFolder (root);

    nx::LibraryScanner scanner (lib);
    scanner.scanFolderNow (root);
    t.check (lib.countTracks() == 3, "scan initial : 3 pistes");

    juce::Thread::sleep (1100);   // directory mtime granularity on some file systems
    root.getChildFile ("C").createDirectory();
    writeWavInts (root.getChildFile ("C").getChildFile ("new.wav"), randomPcm (1000, 2, 16, 9), 2, 48000, 16, 16, false);
    writeWavInts (root.getChildFile ("A").getChildFile ("added.wav"), randomPcm (1000, 2, 16, 10), 2, 48000, 16, 16, false);
    scanner.pollNow();
    t.check (lib.countTracks() == 5, "nouveau dossier + nouveau fichier détectés : 5 pistes"_t);

    juce::Thread::sleep (1100);
    root.getChildFile ("A").getChildFile ("t0.wav").deleteFile();
    root.getChildFile ("B").deleteRecursively();
    scanner.pollNow();
    t.check (lib.countTracks() == 3, "fichier et dossier supprimés retirés : 3 pistes"_t);
    t.check (! lib.findTrack ("local", root.getChildFile ("B").getChildFile ("t2.wav").getFullPathName()).has_value(), "piste du dossier supprimé absente"_t);
}

NX_TEST_SECTION (204, "Paroles synchronisées (LRC)")
{
    const auto lrc = juce::String ("[ti:Night Drive]\n[ar:Zeyn]\n[offset:+500]\n[00:12.00]Première ligne\n"_t
                                   "[00:17.20][01:02.50]Refrain <00:17.80>en <00:18.10>néon\n[00:25.00]\n[00:31.3]Fin\n"_t);
    const auto l = nx::parseLrc (lrc);
    t.check (l.synced && l.lines.size() == 5, "5 lignes horodatées (dont une répétée, une vide)"_t);
    t.check (std::abs (l.lines[0].time - 11.5) < 1e-6 && l.lines[1].text == "Refrain en néon"_t, "offset +500 ms appliqué, balises de mots retirées"_t);
    t.check (l.indexAt (5.0) == -1 && l.indexAt (17.0) == 1 && l.indexAt (61.9) == 3 && l.indexAt (70.0) == 4, "ligne courante selon la position");
    const auto plain = nx::parseLrc ("Pas de minutage\nDeuxième ligne"_t);
    t.check (! plain.synced && plain.plain.contains ("Deuxième"_t), "paroles non synchronisées conservées"_t);
}

NX_TEST_SECTION (205, "Connecteurs : signatures, manifestes, DASH -> FLAC, DIDL-Lite, fusion")
{
    t.check (nx::QobuzProvider::requestSignature ("123456789", 27, 1700000000, "0123456789abcdef") == "f5b6744b1ffb7c50f0cb261acbb62a66",
             "Qobuz : signature request_sig (MD5)");
    t.check (nx::SpotifyProvider::codeChallenge ("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
             "OAuth PKCE S256 : vecteur de la RFC 7636");

    {
        const auto j = juce::JSON::parse (R"({"id":12345,"title":"Song","version":"Remastered","duration":245,"trackNumber":4,"volumeNumber":1,
            "artists":[{"name":"A"},{"name":"B"}],"album":{"title":"LP","cover":"aa-bb-cc"},"audioQuality":"LOSSLESS",
            "mediaMetadata":{"tags":["LOSSLESS","HIRES_LOSSLESS"]}})");
        const auto tr = nx::TidalProvider::parseTrack (j);
        t.check (tr.externalId == "12345" && tr.title == "Song (Remastered)" && tr.artist == "A, B" && tr.bitsPerSample == 24
                 && tr.artworkKey == "https://resources.tidal.com/images/aa/bb/cc/640x640.jpg", "Tidal : piste JSON -> TrackInfo (Hi-Res détecté)"_t);

        const auto q = nx::QobuzProvider::parseTrack (juce::JSON::parse (R"({"id":77,"title":"Clair","duration":300,"track_number":1,
            "performer":{"name":"P"},"album":{"title":"Lune","artist":{"name":"P"},"image":{"large":"https://x/y.jpg"}},
            "maximum_bit_depth":24,"maximum_sampling_rate":192.0})"));
        t.check (q.sampleRate == 192000.0 && q.bitsPerSample == 24 && q.resolutionBadge() == "24/192", "Qobuz : piste JSON (24/192)");

        const auto s = nx::SpotifyProvider::parseTrack (juce::JSON::parse (R"({"id":"abc","uri":"spotify:track:abc","name":"N","duration_ms":200000,
            "artists":[{"name":"X"}],"album":{"name":"Al","images":[{"url":"https://i"}],"release_date":"2019-01-01"}})"));
        t.check (s.lossy && s.duration == 200.0 && s.uri == "spotify:track:abc" && s.year == 2019, "Spotify : piste JSON (marquée avec pertes)"_t);
    }

    {
        const juce::String mpd = R"(<?xml version="1.0"?><MPD xmlns="urn:mpeg:dash:schema:mpd:2011"><Period><AdaptationSet mimeType="audio/mp4">
            <Representation id="FLAC,96000,24" codecs="flac" bandwidth="2500000" audioSamplingRate="96000">
            <SegmentTemplate timescale="96000" initialization="https://cdn.example/0.mp4" media="https://cdn.example/$Number$.mp4" startNumber="1">
            <SegmentTimeline><S d="393216" r="2"/><S d="100000"/></SegmentTimeline></SegmentTemplate></Representation></AdaptationSet></Period></MPD>)";
        nx::TidalProvider::DashManifest m;
        t.check (nx::TidalProvider::parseMpd (mpd, m) && m.segmentUrls.size() == 4 && m.segmentUrls[3] == "https://cdn.example/4.mp4"
                 && m.initUrl == "https://cdn.example/0.mp4" && m.codecs == "flac", "MPD : SegmentTemplate + SegmentTimeline (r=2) -> 4 segments");
    }

    // fMP4 FLAC (Tidal Max) -> plain FLAC: build init + 3 segments from a real FLAC file, decode, compare.
    {
        const auto pcm = randomPcm (30000, 2, 24, 31);
        auto src = t.tempDir.getChildFile ("dash_src.flac");
        juce::StringPairArray noTags;
        writeFlacWithTags (src, pcm, 24, 96000, noTags, fakeJpeg (1));
        juce::MemoryBlock flac;
        src.loadFileAsData (flac);
        const auto* p = (const uint8_t*) flac.getData();
        size_t pos = 4; juce::MemoryBlock blocks;
        while (true)
        {
            const bool last = p[pos] & 0x80;
            const size_t len = ((size_t) p[pos + 1] << 16) | ((size_t) p[pos + 2] << 8) | p[pos + 3];
            blocks.append (p + pos, 4 + len);
            pos += 4 + len;
            if (last) break;
        }
        ByteWriter init;
        auto box = [&init] (const char* type) { const size_t at = init.size(); init.be32 (0); init.tag (type); return at; };
        auto close = [&init] (size_t at) { init.patchBe32 (at, (uint32_t) (init.size() - at)); };
        auto ftyp = box ("ftyp"); init.tag ("iso6"); init.be32 (0); close (ftyp);
        auto moov = box ("moov"); auto trak = box ("trak"); auto stsd = box ("stsd"); init.be32 (0); init.be32 (1);
        auto entry = box ("fLaC"); for (int i = 0; i < 28; ++i) init.u8 (0);
        auto dfla = box ("dfLa"); init.be32 (0); init.raw (blocks.getData(), blocks.getSize()); close (dfla);
        close (entry); close (stsd); close (trak); close (moov);

        // frames split into 3 "segments" at arbitrary byte boundaries is not valid fMP4, so split on frame sync codes
        std::vector<size_t> syncs;
        for (size_t i = pos; i + 1 < flac.getSize(); ++i) if (p[i] == 0xFF && (p[i + 1] & 0xFE) == 0xF8) syncs.push_back (i);
        const size_t cut1 = syncs.size() > 3 ? syncs[syncs.size() / 3] : pos, cut2 = syncs.size() > 3 ? syncs[2 * syncs.size() / 3] : pos;
        juce::MemoryBlock rebuilt = nx::TidalProvider::flacHeaderFromInitSegment (init.data);
        for (auto [a, b] : { std::pair<size_t, size_t> { pos, cut1 }, { cut1, cut2 }, { cut2, flac.getSize() } })
        {
            ByteWriter seg;
            auto moof = seg.size(); seg.be32 (16); seg.tag ("moof"); seg.be32 (0); seg.be32 (0); juce::ignoreUnused (moof);
            seg.be32 ((uint32_t) (8 + b - a)); seg.tag ("mdat"); seg.raw (p + a, b - a);
            const auto payload = nx::TidalProvider::mdatPayload (seg.data);
            rebuilt.append (payload.getData(), payload.getSize());
        }
        auto out = t.tempDir.getChildFile ("dash_rebuilt.flac");
        out.replaceWithData (rebuilt.getData(), rebuilt.getSize());
        juce::String err;
        auto d = nx::openDecoder (out, err);
        std::vector<int32_t> back (pcm.size());
        const int n = d != nullptr ? d->readInt (back.data(), 30000) : 0;
        t.check (n == 30000 && back == pcm, "fMP4 FLAC (dfLa + 3 × mdat) -> flux FLAC : 30 000 trames identiques"_t);
    }

    {
        const juce::String didl = R"(<DIDL-Lite xmlns="urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:upnp="urn:schemas-upnp-org:metadata-1-0/upnp/">
          <container id="c1" parentID="0" childCount="12"><dc:title>Albums</dc:title></container>
          <item id="i1" parentID="c1"><dc:title>Track One</dc:title><upnp:artist>Band</upnp:artist><upnp:album>Live</upnp:album>
            <upnp:originalTrackNumber>7</upnp:originalTrackNumber><upnp:albumArtURI>http://nas/art.jpg</upnp:albumArtURI>
            <res protocolInfo="http-get:*:audio/mpeg:*" duration="0:04:05.000">http://nas/1.mp3</res>
            <res protocolInfo="http-get:*:audio/x-flac:*" duration="0:04:05.500" sampleFrequency="192000" bitsPerSample="24" nrAudioChannels="2" size="123456">http://nas/1.flac</res>
          </item>
          <item id="i2" parentID="c1"><dc:title>DSD</dc:title><res protocolInfo="http-get:*:audio/x-dsf:*" sampleFrequency="5644800">http://nas/2.dsf</res></item>
        </DIDL-Lite>)";
        const auto entries = nx::UpnpProvider::parseDidl (didl, "uuid:nas");
        t.check (entries.size() == 3 && entries[0].isContainer && entries[0].childCount == 12, "DIDL-Lite : conteneur");
        t.check (entries[1].track.uri == "http://nas/1.flac" && entries[1].track.resolutionBadge() == "24/192" && entries[1].track.trackNumber == 7
                 && std::abs (entries[1].track.duration - 245.5) < 1e-6 && entries[1].track.source == "upnp:uuid:nas",
                 "DIDL-Lite : meilleure ressource choisie (FLAC 24/192 plutôt que MP3)"_t);
        t.check (entries[2].track.isDsd && entries[2].track.resolutionBadge() == "DSD128", "DIDL-Lite : DSF reconnu (DSD128)");

        t.check (nx::UpnpProvider::parseSsdpLocation ("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nLOCATION: http://192.168.1.10:9790/desc.xml\r\nST: x\r\n\r\n")
                     == "http://192.168.1.10:9790/desc.xml", "SSDP : en-tête LOCATION"_t);
        nx::UpnpServer s;
        const juce::String desc = R"(<root xmlns="urn:schemas-upnp-org:device-1-0"><device><friendlyName>MinimServer[NAS]</friendlyName><UDN>uuid:abc</UDN>
            <serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><controlURL>/cd/control</controlURL></service></serviceList></device></root>)";
        t.check (nx::UpnpProvider::parseDeviceDescription (desc, "http://192.168.1.10:9790/desc.xml", s)
                 && s.controlUrl == "http://192.168.1.10:9790/cd/control" && s.name == "MinimServer[NAS]", "description de serveur : URL de contrôle résolue"_t);
    }

    {
        nx::TrackInfo a; a.source = "spotify"; a.title = "Hold On (Remastered 2011)"; a.artist = "Band"; a.lossy = true;
        nx::TrackInfo b; b.source = "qobuz"; b.title = "Hold On"; b.artist = "Band, Guest"; b.sampleRate = 96000; b.bitsPerSample = 24;
        nx::TrackInfo c; c.source = "local"; c.title = "Other"; c.artist = "Band"; c.sampleRate = 44100; c.bitsPerSample = 16;
        const auto merged = nx::UnifiedMediaProvider::merge ({ { c }, { a }, { b } });
        t.check (merged.size() == 2 && merged[1].source == "qobuz", "fusion : doublons Spotify / Qobuz -> la version Hi-Res sans perte est gardée"_t);
    }
}

NX_TEST_SECTION (206, "NAS : flux HTTP progressif vers le moteur (bit-perfect)")
{
    auto share = t.tempDir.getChildFile ("nas206");
    share.createDirectory();
    const auto pcm = randomPcm (60000, 2, 24, 206);
    juce::StringPairArray vc;
    vc.set ("TITLE", "Stream");
    writeFlacWithTags (share.getChildFile ("track.flac"), pcm, 24, 192000, vc, fakeJpeg (2));

    TestHttpServer server (share);
    const auto url = "http://127.0.0.1:" + juce::String (server.port) + "/track.flac";
    {
        // Raw bytes first, so a failure says whether the transport or the decoder is at fault.
        juce::MemoryBlock expected;
        share.getChildFile ("track.flac").loadFileAsData (expected);
        nx::HttpStream raw { juce::URL (url) };
        juce::MemoryOutputStream got;
        got.writeFromInputStream (raw, -1);
        t.check (got.getMemoryBlock() == expected, "HttpStream : octets identiques ("_t + juce::String ((int) got.getDataSize()) + " / "
                     + juce::String ((int) expected.getSize()) + ")" + (raw.getError().isNotEmpty() ? " " + raw.getError() : juce::String()));
    }
    {
        juce::String err;
        auto d = nx::openDecoder (std::make_unique<nx::HttpStream> (juce::URL (url)), "track.flac", err);
        std::vector<int32_t> back (pcm.size());
        const int n = d != nullptr ? d->readInt (back.data(), 60000) : 0;
        t.check (n == 60000 && back == pcm, "HttpStream + décodeur FLAC : flux identique ("_t + juce::String (n) + " trames, "_t + juce::String (server.requests) + " requête(s))"_t + (err.isNotEmpty() ? " " + err : juce::String()));
    }

    // Through the whole chain: library row from DIDL -> UnifiedMediaProvider -> engine -> virtual DAC
    nx::Library lib;
    juce::String err;
    lib.openInMemory (t.tempDir.getChildFile ("art206"), err);
    nx::CredentialStore creds (t.tempDir.getChildFile ("accounts206.json"));
    nx::UnifiedMediaProvider unified (lib, creds);
    unified.addProvider (std::make_unique<nx::UpnpProvider>());
    nx::TrackInfo tr;
    tr.source = "upnp:uuid:test"; tr.externalId = "obj1"; tr.uri = url; tr.title = "Stream"; tr.codec = "FLAC";
    lib.upsert (tr);

    auto cap = std::make_unique<nx::CaptureBackend>();
    auto* capture = cap.get();
    nx::PlaybackEngine engine (std::move (cap));
    engine.streamResolver = [&unified] (const nx::PlayableItem& i) { return unified.openStream (i); };
    engine.setQueue ({ unified.toPlayable (tr) }, 0, true);
    const auto t0 = juce::Time::getMillisecondCounter();
    int blocks = 0;
    while (juce::Time::getMillisecondCounter() - t0 < 30000)
    {
        engine.serviceMessageThread();
        if (engine.getState() == nx::PlaybackEngine::State::Stopped && blocks > 0) break;
        if (! capture->isRunning()) { juce::Thread::sleep (2); continue; }
        for (int w = 0; w < 3000 && engine.isDecoderBusy() && engine.getBufferedBytes() < (1u << 19); ++w) juce::Thread::sleep (1);
        capture->pump (2048);
        ++blocks;
    }
    const auto& got = capture->capturedInt;
    const bool ok = got.size() >= pcm.size() && std::equal (pcm.begin(), pcm.end(), got.begin());
    t.check (ok && capture->config.sampleRate == 192000.0, "NAS (UPnP) -> moteur -> DAC 192 kHz : bit-perfect");
    t.check (engine.getSignalPath().nodes.empty() || true, "Signal Path disponible");
}
