// NX Player — MIT License
// v1.0: Memory Play, studio tools, library filters / favourites / playlists / NAS clean-up, artist pages,
// NX Remote (HTTP API, token, QR code), AuraEngine X bridge, English dictionary.
#include "TestFramework.h"
#include "TestAudioFiles.h"
#include "../Source/Audio/MemoryPlayStream.h"
#include "../Source/DSP/StudioTools.h"
#include "../Source/Engine/PlaybackEngine.h"
#include "../Source/Engine/DeviceProfiles.h"
#include "../Source/Output/CaptureBackend.h"
#include "../Source/Library/Library.h"
#include "../Source/Library/ArtistInfo.h"
#include "../Source/Remote/RemoteServer.h"
#include "../Source/Providers/Http.h"
#include "../Source/Platform/AuraBridge.h"
#include "../Source/Platform/SystemMonitor.h"
#include "../Source/GUI/Waveform.h"
#include <qrcodegen.hpp>
#include <cmath>
#include <map>

using namespace nxtest;

namespace
{
    struct Rig
    {
        std::unique_ptr<nx::PlaybackEngine> engine;
        nx::CaptureBackend* cap = nullptr;
        explicit Rig (std::function<void (nx::CaptureBackend&)> configure = {})
        {
            auto c = std::make_unique<nx::CaptureBackend>();
            if (configure) configure (*c);
            cap = c.get();
            engine = std::make_unique<nx::PlaybackEngine> (std::move (c));
        }
        static nx::PlayableItem item (const juce::File& f)
        {
            nx::PlayableItem i;
            i.id = f.getFileName();
            i.uri = f.getFullPathName();
            i.title = f.getFileNameWithoutExtension();
            return i;
        }
        void run (std::function<void (int)> perBlock = {}, int block = 1024)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            int blocks = 0;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < 60000.0)
            {
                engine->serviceMessageThread();
                if (engine->getState() == nx::PlaybackEngine::State::Stopped && blocks > 0) break;
                if (! cap->isRunning()) { juce::Thread::sleep (1); continue; }
                for (int w = 0; w < 4000 && engine->isDecoderBusy() && engine->getBufferedBytes() < (1u << 19); ++w) juce::Thread::sleep (1);
                cap->pump (block);
                if (perBlock) perBlock (blocks);
                ++blocks;
            }
        }
    };

    /** Minimal HTTP server: path (without query) -> body; also records the queries. */
    class MiniHttp : public juce::Thread
    {
    public:
        std::map<juce::String, juce::String> routes;      // "/search.php?s=Zeyn" or "/wiki/Zeyn"
        juce::StringArray requests;
        int port = 0;
        MiniHttp() : juce::Thread ("mini http")
        {
            for (port = 38600; port < 38700; ++port) if (listener.createListener (port, "127.0.0.1")) break;
            startThread();
        }
        ~MiniHttp() override { signalThreadShouldExit(); listener.close(); stopThread (3000); }
        juce::String base() const { return "http://127.0.0.1:" + juce::String (port); }
    private:
        void run() override
        {
            while (! threadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> s (listener.waitForNextConnection());
                if (s == nullptr) continue;
                char buf[8192];
                juce::String req;
                while (! req.contains ("\r\n\r\n") && s->waitUntilReady (true, 2000) == 1)
                {
                    const int n = s->read (buf, sizeof (buf), false);
                    if (n <= 0) break;
                    req += juce::String::fromUTF8 (buf, n);
                }
                const auto target = juce::URL::removeEscapeChars (req.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false).replaceCharacter ('+', ' '));
                requests.add (target);
                auto it = routes.find (target);
                const bool found = it != routes.end();
                const auto body = found ? it->second : juce::String ("{}");
                juce::String head;
                head << "HTTP/1.1 " << (found ? "200 OK" : "404 Not Found") << "\r\nContent-Type: application/json\r\nContent-Length: "
                     << (int) body.getNumBytesAsUTF8() << "\r\nConnection: close\r\n\r\n";
                s->write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
                s->write (body.toRawUTF8(), (int) body.getNumBytesAsUTF8());
            }
        }
        juce::StreamingSocket listener;
    };

    nx::TrackInfo makeTrack (const juce::String& source, const juce::String& id, const juce::String& artist, const juce::String& album,
                             double rate, int bits, bool dsd, bool lossy, const juce::String& genre)
    {
        nx::TrackInfo t;
        t.source = source; t.externalId = id; t.uri = id; t.title = "T " + id; t.artist = t.albumArtist = artist; t.album = album;
        t.sampleRate = rate; t.bitsPerSample = bits; t.isDsd = dsd; t.lossy = lossy; t.genre = genre; t.codec = dsd ? "DSF" : lossy ? "MP3" : "FLAC";
        t.duration = 100;
        return t;
    }
}

//==============================================================================
NX_TEST_SECTION (111, "Moteur v1 : Memory Play (fichier en RAM) et Outils Studio (mono, M/S, phase, crossfeed)")
{
    // ---- MemoryPlayStream: identical bytes, seek, accounting -------------------------------------------
    {
        auto f = t.tempDir.getChildFile ("memoryplay.bin");
        juce::MemoryBlock data (5 * 1024 * 1024 + 123);
        juce::Random r (42);
        for (size_t i = 0; i < data.getSize(); ++i) static_cast<uint8_t*> (data.getData())[i] = (uint8_t) r.nextInt (256);
        f.replaceWithData (data.getData(), data.getSize());
        const auto before = nx::MemoryPlayStream::residentBytes();
        bool same = false, seekOk = false;
        juce::int64 resident = 0;
        {
            auto s = nx::MemoryPlayStream::open (f, 64 * 1024 * 1024);
            if (s != nullptr)
            {
                juce::MemoryBlock back;
                s->readIntoMemoryBlock (back);
                same = back == data;
                s->setPosition (3 * 1024 * 1024 + 7);
                uint8_t b[64];
                seekOk = s->read (b, 64) == 64 && std::memcmp (b, static_cast<const uint8_t*> (data.getData()) + 3 * 1024 * 1024 + 7, 64) == 0;
                resident = nx::MemoryPlayStream::residentBytes() - before;
            }
        }
        t.check (same && seekOk, "Memory Play : 5 Mo relus en RAM octet pour octet, seek exact");
        t.check (resident == (juce::int64) data.getSize() && nx::MemoryPlayStream::residentBytes() == before,
                 "RAM comptée pendant la lecture ("_t + juce::String (resident / 1024) + " Ko), rendue à la fermeture");
        t.check (nx::MemoryPlayStream::open (f, 1024 * 1024) == nullptr, "fichier plus gros que la limite : lu depuis le disque"_t);
    }

    // ---- Engine: Memory Play stays bit-perfect, Signal Path "Moteur" node -----------------------------
    const auto pcm = randomPcm (60000, 2, 24, 777u);
    auto wav = t.tempDir.getChildFile ("mp_2496.wav");
    writeWav (wav, &pcm, nullptr, 2, 96000, 24, 24, false);
    for (bool memory : { true, false })
    {
        Rig rig;
        auto st = rig.engine->getSettings();
        st.memoryPlay = memory;
        rig.engine->setSettings (st);
        rig.engine->setQueue ({ Rig::item (wav) }, 0, true);
        nx::SignalPath path;
        nx::PlaybackPlan plan;
        rig.run ([&] (int b) { if (b == 5) { path = rig.engine->getSignalPath(); plan = rig.engine->getCurrentPlan(); } });
        const auto& got = rig.cap->capturedInt;
        bool exact = got.size() >= pcm.size();
        for (size_t i = 0; exact && i < pcm.size(); ++i) exact = got[i] == pcm[i];
        bool engineNode = false, dacNode = false;
        for (auto& n : path.nodes)
        {
            engineNode |= n.stage == "Moteur" && (n.title.contains ("Memory Play") == memory);
            dacNode |= n.stage == "DAC";
        }
        t.check (exact && plan.inMemory == memory && path.overall == nx::PathQuality::BitPerfect,
                 (memory ? "Memory Play actif"_t : juce::String ("Memory Play désactivé"_t)) + " : 60 000 trames 24/96 bit-perfect");
        t.check (engineNode && dacNode, "Signal Path : nœuds « Moteur NX 64 bits"_t + (memory ? " · Memory Play" : "") + " » et « DAC »");
    }

    // ---- Studio tools: numeric checks -----------------------------------------------------------------
    auto state = std::make_shared<nx::StudioToolsState>();
    nx::StudioToolsProcessor proc (state);
    proc.prepare (48000.0, 2, 4096);
    const int n = 4800;
    std::vector<double> L (n), R (n);
    auto fill = [&] (double fl, double ampL, double fr, double ampR)
    {
        for (int i = 0; i < n; ++i)
        {
            L[(size_t) i] = ampL * std::sin (2.0 * juce::MathConstants<double>::pi * fl * i / 48000.0);
            R[(size_t) i] = ampR * std::sin (2.0 * juce::MathConstants<double>::pi * fr * i / 48000.0);
        }
    };
    auto runProc = [&] { proc.reset(); double* ch[2] = { L.data(), R.data() }; proc.process (ch, 2, n); };
    t.check (proc.isBypassed(), "outils tous éteints : processeur contourné (bit-perfect conservé)"_t);

    fill (1000, 0.5, 1000, 0.0);
    state->monitor = nx::StudioToolsState::Monitor::Mono;
    runProc();
    double maxDiff = 0, maxAmp = 0;
    for (int i = 0; i < n; ++i) { maxDiff = juce::jmax (maxDiff, std::abs (L[(size_t) i] - R[(size_t) i])); maxAmp = juce::jmax (maxAmp, std::abs (L[(size_t) i])); }
    t.check (! proc.isBypassed() && maxDiff == 0.0 && std::abs (maxAmp - 0.25) < 1e-3, "Mono (L+R)/2 : voies identiques, gauche seule -> 0,25 sur les deux"_t);

    fill (1000, 0.5, 1000, 0.5);    // identical channels: Side = 0, Mid = signal
    state->monitor = nx::StudioToolsState::Monitor::SideSolo;
    runProc();
    double side = 0;
    for (int i = 0; i < n; ++i) side = juce::jmax (side, std::abs (L[(size_t) i]) + std::abs (R[(size_t) i]));
    fill (1000, 0.5, 1000, 0.5);
    state->monitor = nx::StudioToolsState::Monitor::MidSolo;
    runProc();
    double mid = 0;
    for (int i = 0; i < n; ++i) mid = juce::jmax (mid, std::abs (L[(size_t) i]));
    t.check (side == 0.0 && std::abs (mid - 0.5) < 1e-3, "Solo Side d'un signal centré = silence, Solo Mid = le signal"_t);

    fill (1000, 0.5, 700, -0.3);
    const auto origL = L, origR = R;
    state->monitor = nx::StudioToolsState::Monitor::Stereo;
    state->invertPhase = true;
    runProc();
    bool inverted = true;
    for (int i = 0; i < n; ++i) inverted &= L[(size_t) i] == -origL[(size_t) i] && R[(size_t) i] == -origR[(size_t) i];
    t.check (inverted, juce::String::fromUTF8 ("Inversion de phase (\xc3\x98) : chaque \xc3\xa9" "chantillon exactement oppos\xc3\xa9"));
    state->invertPhase = false;

    // crossfeed: left only -> the right channel receives a low-passed share
    state->crossfeed = true;
    auto level = [&] (double freq)
    {
        fill (freq, 0.5, freq, 0.0);
        runProc();
        double l2 = 0, r2 = 0;
        for (int i = n / 2; i < n; ++i) { l2 += L[(size_t) i] * L[(size_t) i]; r2 += R[(size_t) i] * R[(size_t) i]; }
        return 10.0 * std::log10 (r2 / juce::jmax (1e-30, l2));
    };
    const double low = level (100.0), high = level (10000.0);
    t.check (low > -12.0 && low < -4.0 && high < low - 8.0,
             "Smart Crossfeed (bs2b 700 Hz / 4,5 dB) : voie opposée à "_t + juce::String (low, 1) + " dB à 100 Hz, " + juce::String (high, 1) + " dB à 10 kHz");
    state->monitor = nx::StudioToolsState::Monitor::Mono;
    fill (100.0, 0.5, 100.0, 0.0);
    runProc();
    t.check (std::abs (L[(size_t) n / 2] - R[(size_t) n / 2]) < 1e-12 && state->describe().contains ("inactif"), "crossfeed coupé automatiquement en mono"_t);
    state->crossfeed = false;

    // ---- Engine: studio tools in the chain, DSD converted to PCM while a tool is on -------------------
    {
        const auto stereo = sinePcm (48000, 2, 24, 48000, 440.0, 0.4);
        auto mixed = stereo;
        for (size_t i = 1; i < mixed.size(); i += 2) mixed[i] = 0;     // right channel silent
        auto f = t.tempDir.getChildFile ("studio.wav");
        writeWav (f, &mixed, nullptr, 2, 48000, 24, 24, false);
        Rig rig;
        auto chain = std::make_unique<nx::DspChain>();
        auto tools = std::make_shared<nx::StudioToolsState>();
        tools->monitor = nx::StudioToolsState::Monitor::Mono;
        chain->processors.push_back (std::make_unique<nx::StudioToolsProcessor> (tools));
        rig.engine->setDspChain (std::move (chain));
        rig.engine->setQueue ({ Rig::item (f) }, 0, true);
        nx::SignalPath path;
        rig.run ([&] (int b) { if (b == 5) path = rig.engine->getSignalPath(); });
        const auto& got = rig.cap->capturedInt;
        bool lrEqual = got.size() > 40000;
        for (size_t i = 20000; lrEqual && i + 1 < 80000 && i + 1 < got.size(); i += 2) lrEqual = got[i] == got[i + 1];
        bool dspNode = false;
        for (auto& nd : path.nodes) dspNode |= nd.stage == "DSP" && nd.title.contains ("Mono");
        t.check (lrEqual && dspNode && path.overall == nx::PathQuality::Enhanced,
                 "moteur : Outils Studio « Mono » dans la chaîne, L = R en sortie, Signal Path « Amélioré »"_t);
    }
    {
        auto dsf = t.tempDir.getChildFile ("studio_dsd.dsf");
        writeDsf (dsf, dsdSine (2822400.0, 0.3, 2, 1000.0, 0.4), 2, 2822400.0, true);
        Rig rig ([] (nx::CaptureBackend& c) { c.capabilities.sampleRates = { 44100.0, 88200.0, 176400.0, 352800.0 }; });
        auto st = rig.engine->getSettings();
        st.dspNeedsPcm = true;
        rig.engine->setSettings (st);
        rig.engine->setQueue ({ Rig::item (dsf) }, 0, true);
        nx::PlaybackPlan plan;
        rig.run ([&] (int b) { if (b == 3) plan = rig.engine->getCurrentPlan(); }, 2048);
        t.check (plan.wire == nx::WireFormat::Pcm && plan.conversion == nx::PlaybackPlan::Conversion::DsdToPcm && plan.reason.contains ("Outils Studio"),
                 "DSD + outil actif : converti en PCM "_t + nx::formatRate (plan.deviceRate) + " (au lieu du DoP) pour que l'outil s'applique");
    }
}

//==============================================================================
NX_TEST_SECTION (210, "Bibliothèque v1 : filtres DXD / DSD / Hi-Res / CD, genres, favoris, playlists, nettoyage NAS")
{
    nx::Library lib;
    juce::String err;
    lib.openInMemory (t.tempDir.getChildFile ("art207"), err);
    std::vector<nx::TrackInfo> all {
        makeTrack ("local", "a1", "Quatuor", "Nocturnes DXD", 352800, 24, false, false, "Classique"),
        makeTrack ("local", "a2", "Quatuor", "Nocturnes DXD", 352800, 24, false, false, "Classique"),
        makeTrack ("local", "b1", "Lumen", "Sacred", 5644800, 1, true, false, "Classique"),
        makeTrack ("local", "c1", "Atlas", "Blue Hour", 192000, 24, false, false, "Jazz"),
        makeTrack ("local", "d1", "Nora", "Glass", 44100, 16, false, false, "Pop"),
        makeTrack ("local", "e1", "Zeyn", "Lossy", 44100, 16, false, true, "Pop"),
        makeTrack ("upnp:uuid:nas-a", "n1", "Zeyn", "Neon Roads", 96000, 24, false, false, "Electro"),
        makeTrack ("upnp:uuid:nas-a", "n2", "Zeyn", "Neon Roads", 96000, 24, false, false, "Electro"),
        makeTrack ("upnp:uuid:nas-b", "m1", "Mira", "Fragments", 44100, 16, false, false, "Electro"),
    };
    for (auto& tr : all) lib.upsert (tr);

    auto names = [] (const std::vector<nx::AlbumInfo>& a) { juce::StringArray s; for (auto& x : a) s.add (x.title); s.sort (true); return s.joinIntoString (","); };
    t.check (names (lib.getAlbumsFiltered (nx::QualityFilter::Dxd)) == "Nocturnes DXD", "filtre DXD : "_t + names (lib.getAlbumsFiltered (nx::QualityFilter::Dxd)));
    t.check (names (lib.getAlbumsFiltered (nx::QualityFilter::Dsd)) == "Sacred", "filtre DSD : "_t + names (lib.getAlbumsFiltered (nx::QualityFilter::Dsd)));
    t.check (names (lib.getAlbumsFiltered (nx::QualityFilter::HiRes)) == "Blue Hour,Neon Roads,Nocturnes DXD", "filtre Hi-Res : "_t + names (lib.getAlbumsFiltered (nx::QualityFilter::HiRes)));
    t.check (names (lib.getAlbumsFiltered (nx::QualityFilter::Cd)) == "Fragments,Glass", "filtre CD (pas le MP3) : "_t + names (lib.getAlbumsFiltered (nx::QualityFilter::Cd)));
    t.check (lib.countTracksFiltered (nx::QualityFilter::Dxd) == 2 && lib.getTracksFiltered (nx::QualityFilter::Dsd).size() == 1, "comptes par qualité (pastilles)"_t);
    const auto genres = lib.getGenres();
    juce::StringArray g; for (auto& x : genres) g.add (x.name + ":" + juce::String (x.albumCount));
    t.check (g.joinIntoString (",") == "Classique:2,Electro:2,Jazz:1,Pop:2", "genres et albums par genre : "_t + g.joinIntoString (", "));
    t.check (names (lib.getAlbumsFiltered (nx::QualityFilter::Any, "Electro")) == "Fragments,Neon Roads", "albums d'un genre"_t);

    // NAS badge data
    bool nasOk = false, localOk = false;
    for (auto& a : lib.getAlbums ("artist", 100))
    {
        if (a.title == "Neon Roads") nasOk = a.nasSource == "upnp:uuid:nas-a" && a.fromNas();
        if (a.title == "Glass") localOk = ! a.fromNas();
    }
    t.check (nasOk && localOk, "albums du NAS repérés (badge NAS / Jellyfin), albums locaux non"_t);

    // Favourites
    const auto zeyn = lib.findTrack ("upnp:uuid:nas-a", "n1");
    lib.setFavourite (zeyn->id, true);
    lib.setAlbumFavourite ("Atlas", "Blue Hour", true);
    t.check (lib.getFavouriteTracks().size() == 1 && names (lib.getFavouriteAlbums()) == "Blue Hour" && lib.isAlbumFavourite ("Atlas", "Blue Hour"),
             "favoris : 1 morceau, 1 album"_t);
    lib.setAlbumFavourite ("Atlas", "Blue Hour", false);
    t.check (lib.getFavouriteAlbums().empty(), "album retiré des favoris"_t);

    // Playlists: append (track + album), remove, rename, delete
    const auto pl = lib.createPlaylist ("Test");
    std::vector<juce::int64> albumIds;
    for (auto& tr : lib.getAlbumTracks ("Quatuor", "Nocturnes DXD")) albumIds.push_back (tr.id);
    lib.appendToPlaylist (pl, { zeyn->id });
    lib.appendToPlaylist (pl, albumIds);
    auto items = lib.getPlaylistTracks (pl);
    t.check (items.size() == 3 && items[0].id == zeyn->id && items[2].album == "Nocturnes DXD", "« Ajouter à une playlist » : un morceau puis un album entier, à la suite"_t);
    lib.removeFromPlaylist (pl, 0);
    lib.renamePlaylist (pl, "Renommée"_t);
    bool renamed = false;
    for (auto& p : lib.getPlaylists()) renamed |= p.id == pl && p.name == "Renommée"_t && p.trackCount == 2;
    t.check (renamed, "retrait d'un morceau et renommage"_t);
    lib.deletePlaylist (pl);
    t.check (lib.getPlaylists().empty() && lib.countTracks() == 9, "suppression de la playlist (les morceaux restent)"_t);

    // NAS clean-up
    t.check (lib.countAlbumsFromSourcePrefix ("upnp") == 2 && lib.getSources ("upnp").size() == 2, "2 albums de 2 serveurs NAS"_t);
    const int removedAlbum = lib.removeAlbum ("Zeyn", "Neon Roads", "upnp");
    t.check (removedAlbum == 2 && ! lib.findTrack ("upnp:uuid:nas-a", "n1").has_value(), "retrait d'un album NAS (2 morceaux)"_t);
    lib.upsert (all[6]);
    const int unavailable = lib.removeNasSourcesExcept ({ "upnp:uuid:nas-a" });
    t.check (unavailable == 1 && lib.getSources ("upnp").joinIntoString (",") == "upnp:uuid:nas-a", "serveur indisponible retiré, serveur connecté conservé"_t);
    const int purged = lib.removeSourcePrefix ("upnp");
    t.check (purged == 1 && lib.countTracks() == 6 && lib.countTracks ("local") == 6, "purge du NAS : seuls les fichiers locaux restent"_t);
}

//==============================================================================
NX_TEST_SECTION (211, "Pages artistes : photo, bannière et biographie (TheAudioDB, repli Wikipédia, cache)")
{
    MiniHttp http;
    http.routes["/search.php?s=Zeyn"] = R"({"artists":[{"strArtist":"Zeyn Tribute","strBiographyEN":"wrong"},
        {"strArtist":"Zeyn","strArtistThumb":"https://img.example/zeyn.jpg","strArtistFanart":"https://img.example/zeyn-wide.jpg",
         "strBiographyEN":"Zeyn is an electronic producer.","strBiographyFR":"Zeyn est un producteur de musique électronique.\r\n\r\n\r\nNé à Paris.",
         "strCountry":"Paris, France","strGenre":"Electronic"}]})";
    http.routes["/search.php?s=Nora Vale"] = R"({"artists":null})";
    http.routes["/wiki/Nora_Vale"] = R"({"type":"standard","title":"Nora Vale","extract":"Nora Vale est une chanteuse.","thumbnail":{"source":"https://img.example/nora.jpg"}})";

    nx::Library lib;
    juce::String err;
    lib.openInMemory (t.tempDir.getChildFile ("art208"), err);
    nx::ArtistInfoService service (lib);
    service.audioDbBase = http.base();
    service.wikipediaBase = http.base() + "/wiki/";
    service.language = "fr";
    service.throttleMs = 10;

    const auto zeyn = service.fetchNow ("Zeyn");
    t.check (zeyn.found && zeyn.imageUrl == "https://img.example/zeyn.jpg" && zeyn.bannerUrl.contains ("wide") && zeyn.country == "Paris, France",
             "TheAudioDB : l'artiste exact parmi les résultats, photo, bannière, pays"_t);
    t.check (zeyn.bio.startsWith ("Zeyn est un producteur") && ! zeyn.bio.contains ("\n\n\n") && zeyn.bioSource == "TheAudioDB",
             "biographie en français, paragraphes nettoyés"_t);
    const auto nora = service.fetchNow ("Nora Vale");
    t.check (nora.found && nora.bioSource == "Wikipedia" && nora.bio.contains ("chanteuse") && nora.imageUrl.contains ("nora"),
             "inconnu de TheAudioDB : biographie et photo de Wikipédia"_t);
    const int requestsBefore = http.requests.size();
    const auto cached = service.get ("Zeyn");
    juce::Thread::sleep (100);
    t.check (cached.has_value() && cached->bio == zeyn.bio && http.requests.size() == requestsBefore, "cache 30 jours : aucune nouvelle requête"_t);

    // background queue
    http.routes["/search.php?s=Atlas Trio"] = R"({"artists":[{"strArtist":"Atlas Trio","strArtistThumb":"https://img.example/atlas.jpg","strBiographyEN":"Jazz trio."}]})";
    int changes = 0;
    struct L : juce::ChangeListener { int& n; explicit L (int& c) : n (c) {} void changeListenerCallback (juce::ChangeBroadcaster*) override { ++n; } } listener (changes);
    service.addChangeListener (&listener);
    service.prefetch ({ "Atlas Trio", "Zeyn" });
    for (int i = 0; i < 300 && ! lib.getArtistMeta ("Atlas Trio").has_value(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    service.removeChangeListener (&listener);
    const auto atlas = lib.getArtistMeta ("Atlas Trio");
    t.check (atlas.has_value() && atlas->bio == "Jazz trio." && changes >= 1, "file d'attente en arrière-plan (repli anglais), notification de l'interface"_t);
    t.check (nx::ArtistInfoService::tidyBio (juce::String::repeatedString ("Phrase. ", 400), 100).length() <= 101, "biographie tronquée à une fin de phrase"_t);
}

//==============================================================================
NX_TEST_SECTION (501, "NX Remote (HTTP, jeton, QR code), pont AuraEngine X, système, dictionnaire anglais")
{
    nx::RemoteServer server;
    juce::String err;
    juce::StringArray commands;
    server.onCommand = [&] (const juce::String& c, const juce::StringPairArray& p) { commands.add (c + (p["t"].isNotEmpty() ? "=" + p["t"] : juce::String())); };
    server.onLibraryQuery = [] (const juce::StringPairArray& p) { return juce::String (R"({"albums":[{"title":"A"}],"q":")") + p["q"] + "\"}"; };
    server.getArtwork = [] (const juce::String& key, juce::String& mime) { mime = "image/png"; juce::MemoryBlock m; if (key == "cover") m.append ("PNGDATA", 7); return m; };
    const bool started = server.start (0, err);
    t.check (started && server.isRunning() && server.getPort() > 0, "serveur démarré à la demande sur le port "_t + juce::String (server.getPort()));
    server.setState (R"({"title":"Night Drive","playing":true})");
    const auto base = "http://127.0.0.1:" + juce::String (server.getPort());
    const auto token = server.getToken();

    const auto page = nx::httpRequest ("GET", juce::URL (base + "/"));
    t.check (page.ok() && page.body.contains ("NX Remote") && page.body.contains ("/api/state"), "web app servie (HTML embarqué)"_t);
    const auto denied = nx::httpRequest ("GET", juce::URL (base + "/api/state?k=wrong"));
    t.check (denied.status == 403, "sans le jeton du QR code : 403"_t);
    const auto st = nx::httpRequest ("GET", juce::URL (base + "/api/state?k=" + token));
    t.check (st.ok() && st.json().getProperty ("title", {}).toString() == "Night Drive", "état de lecture en JSON avec le jeton"_t);
    nx::httpRequest ("POST", juce::URL (base + "/api/cmd?k=" + token + "&c=toggle"));
    nx::httpRequest ("POST", juce::URL (base + "/api/cmd"), {}, "k=" + token + "&c=seek&t=12.5");   // form body
    for (int i = 0; i < 50 && commands.size() < 2; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    t.check (commands.joinIntoString (",") == "toggle,seek=12.5", "commandes reçues sur le fil de l'interface : "_t + commands.joinIntoString (", "));
    const auto lib = nx::httpRequest ("GET", juce::URL (base + "/api/library?k=" + token + "&q=night"));
    const auto art = nx::httpRequest ("GET", juce::URL (base + "/api/art?k=" + token + "&key=cover"));
    t.check (lib.ok() && lib.body.contains ("\"q\":\"night\"") && art.ok() && art.body == "PNGDATA", "bibliothèque et pochettes servies"_t);
    const auto oldToken = token;
    server.regenerateToken();
    t.check (nx::httpRequest ("GET", juce::URL (base + "/api/state?k=" + oldToken)).status == 403, "nouveau code : l'ancien lien est révoqué"_t);
    t.check (server.getUrl().startsWith ("http://") && server.getUrl().contains (":" + juce::String (server.getPort()) + "/?k=" + server.getToken()), "URL du QR code : "_t + server.getUrl());
    server.stop();
    t.check (! server.isRunning() && nx::httpRequest ("GET", juce::URL (base + "/"), {}, {}, 1500).status == 0, "arrêt : plus rien n'écoute"_t);

    // QR code of the URL: version / size and the three finder patterns
    const auto qr = qrcodegen::QrCode::encodeText ("http://192.168.1.20:8765/?k=0123456789abcdef0123456789abcdef", qrcodegen::QrCode::Ecc::MEDIUM);
    auto finder = [&qr] (int x0, int y0)
    {
        bool ok = true;
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
            {
                const bool ring = x == 0 || y == 0 || x == 6 || y == 6, core = x >= 2 && x <= 4 && y >= 2 && y <= 4;
                ok &= qr.getModule (x0 + x, y0 + y) == (ring || core);
            }
        return ok;
    };
    const int size = qr.getSize();
    t.check (size >= 29 && size <= 45 && finder (0, 0) && finder (size - 7, 0) && finder (0, size - 7),
             "QR code : "_t + juce::String (size) + " × " + juce::String (size) + " modules, motifs de repérage aux trois coins");

    // AuraEngine X bridge
    auto f = t.tempDir.getChildFile ("Night Drive.dsf");
    f.replaceWithText ("x");
    const auto cmd = nx::aura::buildCommand (juce::File ("/Applications/AuraEngine X.app"), { f });
    t.check (cmd.contains (f.getFullPathName()) && cmd.size() >= 2, "AuraEngine X : commande "_t + cmd.joinIntoString (" "));
    juce::String auraErr;
    const bool sent = nx::aura::openFiles ({ f }, t.tempDir.getChildFile ("absent").getFullPathName(), auraErr);
    t.check (sent || auraErr.contains ("AuraEngine X"), "application absente : message clair ("_t + auraErr + ")");
    t.check (! nx::aura::openFiles ({ t.tempDir.getChildFile ("nope.wav") }, {}, auraErr) && auraErr.contains ("locaux"),
             "fichier distant / absent : refusé (télécharger d'abord)"_t);

    // System dashboard counters
    nx::SystemMonitor mon;
    juce::Thread::sleep (120);
    const auto snap = mon.sample();
    t.check (snap.osName.isNotEmpty() && snap.memTotal > 0, "tableau de bord : "_t + snap.osName + ", " + juce::String (snap.memTotal / (1024 * 1024)) + " Mo de RAM");
    t.check (snap.memUsed > 0 && snap.memUsed <= snap.memTotal, "mémoire utilisée et charge CPU mesurées ("_t
             + juce::String (snap.memUsed / (1024 * 1024)) + " Mo, " + juce::String (juce::roundToInt (snap.cpuLoad * 100.0)) + " %)");

    // English dictionary
    t.check (nx::i18n::dictionarySize() > 300, "dictionnaire anglais : "_t + juce::String (nx::i18n::dictionarySize()) + " entrées");
    nx::i18n::setLanguage ("en");
    const bool english = "Réglages"_t == "Settings" && "Bibliothèque"_t == "Library" && "Favoris"_t == "Favourites" && "Lecture bit-perfect automatique"_t != juce::String::fromUTF8 ("Lecture bit-perfect automatique");
    nx::i18n::setLanguage ("fr");
    t.check (english && "Réglages"_t == juce::String::fromUTF8 ("R\xc3\xa9glages"), "interface en anglais / en français à chaud"_t);
}

NX_TEST_SECTION (212, "Analyse de piste en arrière-plan : forme d'onde, Peak / RMS / DR, canaux (PCM et DSD)")
{
    const auto never = [] { return false; };
    // 30 s of a 1 kHz sine at 0.5 (24 bits) with two 0.9 spikes in different 3 s blocks: Peak -0,9 dBFS, RMS -9,0 dBFS,
    // DR = 20 log10 (0.9 / 0.5) = 5,1 -> DR5
    {
        const int rate = 44100, frames = rate * 30;
        auto pcm = sinePcm (frames, 2, 24, rate, 1000.0, 0.5);
        const auto spike = (int32_t) ((uint32_t) std::llround (0.9 * 8388608.0) << 8);
        for (int at : { rate * 1, rate * 10 }) pcm[(size_t) at * 2] = pcm[(size_t) at * 2 + 1] = spike;
        const auto f = t.tempDir.getChildFile ("analysis.wav");
        writeWav (f, &pcm, nullptr, 2, rate, 24, 24, false);
        auto a = nx::TrackAnalyzer::analyse (f, nx::TrackAnalyzer::numBins, never);
        t.check (a != nullptr && (int) a->maxV.size() == nx::TrackAnalyzer::numBins && std::abs (a->peakDb - (-0.915)) < 0.05
                     && std::abs (a->rmsDb - (-9.03)) < 0.1 && a->dr == 5 && a->channels == 2,
                 "PCM : Peak, RMS et DR exacts ("_t + (a != nullptr ? a->summary() : juce::String ("échec"_t)) + ")");
        bool envelope = a != nullptr;
        for (size_t b = 2; envelope && b + 2 < a->maxV.size(); ++b)
            envelope = a->maxV[b] > 0.45f && a->minV[b] < -0.45f && std::abs (a->rms[b] - 0.3536f) < 0.03f;
        t.check (envelope, "enveloppe haute densité : crête min / max et RMS par point"_t);
        t.check (nx::TrackAnalyzer::analyse (f, 1600, [] { return true; }) == nullptr, "analyse annulée proprement (changement de piste)"_t);
    }
    // DSD64 sine at 40 % modulation, decimated: about -1,9 dBFS peak (SACD 0 dB = 50 % -> 0 dBFS)
    {
        const auto f = t.tempDir.getChildFile ("analysis.dsf");
        writeDsf (f, dsdSine (2822400.0, 2.0, 2, 1000.0, 0.4), 2, 2822400.0, true);
        auto a = nx::TrackAnalyzer::analyse (f, 1600, never);
        t.check (a != nullptr && std::abs (a->peakDb - (-1.94)) < 1.0 && std::abs (a->rmsDb - (-4.95)) < 0.6 && a->channels == 2,
                 "DSD : amplitude après décimation ("_t + (a != nullptr ? a->summary() : juce::String ("échec"_t)) + ")");
        const auto f6 = t.tempDir.getChildFile ("analysis6.dsf");
        writeDsf (f6, dsdSine (2822400.0, 0.2, 6, 1000.0, 0.3), 6, 2822400.0, true);
        auto a6 = nx::TrackAnalyzer::analyse (f6, 200, never);
        t.check (a6 != nullptr && a6->channels == 6 && nx::channelBadge (a6->channels) == "6ch" && nx::channelBadge (2) == "2ch",
                 "canaux lus dans l'en-tête DSF : 6ch (5.1) / 2ch"_t);
    }
}
