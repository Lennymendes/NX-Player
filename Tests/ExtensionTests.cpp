// NX Player — MIT License
// Brique 3 & 4: themes (CSS, hot reload, import / export), JS UI plug-ins, C DSP plug-ins, and the
// whole interface rendered off-screen (screenshots of every page).
#include "TestFramework.h"
#include <set>
#include "TestAudioFiles.h"
#include "../Source/Ext/ThemeEngine.h"
#include "../Source/Ext/PluginManager.h"
#include "../Source/GUI/AppController.h"
#include "../Source/GUI/MainComponent.h"
#include "../Source/GUI/Windows.h"
#include "../Source/Output/CaptureBackend.h"

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
    t.check (tm.getThemeIds().contains ("nx-emerald") && tm.getThemeIds().contains ("aura-neon"), "2 thèmes livrés : "_t + tm.getThemeIds().joinIntoString (", "));
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

    app.applyTheme ("aura-neon");
    app.navigate ({ nx::PageId::Albums });
    t.check (shoot ("05_theme_aura_neon") > 40 && app.theme().id == "aura-neon", "thème Aura Neon appliqué à chaud"_t);
    app.applyTheme ("nx-emerald");

    app.navigate ({ nx::PageId::Search, "lum" });
    t.check (shoot ("06_recherche") > 40, "Recherche unifiée"_t);
    app.navigate ({ nx::PageId::Source, "qobuz" });
    t.check (shoot ("07_source_qobuz") > 30, "Source Qobuz (identifiants partenaire)");
    app.navigate ({ nx::PageId::Settings });
    t.check (shoot ("08_reglages") > 30, "Réglages (sortie, DSD, thèmes, plug-ins)"_t);
    app.plugins().setUiPluginEnabled ("nx.analog-vu", true);
    app.navigate ({ nx::PageId::NowPlaying });
    t.check (shoot ("09_vu_metres") > 60, "plug-in VU-mètres dans « En cours »"_t);

    // mini player
    nx::MiniPlayer mini (app);
    mini.setSize (380, 96);
    mini.trackChanged();
    settle (100);
    const auto miniImg = mini.createComponentSnapshot (mini.getLocalBounds());
    savePng (miniImg, shots.getChildFile ("10_mini_lecteur.png"));
    t.check (colourVariety (miniImg) > 20, "mini-lecteur");
    t.info ("captures : " + shots.getFullPathName());
}
