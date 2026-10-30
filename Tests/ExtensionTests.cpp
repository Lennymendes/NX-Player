// NX Player — MIT License
// Brique 3 & 4: themes (CSS, hot reload, import / export), JS UI plug-ins, C DSP plug-ins, and the
// whole interface rendered off-screen (screenshots of every page).
#include "TestFramework.h"
#include "FakeUpnp.h"
#include <set>
#include "TestAudioFiles.h"
#include "CoverFixtures.h"
#include "../Source/Core/ImageDecode.h"
#include "../Source/Ext/ThemeEngine.h"
#include "../Source/Ext/PluginManager.h"
#include "../Source/GUI/AppController.h"
#include "../Source/GUI/MainComponent.h"
#include "../Source/GUI/Windows.h"
#include "../Source/Output/CaptureBackend.h"
#include "../Source/Platform/MediaControls.h"
#include "../Source/GUI/LibraryActions.h"
#include "../Source/GUI/Dashboard.h"
#include "../Source/Remote/RemoteServer.h"
#include "../Source/Library/ArtistInfo.h"
#include "../Source/DSP/StudioTools.h"

using namespace nxtest;

namespace
{
    juce::File resources()
    {
        return nx::AppController::defaultResourcesDir();   // next to the executable, or the bundle's Resources on macOS
    }

    /** How many distinct colours an image has (a blank render has very few). */
    int colourVariety (const juce::Image& img)
    {
        std::set<juce::uint32> colours;
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < bd.height; y += 3)
            for (int x = 0; x < bd.width; x += 3)
                colours.insert (bd.getPixelColour (x, y).getARGB() & 0xF0F0F0F0u);
        return (int) colours.size();
    }

    juce::Image coverArt (juce::Colour a, juce::Colour b, const juce::String& text)
    {
        juce::Image img (juce::Image::RGB, 600, 600, true);
        juce::Graphics g (img);
        g.setGradientFill (juce::ColourGradient (a, 0, 0, b, 600, 600, false));
        g.fillAll();
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f + 0.04f * (float) i));
            g.fillEllipse (60.0f + i * 50.0f, 80.0f + i * 30.0f, 300.0f - i * 30.0f, 300.0f - i * 30.0f);
        }
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
        g.drawText (text, img.getBounds().removeFromBottom (160), juce::Justification::centred);
        return img;
    }

    void saveJpegInto (const juce::File& f, const juce::Image& img)
    {
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::JPEGImageFormat jpg;
        jpg.setQuality (0.9f);
        jpg.writeImageToStream (img, out);
    }

    bool savePng (const juce::Image& img, const juce::File& f)
    {
        f.deleteFile();
        juce::FileOutputStream out (f);
        return juce::PNGImageFormat().writeImageToStream (img, out);
    }
}

//==============================================================================
NX_TEST_SECTION (401, "Thèmes .nxtheme : CSS, rechargement à chaud, import / export")
{
    const auto vars = nx::parseCssVariables (R"(
        /* commentaire */ :root { --a: #00F5A0; --b: var(--a); --c: rgba(10, 20, 30, 0.5); --d: var(--missing, #123); --size: 14px; }
        .x { --e: #ABC; })");
    juce::Colour c;
    t.check (vars.at ("--b") == "#00F5A0" && vars.at ("--d") == "#123" && vars.at ("--e") == "#ABC", "variables CSS, var() et valeurs de repli");
    t.check (nx::parseCssColour (vars.at ("--c"), c) && c.getRed() == 10 && std::abs (c.getFloatAlpha() - 0.5f) < 0.01f
             && nx::parseCssColour ("#ABC", c) && c == juce::Colour (0xffaabbcc), "couleurs #RGB, #RRGGBB, rgba()");

    nx::ThemeManager tm;
    auto userDir = t.tempDir.getChildFile ("themes401");
    userDir.createDirectory();
    tm.setSearchFolders ({ resources().getChildFile ("Themes"), userDir });
    t.check (tm.getThemeIds().contains ("nx-emerald") && tm.getThemeIds().contains ("aura-neon") && tm.getThemeIds().contains ("glass-rose")
             && tm.getThemeIds().contains ("ocean-blue"), "4 thèmes livrés : "_t + tm.getThemeIds().joinIntoString (", "));
    t.check (tm.apply ("nx-emerald") && tm.current().accent() == juce::Colour (0xff00f5a0) && tm.current().bg() == juce::Colour (0xff0a0c0e),
             "NX Emerald : fond #0A0C0E, accent #00F5A0");
    t.check (tm.findTheme ("aura-neon") != nullptr && tm.findTheme ("aura-neon")->shaderCode.contains ("gl_FragColor"),
             "Aura Neon : shader GLSL chargé"_t);

    // hot reload: a copy of NX Emerald in the user folder, edited while applied
    resources().getChildFile ("Themes/nx-emerald.nxtheme").copyDirectoryTo (userDir.getChildFile ("live.nxtheme"));
    auto manifest = userDir.getChildFile ("live.nxtheme/manifest.json");
    manifest.replaceWithText (manifest.loadFileAsString().replace ("\"nx-emerald\"", "\"live\""));
    tm.rescan();
    int changes = 0;
    juce::Colour seen;
    tm.onThemeChanged = [&] (const nx::Theme& th) { ++changes; seen = th.accent(); };
    tm.apply ("live");
    juce::Thread::sleep (1100);
    auto css = userDir.getChildFile ("live.nxtheme/theme.css");
    css.replaceWithText (css.loadFileAsString().replace ("--nx-accent: #00F5A0;", "--nx-accent: #FF00AA;"));
    tm.checkForChanges();
    t.check (changes == 2 && seen == juce::Colour (0xffff00aa), "rechargement à chaud : theme.css modifié -> nouvel accent appliqué"_t);

    // export -> import round trip
    auto zip = t.tempDir.getChildFile ("live.nxtheme.zip");
    t.check (tm.exportTheme ("live", zip) && zip.getSize() > 200, "export en archive .nxtheme");
    userDir.getChildFile ("live.nxtheme").deleteRecursively();
    tm.rescan();
    juce::String err;
    const auto id = tm.importTheme (zip, err);
    t.check (id == "live" && tm.findTheme ("live") != nullptr && tm.findTheme ("live")->accent() == juce::Colour (0xffff00aa),
             "import d'une archive : thème « live » restauré"_t + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
}

NX_TEST_SECTION (402, "Plug-ins d'interface JavaScript")
{
    nx::PluginManager pm;
    auto userPlugins = t.tempDir.getChildFile ("plugins402");
    userPlugins.getChildFile ("UI").createDirectory();
    pm.setSearchFolders ({ resources().getChildFile ("Plugins"), userPlugins });
    const auto plugins = pm.getUiPlugins();
    t.check (plugins.size() >= 2, juce::String ((int) plugins.size()) + " plug-ins d'interface trouvés"_t);

    pm.setUiPluginEnabled ("nx.spectrum", true);
    pm.setUiPluginEnabled ("nx.analog-vu", true);
    juce::Array<juce::var> spectrum;
    for (int i = 0; i < 64; ++i) spectrum.add (0.3 + 0.6 * std::abs (std::sin (i * 0.2)));
    auto* audio = new juce::DynamicObject();
    audio->setProperty ("spectrum", spectrum);
    audio->setProperty ("rmsL", 0.12); audio->setProperty ("rmsR", 0.2);
    audio->setProperty ("peakL", 0.5); audio->setProperty ("peakR", 0.95);
    pm.setState ("audio", juce::var (audio));
    auto* theme = new juce::DynamicObject();
    theme->setProperty ("accent", "#00F5A0"); theme->setProperty ("accent2", "#00D9F5");
    theme->setProperty ("text", "#EEF3F1"); theme->setProperty ("textDim", "#8A9792");
    pm.setState ("theme", juce::var (theme));

    for (const char* id : { "nx.spectrum", "nx.analog-vu" })
    {
        juce::Image img (juce::Image::ARGB, 420, 170, true);
        bool ok = true;
        for (int frame = 0; frame < 5; ++frame)
        {
            juce::Graphics g (img);
            g.fillAll (juce::Colour (0xff0a0c0e));
            pm.setState ("time", frame / 60.0);
            ok &= pm.render (id, "nowplaying.side", g, img.getBounds().toFloat());
        }
        savePng (img, t.tempDir.getChildFile (juce::String ("plugin_") + id + ".png"));
        t.check (ok && colourVariety (img) > 8, juce::String (id) + " : rendu JavaScript (" + juce::String (colourVariety (img)) + " teintes)");
    }

    // errors are contained, layout requests reach the host, edits are hot-reloaded
    auto dir = userPlugins.getChildFile ("UI/broken");
    dir.createDirectory();
    dir.getChildFile ("manifest.json").replaceWithText (R"({"id":"t.broken","name":"Cassé","main":"plugin.js","slots":["home.top"],"fps":10})");
    dir.getChildFile ("plugin.js").replaceWithText ("function render(g, w, h, nx) { this is not javascript }");
    pm.rescan();
    pm.setUiPluginEnabled ("t.broken", true);
    juce::Image img (juce::Image::ARGB, 300, 60, true);
    bool rendered = true;
    {
        juce::Graphics g (img);
        rendered = pm.render ("t.broken", "home.top", g, img.getBounds().toFloat());
    }
    t.check (! rendered, "script invalide : erreur affichée dans l'emplacement, l'application continue"_t);

    juce::var layoutSeen;
    pm.onLayoutRequest = [&] (const juce::var& l) { layoutSeen = l; };
    juce::Thread::sleep (1100);
    dir.getChildFile ("plugin.js").replaceWithText ("let n = 0; function render(g, w, h, nx) { n++; g.color('#FF0000'); g.rect(0, 0, w, h); if (n === 1) nx.ui.setLayout({ playerPosition: 'top' }); }");
    pm.checkForChanges();
    {
        juce::Graphics g (img);
        rendered = pm.render ("t.broken", "home.top", g, img.getBounds().toFloat());
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    t.check (rendered && img.getPixelAt (10, 10).getRed() > 200, "rechargement à chaud du script corrigé"_t + (rendered ? juce::String() : " : " + pm.getUiError ("t.broken")));
    t.check (layoutSeen.getProperty ("playerPosition", {}).toString() == "top", "nx.ui.setLayout() transmis à l'interface"_t);
}

NX_TEST_SECTION (403, "Plug-in DSP en C (ABI nxplayer_dsp.h) dans la chaîne d'écoute")
{
    nx::PluginManager pm;
    pm.setSearchFolders ({ resources().getChildFile ("Plugins") });
    const auto dsp = pm.getDspPlugins();
    const auto* cf = [&] () -> const nx::DspPluginInfo* { for (auto& p : dsp) if (p.id == "nx.crossfeed") return &p; return nullptr; }();
    t.check (cf != nullptr && cf->error.isEmpty() && cf->params.size() == 3,
             "crossfeed chargé (bibliothèque partagée, 3 paramètres)"_t + (cf != nullptr && cf->error.isNotEmpty() ? " : " + cf->error : juce::String()));
    if (cf == nullptr || cf->error.isNotEmpty()) return;

    pm.setDspPluginEnabled ("nx.crossfeed", true);
    auto chain = pm.buildDspChain();
    chain->prepare (96000.0, 2, 4096);
    std::vector<double> l (4096, 0.0), r (4096, 0.0);
    for (int i = 0; i < 4096; ++i) l[(size_t) i] = 0.5 * std::sin (2 * juce::MathConstants<double>::pi * 300.0 * i / 96000.0);
    double* ch[] = { l.data(), r.data() };
    chain->process (ch, 2, 4096);
    double energyR = 0;
    for (auto v : r) energyR += v * v;
    t.check (chain->isActive() && energyR > 1.0, "voie gauche seule -> la droite reçoit la part filtrée (crossfeed actif)"_t);

    // in the engine: the Signal Path shows the DSP and the stream is no longer bit-perfect
    const auto pcm = randomPcm (40000, 2, 24, 403);
    auto f = t.tempDir.getChildFile ("dsp403.wav");
    writeWav (f, &pcm, nullptr, 2, 96000, 24, 24, false);
    auto cap = std::make_unique<nx::CaptureBackend>();
    auto* capture = cap.get();
    nx::PlaybackEngine engine (std::move (cap));
    engine.setDspChain (pm.buildDspChain());
    nx::PlayableItem item; item.uri = f.getFullPathName(); item.title = "dsp";
    engine.setQueue ({ item }, 0, true);
    nx::SignalPath path;
    for (int i = 0; i < 4000 && engine.getState() != nx::PlaybackEngine::State::Stopped; ++i)
    {
        engine.serviceMessageThread();
        if (! capture->isRunning()) { juce::Thread::sleep (2); continue; }
        while (engine.isDecoderBusy() && engine.getBufferedBytes() < (1u << 18)) juce::Thread::sleep (1);
        capture->pump (2048);
        if (i == 5) path = engine.getSignalPath();
    }
    bool hasDsp = false;
    for (auto& n : path.nodes) hasDsp |= n.stage == "DSP" && n.title.contains ("Crossfeed");
    bool differs = false;
    for (size_t i = 1000; i < 2000 && i < capture->capturedInt.size(); ++i) differs |= capture->capturedInt[i] != pcm[i];
    t.check (hasDsp && path.overall == nx::PathQuality::Enhanced && differs, "Signal Path : « DSP · Crossfeed casque », plus bit-perfect"_t);
}

NX_TEST_SECTION (301, "Interface complète hors écran (captures de chaque page)")
{
    // A small library with artwork, lyrics and several formats.
    auto music = t.tempDir.getChildFile ("music301");
    struct Alb { const char* artist; const char* album; juce::Colour a, b; int bits; double rate; bool dsd; };
    const Alb albums[] = {
        { "Quatuor Émeraude", "Nocturnes DXD", juce::Colour (0xff0b6e4f), juce::Colour (0xff08a4bd), 24, 352800, false },  // raw-utf8
        { "Zeyn", "Neon Roads", juce::Colour (0xffff2a6d), juce::Colour (0xff3a0ca3), 24, 96000, false },  // raw-utf8
        { "Ensemble Lumen", "Sacred Spaces", juce::Colour (0xffe9c46a), juce::Colour (0xff264653), 1, 5644800, true },  // raw-utf8
        { "Nora Vale", "Glass Hours", juce::Colour (0xff8ecae6), juce::Colour (0xff023047), 16, 44100, false },  // raw-utf8
        { "Atlas Trio", "Blue Hour Sessions", juce::Colour (0xff4361ee), juce::Colour (0xff111111), 24, 192000, false },  // raw-utf8
        { "Mira Solenne", "Fragments", juce::Colour (0xfff4a261), juce::Colour (0xff9d0208), 24, 88200, false },  // raw-utf8
    };
    const char* titles[] = { "Ouverture", "Lumière froide", "Night Drive", "Échos", "Sous la pluie" };  // raw-utf8
    for (auto& a : albums)
    {
        auto dir = music.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (a.album)));
        dir.createDirectory();
        saveJpegInto (dir.getChildFile ("cover.jpg"), coverArt (a.a, a.b, juce::String::fromUTF8 (a.album).substring (0, 1)));
        for (int k = 0; k < 4; ++k)
        {
            const auto name = juce::String (k + 1).paddedLeft ('0', 2) + " " + juce::String::fromUTF8 (titles[k]);
            if (a.dsd)
            {
                writeDsf (dir.getChildFile (name + ".dsf"), dsdSine (a.rate, 0.2, 2, 440.0 * (k + 1), 0.4), 2, a.rate, true);
            }
            else
            {
                const auto pcm = sinePcm ((int) (a.rate * 0.2), 2, a.bits, a.rate, 220.0 * (k + 1), 0.3);
                writeWav (dir.getChildFile (name + ".wav"), &pcm, nullptr, 2, a.rate, a.bits == 16 ? 16 : 24, a.bits, false);
            }
        }
    }
    music.getChildFile ("Neon Roads/03 Night Drive.lrc").replaceWithText (
        "[00:00.00]Les phares glissent sur l'asphalte\n[00:00.05]Néon bleu, néon rose\n[00:00.10]On roule jusqu'à l'aube\n[00:00.15]Night drive\n"_t
        "[00:00.18]Dernière ligne\n"_t);

    nx::AppController::Options opts;
    opts.dataDir = t.tempDir.getChildFile ("appdata301");
    opts.resourcesDir = resources();
    opts.createWindow = false;
    auto cap = std::make_unique<nx::CaptureBackend>();
    cap->record = false;
    auto* virtualDac = cap.get();
    opts.backend = std::move (cap);
    nx::AppController app (std::move (opts));

    // Tags for the generated WAVs come from folder / file names; set artists through the library.
    app.library().addFolder (music);
    app.scanner().scanFolderNow (music);
    for (auto& tr : app.library().getTracks ("title", 1000))
    {
        for (auto& a : albums)
            if (tr.album == juce::String::fromUTF8 (a.album) || juce::File (tr.externalId).getParentDirectory().getFileName() == juce::File::createLegalFileName (juce::String::fromUTF8 (a.album)))
            {
                auto copy = tr;
                copy.artist = copy.albumArtist = juce::String::fromUTF8 (a.artist);
                copy.album = juce::String::fromUTF8 (a.album);
                copy.title = copy.title.fromFirstOccurrenceOf (" ", false, false);
                copy.trackNumber = juce::File (tr.externalId).getFileName().substring (0, 2).getIntValue();
                copy.year = 2024;
                app.library().upsert (copy);
            }
    }
    t.check (app.library().countTracks() == 24, "bibliothèque de test : "_t + juce::String (app.library().countTracks()) + " morceaux, 6 albums");
    int withArt = 0;
    for (auto& tr : app.library().getTracks ("title", 100)) withArt += tr.artworkKey.isNotEmpty() ? 1 : 0;
    t.check (withArt == 24, "pochettes cover.jpg associées : "_t + juce::String (withArt));

    auto* main = app.getMainComponent();
    auto shots = t.tempDir.getChildFile ("screens");
    shots.createDirectory();
    auto settle = [&] (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            app.engine().serviceMessageThread();
            if (virtualDac->isRunning()) virtualDac->pump (64);   // the virtual DAC plays very slowly
        }
    };
    auto shoot = [&] (const juce::String& name)
    {
        settle (250);
        main->createComponentSnapshot (main->getLocalBounds(), true, 1.0f);   // off-screen: the first paint requests the artwork
        settle (350);
        auto img = main->createComponentSnapshot (main->getLocalBounds(), true, 1.0f);
        savePng (img, shots.getChildFile (name + ".png"));
        return colourVariety (img);
    };

    app.navigate ({ nx::PageId::Home });
    t.check (shoot ("01_accueil") > 60, "Accueil (grille, ajouts récents, spectre)"_t);
    app.navigate ({ nx::PageId::Albums });
    t.check (shoot ("02_albums") > 60, "Albums (pochettes, badges de résolution)"_t);
    app.navigate ({ nx::PageId::AlbumDetail, "Zeyn", "Neon Roads" });
    t.check (shoot ("03_album") > 60, "Détail d'album"_t);

    const auto tracks = app.library().getAlbumTracks ("Zeyn", "Neon Roads");
    app.playTracks (tracks, 2);
    settle (600);
    app.navigate ({ nx::PageId::NowPlaying });
    settle (300);
    t.check (app.currentTrack() != nullptr && app.currentTrack()->title.contains ("Night"), "lecture lancée depuis l'interface : "_t + (app.currentTrack() ? app.currentTrack()->title : juce::String()));
    t.check (shoot ("04_en_cours_paroles") > 60, "En cours : pochette, paroles synchronisées, chemin du signal"_t);
    main->player.setWaveformShown (true);
    for (int i = 0; i < 60 && app.currentAnalysis() == nullptr; ++i) settle (50);
    shoot ("04b_barre_forme_onde");
    t.check (app.currentAnalysis() != nullptr && (bool) app.settings().get ("playerWaveform", false),
             "barre de lecture : forme d'onde (analyse en arrière-plan) et choix mémorisé"_t);
    main->player.setWaveformShown (false);

    // Lire / Aléatoire = the whole library, not 12 tracks; the shuffle queue is topped up from the library
    app.playCollection ("local", true);
    settle (300);
    t.check ((int) app.engine().getQueue().size() == juce::jmin (200, app.library().countTracks()),
             "Aléatoire : toute la bibliothèque (pas 12 titres), file rechargée par passes"_t);
    // Local (sidebar) returns to the album that was open instead of the grid
    main->navigate ({ nx::PageId::AlbumDetail, "Zeyn", "Neon Roads" });
    main->navigate ({ nx::PageId::Playlists });
    main->navigate ({ nx::PageId::Library, "albums", "sidebar" });
    t.check (main->getCurrentPage().page == nx::PageId::AlbumDetail, "menu Local : reste sur l'album ouvert"_t);
    main->navigate ({ nx::PageId::Library, "albums" });

    app.applyThemeNow ("aura-neon");
    app.navigate ({ nx::PageId::Albums });
    t.check (shoot ("05_theme_aura_neon") > 40 && app.theme().id == "aura-neon", "thème Aura Neon appliqué à chaud"_t);
    app.applyThemeNow ("nx-emerald");

    app.navigate ({ nx::PageId::Search, "lum" });
    t.check (shoot ("06_recherche") > 40, "Recherche unifiée"_t);
    // Streaming sources are off: the sidebar keeps local files and the NAS only.
    t.check (app.providers().getProvider ("qobuz") == nullptr && app.providers().getProvider ("spotify") == nullptr
             && app.providers().getProvider ("upnp") != nullptr, "sources : fichiers locaux et NAS uniquement (streaming retiré)"_t);

    // NAS: a fake UPnP server on 127.0.0.1 that serves the Neon Roads files.
    auto nasMedia = music.getChildFile ("Neon Roads");
    FakeUpnpServer nas (nasMedia, "Synology DS920+");
    {
        using N = FakeUpnpServer::Node;
        N music0; music0.id = "music"; music0.parent = "0"; music0.title = "Musique"; music0.container = true; nas.add (music0);
        const char* names[] = { "Neon Roads", "Glass Hours", "Fragments", "Blue Hour Sessions" };
        for (int i = 0; i < 4; ++i)
        {
            N a; a.id = "a" + juce::String (i); a.parent = "music"; a.title = names[i]; a.container = true; a.art = "cover.jpg";
            a.artist = i == 0 ? "Zeyn" : "Artiste " + juce::String (i);
            nas.add (a);
        }
        int k = 0;
        for (const auto& e : juce::RangedDirectoryIterator (nasMedia, false, "*.wav"))
        {
            N it; it.id = "n" + juce::String (++k); it.parent = "a0"; it.title = e.getFile().getFileNameWithoutExtension().fromFirstOccurrenceOf (" ", false, false);
            it.artist = "Zeyn"; it.album = "Neon Roads"; it.file = e.getFile().getFileName(); it.rate = 96000; it.bits = 24; it.art = "cover.jpg";
            nas.add (it);
        }
    }
    auto* upnp = dynamic_cast<nx::UpnpProvider*> (app.providers().getProvider ("upnp"));
    if (upnp != nullptr) upnp->discoverHost ("127.0.0.1", nas.udpPort, 3000);
    const auto nasServers = upnp != nullptr ? upnp->getServers() : std::vector<nx::UpnpServer>();
    t.check (nasServers.size() == 1, "NAS trouvé par l'interface : "_t + (nasServers.empty() ? juce::String ("aucun") : nasServers[0].name));
    app.navigate ({ nx::PageId::Source, "upnp" });
    t.check (shoot ("07_nas_serveurs") > 30, "NAS : serveurs du réseau"_t);
    if (! nasServers.empty())
    {
        app.navigate ({ nx::PageId::Source, "upnp", nx::SourcePage::upnpArg (nasServers[0].udn, "music", { nasServers[0].name, "Musique" }) });
        settle (600);
        t.check (shoot ("08_nas_dossier") > 40, "NAS : dossier d'albums (grille avec pochettes)"_t);
        app.navigate ({ nx::PageId::Source, "upnp", nx::SourcePage::upnpArg (nasServers[0].udn, "a0", { nasServers[0].name, "Musique", "Neon Roads" }) });
        settle (600);
        shoot ("08b_nas_album");
    }

    app.navigate ({ nx::PageId::Settings });
    t.check (shoot ("09_reglages") > 30, "Réglages (sortie, DSD, paroles, téléchargements)"_t);
    app.plugins().setUiPluginEnabled ("nx.analog-vu", true);
    app.navigate ({ nx::PageId::NowPlaying });
    t.check (shoot ("10_vu_metres") > 60, "plug-in VU-mètres dans « En cours »"_t);

    // Extensions & Thèmes gallery, then the user plug-ins catalogue
    auto& market = main->getMarketplace();
    app.navigate ({ nx::PageId::Marketplace });
    const int galleryCards = market.getNumCards();
    t.check (galleryCards >= 7, "Extensions & Thèmes : "_t + juce::String (galleryCards) + " cartes (4 thèmes, plug-ins d'interface et DSP)"_t);
    t.check (shoot ("12_extensions_themes") > 60, "galerie avec aperçus en direct"_t);
    app.navigate ({ nx::PageId::Marketplace, "themes" });
    const int themeCards = market.getNumCards();
    t.check (themeCards == 4, "onglet Thèmes : "_t + juce::String (themeCards) + " cartes");
    app.navigate ({ nx::PageId::UserPlugins, "catalogue" });
    const int catalogueCards = market.getNumCards();
    t.check (catalogueCards == 6, "Plugins utilisateur › Catalogue : "_t + juce::String (catalogueCards) + " plug-ins en plus (5 interface, 1 DSP)");
    t.check (shoot ("13_plugins_catalogue") > 60, "catalogue avec aperçus"_t);

    juce::String installError;
    const bool radialOk = market.installFromCatalogue ("nx.radial", installError);
    bool radialOn = false;
    for (auto& p : app.plugins().getUiPlugins()) if (p.id == "nx.radial") radialOn = p.enabled && p.folder.isAChildOf (app.userDataDirectory());
    t.check (radialOk && radialOn, "installation en un clic : Spectre circulaire copié dans le dossier utilisateur et activé"_t
                                   + (installError.isNotEmpty() ? " (" + installError + ")" : juce::String()));
    const bool toneOk = market.installFromCatalogue ("nx.tone", installError);
    bool toneOn = false;
    for (auto& p : app.plugins().getDspPlugins()) if (p.id == "nx.tone") toneOn = p.enabled && p.error.isEmpty();
    t.check (toneOk && toneOn, "plug-in DSP du catalogue installé, bibliothèque chargée et activé"_t
                               + (installError.isNotEmpty() ? " (" + installError + ")" : juce::String()));
    app.navigate ({ nx::PageId::UserPlugins, "installes" });
    t.check (market.getNumCards() == 2, "onglet Installés : "_t + juce::String (market.getNumCards()) + " plug-ins");
    app.navigate ({ nx::PageId::NowPlaying });
    shoot ("10b_spectre_circulaire");
    app.navigate ({ nx::PageId::UserPlugins, "installes" });
    t.check (shoot ("14_plugins_installes") > 40, "plug-ins installés par l'utilisateur"_t);
    juce::String removeError;
    const bool removed = market.removeUserPlugin ("nx.tone", removeError);
    bool stillThere = false;
    for (auto& p : app.plugins().getDspPlugins()) if (p.id == "nx.tone") stillThere = true;
    t.check (removed && ! stillThere, "retrait d'un plug-in utilisateur"_t + (removeError.isNotEmpty() ? " (" + removeError + ")" : juce::String()));
    app.plugins().setUiPluginEnabled ("nx.radial", false);

    // History (‹ ›) and the sidebar toggle
    app.navigate ({ nx::PageId::Home });
    app.navigate ({ nx::PageId::Albums });
    app.navigate ({ nx::PageId::Favourites });
    main->goBack();
    const bool backOk = main->getCurrentPage().page == nx::PageId::Albums;
    main->goBack();
    const bool back2Ok = main->getCurrentPage().page == nx::PageId::Home && main->canGoForward();
    main->goForward();
    const bool fwdOk = main->getCurrentPage().page == nx::PageId::Albums;
    app.navigate ({ nx::PageId::Artists });
    t.check (backOk && back2Ok && fwdOk && ! main->canGoForward(), "historique : ‹ revient en arrière, › avance, une nouvelle page efface l'avant"_t);
    main->setSidebarVisible (false, false);
    t.check (! main->sidebar.isVisible() && main->sidebarButton.getX() <= 16, "barre latérale masquée : le contenu prend toute la largeur"_t);
    shoot ("15_barre_laterale_masquee");
    main->setSidebarVisible (true, false);

    // Device profiles: the Merging Anubis banner (forced, the virtual DAC has another name), then RME
    {
        auto st = app.engine().getSettings();
        st.hardwareProfile = nx::HardwareProfile::MergingRavenna;
        app.engine().setSettings (st);
        app.navigate ({ nx::PageId::Settings });
        t.check (shoot ("16_profil_anubis") > 40, "Réglages : profils Merging Anubis (3 profils, astuce MT VAD)"_t);
        {   // Step 5 fix: the equipment catalogue (7 cards) can be scrolled fully into view and the page ends with a margin
            auto& page = main->getSettingsWindow().getPage();
            juce::Component* catalog = nullptr;
            for (auto* c : page.content().getChildren()) if (c->getHeight() == 98 + 7 * 46 + 8) catalog = c;
            t.check (catalog != nullptr, "Réglages : le gestionnaire d'équipements est présent"_t);
            if (catalog != nullptr)
            {
                page.setScrollY (catalog->getY() - 16);
                settle (100);
                t.check (catalog->getBottom() <= page.getScrollY() + page.getHeight(), "Réglages : les 7 cartes d'équipements tiennent dans la zone visible, sans troncature"_t);
                shoot ("16c_gestionnaire_equipements");
            }
            page.setScrollY (100000);
            settle (100);
            int lowest = 0;
            for (auto* c : page.content().getChildren()) if (c != catalog) lowest = juce::jmax (lowest, c->getBottom());
            t.check (page.getScrollY() + page.getHeight() >= page.content().getHeight(), "Réglages : le bas de la page est atteignable"_t);
            page.setScrollY (0);
        }
        st.hardwareProfile = nx::HardwareProfile::RmeAdi2;
        app.engine().setSettings (st);
        app.navigate ({ nx::PageId::Settings });
        t.check (shoot ("17_profil_rme") > 40, "Réglages : profils RME ADI-2 (sécurité DSD Direct)"_t);
        st.hardwareProfile = nx::HardwareProfile::Auto;
        app.engine().setSettings (st);
    }
    // Step 4: equipment catalogue (search, apply, save) and the Streamer / Gaming mode
    {
        t.check (nx::searchDeviceModels ("adi").size() == 3 && nx::searchDeviceModels ("ka17").size() == 1
                 && nx::searchDeviceModels ("anubis pro").size() == 1 && nx::searchDeviceModels ("zzz").empty(),
                 "catalogue : la recherche trouve ADI-2 (3), KA17, Anubis Pro"_t);
        auto st = app.engine().getSettings();
        nx::applyDeviceModel (st, *nx::findDeviceModel ("merging-anubis-pro"));
        app.engine().setSettings (st);
        app.applyEngineSettings();
        const auto pro = nx::resolveProfile (app.engine().getSettings(), "Carte quelconque");
        t.check (pro.dsdMode == nx::DsdMode::ConvertToPcm && ! pro.dopAllowed && pro.profileTag == "Anubis Ref",
                 "Anubis Pro / SPS : pas de DSD natif, jamais de DoP (conversion DXD)"_t);
        nx::applyDeviceModel (st, *nx::findDeviceModel ("merging-anubis-premium"));
        st.anubisProfile = nx::AnubisProfile::PureDoP;
        t.check (nx::resolveProfile (st, "Carte quelconque").dsdMode == nx::DsdMode::DoP, "Anubis Premium : DoP disponible"_t);
        nx::applyDeviceModel (st, *nx::findDeviceModel ("fiio-ka17"));
        t.check (nx::resolveProfile (st, "USB DAC").dsdMode == nx::DsdMode::DoP, "FiiO KA17 : DoP, profil appliqué à un pilote non reconnu"_t);
        st.fiioProfile = 1;
        t.check (nx::resolveProfile (st, "USB DAC").dsdMode == nx::DsdMode::ConvertToPcm, "FiiO KA17 : profil DXD"_t);
        nx::applyDeviceModel (st, *nx::findDeviceModel ("rme-adi2-pro-ae"));
        app.engine().setSettings (st);
        app.applyEngineSettings();
        t.check (app.settings().get ("engine").getProperty ("deviceModel", {}).toString() == "rme-adi2-pro-ae", "équipement choisi enregistré dans les préférences"_t);
        st.hardwareProfile = nx::HardwareProfile::Auto;
        st.deviceModel = {};
        st.anubisProfile = nx::AnubisProfile::StudioDxdReference;   // do not leak Pure DoP / FiiO choices into the next tests
        st.fiioProfile = 0;
        app.engine().setSettings (st);
        app.applyEngineSettings();

        app.setStreamerMode (true);
        t.check (app.isStreamerMode() && (bool) app.settings().get ("streamerMode", false) && main->streamerButton.active
                 && nx::perf::hz (30, 4) == 4, "mode Streamer : activé, enregistré, bouton allumé, timers ralentis"_t);
        main->player.stateChanged();
        settle (200);
        shoot ("16b_mode_streamer");
        app.setStreamerMode (false);
        t.check (! app.isStreamerMode() && nx::perf::hz (30, 4) == 30, "mode Streamer : désactivé, cadence normale"_t);
    }
    // A-B loop markers on the seek bar
    {
        const double len = app.engine().getDurationSeconds();
        if (len > 0.1) app.engine().setLoop (len * 0.3, len * 0.7);
        main->player.trackChanged();
        app.navigate ({ nx::PageId::NowPlaying });
        shoot ("18_boucle_ab");
        t.check (app.engine().isLoopActive(), "boucle A-B posée depuis l'interface"_t);
        main->player.clearLoop();
    }

    //==========================================================================================
    // v1.0 interface: sidebar pillars, quality pills, favourites / playlists (drag & drop), artist pages,
    // NAS badges and clean-up, settings window + dashboard, profile chip, studio tools, immersive view,
    // onboarding, NX Remote, English.
    app.artistInfo().setEnabled (false);      // no network in the tests: artist data are stored by hand below
    auto& sb = main->sidebar;
    sb.refresh();
    const int iLocal = sb.findItem (nx::PageId::Library, "albums"), iFav = sb.findItem (nx::PageId::Favourites);
    const int iPl = sb.findItem (nx::PageId::Playlists), iNas = sb.findItem (nx::PageId::Source, "upnp"), iExt = sb.findItem (nx::PageId::Marketplace);
    t.check (iLocal >= 0 && iFav > iLocal && iPl > iFav && iNas > iPl && iExt > iNas && sb.findItem (nx::PageId::UserPlugins) < 0,
             "barre latérale épurée : Local, Favoris, Playlists, NAS / Jellyfin, Extensions & Thèmes"_t);

    auto& listing = main->getListing();
    auto albumTitles = [&] { juce::StringArray a; for (auto& x : listing.grid.getAlbums()) a.add (x.title); a.sort (true); return a.joinIntoString (", "); };
    app.navigate ({ nx::PageId::Library, "dxd" });
    t.check (listing.pills.getNumPills() == 9 && listing.pills.getActive() == "dxd" && albumTitles() == "Nocturnes DXD",
             "pastille DXD : "_t + albumTitles());
    t.check (shoot ("19_filtres_qualite") > 40, "barre de filtres par qualité (Albums | Artistes | Pistes | Genres | Ajouts récents | DXD | DSD | Hi-Res | CD)"_t);
    app.navigate ({ nx::PageId::Library, "dsd" });
    const auto dsdAlbums = albumTitles();
    app.navigate ({ nx::PageId::Library, "hires" });
    const auto hiresAlbums = albumTitles();
    app.navigate ({ nx::PageId::Library, "cd" });
    const auto cdAlbums = albumTitles();
    t.check (dsdAlbums == "Sacred Spaces" && hiresAlbums == "Blue Hour Sessions, Fragments, Neon Roads, Nocturnes DXD" && cdAlbums == "Glass Hours",
             "DSD : "_t + dsdAlbums + " · Hi-Res : " + hiresAlbums + " · CD : " + cdAlbums);
    app.navigate ({ nx::PageId::Albums });
    t.check (listing.pills.isVisible() && listing.pills.getActive() == "albums", "les anciennes vues (Albums...) gardent les pastilles"_t);

    // favourites and playlists by drag & drop on the sidebar
    const auto neon = app.library().getAlbumTracks ("Zeyn", "Neon Roads");
    const auto glass = app.library().getAlbumTracks ("Nora Vale", "Glass Hours");
    t.check (sb.dropTracks ({ neon[0].id, neon[1].id }, iFav) == "favourites" && app.library().getFavouriteTracks().size() == 2,
             "glisser deux morceaux sur Favoris"_t);
    app.library().setAlbumFavourite ("Atlas Trio", "Blue Hour Sessions", true);
    const auto plId = nx::actions::createPlaylistWith (app, "Soirée DXD"_t, { neon[2].id });
    sb.refresh();
    const int iMine = sb.findItem (nx::PageId::PlaylistDetail, juce::String (plId));
    sb.dropTracks (nx::actions::trackIds (glass), iMine);
    t.check (iMine >= 0 && app.library().getPlaylistTracks (plId).size() == 1 + glass.size(),
             "glisser un album entier sur une playlist : "_t + juce::String ((int) app.library().getPlaylistTracks (plId).size()) + " morceaux");
    app.navigate ({ nx::PageId::Favourites });
    t.check (listing.grid.getAlbums().size() == 1 && listing.list.getTracks().size() == 2, "page Favoris : 1 album, 2 morceaux"_t);
    t.check (shoot ("20_favoris") > 40, "page Favoris"_t);

    // artist page (data as the TheAudioDB service would store them)
    {
        nx::ArtistMeta m;
        m.name = "Zeyn";
        m.found = true;
        m.imageUrl = neon.front().artworkKey;
        m.bannerUrl = app.library().getAlbumTracks ("Mira Solenne", "Fragments").front().artworkKey;
        m.bio = "Zeyn est un producteur et ingénieur du son français, figure de la scène électronique « néon ». "
                "Ses albums, enregistrés en DXD 352,8 kHz puis mastérisés en DSD, explorent les nuits urbaines et les longues routes.\n\n"
                "Neon Roads, sorti en 2024, a été mixé sur AuraDAW et converti avec AuraEngine. "
                "Sur scène, il joue sur une Merging Anubis reliée en RAVENNA à un système de diffusion multicanal."_t;
        m.bioSource = "TheAudioDB";
        m.genre = "Electronic";
        m.country = "Paris, France";
        m.fetched = juce::Time::currentTimeMillis();
        app.library().setArtistMeta (m);
    }
    app.navigate ({ nx::PageId::ArtistDetail, "Zeyn" });
    t.check (listing.artistHeader.isVisible() && listing.artistHeader.hasBio() && listing.artistHeader.hasPhoto() && ! listing.grid.getAlbums().empty(),
             "page artiste : bannière, photo, biographie et discographie de la bibliothèque"_t);
    t.check (shoot ("21_page_artiste") > 60, "page artiste"_t);
    app.navigate ({ nx::PageId::Library, "artistes" });
    t.check (listing.grid.roundArt && listing.grid.getAlbums().size() == 6, "vue Artistes en médaillons ronds"_t);
    shoot ("22_artistes");

    // NAS: import the server's album, badge, clean-up
    if (upnp != nullptr && ! nasServers.empty())
    {
        const int imported = upnp->importContainer (app.library(), nasServers[0], "a0");
        app.navigate ({ nx::PageId::Albums });
        nx::AlbumInfo neonAlbum;
        for (auto& a : listing.grid.getAlbums()) if (a.title == "Neon Roads") neonAlbum = a;
        t.check (imported == 4 && neonAlbum.fromNas() && app.networkSourceLabel (neonAlbum.nasSource) == "NAS",
                 "album importé du NAS : badge « NAS » à côté du format"_t);
        t.check (shoot ("23_badges_nas") > 40, "badges NAS sur les pochettes"_t);
        const int removed = nx::actions::removeNasAlbums (app, { neonAlbum });
        bool stillLocal = false;
        for (auto& a : app.library().getAlbums ("artist", 100)) if (a.title == "Neon Roads") stillLocal = ! a.fromNas();
        t.check (removed == 4 && stillLocal, "« Retirer de la bibliothèque » : les 4 morceaux du NAS partent, l'album local reste"_t);
        upnp->importContainer (app.library(), nasServers[0], "a0");
        const int purged = nx::actions::purgeNas (app);
        t.check (purged == 4 && app.library().countAlbumsFromSourcePrefix ("upnp") == 0, "« Purger les éléments du NAS » depuis les réglages"_t);
    }

    // Settings window (modal) with the hardware dashboard; the virtual DAC is renamed as a Merging Anubis
    const auto originalDac = virtualDac->name;
    virtualDac->name = "MT VAD (Merging Anubis Premium)";
    app.selectDevice (virtualDac->name);
    app.navigate ({ nx::PageId::Settings, "audio" });
    auto& sw = main->getSettingsWindow();
    t.check (sw.isOpen() && sw.getTab() == "audio" && main->getCurrentPage().page != nx::PageId::Settings,
             "Réglages : fenêtre modale par-dessus la page (roue crantée), onglet Audio & Système"_t);
    t.check (shoot ("24_tableau_de_bord") > 60, "tableau de bord ORDINATEUR ↔ PÉRIPHÉRIQUE AUDIO (Anubis)"_t);
    {
        nx::DeviceCapabilities caps;
        caps.sampleRates = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        using RS = nx::HardwareDashboard::RateSupport;
        const bool matrix = nx::HardwareDashboard::supportFor (caps, 176400.0, false) == RS::Pcm && nx::HardwareDashboard::supportFor (caps, 384000.0, false) == RS::None
                         && nx::HardwareDashboard::supportFor (caps, 2822400.0, true) == RS::DoP && nx::HardwareDashboard::supportFor (caps, 5644800.0, true) == RS::None;
        caps.nativeDsdRates = { 11289600.0 };
        t.check (matrix && nx::HardwareDashboard::supportFor (caps, 11289600.0, true) == RS::Native,
                 "matrice des fréquences : PCM, DSD en DoP (176,4 kHz -> DSD64) et DSD natif"_t);
    }
    sw.getPage().setTab ("bibliotheque");
    shoot ("25_reglages_bibliotheque");
    sw.getPage().setTab ("interface");
    t.check (shoot ("26_reglages_interface") > 30, "onglet Interface : langue, thème, disposition, vue immersive, relancer l'assistant"_t);
    app.setRemoteEnabled (true);
    sw.getPage().setTab ("remote");
    t.check (app.remote().isRunning() && shoot ("27_nx_remote") > 30, "NX Remote activé : QR code et lien"_t);
    {
        const auto state = juce::JSON::parse (app.buildRemoteState());
        t.check (state.getProperty ("family", {}).toString() == "merging" && state.getProperty ("profiles", {}).size() == 3,
                 "état NX Remote : morceau, volume, profils Anubis ("_t + state.getProperty ("device", {}).toString() + ")");
        juce::StringPairArray p;
        p.set ("v", "0.5");
        // the volume slider is greyed (and the command ignored) while the previous track still holds a fixed volume:
        // wait until the engine has moved on instead of racing it
        for (int i = 0; i < 40; ++i)
        {
            main->player.stateChanged();
            app.handleRemoteCommand ("volume", p);
            if (std::abs (app.getUserVolume() - 0.5) < 0.01) break;
            settle (100);
        }
        const double v = app.getUserVolume();
        p.set ("i", "2");
        app.handleRemoteCommand ("profile", p);
        const bool pureDop = app.engine().getSettings().anubisProfile == nx::AnubisProfile::PureDoP;
        p.set ("v", "1");
        // back to 100 % in the same turn (the slider is still enabled), then wait for the engine to adopt the DoP plan
        p.set ("v", "1");
        app.handleRemoteCommand ("volume", p);
        for (int i = 0; i < 60 && ! app.engine().isFixedVolume(); ++i) settle (100);
        t.check (std::abs (v - 0.5) < 0.01 && pureDop && app.engine().isFixedVolume(), "NX Remote : volume à 50 %, profil Pure DoP depuis le téléphone, retour à 100 % bit-perfect"_t
                 + " [v=" + juce::String (v) + " dop=" + juce::String ((int) pureDop) + " fixed=" + juce::String ((int) app.engine().isFixedVolume()) + " state=" + juce::String ((int) app.engine().getState()) + " vol=" + juce::String (app.getUserVolume()) + "]");
        {
            const auto st = juce::JSON::parse (app.buildRemoteState());
            const bool dopNow = app.engine().getCurrentPlan().wire == nx::WireFormat::DoP;
            t.check ((bool) st.getProperty ("volumeFixed", false) == app.engine().isFixedVolume() || (dopNow && (bool) st.getProperty ("volumeFixed", false)),
                     "état NX Remote : volume fixe (DoP / bit-perfect) transmis au téléphone"_t);
            t.check (! dopNow || (bool) st.getProperty ("dop", false), "état NX Remote : DoP signalé quand la piste passe en DoP"_t);
        }
    }
    {
        // Step 5: covers for the phone are always a baseline RGB JPEG of at most 600 x 600, whatever the stored format
        auto asJpeg = [] (const juce::MemoryBlock& b) { return b.getSize() > 4 && ((const unsigned char*) b.getData())[0] == 0xff && ((const unsigned char*) b.getData())[1] == 0xd8; };
        bool allJpeg = true;
        for (auto& bytes : { std::make_pair (kWebp, sizeof (kWebp)), std::make_pair (kProgressiveJpeg, sizeof (kProgressiveJpeg)),
                             std::make_pair (kCmykJpeg, sizeof (kCmykJpeg)), std::make_pair (kPng16, sizeof (kPng16)) })
        {
            const auto key = app.library().storeArtwork (juce::MemoryBlock (bytes.first, bytes.second), "image/png");
            const auto jpg = app.remoteArtwork (key, 600);
            const auto img = juce::ImageFileFormat::loadFrom (jpg.getData(), jpg.getSize());
            allJpeg = allJpeg && asJpeg (jpg) && img.isValid() && ! img.hasAlphaChannel() && img.getWidth() == 32;
        }
        t.check (allJpeg, "NX Remote : WebP, JPEG progressif / CMYK et PNG 16 bits servis en JPEG RGB standard"_t);
        juce::Image big (juce::Image::ARGB, 1400, 900, true);
        { juce::Graphics g (big); g.fillAll (juce::Colours::orange); }
        juce::MemoryOutputStream png;
        juce::PNGImageFormat().writeImageToStream (big, png);
        const auto bigKey = app.library().storeArtwork (juce::MemoryBlock (png.getData(), png.getDataSize()), "image/png");
        const auto small = juce::ImageFileFormat::loadFrom (app.remoteArtwork (bigKey, 600).getData(), app.remoteArtwork (bigKey, 600).getSize());
        t.check (small.isValid() && small.getWidth() == 600 && small.getHeight() == 386, "NX Remote : pochette réduite à 600 px max, proportions gardées"_t);
        // no key at all: the cover next to the file (cover / folder / front) is found through the track id
        const auto dir = t.tempDir.getChildFile ("remote-cover");
        dir.createDirectory();
        dir.getChildFile ("front.webp").replaceWithData (kWebp, sizeof (kWebp));
        auto pcm = sinePcm (4800, 2, 16, 48000.0, 440.0, 0.3);
        writeWav (dir.getChildFile ("t.wav"), &pcm, nullptr, 2, 48000.0, 16, 16, false);
        nx::TrackInfo ti;
        ti.source = "local"; ti.externalId = ti.uri = dir.getChildFile ("t.wav").getFullPathName(); ti.title = "Sans pochette"_t; ti.artist = "X"; ti.album = "Remote";
        const auto tid = app.library().upsert (ti);
        t.check (asJpeg (app.remoteArtwork ("track:" + juce::String (tid), 600)) && app.remoteArtwork ("inconnue.jpg", 600).getSize() == 0,
                 "NX Remote : repli dossier (front.webp) via l'identifiant du morceau ; clé inconnue = aucune image"_t);
        // permanent link: the token and the port are saved, a new run reuses them
        const auto token = app.remote().getToken();
        t.check (app.settings().get ("remoteToken").toString() == token && token.length() >= 32, "NX Remote : jeton enregistré dans les préférences"_t);
        const auto portBefore = app.remote().getPort();
        const auto urlBefore = app.remote().getUrl();
        app.setRemoteEnabled (false);
        app.setRemoteEnabled (true);
        t.check (app.remote().getPort() == portBefore && app.remote().getUrl() == urlBefore && (int) app.settings().get ("remotePort", 0) == portBefore,
                 "NX Remote : arrêt puis redémarrage, même port, même lien, même QR code"_t);
        const auto oldUrl = app.remote().getUrl();
        app.remote().regenerateToken();
        t.check (app.settings().get ("remoteToken").toString() == app.remote().getToken() && app.remote().getUrl() != oldUrl, "NX Remote : « Nouveau code » régénère le lien et l'enregistre"_t);
        {
            juce::String why;
            nx::RemoteServer fresh;
            fresh.setToken (app.remote().getToken());
            t.check (fresh.getToken() == app.remote().getToken() && fresh.start (portBefore, why) && fresh.getPort() > 0,
                     "NX Remote : un serveur neuf reprend le jeton enregistré"_t);
        }
        const auto state2 = juce::JSON::parse (app.buildRemoteState());
        t.check (state2.hasProperty ("family") && state2.hasProperty ("position"), "état NX Remote : famille de matériel (merging / rme / fiio)"_t);
    }
    app.setRemoteEnabled (false);
    sw.close();

    // Profile chip of the player bar
    {
        auto& chip = main->player.deviceChip;
        auto entries = chip.menuEntries();
        chip.selectEntry (1);
        const bool warm = app.engine().getSettings().anubisProfile == nx::AnubisProfile::AnalogWarmth;
        t.check (entries.size() == 3 && warm, "barre de lecture : miniature de l'Anubis et menu des 3 profils (Analog Warmth activé)"_t);
        chip.selectEntry (0);
    }

    // Studio tools from the player bar menu
    app.playTracks (neon, 0);
    settle (500);
    app.studioTools().monitor = nx::StudioToolsState::Monitor::Mono;
    app.studioTools().invertPhase = true;
    app.applyStudioTools();
    settle (300);
    {
        bool tools = false;
        for (auto& n : app.engine().getSignalPath().nodes) tools |= n.stage == "DSP" && n.title.contains ("Mono") && n.title.contains ("Phase");
        t.check (tools && app.engine().getSettings().dspNeedsPcm && main->player.studioButton.active,
                 "Outils Studio (mono + phase) : dans la chaîne et le chemin du signal, DSD converti tant qu'ils sont actifs"_t);
    }
    shoot ("28_outils_studio");
    app.studioTools().monitor = nx::StudioToolsState::Monitor::Stereo;
    app.studioTools().invertPhase = false;
    app.applyStudioTools();
    t.check (! app.engine().getSettings().dspNeedsPcm, "outils éteints : retour au bit-perfect"_t);

    // Immersive view, modular: theme (CSS + immersive.json), settings, plug-ins
    app.setImmersive (true);
    for (int i = 0; i < 40 && ! main->getImmersive().hasWaveform(); ++i) settle (50);
    t.check (main->isImmersive() && main->getImmersive().getPalette().size() == 4 && main->getImmersive().hasWaveform(),
             "vue immersive : 4 couleurs extraites de la pochette, forme d'onde calculée"_t);
    t.check (shoot ("29_vue_immersive") > 60, "vue immersive (fond animé, pochette HD, paroles, forme d'onde)"_t);
    {
        auto folder = app.userDataDirectory().getChildFile ("Themes/immersive-test.nxtheme");
        folder.createDirectory();
        folder.getChildFile ("manifest.json").replaceWithText (R"({ "id": "immersive-test", "name": "Immersive Test", "stylesheet": "theme.css" })");
        folder.getChildFile ("theme.css").replaceWithText (":root { --nx-accent: #FF2A6D; --nx-immersive-layout: centered; --nx-immersive-vu: on; --nx-immersive-artwork-scale: 0.9; }");
        folder.getChildFile ("immersive.json").replaceWithText (R"({ "spectrum": true, "motion": 1.5 })");
        app.themes().rescan();
        app.applyThemeNow ("immersive-test");
        auto c = nx::ImmersiveConfig::resolve (app);
        const bool fromTheme = c.layout == "centered" && c.vu && c.spectrum && std::abs (c.motion - 1.5f) < 0.01f && c.source.contains ("th");
        main->getImmersive().refreshConfig();
        shoot ("30_vue_immersive_theme");
        auto userImm = juce::var (new juce::DynamicObject());
        userImm.getDynamicObject()->setProperty ("layout", "minimal");
        app.settings().set ("immersive", userImm);
        c = nx::ImmersiveConfig::resolve (app);
        const bool fromSettings = c.layout == "minimal" && c.vu;
        auto pluginImm = juce::var (new juce::DynamicObject());
        pluginImm.getDynamicObject()->setProperty ("lyrics", false);
        app.layout.getDynamicObject()->setProperty ("immersive", pluginImm);
        c = nx::ImmersiveConfig::resolve (app);
        const bool fromPlugin = ! c.lyrics && c.source == "plug-in";
        t.check (fromTheme && fromSettings && fromPlugin, "disposition modulable : thème (CSS + immersive.json), puis réglages, puis plug-ins"_t);
        app.layout.getDynamicObject()->removeProperty ("immersive");
        app.settings().set ("immersive", juce::var (new juce::DynamicObject()));
        app.applyThemeNow ("nx-emerald");
    }
    app.setImmersive (false);
    t.check (! main->isImmersive(), "Échap / bouton : sortie de la vue immersive"_t);

    // Onboarding assistant (first launch, or Réglages › Interface)
    app.showOnboarding();
    auto& ob = main->getOnboarding();
    t.check (ob.isVisible() && ob.getStep() == 0, "assistant de premier démarrage affiché"_t);
    shoot ("31_assistant_langue_theme");
    ob.clickTile ("lang", "en");
    const bool en = nx::i18n::isEnglish() && "Réglages"_t == "Settings";
    shoot ("32_assistant_english");
    ob.clickTile ("lang", "fr");
    t.check (en && ! nx::i18n::isEnglish(), "étape 1 : langue anglais / français appliquée à chaud"_t);
    ob.setStep (1);
    ob.clickTile ("layout", "compact");
    settle (100);
    const bool compact = main->sidebar.isCompact();
    shoot ("33_assistant_disposition");
    ob.clickTile ("layout", "cider");
    t.check (compact && ! main->sidebar.isCompact(), "étape 2 : disposition compacte (icônes) puis Cider 2 étendue"_t);
    ob.setStep (2);
    ob.clickTile ("profile", "0");
    t.check (app.engine().getSettings().anubisProfile == nx::AnubisProfile::StudioDxdReference, "étape 3 : Anubis détectée, profil optimal activé en un clic"_t);
    t.check (shoot ("34_assistant_materiel") > 40, "étape 3 : dessin de la Merging Anubis"_t);
    ob.setStep (3);
    shoot ("35_assistant_sources");
    ob.finish();
    t.check (! ob.isVisible() && (bool) app.settings().get ("onboardingDone", false), "fin de l'assistant : mémorisé (il ne revient plus seul)"_t);

    // Compact sidebar and the English interface
    app.setLayoutOption ("sidebar", "compact");
    app.setLanguage ("en");
    app.navigate ({ nx::PageId::Library, "albums" });
    t.check (shoot ("36_interface_anglais_compacte") > 40 && "Favoris"_t == "Favourites", "interface en anglais, barre latérale compacte"_t);
    app.setLanguage ("fr");
    app.setLayoutOption ("sidebar", "wide");
    virtualDac->name = originalDac;
    app.selectDevice (originalDac);

    // mini player
    nx::MiniPlayer mini (app);
    mini.setSize (380, 96);
    mini.trackChanged();
    settle (100);
    const auto miniImg = mini.createComponentSnapshot (mini.getLocalBounds());
    savePng (miniImg, shots.getChildFile ("11_mini_lecteur.png"));
    t.check (colourVariety (miniImg) > 20, "mini-lecteur");
    t.info ("captures : " + shots.getFullPathName());
    t.check (juce::String (nx::MediaControls::compiledBackend()).isNotEmpty(),
             "touches multimédia du système : "_t + juce::String::fromUTF8 (nx::MediaControls::compiledBackend()));
}
