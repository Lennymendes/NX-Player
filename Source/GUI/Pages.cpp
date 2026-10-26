// NX Player — MIT License
#include "Pages.h"
#include "../Providers/StreamingProviders.h"
#include "../Library/LibraryScanner.h"
#include "../Ext/PluginManager.h"

namespace nx
{

//==============================================================================
// ListingPage
//==============================================================================
ListingPage::ListingPage (AppContext& c) : ScrollPage (c), grid (c), list (c), heroArt (c, 640)
{
    contentComp.addAndMakeVisible (header);
    contentComp.addAndMakeVisible (grid);
    contentComp.addAndMakeVisible (list);
    contentComp.addChildComponent (heroArt);
    contentComp.addChildComponent (playAll);
    contentComp.addChildComponent (shuffleAll);
    contentComp.addChildComponent (cta);
    cta.setToggleState (true, juce::dontSendNotification);
    cta.onClick = [this] { if (ctaAction) ctaAction(); };
    playAll.setToggleState (true, juce::dontSendNotification);
    playAll.onClick = [this] { if (! heroTracks.empty()) ctx.playTracks (heroTracks, 0); };
    shuffleAll.onClick = [this]
    {
        auto t = heroTracks;
        juce::Random r;
        for (size_t i = t.size(); i > 1; --i) std::swap (t[i - 1], t[(size_t) r.nextInt ((int) i)]);
        if (! t.empty()) ctx.playTracks (t, 0);
    };
    grid.onOpen = [this] (const AlbumInfo& a) { ctx.navigate ({ PageId::AlbumDetail, a.artist, a.title }); };
    grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getAlbumTracks (a.artist, a.title), 0); };
    ctx.artwork().addChangeListener (this);
}

ListingPage::~ListingPage() { ctx.artwork().removeChangeListener (this); }

void ListingPage::Header::paint (juce::Graphics& g)
{
    const auto& t = page.ctx.theme();
    auto r = getLocalBounds();
    if (page.hero)
    {
        r.removeFromLeft (page.heroArt.getWidth() + 28);
        r.removeFromBottom (54);
        g.setColour (t.textDim());
        g.setFont (style::font (12.0f, true));
        g.drawText ("ALBUM", r.removeFromTop (r.getHeight() - 96), juce::Justification::bottomLeft);
        g.setColour (t.textCol());
        g.setFont (style::font (34.0f, true));
        g.drawFittedText (page.title, r.removeFromTop (44), juce::Justification::bottomLeft, 1);
        g.setColour (t.accent());
        g.setFont (style::font (18.0f, true));
        g.drawText (page.subtitle.upToFirstOccurrenceOf ("\n", false, false), r.removeFromTop (26), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText (page.subtitle.fromFirstOccurrenceOf ("\n", false, false), r.removeFromTop (22), juce::Justification::centredLeft, true);
        return;
    }
    drawPageTitle (g, t, r, page.title, page.subtitle);
}

void ListingPage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (100, width - pad * 2);
    int y = 24;

    heroArt.setVisible (hero);
    playAll.setVisible (hero || ! heroTracks.empty());
    shuffleAll.setVisible (hero || ! heroTracks.empty());
    if (hero)
    {
        const int art = juce::jmin (240, inner / 3);
        heroArt.setArtwork (heroArtwork, title);
        heroArt.setBounds (pad, y, art, art);
        header.setBounds (pad, y, inner, art);
        playAll.setBounds (pad + art + 28, y + art - 44, 120, 38);
        shuffleAll.setBounds (pad + art + 160, y + art - 44, 130, 38);
        y += art + 28;
    }
    else
    {
        header.setBounds (pad, y, inner, subtitle.isNotEmpty() ? 74 : 54);
        y += header.getHeight() + 8;
        if (! heroTracks.empty())
        {
            playAll.setBounds (pad, y, 120, 36);
            shuffleAll.setBounds (pad + 132, y, 130, 36);
            y += 52;
        }
    }

    cta.setVisible (ctaText.isNotEmpty() && ! hero);
    if (cta.isVisible())
    {
        cta.setButtonText (ctaText);
        cta.setBounds (pad, y, 260, 40);
        y += 60;
    }

    if (topSlot != nullptr && ! topSlot->isEmpty())
    {
        if (topSlot->getParentComponent() != &contentComp) contentComp.addAndMakeVisible (topSlot);
        const int h = juce::jmin (220, topSlot->getPreferredHeight());
        topSlot->setBounds (pad, y, inner, h);
        y += h + 20;
    }

    const int gh = grid.heightForWidth (inner);
    grid.setBounds (pad, y, inner, gh);
    y += gh + (gh > 0 ? 16 : 0);

    list.setBounds (pad - 10, y, inner + 20, list.getTotalHeight());
    y += list.getTotalHeight() + 120;
    setContentHeight (y);
}

//==============================================================================
// SourcePage
//==============================================================================
SourcePage::SourcePage (AppContext& c) : ScrollPage (c) {}

juce::TextEditor* SourcePage::addField (const juce::String& label, const juce::String& key, bool password)
{
    auto* ed = new juce::TextEditor();
    ed->setTextToShowWhenEmpty (label, ctx.theme().textDim());
    ed->setFont (style::font (14.0f));
    ed->setIndents (12, 0);
    ed->setJustification (juce::Justification::centredLeft);
    if (password) ed->setPasswordCharacter ((juce::juce_wchar) 0x2022);
    else ed->setText (ctx.providers().getCredentials().get (sourceId, key), false);
    ed->onTextChange = [this, ed, key, password] { if (! password) ctx.providers().getCredentials().set (sourceId, key, ed->getText().trim()); };
    ed->setName (key);
    rows.add (ed);
    rowHeights.push_back (36);
    contentComp.addAndMakeVisible (ed);
    return ed;
}

juce::TextButton* SourcePage::addButton (const juce::String& text, std::function<void()> fn)
{
    auto* b = new juce::TextButton (text);
    b->onClick = std::move (fn);
    rows.add (b);
    rowHeights.push_back (38);
    contentComp.addAndMakeVisible (b);
    return b;
}

void SourcePage::addText (const juce::String& text, bool dim)
{
    auto* l = new juce::Label ({}, text);
    l->setFont (style::font (14.0f));
    l->setColour (juce::Label::textColourId, dim ? ctx.theme().textDim() : ctx.theme().textCol());
    l->setJustificationType (juce::Justification::topLeft);
    l->setMinimumHorizontalScale (1.0f);
    rows.add (l);
    rowHeights.push_back (juce::jmax (24, 22 * (1 + text.length() / 110 + juce::StringArray::fromLines (text).size() - 1)));
    contentComp.addAndMakeVisible (l);
}

void SourcePage::setSource (const juce::String& id, const juce::String& arg)
{
    sourceId = id;
    sourceArg = arg;
    rows.clear();
    rowHeights.clear();
    tracks.reset();
    albums.reset();
    contentComp.removeAllChildren();

    if (id == "local") buildLocal();
    else if (id == "upnp") buildUpnp();
    else if (auto* p = ctx.providers().getProvider (id)) buildProvider (*p);
    layoutContent (getWidth() - viewport.getScrollBarThickness());
    repaint();
}

void SourcePage::buildLocal()
{
    heading = "Fichiers locaux";
    description = "Disques internes, disques externes et partages réseau montés (SMB / NFS / AFP). Les dossiers sont surveillés : "_t
                  "ajouts, suppressions et modifications apparaissent tout seuls."_t;
    for (auto& f : ctx.library().getFolders())
    {
        auto* row = addButton ("Retirer  " + f, nullptr);
        row->onClick = [this, f]
        {
            ctx.library().removeFolder (juce::File (f));
            ctx.library().notifyChanged();
            setSource ("local");
        };
    }
    addButton ("Ajouter un dossier…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Dossier de musique", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this, chooser] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            ctx.library().addFolder (dir);
            ctx.scanner().requestFullScan();
            ctx.showMessage ("Indexation de " + dir.getFileName() + "…"_t);
            setSource ("local");
        });
    });
    addButton ("Réindexer maintenant"_t, [this] { ctx.scanner().requestFullScan(); ctx.showMessage ("Réindexation lancée"_t); });
    const auto& pr = ctx.scanner().getProgress();
    addText (juce::String (ctx.library().countTracks ("local")) + " morceaux indexés"_t + (pr.scanning.load() ? " · indexation en cours…"_t : ""));
    albums = std::make_unique<AlbumGrid> (ctx);
    albums->setAlbums (ctx.library().getAlbums ("artist", 2000, "local"));
    albums->onOpen = [this] (const AlbumInfo& a) { ctx.navigate ({ PageId::AlbumDetail, a.artist, a.title }); };
    albums->onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getAlbumTracks (a.artist, a.title), 0); };
    contentComp.addAndMakeVisible (*albums);
}

juce::String SourcePage::upnpArg (const juce::String& udn, const juce::String& objectId, const juce::StringArray& path)
{
    return udn + "\x1f" + objectId + "\x1f" + path.joinIntoString ("\x1e");
}

void SourcePage::buildUpnp()
{
    heading = "NAS (UPnP / DLNA)";
    description = "Serveurs de musique du réseau local (MinimServer, Asset, Synology, QNAP, JRiver, Jellyfin…). Les morceaux sont lus en flux HTTP "_t
                  "par le moteur de NX Player, donc en bit-perfect. Pour un partage SMB, ajoutez simplement le dossier monté dans Fichiers locaux."_t;
    auto* upnp = dynamic_cast<UpnpProvider*> (ctx.providers().getProvider ("upnp"));
    if (upnp == nullptr) return;
    juce::Component::SafePointer<SourcePage> safe (this);
    auto* app = &ctx;

    const auto parts = juce::StringArray::fromTokens (sourceArg, "\x1f", {});
    if (sourceArg.isNotEmpty() && parts.size() >= 2)
    {
        // ---- inside a server folder -----------------------------------------------------------
        const auto udn = parts[0], objectId = parts[1];
        const auto path = juce::StringArray::fromTokens (parts[2], "\x1e", {});
        UpnpServer server;
        for (auto& sv : upnp->getServers()) if (sv.udn == udn) server = sv;
        if (server.udn.isEmpty()) { addText ("Ce serveur n'est plus disponible : relancez la recherche."_t); return; }
        heading = path.isEmpty() ? server.name : path[path.size() - 1];
        description = path.size() > 1 ? path.joinIntoString ("  ›  "_t, 0, path.size() - 1) : server.name;

        const auto key = udn + "|" + objectId;
        auto it = upnpCache.find (key);
        if (it == upnpCache.end())
        {
            addText ("Chargement du dossier…"_t);
            if (! upnpLoading.contains (key))
            {
                upnpLoading.add (key);
                const auto arg = sourceArg;
                juce::Thread::launch ([safe, upnp, server, objectId, key, arg]
                {
                    auto entries = upnp->browseAll (server, objectId);
                    const auto err = upnp->lastError;
                    juce::MessageManager::callAsync ([safe, entries, key, arg, err]
                    {
                        if (safe == nullptr) return;
                        safe->upnpLoading.removeString (key);
                        safe->upnpCache[key] = entries;
                        if (entries.empty() && err.isNotEmpty()) safe->ctx.showMessage (err);
                        if (safe->sourceId == "upnp" && safe->sourceArg == arg) safe->setSource ("upnp", arg);
                    });
                });
            }
            return;
        }

        const auto& entries = it->second;
        std::vector<TrackInfo> items;
        std::vector<AlbumInfo> folders;
        for (auto& e : entries)
        {
            if (! e.isContainer) { items.push_back (e.track); continue; }
            AlbumInfo a;
            a.title = e.title;
            a.artist = e.track.artist.isNotEmpty() ? e.track.artist
                     : (e.childCount >= 0 ? juce::String (e.childCount) + " éléments"_t : juce::String ("Dossier"));
            a.artworkKey = e.track.artworkKey;
            a.source = e.id;
            folders.push_back (a);
        }

        addButton (items.empty() ? juce::String ("Tout lire (sous-dossiers compris)"_t) : juce::String ("Lire ce dossier"), [safe, upnp, server, objectId, app]
        {
            app->showMessage ("Préparation de la lecture…"_t);
            juce::Thread::launch ([safe, upnp, server, objectId, app]
            {
                auto all = upnp->collectTracks (server, objectId, 5000);
                juce::MessageManager::callAsync ([safe, all, app]
                {
                    if (safe == nullptr) return;
                    if (all.empty()) app->showMessage ("Aucun morceau dans ce dossier"_t);
                    else app->playTracks (all, 0);
                });
            });
        });
        addButton ("Importer dans la bibliothèque"_t, [safe, upnp, server, objectId, app]
        {
            auto* lib = &app->library();
            juce::Thread::launch ([safe, app, lib, upnp, server, objectId]
            {
                const int n = upnp->importContainer (*lib, server, objectId);
                juce::MessageManager::callAsync ([safe, app, n] { if (safe != nullptr) app->showMessage (juce::String (n) + " morceaux importés depuis le NAS"_t); });
            });
        });
        addText (juce::String ((int) folders.size()) + " dossier(s) · "_t + juce::String ((int) items.size()) + " morceau(x)"_t);

        if (! folders.empty())
        {
            albums = std::make_unique<AlbumGrid> (ctx);
            albums->setAlbums (folders);
            albums->onOpen = [this, udn, path] (const AlbumInfo& a)
            {
                auto deeper = path;
                deeper.add (a.title);
                ctx.navigate ({ PageId::Source, "upnp", upnpArg (udn, a.source, deeper) });
            };
            albums->onPlay = [safe, upnp, server, app] (const AlbumInfo& a)
            {
                const auto id = a.source;
                juce::Thread::launch ([safe, upnp, server, id, app]
                {
                    auto all = upnp->collectTracks (server, id, 5000);
                    juce::MessageManager::callAsync ([safe, all, app] { if (safe != nullptr && ! all.empty()) app->playTracks (all, 0); });
                });
            };
            contentComp.addAndMakeVisible (*albums);
        }
        if (! items.empty())
        {
            tracks = std::make_unique<TrackList> (ctx);
            tracks->setTracks (items, true, true, true);
            contentComp.addAndMakeVisible (*tracks);
        }
        return;
    }

    // ---- server list ----------------------------------------------------------------------------
    addButton ("Rechercher les serveurs du réseau"_t, [safe, upnp, app]
    {
        app->showMessage ("Recherche des serveurs UPnP / DLNA…"_t);
        juce::Thread::launch ([safe, upnp, app]
        {
            const auto found = upnp->discover (3000);
            const auto err = upnp->lastError;
            juce::MessageManager::callAsync ([safe, app, found, err]
            {
                if (safe == nullptr) return;
                app->saveNasServers();
                app->showMessage (found.empty() ? err : juce::String ((int) found.size()) + " serveur(s) trouvé(s)"_t);
                safe->setSource ("upnp");
            });
        });
    });

    auto* address = addField ("Adresse IP du NAS ou URL de description (ex. 192.168.1.20)"_t, "__nas_address", true);
    address->setPasswordCharacter (0);
    auto connect = [safe, upnp, app, address]
    {
        auto text = address->getText().trim();
        if (text.isEmpty()) return;
        app->showMessage ("Connexion à "_t + text + "…"_t);
        juce::Thread::launch ([safe, upnp, app, text]
        {
            const auto before = upnp->getServers().size();
            bool ok;
            if (text.containsChar ('/')) ok = upnp->addServerFromLocation (text.startsWith ("http") ? text : "http://" + text);
            else ok = upnp->discoverHost (text.upToFirstOccurrenceOf (":", false, false)).size() > before;
            const auto err = upnp->lastError;
            juce::MessageManager::callAsync ([safe, app, ok, err]
            {
                if (safe == nullptr) return;
                app->saveNasServers();
                app->showMessage (ok ? juce::String ("Serveur ajouté"_t) : err);
                safe->setSource ("upnp");
            });
        });
    };
    address->onReturnKey = connect;
    addButton ("Ajouter ce serveur"_t, connect);

    const auto servers = upnp->getServers();
    if (servers.empty())
    {
        addText ("Aucun serveur trouvé pour l'instant. Vérifiez que le serveur UPnP / DLNA du NAS est activé (MinimServer, "_t
                 "Synology Media Server, Asset…) ; si votre réseau filtre la découverte automatique, entrez l'adresse IP du NAS ci-dessus."_t);
        return;
    }
    std::vector<AlbumInfo> cards;
    for (auto& sv : servers)
    {
        AlbumInfo a;
        a.title = sv.name;
        a.artist = juce::URL (sv.location).getDomain();
        a.artworkKey = sv.iconUrl;
        a.source = sv.udn;
        cards.push_back (a);
    }
    albums = std::make_unique<AlbumGrid> (ctx);
    albums->setAlbums (cards);
    albums->onOpen = [this] (const AlbumInfo& a) { ctx.navigate ({ PageId::Source, "upnp", upnpArg (a.source, "0", { a.title }) }); };
    albums->onPlay = albums->onOpen;
    contentComp.addAndMakeVisible (*albums);
}

void SourcePage::buildProvider (MediaProvider& p)
{
    heading = p.getDisplayName();
    const auto id = p.getId();
    if (id == "qobuz")
    {
        description = "FLAC jusqu'à 24 bits / 192 kHz, lu par le moteur bit-perfect de NX Player (changement automatique de fréquence du DAC). "_t
                      "L'API Qobuz demande des identifiants d'application partenaire (app_id / app_secret) délivrés par Qobuz."_t;
        if (CredentialStore::builtIn ("qobuz", "app_id").isEmpty())
        {
            addField ("app_id (partenaire Qobuz)", "app_id");
            addField ("app_secret", "app_secret", false);
        }
        auto* email = addField ("E-mail du compte Qobuz", "email");
        auto* password = addField ("Mot de passe", "password_input", true);
        addButton (p.isSignedIn() ? "Se reconnecter" : "Se connecter", [this, &p, email, password]
        {
            juce::String err;
            if (static_cast<QobuzProvider&> (p).signIn (email->getText(), password->getText(), err)) ctx.showMessage ("Qobuz connecté"_t);
            else ctx.showMessage ("Qobuz : " + err);
            setSource ("qobuz");
        });
    }
    else if (id == "tidal")
    {
        description = "FLAC (et FLAC Max jusqu'à 24 / 192 en DASH) lu par le moteur bit-perfect. Connexion par code sur link.tidal.com "_t
                      "avec un client_id délivré par Tidal (programme développeurs). Les flux chiffrés (DRM) ne sont pas pris en charge."_t;
        if (CredentialStore::builtIn ("tidal", "client_id").isEmpty())
        {
            addField ("client_id (Tidal)", "client_id");
            addField ("client_secret (facultatif)", "client_secret");
        }
        addButton (p.isSignedIn() ? "Se reconnecter" : "Se connecter", [this, &p]
        {
            auto& tidal = static_cast<TidalProvider&> (p);
            TidalProvider::DeviceLogin login;
            juce::String err;
            if (! tidal.beginDeviceLogin (login, err)) { ctx.showMessage ("Tidal : " + err); return; }
            juce::URL (login.verificationUri).launchInDefaultBrowser();
            ctx.showMessage ("Tidal : entrez le code " + login.userCode + " sur " + login.verificationUri);
            juce::Component::SafePointer<SourcePage> safe (this);
            juce::Thread::launch ([safe, &tidal, login]
            {
                for (int i = 0; i * login.interval < login.expiresIn; ++i)
                {
                    juce::Thread::sleep (login.interval * 1000);
                    juce::String e;
                    if (tidal.pollDeviceLogin (login, e))
                    {
                        juce::MessageManager::callAsync ([safe] { if (safe) { safe->ctx.showMessage ("Tidal connecté"_t); safe->setSource ("tidal"); } });
                        return;
                    }
                    if (e != "authorization_pending" && e != "slow_down") break;
                }
            });
        });
    }
    else if (id == "spotify")
    {
        description = "Recherche, bibliothèque et playlists via l'API Web officielle de Spotify (connexion PKCE dans votre navigateur, "_t
                      "redirection http://127.0.0.1:43821/callback). La lecture est confiée à votre appareil Spotify Connect : Spotify ne "_t
                      "délivre pas de flux audio aux applications tierces, ces morceaux ne passent donc pas par le moteur bit-perfect."_t;
        if (CredentialStore::builtIn ("spotify", "client_id").isEmpty())
            addField ("client_id (developer.spotify.com, redirection http://127.0.0.1:43821/callback)", "client_id");
        addButton (p.isSignedIn() ? "Se reconnecter" : "Se connecter (navigateur)", [this, &p]
        {
            juce::Component::SafePointer<SourcePage> safe (this);
            juce::Thread::launch ([safe, &p]
            {
                juce::String err;
                const bool ok = static_cast<SpotifyProvider&> (p).signIn (err);
                juce::MessageManager::callAsync ([safe, ok, err] { if (safe) { safe->ctx.showMessage (ok ? juce::String ("Spotify connecté"_t) : "Spotify : " + err); safe->setSource ("spotify"); } });
            });
        });
    }
    else if (id == "youtube")
    {
        description = "Recherche et playlists via l'API YouTube Data v3 (clé d'API Google). La lecture s'ouvre sur music.youtube.com : "_t
                      "les conditions de YouTube interdisent d'extraire l'audio, ces morceaux ne passent donc pas par le moteur."_t;
        addField ("Clé d'API YouTube Data v3"_t, "api_key");
        addField ("Identifiants de playlists publiques (séparés par des virgules)"_t, "playlists");
    }

    addText ("État : "_t + p.getStatusText() + (p.lastError.isNotEmpty() ? "  ·  "_t + p.lastError : juce::String()), false);

    if (p.isSignedIn())
    {
        juce::Component::SafePointer<SourcePage> safe (this);
        auto* provider = &p;
        if (id == "spotify")
        {
            // Spotify browser: search, liked tracks, playlists. Double-click plays through Spotify Connect.
            auto* spotify = static_cast<SpotifyProvider*> (provider);
            auto showTracks = [safe] (std::vector<TrackInfo> list, juce::String error, juce::String what)
            {
                juce::MessageManager::callAsync ([safe, list = std::move (list), error, what]
                {
                    if (safe == nullptr || safe->tracks == nullptr) return;
                    safe->tracks->setTracks (list, true, true);
                    safe->ctx.showMessage (list.empty() ? (error.isNotEmpty() ? error : what + " : aucun morceau"_t)
                                                        : what + " : " + juce::String ((int) list.size()) + " morceaux"_t);
                    safe->layoutContent (safe->getWidth() - safe->viewport.getScrollBarThickness());
                });
            };

            auto* search = addField ("Rechercher sur Spotify (Entrée)"_t, "__search", true);
            search->setPasswordCharacter (0);
            search->onReturnKey = [search, spotify, showTracks]
            {
                const auto q = search->getText().trim();
                if (q.isEmpty()) return;
                juce::Thread::launch ([spotify, showTracks, q] { auto r = spotify->searchTracks (q, 30); showTracks (std::move (r), spotify->lastError, "« "_t + q + " »"_t); });
            };

            auto* lists = new juce::ComboBox();
            lists->setTextWhenNothingSelected ("Chargement de vos playlists…"_t);
            rows.add (lists);
            rowHeights.push_back (36);
            contentComp.addAndMakeVisible (lists);
            auto playlistIds = std::make_shared<juce::StringArray>();
            lists->onChange = [lists, spotify, showTracks, playlistIds]
            {
                const int idx = lists->getSelectedItemIndex();
                if (idx < 0) return;
                const auto name = lists->getText();
                const auto plId = (*playlistIds)[idx];
                juce::Thread::launch ([spotify, showTracks, plId, name]
                {
                    auto r = plId == "__liked" ? spotify->getLikedTracks() : spotify->getPlaylistTracks (plId);
                    showTracks (std::move (r), spotify->lastError, name);
                });
            };
            juce::Component::SafePointer<juce::ComboBox> safeList (lists);
            juce::Thread::launch ([spotify, safeList, playlistIds, showTracks]
            {
                auto pls = spotify->getPlaylists();
                const auto err = spotify->lastError;
                auto liked = spotify->getLikedTracks (200);
                const auto likedErr = spotify->lastError;
                juce::MessageManager::callAsync ([safeList, playlistIds, pls, err]
                {
                    if (safeList == nullptr) return;
                    safeList->clear (juce::dontSendNotification);
                    playlistIds->clear();
                    safeList->addItem ("Titres likés"_t, 1);
                    playlistIds->add ("__liked");
                    for (auto& pl : pls)
                    {
                        safeList->addItem (pl.name + (pl.trackCount > 0 ? "  (" + juce::String (pl.trackCount) + ")" : juce::String()), playlistIds->size() + 1);
                        playlistIds->add (pl.externalId);
                    }
                    safeList->setTextWhenNothingSelected (pls.empty() && err.isNotEmpty() ? err : juce::String ("Choisir une playlist…"_t));
                });
                showTracks (std::move (liked), likedErr, "Titres likés"_t);
            });
            addText ("Double-cliquez sur un morceau : il est lancé sur votre appareil Spotify (application Spotify ouverte, "_t
                     "compte Premium nécessaire pour le pilotage à distance ; sinon le morceau s'ouvre dans Spotify)."_t);
        }
        addButton ("Importer mes playlists dans la bibliothèque"_t, [safe, provider]
        {
            if (safe == nullptr) return;
            juce::Thread::launch ([safe, provider]
            {
                const auto lists = provider->getPlaylists();
                juce::MessageManager::callAsync ([safe, provider, lists]
                {
                    if (safe == nullptr) return;
                    for (auto& pl : lists)
                        safe->ctx.providers().importPlaylist (provider->getId(), pl, [safe, name = pl.name] (int n)
                        {
                            if (safe) safe->ctx.showMessage (name + " : " + juce::String (n) + " morceaux");
                        });
                    if (lists.empty()) safe->ctx.showMessage (provider->lastError.isNotEmpty() ? provider->lastError : juce::String ("Aucune playlist"_t));
                });
            });
        });
        tracks = std::make_unique<TrackList> (ctx);
        if (id != "spotify") tracks->setTracks (ctx.library().getTracks ("artist", 3000, id), true, true);
        contentComp.addAndMakeVisible (*tracks);
    }
}

void SourcePage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (100, width - pad * 2);
    int y = 24 + 54;
    // description height
    juce::AttributedString as;
    as.append (description, style::font (14.0f));
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (as, (float) inner);
    y += (int) tl.getHeight() + 30;

    for (int i = 0; i < rows.size(); ++i)
    {
        const int h = rowHeights[(size_t) i];
        const int w = dynamic_cast<juce::TextButton*> (rows[i]) != nullptr ? juce::jmin (inner, 520) : juce::jmin (inner, 620);
        rows[i]->setBounds (pad, y, dynamic_cast<juce::Label*> (rows[i]) != nullptr ? inner : w, h);
        y += h + 10;
    }
    y += 10;
    if (albums != nullptr)
    {
        const int h = albums->heightForWidth (inner);
        albums->setBounds (pad, y, inner, h);
        y += h + 20;
    }
    if (tracks != nullptr)
    {
        tracks->setBounds (pad - 10, y, inner + 20, tracks->getTotalHeight());
        y += tracks->getTotalHeight();
    }
    setContentHeight (y + 120);

    struct Head : juce::Component
    {
        SourcePage& page;
        explicit Head (SourcePage& p) : page (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            drawPageTitle (g, page.ctx.theme(), getLocalBounds().removeFromTop (54), page.heading);
            juce::AttributedString as;
            as.append (page.description, style::font (14.0f), page.ctx.theme().textDim());
            as.setWordWrap (juce::AttributedString::byWord);
            juce::TextLayout tl;
            tl.createLayout (as, (float) getWidth());
            tl.draw (g, getLocalBounds().toFloat().withTrimmedTop (60.0f));
        }
    };
    if (contentComp.findChildWithID ("head") == nullptr)
    {
        auto* h = new Head (*this);
        h->setComponentID ("head");
        rows.add (h);
        rowHeights.push_back (0);
        contentComp.addAndMakeVisible (h);
    }
    if (auto* h = contentComp.findChildWithID ("head")) h->setBounds (pad, 24, inner, 60 + (int) tl.getHeight() + 10);
}

//==============================================================================
// SettingsPage
//==============================================================================
SettingsPage::SettingsPage (AppContext& c) : ScrollPage (c)
{
    contentComp.addAndMakeVisible (labels);
    refresh();
}

void SettingsPage::section (const juce::String& title)
{
    Row r;
    r.label = title;
    r.isSection = true;
    r.height = 48;
    layoutRows.push_back (r);
}

void SettingsPage::row (const juce::String& label, juce::Component* control, int height, const juce::String& help)
{
    Row r;
    r.label = label;
    r.help = help;
    r.control = control;
    r.height = height;
    if (control != nullptr) { owned.add (control); contentComp.addAndMakeVisible (control); }
    layoutRows.push_back (r);
}

void SettingsPage::refresh()
{
    for (auto* c : owned) contentComp.removeChildComponent (c);
    owned.clear();
    layoutRows.clear();

    auto combo = [this] (const juce::StringArray& items, const juce::String& selected, std::function<void (const juce::String&)> fn)
    {
        auto* cb = new juce::ComboBox();
        cb->addItemList (items, 1);
        cb->setText (selected, juce::dontSendNotification);
        cb->onChange = [cb, fn] { fn (cb->getText()); };
        return cb;
    };
    auto toggle = [] (bool on, std::function<void (bool)> fn)
    {
        auto* t = new juce::ToggleButton();
        t->setToggleState (on, juce::dontSendNotification);
        t->onClick = [t, fn] { fn (t->getToggleState()); };
        return t;
    };
    auto button = [] (const juce::String& text, std::function<void()> fn) { auto* b = new juce::TextButton (text); b->onClick = std::move (fn); return b; };

    auto& eng = ctx.engine();
    auto s = eng.getSettings();

    section ("Sortie audio");
    row ("Pilote", combo (ctx.getBackendNames(), ctx.getCurrentBackendName(), [this] (const juce::String& n) { ctx.selectBackend (n); refresh(); }), 36,
         "ASIO et WASAPI exclusif (Windows), CoreAudio + Hog Mode (macOS), ALSA hw (Linux) contournent le mixeur du système."_t);
    row ("Périphérique"_t, combo (eng.getBackend().getDeviceNames(), eng.getDeviceName(), [this] (const juce::String& n) { ctx.selectDevice (n); refresh(); }));
    row ("Accès exclusif (Hog Mode / exclusif)"_t, toggle (s.exclusive, [this] (bool v) { auto st = ctx.engine().getSettings(); st.exclusive = v; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }));
    {
        auto* buf = combo ({ "Automatique", "10 ms", "20 ms", "50 ms", "100 ms", "200 ms" },
                           s.bufferMilliseconds == 0 ? juce::String ("Automatique") : juce::String (s.bufferMilliseconds) + " ms",
                           [this] (const juce::String& v) { auto st = ctx.engine().getSettings(); st.bufferMilliseconds = v.getIntValue(); ctx.engine().setSettings (st); ctx.applyEngineSettings(); });
        row ("Tampon du périphérique"_t, buf);
    }
    const auto status = eng.getOutputStatus();
    row (status.open ? "État : "_t + status.backendName + " · "_t + formatRate (status.sampleRate) + " · "_t + status.detail : juce::String ("État : périphérique fermé (s'ouvre à la lecture)"_t), nullptr, 28);

    section ("DSD");
    const juce::StringArray modes { "Automatique (natif, puis DoP, puis PCM)", "DSD natif (ASIO)", "DoP (DSD over PCM)", "Conversion en PCM (DXD)" };
    row ("Mode DSD", combo (modes, modes[(int) s.dsdMode], [this, modes] (const juce::String& v)
    {
        auto st = ctx.engine().getSettings();
        st.dsdMode = (DsdMode) juce::jmax (0, modes.indexOf (v));
        ctx.engine().setSettings (st);
        ctx.applyEngineSettings();
    }));
    row ("Autoriser DoP"_t, toggle (s.dopAllowed, [this] (bool v) { auto st = ctx.engine().getSettings(); st.dopAllowed = v; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }), 36,
         "Le DAC doit reconnaître les marqueurs DoP (0x05 / 0xFA) ; sinon il joue du bruit à faible niveau."_t);
    row ("Gain DSD -> PCM", combo ({ "0 dB (sans écrêtage)"_t, "+6 dB (SACD 0 dB = 0 dBFS)" }, s.dsdToPcmGainDb > 3.0 ? "+6 dB (SACD 0 dB = 0 dBFS)" : "0 dB (sans écrêtage)"_t,
         [this] (const juce::String& v) { auto st = ctx.engine().getSettings(); st.dsdToPcmGainDb = v.startsWith ("+6") ? 6.0 : 0.0; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }));

    section ("Traitement"_t);
    row ("Suréchantillonnage sinc vers DXD (352,8 / 384 kHz)"_t, toggle (s.upsampleToDxd, [this] (bool v) { auto st = ctx.engine().getSettings(); st.upsampleToDxd = v; ctx.engine().setSettings (st); ctx.applyEngineSettings(); }), 36,
         "Filtre polyphase 64 bits, 160 dB. Rapport entier (×2, ×4, ×8) dans la même famille de fréquences. Désactive le bit-perfect."_t);
    row ("Volume numérique : 100 % = bit-perfect"_t, nullptr, 28);

    section ("Paroles");
    row ("Chercher les paroles synchronisées en ligne (LRCLIB)"_t, toggle ((bool) ctx.settings().get ("lyricsOnline", true), [this] (bool v) { ctx.settings().set ("lyricsOnline", v); }), 36,
         "Artiste, titre, album et durée sont envoyés à lrclib.net. Les fichiers .lrc et les paroles des tags sont toujours prioritaires."_t);

    section ("Thèmes"_t);
    for (auto& t : ctx.themes().getThemes())
    {
        const bool current = t.id == ctx.theme().id;
        auto* b = button (current ? "Actif" : "Appliquer", [this, id = t.id] { ctx.applyTheme (id); refresh(); });
        b->setToggleState (current, juce::dontSendNotification);
        row (t.name + "  ·  "_t + t.author, b, 38, t.description);
    }
    row ("Importer / exporter", button ("Importer un thème…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Thème NX (.nxtheme)"_t, juce::File(), "*.nxtheme;*.zip");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File()) return;
            juce::String err;
            const auto id = ctx.themes().importTheme (fc.getResult(), err);
            if (id.isEmpty()) ctx.showMessage ("Import impossible : " + err);
            else { ctx.applyTheme (id); refresh(); }
        });
    }));
    row ({}, button ("Exporter le thème actif…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Exporter", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (ctx.theme().id + ".nxtheme"), "*.nxtheme");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode, [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResult() != juce::File() && ctx.themes().exportTheme (ctx.theme().id, fc.getResult())) ctx.showMessage ("Thème exporté"_t);
        });
    }), 38, "Les thèmes se modifient à chaud : éditez theme.css pendant que NX Player tourne."_t);

    section ("Plug-ins d'interface");
    for (auto& p : ctx.plugins().getUiPlugins())
        row (p.name + "  ·  v"_t + p.version, toggle (p.enabled, [this, id = p.id] (bool v) { ctx.plugins().setUiPluginEnabled (id, v); }), 36,
             p.description + "  (emplacements : " + p.slots.joinIntoString (", ") + ")");

    section ("Plug-ins DSP");
    for (auto& p : ctx.plugins().getDspPlugins())
    {
        row (p.name, toggle (p.enabled, [this, id = p.id] (bool v) { ctx.plugins().setDspPluginEnabled (id, v); }), 36,
             p.error.isNotEmpty() ? "Indisponible : " + p.error : p.description);
        for (int i = 0; i < (int) p.params.size(); ++i)
        {
            const auto& prm = p.params[(size_t) i];
            auto* sl = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            sl->setRange (prm.min_value, prm.max_value, (prm.max_value - prm.min_value) / 200.0);
            sl->setValue (ctx.plugins().getDspParameter (p.id, i), juce::dontSendNotification);
            sl->setTextValueSuffix (juce::String (" ") + juce::String::fromUTF8 (prm.unit));
            sl->setColour (juce::Slider::textBoxTextColourId, ctx.theme().textCol());
            sl->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            sl->onDragEnd = [this, sl, id = p.id, i] { ctx.plugins().setDspParameter (id, i, sl->getValue()); };
            row ("   " + juce::String::fromUTF8 (prm.name), sl, 32);
        }
    }

    section ("À propos"_t);
    row ("NX Player " NX_VERSION " · NX Audio Suite · licence MIT"_t, nullptr, 26);
    row ("Utilise JUCE (AGPLv3 / commercial), SQLite (domaine public), Apple ALAC (Apache 2.0). Voir THIRD_PARTY_NOTICES.md.", nullptr, 26);

    layoutContent (getWidth() - viewport.getScrollBarThickness());
}

void SettingsPage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (200, width - pad * 2);
    int y = 24 + 60;
    painted.clear();
    for (auto& r : layoutRows)
    {
        if (r.isSection)
        {
            painted.push_back ({ { pad, y, inner, r.height }, r });
            y += r.height;
            continue;
        }
        const int controlW = juce::jmin (380, inner / 2);
        auto rowArea = juce::Rectangle<int> (pad, y, inner, r.height + (r.help.isNotEmpty() ? 20 : 0));
        if (r.control != nullptr)
            r.control->setBounds (rowArea.getRight() - controlW, y, controlW, r.height);
        painted.push_back ({ rowArea.withTrimmedRight (r.control != nullptr ? controlW + 20 : 0), r });
        y += rowArea.getHeight() + 10;
    }
    labels.setBounds (0, 0, width, y + 80);
    setContentHeight (y + 80);
}

void SettingsPage::Labels::paint (juce::Graphics& g)
{
    const auto& t = p.ctx.theme();
    drawPageTitle (g, t, { 32, 24, getWidth() - 64, 54 }, "Réglages"_t);
    for (auto& [area, r] : p.painted)
    {
        if (r.isSection)
        {
            g.setColour (t.accent());
            g.setFont (style::font (12.5f, true));
            g.drawText (style::upper (r.label), area.withTrimmedTop (18), juce::Justification::centredLeft);
            g.setColour (t.border());
            g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
            continue;
        }
        auto a = area;
        g.setColour (t.textCol());
        g.setFont (style::font (14.0f));
        g.drawFittedText (r.label, a.removeFromTop (r.height), juce::Justification::centredLeft, 2);
        if (r.help.isNotEmpty())
        {
            g.setColour (t.textDim());
            g.setFont (style::font (12.0f));
            g.drawFittedText (r.help, a.withHeight (20), juce::Justification::centredLeft, 2);
        }
    }
}

} // namespace nx
