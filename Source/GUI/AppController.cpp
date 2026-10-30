// NX Player — MIT License
#include "AppController.h"
#include <numeric>
#include "Windows.h"
#include "../Providers/UpnpProvider.h"
#include "../Providers/StreamingProviders.h"
#include "../Output/CaptureBackend.h"
#include "../Engine/DeviceProfiles.h"
#include "../Platform/AuraBridge.h"

namespace nx
{

juce::File AppController::defaultDataDir()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/NX Player");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("NX Player");
   #endif
}

juce::File AppController::defaultResourcesDir()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_MAC
    auto res = exe.getParentDirectory().getParentDirectory().getChildFile ("Resources");
    if (res.getChildFile ("Themes").isDirectory()) return res;
   #endif
    return exe.getParentDirectory();
}

AppController::AppController() : AppController (Options()) {}

AppController::AppController (Options options)
{
    dataDir = options.dataDir != juce::File() ? options.dataDir : defaultDataDir();
    resourcesDir = options.resourcesDir != juce::File() ? options.resourcesDir : defaultResourcesDir();
    dataDir.createDirectory();
    appSettings.file = dataDir.getChildFile ("settings.json");
    appSettings.load();
    i18n::setLanguage (appSettings.get ("language", "fr").toString());
    applyLayoutSettings();

    juce::String err;
    if (! lib.open (dataDir, err)) juce::Logger::writeToLog ("bibliothèque : "_t + err);
    libScanner = std::make_unique<LibraryScanner> (lib);

    credentials = std::make_unique<CredentialStore> (dataDir.getChildFile ("accounts.json"));
    unified = std::make_unique<UnifiedMediaProvider> (lib, *credentials);
    unified->addProvider (std::make_unique<UpnpProvider>());
   #if NX_ENABLE_STREAMING
    // Streaming services: off for now (cmake -DNX_ENABLE_STREAMING=ON brings them back).
    unified->addProvider (std::make_unique<QobuzProvider> (*credentials));
    unified->addProvider (std::make_unique<TidalProvider> (*credentials));
    unified->addProvider (std::make_unique<SpotifyProvider> (*credentials));
    unified->addProvider (std::make_unique<YouTubeMusicProvider> (*credentials));
   #endif

    // Output: the saved backend, else the best one of the platform, else a silent virtual DAC.
    std::unique_ptr<OutputBackend> backend = std::move (options.backend);
    if (backend == nullptr)
    {
        auto list = createPlatformBackends();
        const auto saved = appSettings.get ("backend").toString();
        for (auto& b : list) if (backend == nullptr && b->getTypeName() == saved) backend = std::move (b);
        for (auto& b : list) if (backend == nullptr && b != nullptr) backend = std::move (b);
        if (backend == nullptr)
        {
            auto silent = std::make_unique<CaptureBackend>();
            silent->record = false;
            silent->name = "Sortie silencieuse (aucun pilote audio)";
            backend = std::move (silent);
        }
    }
    backendName = backend->getTypeName();
    eng = std::make_unique<PlaybackEngine> (std::move (backend));
    eng->streamResolver = [this] (const PlayableItem& item) { return unified->openStream (item); };
    loadEngineSettings();

    audioAnalyzer = std::make_unique<AudioAnalyzer> (eng->getAnalyzer());
    artworkCache = std::make_unique<ArtworkCache> (lib, dataDir.getChildFile ("ArtworkWeb"));

    // Offline copies of NAS / Jellyfin tracks
    downloadManager = std::make_unique<DownloadManager> (lib);
    if (const auto folder = appSettings.get ("downloadFolder").toString(); folder.isNotEmpty())
        downloadManager->setFolder (juce::File (folder));
    downloadManager->onMessage = [this] (const juce::String& m) { showMessage (m); };

    // Artist pages (photos, biographies) and NX Remote
    artistInfoService = std::make_unique<ArtistInfoService> (lib);
    artistInfoService->setEnabled ((bool) appSettings.get ("artistInfoOnline", true));
    artistInfoService->language = i18n::getLanguage();
    remoteServer = std::make_unique<RemoteServer>();
    remoteServer->onCommand = [this] (const juce::String& c, const juce::StringPairArray& p) { handleRemoteCommand (c, p); };
    remoteServer->onLibraryQuery = [this] (const juce::StringPairArray& p) { return buildRemoteLibrary (p); };
    remoteServer->getArtwork = [this] (const juce::String& key, juce::String& mime) -> juce::MemoryBlock
    {
        juce::MemoryBlock out;
        if (key.isEmpty()) return out;
        if (auto f = lib.getArtworkFile (key); f.existsAsFile() && f.getSize() < 8 * 1024 * 1024)
        {
            f.loadFileAsData (out);
            mime = f.hasFileExtension ("png") ? "image/png" : "image/jpeg";
            return out;
        }
        const auto img = artworkCache->loadNow (key, 600);
        if (img.isValid())
        {
            juce::MemoryOutputStream jpg (out, false);
            juce::JPEGImageFormat fmt;
            fmt.setQuality (0.85f);
            fmt.writeImageToStream (img, jpg);
        }
        mime = "image/jpeg";
        return out;
    };

    // Themes
    themeManager.setSearchFolders ({ resourcesDir.getChildFile ("Themes"), dataDir.getChildFile ("Themes") });
    themeManager.onThemeChanged = [this] (const Theme& t)
    {
        lookAndFeel.applyTheme (t);
        if (auto* m = getMainComponent()) m->themeChanged();
        if (miniWindow) miniWindow->repaint();
        if (mainWindow) mainWindow->setBackgroundColour (t.bg());
    };
    if (! themeManager.apply (appSettings.get ("theme", "nx-emerald").toString()))
        themeManager.apply ("nx-emerald");
    lookAndFeel.applyTheme (themeManager.current());
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    // Plug-ins
    pluginManager.setSearchFolders ({ resourcesDir.getChildFile ("Plugins"), dataDir.getChildFile ("Plugins") });
    if (auto* ui = appSettings.get ("uiPlugins").getArray())
        for (auto& p : pluginManager.getUiPlugins()) pluginManager.setUiPluginEnabled (p.id, ui->contains (p.id));
    if (auto* dsp = appSettings.get ("dspPlugins").getArray())
        for (auto& p : pluginManager.getDspPlugins()) pluginManager.setDspPluginEnabled (p.id, dsp->contains (p.id));
    pluginManager.onLayoutRequest = [this] (const juce::var& l)
    {
        if (auto* o = l.getDynamicObject())
            for (auto& prop : o->getProperties()) layout.getDynamicObject()->setProperty (prop.name, prop.value);
        if (auto* m = getMainComponent()) { m->resized(); if (m->isImmersive()) m->getImmersive().refreshConfig(); }
    };
    pluginManager.onPluginsChanged = [this]
    {
        juce::Array<juce::var> ui, dsp;
        for (auto& p : pluginManager.getUiPlugins()) if (p.enabled) ui.add (p.id);
        for (auto& p : pluginManager.getDspPlugins()) if (p.enabled) dsp.add (p.id);
        appSettings.set ("uiPlugins", ui);
        appSettings.set ("dspPlugins", dsp);
        rebuildDspChain();
        if (auto* m = getMainComponent()) m->pluginsChanged();
    };
    rebuildDspChain();

    // Engine notifications (message thread)
    eng->onTrackChanged = [this] { onTrackChanged(); };
    eng->onStateChanged = [this] { if (auto* m = getMainComponent()) m->stateChanged(); updateSystemNowPlaying (true); };
    eng->onSignalPathChanged = [this] { if (auto* m = getMainComponent()) m->stateChanged(); };
    eng->onQueueFinished = [this] { if (repeatOn && ! queueTracks.empty()) eng->playIndex (0); };
    eng->onError = [this] (const juce::String& e) { showMessage (e); };

    lib.onChanged = [this]
    {
        if (auto* m = getMainComponent())
        {
            const auto page = m->getCurrentPage().page;
            if (page != PageId::NowPlaying && page != PageId::Settings) m->refreshCurrentPage();
            else m->sidebar.refresh();
        }
    };

    if (options.createWindow)
    {
        mainWindow = std::make_unique<MainWindow> (*this, lookAndFeel);
        libScanner->start (3000);

        // Media keys and the system's "Now Playing" (works with NX Player in the background)
        MediaControls::Callbacks cb;
        cb.play = [this] { eng->play(); };
        cb.pause = [this] { eng->pause(); };
        cb.toggle = [this] { eng->togglePlayPause(); };
        cb.next = [this] { eng->next(); };
        cb.previous = [this] { eng->previous(); };
        cb.seek = [this] (double t) { eng->seek (t); updateSystemNowPlaying (true); };
        void* handle = mainWindow->getPeer() != nullptr ? mainWindow->getPeer()->getNativeHandle() : nullptr;
        mediaControls = MediaControls::create (std::move (cb), handle);

        // NAS: no network scan at startup (macOS would ask for the local network permission before the user
        // even looks at the NAS). Only the servers already configured are reconnected; discovery runs when the
        // NAS page is opened.
        juce::StringArray known;
        if (auto* arr = appSettings.get ("nasServers").getArray()) for (auto& v : *arr) known.add (v.toString());
        if (auto* upnp = dynamic_cast<UpnpProvider*> (unified->getProvider ("upnp")); upnp != nullptr && ! known.isEmpty())
        {
            std::weak_ptr<int> weak = alive;
            nasDiscoveryRunning = true;
            juce::Thread::launch ([this, upnp, known, weak]
            {
                for (auto& loc : known) upnp->addServerFromLocation (loc);
                nasDiscoveryRunning = false;
                juce::MessageManager::callAsync ([this, weak]
                {
                    if (weak.expired()) return;
                    saveNasServers();
                    if (auto* m = getMainComponent()) m->sidebar.refresh();
                });
            });
        }
    }
    else
    {
        headlessMain = std::make_unique<MainComponent> (*this);
        headlessMain->setSize (1360, 860);
    }

    // First launch: the setup assistant (language, layout, audio device, music sources)
    if (options.createWindow && ! (bool) appSettings.get ("onboardingDone", false))
    {
        std::weak_ptr<int> weak = alive;
        juce::MessageManager::callAsync ([this, weak] { if (! weak.expired()) showOnboarding(); });
    }

    lastFrame = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz (30);
}

AppController::~AppController()
{
    stopTimer();
    remoteServer->stop();
    sleepBlocker.setActive (false);
    mediaControls.reset();
    for (int i = 0; i < 400 && nasDiscoveryRunning.load(); ++i) juce::Thread::sleep (10);   // the NAS thread uses the providers
    if (mainWindow) appSettings.set ("windowBounds", mainWindow->getWindowStateAsString());
    eng->stop();
    miniWindow.reset();
    mainWindow.reset();
    headlessMain.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    libScanner.reset();
    // The engine's DSP chains call into plug-in libraries: release them before the plug-in manager unloads those.
    audioAnalyzer.reset();
    eng.reset();
    downloadManager.reset();
    artistInfoService.reset();
}

MainComponent* AppController::getMainComponent()
{
    if (mainWindow) return &mainWindow->getMain();
    return headlessMain.get();
}

//==============================================================================
void AppController::loadEngineSettings()
{
    EngineSettings s;
    const auto v = appSettings.get ("engine");
    if (v.isObject())
    {
        s.dsdMode = (DsdMode) (int) v.getProperty ("dsdMode", 0);
        s.dopAllowed = (bool) v.getProperty ("dop", true);
        s.upsampleToDxd = (bool) v.getProperty ("upsample", false);
        s.dsdToPcmGainDb = (double) v.getProperty ("dsdGain", 0.0);
        s.exclusive = (bool) v.getProperty ("exclusive", true);
        s.bufferMilliseconds = (int) v.getProperty ("bufferMs", 0);
        s.hardwareProfile = (HardwareProfile) juce::jlimit (0, 3, (int) v.getProperty ("hardwareProfile", 0));
        s.rmeProfile = (RmeProfile) juce::jlimit (0, 1, (int) v.getProperty ("rmeProfile", 0));
        s.rmeLockVolume = (bool) v.getProperty ("rmeLockVolume", true);
        s.anubisProfile = (AnubisProfile) juce::jlimit (0, 2, (int) v.getProperty ("anubisProfile", 0));
        s.anubisDsdGainDb = (double) v.getProperty ("anubisDsdGain", 6.0);
        s.anubisHarmonics = (bool) v.getProperty ("anubisHarmonics", true);
        s.memoryPlay = (bool) v.getProperty ("memoryPlay", true);
        s.memoryPlayMaxMb = juce::jlimit (64, 8192, (int) v.getProperty ("memoryPlayMaxMb", 1536));
    }
    eng->setSettings (s);
    const auto device = appSettings.get ("device").toString();
    if (device.isNotEmpty() && eng->getBackend().getDeviceNames().contains (device)) eng->setDevice (device);
}

void AppController::saveEngineSettings()
{
    const auto s = eng->getSettings();
    auto* o = new juce::DynamicObject();
    o->setProperty ("dsdMode", (int) s.dsdMode);
    o->setProperty ("dop", s.dopAllowed);
    o->setProperty ("upsample", s.upsampleToDxd);
    o->setProperty ("dsdGain", s.dsdToPcmGainDb);
    o->setProperty ("exclusive", s.exclusive);
    o->setProperty ("bufferMs", s.bufferMilliseconds);
    o->setProperty ("hardwareProfile", (int) s.hardwareProfile);
    o->setProperty ("anubisProfile", (int) s.anubisProfile);
    o->setProperty ("anubisDsdGain", s.anubisDsdGainDb);
    o->setProperty ("anubisHarmonics", s.anubisHarmonics);
    o->setProperty ("rmeProfile", (int) s.rmeProfile);
    o->setProperty ("rmeLockVolume", s.rmeLockVolume);
    o->setProperty ("memoryPlay", s.memoryPlay);
    o->setProperty ("memoryPlayMaxMb", s.memoryPlayMaxMb);
    appSettings.set ("engine", juce::var (o));
}

void AppController::applyEngineSettings()
{
    saveEngineSettings();
    if (auto* m = getMainComponent()) m->stateChanged();
}

juce::StringArray AppController::getBackendNames()
{
    juce::StringArray names;
    for (auto& b : createPlatformBackends()) names.add (b->getTypeName());
    if (names.isEmpty()) names.add (backendName);
    return names;
}

juce::String AppController::getCurrentBackendName() { return eng->getBackend().getTypeName(); }

void AppController::selectBackend (const juce::String& name)
{
    for (auto& b : createPlatformBackends())
        if (b->getTypeName() == name)
        {
            eng->setBackend (std::move (b));
            backendName = name;
            appSettings.set ("backend", name);
            appSettings.set ("device", eng->getDeviceName());
            showMessage ("Sortie : "_t + name + " · "_t + eng->getDeviceName());
            return;
        }
}

void AppController::selectDevice (const juce::String& name)
{
    eng->setDevice (name);
    appSettings.set ("device", name);
    showMessage ("Périphérique : "_t + name);
}

void AppController::saveNasServers()
{
    if (auto* upnp = dynamic_cast<UpnpProvider*> (unified->getProvider ("upnp")))
    {
        juce::Array<juce::var> list;
        for (auto& loc : upnp->getKnownLocations()) list.add (loc);
        if (! list.isEmpty()) appSettings.set ("nasServers", list);
    }
}

void AppController::applyThemeNow (const juce::String& id)
{
    if (themeManager.apply (id)) appSettings.set ("theme", id);
}

void AppController::applyTheme (const juce::String& id)
{
    if (themeManager.findTheme (id) == nullptr) return;
    appSettings.set ("theme", id);                       // read at the next launch
    if (id == themeManager.current().id || mainWindow == nullptr || restartPromptOpen) return;

    restartPromptOpen = true;
    std::weak_ptr<int> weak = alive;
    const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Thème modifié"_t)
                             .withMessage ("Le nouveau thème sera appliqué au prochain démarrage de NX Player. Redémarrer maintenant ?"_t)
                             .withButton ("Redémarrer maintenant"_t).withButton ("Plus tard"_t);
    juce::AlertWindow::showAsync (options, [this, weak] (int result)
    {
        if (weak.expired()) return;
        restartPromptOpen = false;
        if (result == 1) restartApplication();           // first button
    });
}

void AppController::restartApplication()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentApplicationFile).getFullPathName();
    // Single-instance app: a helper waits for this process to exit, then starts the new one.
   #if JUCE_WINDOWS
    const juce::StringArray cmd { "cmd", "/c", "ping", "-n", "3", "127.0.0.1", ">nul", "&", "start", "\"\"", exe };
   #elif JUCE_MAC
    const juce::StringArray cmd { "/bin/sh", "-c", "sleep 1; open -n \"$0\"", exe };
   #else
    const juce::StringArray cmd { "/bin/sh", "-c", "sleep 1; exec \"$0\"", exe };
   #endif
    static juce::ChildProcess helper;
    if (helper.start (cmd)) juce::JUCEApplication::getInstance()->systemRequestedQuit();
    else showMessage ("Redémarrage automatique impossible : relancez NX Player manuellement."_t);
}

//==============================================================================
void AppController::playTracks (const std::vector<TrackInfo>& tracks, int startIndex)
{
    if (tracks.empty()) return;
    startIndex = juce::jlimit (0, (int) tracks.size() - 1, startIndex);
    const auto& chosen = tracks[(size_t) startIndex];

    if (unified->routeFor (chosen) == PlaybackRoute::External)
    {
        juce::String err;
        if (auto* p = unified->getProvider (chosen.source))
        {
            if (p->playExternally (chosen, err))
            {
                eng->pause();
                externalTrack = chosen;
                externalPlaying = true;
                onTrackChanged();
                showMessage (p->lastError.isNotEmpty() ? p->lastError
                                                       : "Lecture confiée à "_t + p->getDisplayName() + " (hors moteur bit-perfect)");
            }
            else showMessage (p->getDisplayName() + " : " + err);
        }
        return;
    }

    externalPlaying = false;
    shuffleScope = {};                                   // a hand-picked list ends any endless shuffle
    queueTracks.clear();
    std::vector<PlayableItem> items;
    int start = 0;
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        const auto& t = tracks[(size_t) i];
        if (unified->routeFor (t) != PlaybackRoute::Engine) continue;
        if (i == startIndex) start = (int) items.size();
        queueTracks.push_back (t);
        items.push_back (unified->toPlayable (t));
    }
    if (shuffleOn && items.size() > 1)
    {
        // the chosen track stays first, the rest is shuffled
        std::vector<size_t> order (items.size() - (size_t) start - 1);
        std::iota (order.begin(), order.end(), (size_t) start + 1);
        std::shuffle (order.begin(), order.end(), rng);
        auto items2 = items;
        auto tracks2 = queueTracks;
        for (size_t k = 0; k < order.size(); ++k)
        {
            items[(size_t) start + 1 + k] = items2[order[k]];
            queueTracks[(size_t) start + 1 + k] = tracks2[order[k]];
        }
    }
    eng->setQueue (std::move (items), start, true);
}

std::vector<TrackInfo> AppController::collectionTracks (const juce::String& scope)
{
    return scope == "favourites" ? lib.getFavouriteTracks() : lib.getTracks ("artist", 1000000);
}

void AppController::playCollection (const juce::String& scope, bool shuffle)
{
    auto tracks = collectionTracks (scope);
    if (tracks.empty()) { showMessage ("Aucun morceau à lire"_t); return; }
    if (! shuffle) { playTracks (tracks, 0); return; }

    shufflePass = std::move (tracks);
    std::shuffle (shufflePass.begin(), shufflePass.end(), rng);
    shuffleNext = 0;
    lastQueuedKey = 0;
    const bool wasShuffle = shuffleOn;
    shuffleOn = false;                                    // the pass is already shuffled
    const size_t first = std::min<size_t> (shufflePass.size(), 200);
    playTracks (std::vector<TrackInfo> (shufflePass.begin(), shufflePass.begin() + (std::ptrdiff_t) first), 0);
    shuffleOn = wasShuffle;
    shuffleNext = first;
    shuffleScope = scope;                                 // after playTracks, which clears it
    if (! queueTracks.empty()) lastQueuedKey = queueTracks.back().id;
}

void AppController::extendShuffle()
{
    // Called on every track change: keep at least 20 tracks queued after the current one.
    if (shuffleScope.isEmpty() || externalPlaying) return;
    const int remaining = (int) queueTracks.size() - 1 - eng->getCurrentIndex();
    if (remaining >= 20) return;

    std::vector<TrackInfo> chunk;
    while (chunk.size() < 200)
    {
        if (shuffleNext >= shufflePass.size())
        {
            // new pass over the (possibly changed) collection; its first track must differ from the last one queued
            shufflePass = collectionTracks (shuffleScope);
            if (shufflePass.empty()) break;
            std::shuffle (shufflePass.begin(), shufflePass.end(), rng);
            const auto last = chunk.empty() ? lastQueuedKey : chunk.back().id;
            if (shufflePass.size() > 1 && shufflePass.front().id == last) std::swap (shufflePass.front(), shufflePass.back());
            shuffleNext = 0;
        }
        chunk.push_back (shufflePass[shuffleNext++]);
        if (shufflePass.size() == 1 && chunk.size() >= 1) break;     // a single track: repeat it, one per call
    }
    if (chunk.empty()) return;
    lastQueuedKey = chunk.back().id;

    std::vector<PlayableItem> items;
    for (auto& t : chunk)
        if (unified->routeFor (t) == PlaybackRoute::Engine) { queueTracks.push_back (t); items.push_back (unified->toPlayable (t)); }
    if (! items.empty()) eng->appendToQueue (std::move (items));
}

void AppController::enqueue (const std::vector<TrackInfo>& tracks)
{
    std::vector<PlayableItem> items;
    for (auto& t : tracks)
        if (unified->routeFor (t) == PlaybackRoute::Engine) { queueTracks.push_back (t); items.push_back (unified->toPlayable (t)); }
    if (eng->getQueue().empty()) { playTracks (tracks, 0); return; }
    eng->appendToQueue (std::move (items));
    showMessage (juce::String ((int) tracks.size()) + " morceau(x) ajouté(s) à la file"_t);
}

const TrackInfo* AppController::currentTrack()
{
    if (externalPlaying) return &externalTrack;
    const int i = eng->getCurrentIndex();
    return i >= 0 && i < (int) queueTracks.size() ? &queueTracks[(size_t) i] : nullptr;
}

void AppController::setShuffle (bool on)
{
    shuffleOn = on;
    showMessage (on ? "Lecture aléatoire activée"_t : "Lecture aléatoire désactivée"_t);
}

void AppController::navigate (const PageRequest& r)
{
    if (auto* m = getMainComponent()) m->navigate (r);
}

void AppController::toggleMiniPlayer()
{
    if (mainWindow == nullptr) return;
    if (miniWindow == nullptr) miniWindow = std::make_unique<MiniWindow> (*this);
    const bool showMini = ! miniWindow->isVisible();
    if (showMini)
    {
        miniWindow->setTopLeftPosition (mainWindow->getScreenX() + mainWindow->getWidth() - 400, mainWindow->getScreenY() + 40);
        miniWindow->setVisible (true);
        miniWindow->getMini().trackChanged();
        mainWindow->setMinimised (true);
    }
    else
    {
        miniWindow->setVisible (false);
        mainWindow->setMinimised (false);
        mainWindow->toFront (true);
    }
}

void AppController::showMessage (const juce::String& text)
{
    if (auto* m = getMainComponent()) m->showMessage (text);
    juce::Logger::writeToLog (text);
}

const juce::Image& AppController::backdrop()
{
    if (auto* m = getMainComponent()) return m->background.getGlassBackdrop();
    return emptyImage;
}

juce::Component* AppController::backdropOwner() { return getMainComponent(); }

void AppController::openFiles (const juce::StringArray& paths)
{
    std::vector<TrackInfo> tracks;
    for (auto& p : paths)
    {
        juce::File f (p);
        TrackInfo t;
        if (f.existsAsFile() && readLocalTrack (f, lib, t)) { lib.upsert (t); tracks.push_back (t); }
    }
    if (! tracks.empty()) playTracks (tracks, 0);
}

//==============================================================================
void AppController::updateSystemNowPlaying (bool force)
{
    if (mediaControls == nullptr) return;
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto* t = currentTrack();
    // Without a forced update, refresh every 5 s (position drift) or while the artwork is still loading.
    const bool artworkPending = t != nullptr && t->artworkKey.isNotEmpty() && nowPlayingArtworkSent != t->artworkKey;
    if (! force && now - lastNowPlayingUpdate < 5.0 && ! (artworkPending && now - lastNowPlayingUpdate > 0.5)) return;
    lastNowPlayingUpdate = now;

    NowPlayingInfo info;
    if (t != nullptr)
    {
        info.hasTrack = true;
        info.title = t->title;
        info.artist = t->displayArtist();
        info.album = t->album;
        info.duration = t->duration;
        info.position = externalPlaying ? 0.0 : eng->getPositionSeconds();
        info.playing = eng->getState() == PlaybackEngine::State::Playing;
        info.artworkId = nowPlayingArtworkSent;
        if (artworkPending)
        {
            const auto img = artworkCache->get (t->artworkKey, 600);
            if (img.isValid())
            {
                juce::MemoryOutputStream png;
                juce::PNGImageFormat().writeImageToStream (img, png);
                info.artworkPng = png.getMemoryBlock();
                info.artworkId = nowPlayingArtworkSent = t->artworkKey;
            }
        }
    }
    mediaControls->update (info);
}

void AppController::onTrackChanged()
{
    if (mediaControls != nullptr && nowPlayingArtworkSent.isNotEmpty())
    {
        const auto* t = currentTrack();
        if (t == nullptr || t->artworkKey != nowPlayingArtworkSent) nowPlayingArtworkSent = "none:" + juce::String (juce::Random::getSystemRandom().nextInt());
    }
    updateSystemNowPlaying (true);
    extendShuffle();
    {
        // one background scan per track (cancels the previous one), read by the player bar and the immersive view
        const auto* t = currentTrack();
        analysisPath = t == nullptr || externalPlaying ? juce::String()
                     : t->source == "local" ? t->externalId : lib.getOfflineCopy (*t);
        if (analysisPath.isNotEmpty()) trackScan->request (analysisPath);
    }
    if (auto* m = getMainComponent()) m->trackChanged();
    if (miniWindow) miniWindow->getMini().trackChanged();
    if (const auto* t = currentTrack())
    {
        if (t->id > 0 && ! externalPlaying) lib.markPlayed (t->id);
        loadLyrics (*t);
    }
}

void AppController::loadLyrics (const TrackInfo& t)
{
    const int gen = ++lyricsGeneration;
    auto local = loadLocalLyrics (t);
    auto* m = getMainComponent();
    if (m == nullptr) return;
    if (! local.isEmpty() || ! (bool) appSettings.get ("lyricsOnline", true))
    {
        m->setLyrics (local, local.isEmpty() ? juce::String ("Pas de paroles pour ce morceau") : juce::String());
        return;
    }
    m->setLyrics ({}, "Recherche des paroles…"_t);
    const auto artist = t.displayArtist(), title = t.title, album = t.album;
    const double duration = t.duration;
    std::weak_ptr<int> token = alive;
    juce::Thread::launch ([this, gen, artist, title, album, duration, token]
    {
        auto found = fetchOnlineLyrics (artist, title, album, duration);   // LRCLIB, then lyrics.ovh
        juce::MessageManager::callAsync ([this, gen, found, token]
        {
            if (token.expired() || gen != lyricsGeneration) return;
            if (auto* main = getMainComponent())
                main->setLyrics (found.value_or (Lyrics()), found ? juce::String() : juce::String ("Pas de paroles trouvées"_t));
        });
    });
}

void AppController::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double dt = juce::jlimit (0.001, 0.2, now - lastFrame);
    lastFrame = now;
    const bool playing = eng->getState() == PlaybackEngine::State::Playing;
    audioAnalyzer->update (dt, playing);

    pluginManager.setState ("time", now);
    updateSystemNowPlaying (false);
    sleepBlocker.setActive (playing && (bool) appSettings.get ("preventSleep", true));
    if (remoteServer->isRunning() && now - lastRemoteState > 0.25)
    {
        lastRemoteState = now;
        remoteServer->setState (buildRemoteState());
    }
    pluginManager.setState ("audio", audioAnalyzer->toVar());
    auto* player = new juce::DynamicObject();
    if (const auto* t = currentTrack())
    {
        player->setProperty ("title", t->title);
        player->setProperty ("artist", t->displayArtist());
        player->setProperty ("album", t->album);
        player->setProperty ("duration", t->duration);
        player->setProperty ("format", t->resolutionBadge());
    }
    player->setProperty ("position", eng->getPositionSeconds());
    player->setProperty ("playing", playing);
    pluginManager.setState ("player", juce::var (player));

    const auto& th = themeManager.current();
    auto* theme = new juce::DynamicObject();
    theme->setProperty ("accent", "#" + th.accent().toDisplayString (false));
    theme->setProperty ("accent2", "#" + th.accent2().toDisplayString (false));
    theme->setProperty ("text", "#" + th.textCol().toDisplayString (false));
    theme->setProperty ("textDim", "#" + th.textDim().toDisplayString (false));
    theme->setProperty ("bg", "#" + th.bg().toDisplayString (false));
    pluginManager.setState ("theme", juce::var (theme));
}

//==============================================================================
void AppController::rebuildDspChain()
{
    auto chain = pluginManager.buildDspChain();
    if (chain == nullptr) chain = std::make_unique<DspChain>();
    chain->processors.push_back (std::make_unique<StudioToolsProcessor> (studio));
    eng->setDspChain (std::move (chain));
}

void AppController::applyStudioTools()
{
    rebuildDspChain();
    // DSD cannot be processed as 1-bit data: while a tool is on, DSD tracks are converted to PCM (DXD).
    auto s = eng->getSettings();
    const bool need = studio->anyActive();
    if (s.dspNeedsPcm != need)
    {
        s.dspNeedsPcm = need;
        // Re-plan the current track only when DSD is on the wire (DSD -> DXD now, or back to DoP / native).
        const auto plan = eng->getCurrentPlan();
        const bool affectsCurrent = plan.source.isDsd();
        eng->setSettings (s, affectsCurrent);
    }
    if (auto* m = getMainComponent()) m->stateChanged();
}

void AppController::setUserVolume (double v)
{
    if (auto* m = getMainComponent()) m->player.setVolumeLevel (v);
    else appSettings.set ("volume", juce::jlimit (0.0, 1.0, v));
}

double AppController::getUserVolume() { return (double) appSettings.get ("volume", 1.0); }

bool AppController::setRemoteEnabled (bool on)
{
    if (! on) { remoteServer->stop(); return true; }
    juce::String err;
    if (! remoteServer->start (0, err)) { showMessage (err); return false; }
    remoteServer->setState (buildRemoteState());
    return true;
}

void AppController::setLanguage (const juce::String& code)
{
    i18n::setLanguage (code);
    appSettings.set ("language", i18n::getLanguage());
    artistInfoService->language = i18n::getLanguage();
    if (auto* m = getMainComponent()) m->languageChanged();
}

void AppController::applyLayoutSettings()
{
    const auto l = appSettings.get ("layout");
    auto* o = layout.getDynamicObject();
    const auto sidebarMode = l.getProperty ("sidebar", "wide").toString();
    if (sidebarMode == "compact") o->setProperty ("sidebarWidth", 76);
    else o->removeProperty ("sidebarWidth");
    const auto player = l.getProperty ("player", "bottom").toString();
    if (player == "top") o->setProperty ("playerPosition", "top");
    else o->removeProperty ("playerPosition");
}

void AppController::setLayoutOption (const juce::String& key, const juce::var& value)
{
    auto l = appSettings.get ("layout");
    if (! l.isObject()) l = juce::var (new juce::DynamicObject());
    l.getDynamicObject()->setProperty (key, value);
    appSettings.set ("layout", l);
    applyLayoutSettings();
    if (auto* m = getMainComponent()) { m->resized(); m->sidebar.resized(); m->repaint(); }
}

void AppController::showOnboarding()
{
    if (auto* m = getMainComponent()) m->showOnboarding();
}

void AppController::setImmersive (bool on)
{
    if (auto* m = getMainComponent()) m->setImmersive (on);
}

void AppController::sendToAuraEngine (const std::vector<TrackInfo>& tracks)
{
    juce::Array<juce::File> files;
    for (auto& t : tracks)
    {
        if (t.source == "local" && juce::File::isAbsolutePath (t.externalId)) files.add (juce::File (t.externalId));
        else if (auto copy = lib.getOfflineCopy (t); copy.isNotEmpty()) files.add (juce::File (copy));
    }
    juce::String err;
    if (aura::openFiles (files, appSettings.get ("auraEnginePath").toString(), err))
        showMessage ("AuraEngine X : "_t + juce::String (files.size()) + " fichier(s) envoyé(s) pour analyse / conversion"_t);
    else
        showMessage (err);
}

juce::String AppController::networkSourceLabel (const juce::String& source)
{
    if (! source.startsWith ("upnp")) return {};
    const auto udn = source.fromFirstOccurrenceOf (":", false, false);
    if (auto* upnp = dynamic_cast<UpnpProvider*> (unified->getProvider ("upnp")))
        for (auto& sv : upnp->getServers())
            if (sv.udn == udn && sv.name.containsIgnoreCase ("jellyfin")) return "Jellyfin";
    return "NAS";
}

//==============================================================================
juce::String AppController::buildRemoteState()
{
    auto* o = new juce::DynamicObject();
    const auto* t = currentTrack();
    if (t != nullptr)
    {
        o->setProperty ("title", t->title);
        o->setProperty ("artist", t->displayArtist());
        o->setProperty ("album", t->album);
        o->setProperty ("art", t->artworkKey.startsWith ("http") ? juce::String() : t->artworkKey);
        o->setProperty ("format", t->resolutionBadge());
        o->setProperty ("duration", t->duration > 0 ? t->duration : eng->getDurationSeconds());
    }
    o->setProperty ("position", eng->getPositionSeconds());
    o->setProperty ("playing", eng->getState() == PlaybackEngine::State::Playing);
    o->setProperty ("volume", getUserVolume());
    o->setProperty ("volumeLocked", eng->isVolumeLockedByProfile());
    o->setProperty ("loop", eng->isLoopActive());
    o->setProperty ("loopA", eng->getLoopA());
    o->setProperty ("loopB", eng->getLoopB());
    const auto path = eng->getSignalPath();
    o->setProperty ("path", path.nodes.empty() ? juce::String() : pipelineLabel (eng->getCurrentPlan(), toString (path.overall)));
    o->setProperty ("quality", path.nodes.empty() ? juce::String() : toString (path.overall));
    const auto settings = eng->getSettings();
    const auto family = activeFamily (settings, eng->getDeviceName());
    o->setProperty ("device", eng->getDeviceName());
    o->setProperty ("family", family == DeviceFamily::Merging ? "merging" : family == DeviceFamily::Rme ? "rme" : "");
    juce::Array<juce::var> profiles;
    for (int i = 0; i < familyProfileCount (family); ++i) profiles.add (profileName (family, i));
    o->setProperty ("profiles", profiles);
    o->setProperty ("profile", family == DeviceFamily::None ? -1 : familyCurrentProfile (settings, family));
    o->setProperty ("studio", studio->anyActive() ? studio->describe() : juce::String());
    return juce::JSON::toString (juce::var (o), true);
}

juce::String AppController::buildRemoteLibrary (const juce::StringPairArray& params)
{
    auto* o = new juce::DynamicObject();
    const auto q = params["q"].trim();
    if (q.isEmpty())
    {
        juce::Array<juce::var> albums;
        for (auto& a : lib.getAlbums ("artist", 400))
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("title", a.title);
            x->setProperty ("artist", a.artist);
            x->setProperty ("art", a.artworkKey.startsWith ("http") ? juce::String() : a.artworkKey);
            x->setProperty ("badge", a.badge());
            albums.add (juce::var (x));
        }
        o->setProperty ("albums", albums);
    }
    else
    {
        juce::Array<juce::var> tracks;
        for (auto& tr : lib.search (q, 80))
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("id", tr.id);
            x->setProperty ("title", tr.title);
            x->setProperty ("artist", tr.displayArtist());
            x->setProperty ("album", tr.album);
            x->setProperty ("badge", tr.resolutionBadge());
            x->setProperty ("art", tr.artworkKey.startsWith ("http") ? juce::String() : tr.artworkKey);
            tracks.add (juce::var (x));
        }
        o->setProperty ("tracks", tracks);
    }
    return juce::JSON::toString (juce::var (o), true);
}

void AppController::handleRemoteCommand (const juce::String& c, const juce::StringPairArray& p)
{
    auto* main = getMainComponent();
    if (c == "play") eng->play();
    else if (c == "pause") eng->pause();
    else if (c == "toggle") eng->togglePlayPause();
    else if (c == "next") eng->next();
    else if (c == "prev") eng->previous();
    else if (c == "seek") eng->seek (juce::jmax (0.0, p["t"].getDoubleValue()));
    else if (c == "volume") setUserVolume (juce::jlimit (0.0, 1.0, p["v"].getDoubleValue()));
    else if (c == "loopA" && main != nullptr) main->player.markLoopA();
    else if (c == "loopB" && main != nullptr) main->player.markLoopB();
    else if (c == "loopClear" && main != nullptr) main->player.clearLoop();
    else if (c == "profile")
    {
        auto s = eng->getSettings();
        const auto family = activeFamily (s, eng->getDeviceName());
        const int i = p["i"].getIntValue();
        if (family != DeviceFamily::None && i >= 0 && i < familyProfileCount (family))
        {
            familySetProfile (s, family, i);
            eng->setSettings (s);
            applyEngineSettings();
            showMessage ("NX Remote : profil "_t + profileName (family, i));
        }
    }
    else if (c == "playAlbum")
    {
        const auto tracks = lib.getAlbumTracks (p["artist"], p["album"]);
        if (! tracks.empty()) playTracks (tracks, 0);
    }
    else if (c == "playTrack")
    {
        if (auto t = lib.getTrack (p["id"].getLargeIntValue())) playTracks ({ *t }, 0);
    }
    if (remoteServer->isRunning()) remoteServer->setState (buildRemoteState());
}

} // namespace nx
