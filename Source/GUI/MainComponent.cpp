// NX Player — MIT License
#include "MainComponent.h"
#include "../Ext/PluginManager.h"
#include "../Engine/DeviceProfiles.h"
#include "../Library/ArtistInfo.h"
#include "../Remote/RemoteServer.h"
#include "LibraryActions.h"
#include "Dashboard.h"

namespace nx
{

//==============================================================================
void QueuePanel::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), t.radius(), true);
    auto r = getLocalBounds().reduced (16);
    g.setColour (t.textCol());
    g.setFont (style::font (18.0f, true));
    g.drawText ("File d'attente"_t, r.removeFromTop (30), juce::Justification::centredLeft);
    r.removeFromTop (8);
    const auto& q = ctx.engine().getQueue();
    const int cur = ctx.engine().getCurrentIndex();
    g.saveState();
    g.reduceClipRegion (r);
    int y = r.getY() - (int) scroll;
    for (int i = 0; i < (int) q.size(); ++i)
    {
        auto row = juce::Rectangle<int> (r.getX(), y, r.getWidth(), 44);
        if (i == cur)
        {
            g.setColour (t.accent().withAlpha (0.15f));
            g.fillRoundedRectangle (row.toFloat(), 8.0f);
        }
        auto tr = row.reduced (10, 4);
        g.setColour (i == cur ? t.accent() : t.textCol());
        g.setFont (style::font (13.5f, true));
        g.drawText (q[(size_t) i].title, tr.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (12.0f));
        g.drawText (q[(size_t) i].artist + "  ·  "_t + q[(size_t) i].sourceName, tr, juce::Justification::centredLeft, true);
        y += 46;
    }
    if (q.empty())
    {
        g.setColour (t.textDim());
        g.setFont (style::font (14.0f));
        g.drawText ("Vide"_t, r.removeFromTop (30), juce::Justification::centredLeft);
    }
    g.restoreState();
}

void QueuePanel::mouseUp (const juce::MouseEvent& e)
{
    const int i = (int) ((float) (e.y - 16 - 38) + scroll) / 46;
    if (i >= 0 && i < (int) ctx.engine().getQueue().size()) ctx.engine().playIndex (i);
    repaint();
}

//==============================================================================
void Toast::show (const juce::String& text)
{
    message = text;
    alpha = 1.0f;
    shownAt = juce::Time::getMillisecondCounterHiRes();
    setVisible (true);
    toFront (false);
    repaint();
    startTimerHz (30);
}

void Toast::timerCallback()
{
    const double age = (juce::Time::getMillisecondCounterHiRes() - shownAt) / 1000.0;
    alpha = age < 3.5 ? 1.0f : juce::jmax (0.0f, 1.0f - (float) (age - 3.5) / 0.5f);
    repaint();
    if (alpha <= 0.0f) { stopTimer(); setVisible (false); }
}

void Toast::paint (juce::Graphics& g)
{
    if (message.isEmpty()) return;
    const auto& t = ctx.theme();
    g.setFont (style::font (14.0f, true));
    const float w = juce::jmin ((float) getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), message) + 48.0f);
    auto r = juce::Rectangle<float> (w, (float) getHeight()).withCentre (getLocalBounds().toFloat().getCentre());
    g.setOpacity (alpha);
    g.setColour (t.surface().withAlpha (0.95f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (t.accent().withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    g.setColour (t.textCol());
    g.drawText (message, r.reduced (16, 0), juce::Justification::centred, true);
}

//==============================================================================
MainComponent::MainComponent (AppContext& c)
    : background (c), sidebar (c), player (c),
      sidebarButton (style::Icon::SidebarToggle, "Afficher / masquer la barre latérale"_t, c),
      backButton (style::Icon::ChevronLeft, "Précédent"_t, c),
      forwardButton (style::Icon::ChevronRight, "Suivant"_t, c),
      remoteButton (style::Icon::Phone, "NX Remote : piloter depuis le téléphone (QR code)"_t, c),
      settingsButton (style::Icon::Settings, "Réglages (Cmd/Ctrl + ,)"_t, c),
      ctx (c), listing (c), nowPlaying (c), sourcePage (c), marketplace (c), queue (c), toast (c),
      settingsWindow (c), immersive (c), onboarding (c)
{
    homeSlot = std::make_unique<PluginSlot> (c, "home.top");
    addAndMakeVisible (background);
    addAndMakeVisible (sidebar);
    addChildComponent (listing);
    addChildComponent (nowPlaying);
    addChildComponent (sourcePage);
    addChildComponent (marketplace);
    for (auto* b : { &sidebarButton, &backButton, &forwardButton, &remoteButton, &settingsButton }) addAndMakeVisible (b);
    sidebarButton.onClick = [this] { setSidebarVisible (! sidebarVisible); };
    remoteButton.onClick = [this] { showRemote(); };
    settingsButton.onClick = [this] { openSettings ({}); };
    backButton.onClick = [this] { goBack(); };
    forwardButton.onClick = [this] { goForward(); };
    sidebarVisible = (bool) ctx.settings().get ("sidebarVisible", true);
    sidebarAmount = sidebarVisible ? 1.0f : 0.0f;
    sidebar.setVisible (sidebarVisible);
    sidebarAnim.tick = [this]
    {
        const float target = sidebarVisible ? 1.0f : 0.0f;
        const float step = (1.0f / 60.0f) / 0.2f * ctx.theme().number ("--nx-animation-speed", 1.0f);
        sidebarAmount = sidebarAmount < target ? juce::jmin (target, sidebarAmount + step) : juce::jmax (target, sidebarAmount - step);
        if (sidebarAmount == target) { sidebarAnim.stopTimer(); sidebar.setVisible (sidebarVisible); }
        resized();
    };
    addAndMakeVisible (player);
    addChildComponent (queue);
    addChildComponent (immersive);
    addChildComponent (settingsWindow);
    addChildComponent (onboarding);
    addChildComponent (toast);
    settingsWindow.onClose = [this] { grabKeyboardFocus(); };
    immersive.onClose = [this]
    {
        if (auto* peer = getPeer(); peer != nullptr && peer->isFullScreen() && immersiveMadeFullScreen) peer->setFullScreen (false);
        immersiveMadeFullScreen = false;
        player.immersiveButton.active = false;
        player.immersiveButton.repaint();
        grabKeyboardFocus();
    };
    onboarding.onFinished = [this] { sidebar.refresh(); navigate ({ PageId::Home }); grabKeyboardFocus(); };
    setWantsKeyboardFocus (true);
    juce::Desktop::getInstance().addFocusChangeListener (this);

    player.onToggleQueue = [this] { queueVisible = ! queueVisible; queue.setVisible (queueVisible); player.queueButton.active = queueVisible; player.queueButton.repaint(); resized(); };
    player.onToggleLyrics = [this]
    {
        if (current.page != PageId::NowPlaying) { nowPlaying.showLyrics = true; navigate ({ PageId::NowPlaying }); }
        else { nowPlaying.showLyrics = ! nowPlaying.showLyrics; nowPlaying.resized(); }
        player.lyricsButton.active = current.page == PageId::NowPlaying && nowPlaying.showLyrics;
        player.lyricsButton.repaint();
    };
    player.onShowSignalPath = [this] { showSignalPath(); };
    player.onOpenNowPlaying = [this] { navigate ({ PageId::NowPlaying }); };
    player.onImmersive = [this] { setImmersive (! immersive.isVisible()); };

    navigate ({ PageId::Home });
}

MainComponent::~MainComponent() { juce::Desktop::getInstance().removeFocusChangeListener (this); }

void MainComponent::globalFocusChanged (juce::Component* focused)
{
    // A clicked button, slider or list keeps the keyboard focus and would swallow the space bar: hand the focus
    // back to the window so that space = play / pause. Text fields keep it (space types a space there).
    if (focused == nullptr || focused == this || ! isParentOf (focused)) return;
    if (dynamic_cast<juce::TextEditor*> (focused) != nullptr || dynamic_cast<juce::ComboBox*> (focused) != nullptr) return;
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->isShowing()) safe->grabKeyboardFocus(); });
}

void MainComponent::showSignalPath()
{
    auto view = std::make_unique<SignalPathView> (ctx, false);
    view->setSize (380, juce::jmax (140, view->getIdealHeight()));
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (view), player.getBounds().withWidth (player.getWidth() * 2 / 3).withX (player.getWidth() / 2), this);
    box.setArrowSize (10.0f);
}

void MainComponent::showRemote()
{
    struct RemotePopup : juce::Component
    {
        AppContext& ctx;
        juce::ToggleButton enable;
        std::unique_ptr<RemotePanel> panel;
        explicit RemotePopup (AppContext& c) : ctx (c), enable ("Activer NX Remote (Wi-Fi)"_t)
        {
            enable.setToggleState (ctx.remote().isRunning(), juce::dontSendNotification);
            enable.onClick = [this] { ctx.setRemoteEnabled (enable.getToggleState()); enable.setToggleState (ctx.remote().isRunning(), juce::dontSendNotification); update(); };
            addAndMakeVisible (enable);
            update();
        }
        void update()
        {
            if (ctx.remote().isRunning() && panel == nullptr) { panel = std::make_unique<RemotePanel> (ctx); addAndMakeVisible (*panel); }
            if (! ctx.remote().isRunning()) panel.reset();
            setSize (620, ctx.remote().isRunning() ? 48 + RemotePanelHeight : 110);
            resized();
            repaint();
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            enable.setBounds (r.removeFromTop (32));
            if (panel != nullptr) panel->setBounds (r.withTrimmedTop (6));
        }
        void paint (juce::Graphics& g) override
        {
            if (panel != nullptr) return;
            g.setColour (ctx.theme().textDim());
            g.setFont (style::font (13.0f));
            g.drawFittedText ("Contrôlez NX Player depuis votre téléphone, sur le même réseau Wi-Fi : activez NX Remote puis scannez le QR code. "_t
                              "Le serveur ne démarre qu'à la demande."_t, getLocalBounds().reduced (10).withTrimmedTop (40), juce::Justification::topLeft, 3);
        }
    };
    auto popup = std::make_unique<RemotePopup> (ctx);
    juce::CallOutBox::launchAsynchronously (std::move (popup), remoteButton.getBounds(), this);
}

void MainComponent::openSettings (const juce::String& tab)
{
    if (immersive.isVisible()) setImmersive (false);
    settingsWindow.setBounds (getLocalBounds());
    settingsWindow.open (tab);
    if (onboarding.isVisible()) onboarding.toFront (false);
    toast.toFront (false);
}

void MainComponent::showOnboarding()
{
    settingsWindow.close();
    if (immersive.isVisible()) setImmersive (false);
    onboarding.setBounds (getLocalBounds());
    onboarding.start();
    toast.toFront (false);
}

void MainComponent::setImmersive (bool on)
{
    if (on == immersive.isVisible()) return;
    if (on)
    {
        immersive.setBounds (getLocalBounds());
        immersive.open();
        toast.toFront (false);
        if (auto* peer = getPeer(); peer != nullptr && ! peer->isFullScreen() && (bool) ctx.settings().get ("immersiveFullScreen", true))
        {
            peer->setFullScreen (true);
            immersiveMadeFullScreen = true;
        }
        player.immersiveButton.active = true;
        player.immersiveButton.repaint();
    }
    else immersive.close();
}

void MainComponent::languageChanged()
{
    sidebar.refresh();
    player.trackChanged();
    if (settingsWindow.isVisible()) settingsWindow.refresh();
    if (onboarding.isVisible()) { onboarding.setStep (onboarding.getStep()); }
    open (current);
    repaint();
}

void MainComponent::resized()
{
    background.setBounds (getLocalBounds());
    const auto& t = ctx.theme();
    const int margin = 12;
    auto r = getLocalBounds().reduced (margin);

    const int playerH = (int) t.number ("--nx-player-height", 84.0f);
    auto position = ctx.layout.getProperty ("playerPosition", t.text ("--nx-player-position", "bottom")).toString();
    player.setBounds (position == "top" ? r.removeFromTop (playerH) : r.removeFromBottom (playerH));
    if (position == "top") r.removeFromTop (margin); else r.removeFromBottom (margin);

    // Sidebar: slides out to the left (Cider-style toggle), the content takes the room.
    const int sideW = (int) ctx.layout.getProperty ("sidebarWidth", t.number ("--nx-sidebar-width", 236.0f));
    const float ease = sidebarAmount * sidebarAmount * (3.0f - 2.0f * sidebarAmount);
    const int shown = juce::roundToInt ((float) (sideW + margin) * ease);
    sidebar.setBounds (r.getX() - (sideW + margin) + shown, r.getY(), sideW, r.getHeight());
    r.removeFromLeft (shown);

    // Navigation row: sidebar toggle, back, forward ... NX Remote, settings (gear).
    auto nav = r.removeFromTop (34);
    for (auto* b : { &sidebarButton, &backButton, &forwardButton })
    {
        b->setBounds (nav.removeFromLeft (34).reduced (2));
        nav.removeFromLeft (2);
    }
    settingsButton.setBounds (nav.removeFromRight (34).reduced (2));
    nav.removeFromRight (4);
    remoteButton.setBounds (nav.removeFromRight (34).reduced (2));
    remoteButton.active = ctx.remote().isRunning();
    r.removeFromTop (2);

    if (queueVisible) queue.setBounds (r.removeFromRight (juce::jmin (340, r.getWidth() / 3)).withTrimmedLeft (0));
    if (queueVisible) r.removeFromRight (margin);

    for (auto* p : std::initializer_list<juce::Component*> { &listing, &nowPlaying, &sourcePage, &marketplace }) p->setBounds (r);
    for (auto* o : std::initializer_list<juce::Component*> { &settingsWindow, &immersive, &onboarding }) o->setBounds (getLocalBounds());
    toast.setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 40), 40).withY (getHeight() - playerH - margin * 2 - 52));
}

void MainComponent::showPage (juce::Component* page)
{
    if (visiblePage == page) { page->setVisible (true); return; }
    if (visiblePage != nullptr) visiblePage->setVisible (false);
    visiblePage = page;
    page->setAlpha (0.0f);
    page->setVisible (true);
    fadeProgress = 0.0f;
    startTimerHz (60);
}

void MainComponent::setSidebarVisible (bool visible, bool animate)
{
    sidebarVisible = visible;
    ctx.settings().set ("sidebarVisible", visible);
    sidebarButton.active = false;
    if (visible) sidebar.setVisible (true);
    if (animate && isShowing()) sidebarAnim.startTimerHz (60);
    else { sidebarAmount = visible ? 1.0f : 0.0f; sidebar.setVisible (visible); resized(); }
}

void MainComponent::navigate (const PageRequest& r)
{
    if (r.page == PageId::Settings) { openSettings (r.a); return; }   // a window over the page, not a page
    if (settingsWindow.isVisible()) settingsWindow.close();
    // History: drop the "forward" part, collapse consecutive searches (typing) into one entry.
    if (historyPos >= 0 && history[(size_t) historyPos] == r) { open (r); return; }
    history.resize ((size_t) (historyPos + 1));
    if (! history.empty() && history.back().page == PageId::Search && r.page == PageId::Search) history.back() = r;
    else history.push_back (r);
    if (history.size() > 200) history.erase (history.begin());
    historyPos = (int) history.size() - 1;
    open (r);
}

void MainComponent::goBack()
{
    if (! canGoBack()) return;
    open (history[(size_t) --historyPos]);
}

void MainComponent::goForward()
{
    if (! canGoForward()) return;
    open (history[(size_t) ++historyPos]);
}

void MainComponent::updateNavButtons()
{
    backButton.setEnabled (canGoBack());
    forwardButton.setEnabled (canGoForward());
}

void MainComponent::open (const PageRequest& r)
{
    current = r;
    updateNavButtons();
    sidebar.setActive (r);
    player.lyricsButton.active = r.page == PageId::NowPlaying && nowPlaying.showLyrics;
    player.lyricsButton.repaint();

    if (r.page == PageId::NowPlaying) { nowPlaying.trackChanged(); showPage (&nowPlaying); return; }
    if (r.page == PageId::Source) { sourcePage.setSource (r.a, r.b); showPage (&sourcePage); return; }
    if (r.page == PageId::Marketplace || r.page == PageId::UserPlugins)
    {
        marketplace.setMode (r.page == PageId::UserPlugins ? MarketplacePage::Mode::UserPlugins : MarketplacePage::Mode::Gallery, r.a);
        showPage (&marketplace);
        return;
    }

    auto& lib = ctx.library();
    listing.hero = false;
    listing.heroArtwork = {};
    listing.heroArtist = {};
    listing.heroTracks.clear();
    listing.topSlot = nullptr;
    listing.ctaText = {};
    listing.ctaAction = nullptr;
    listing.grid.maxRows = 0;
    listing.grid.roundArt = false;
    listing.grid.libraryAlbums = true;
    listing.grid.onContextMenu = nullptr;
    listing.showPills = false;
    listing.artistName = {};
    listing.list.playlistContext = 0;
    listing.list.onChanged = nullptr;
    listing.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::AlbumDetail, a.artist, a.title }); };
    listing.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getAlbumTracks (a.artist, a.title), 0); };
    if (homeSlot->getParentComponent() != nullptr) homeSlot->getParentComponent()->removeChildComponent (homeSlot.get());

    switch (r.page)
    {
        case PageId::Home:
            listing.loader = [this, &lib] (ListingPage& p)
            {
                const int hour = juce::Time::getCurrentTime().getHours();
                p.title = hour < 6 || hour >= 18 ? "Bonsoir"_t : "Bonjour"_t;
                const int n = lib.countTracks();
                p.subtitle = n == 0 ? juce::String ("Ajoutez un dossier de musique ou connectez une source dans la barre latérale."_t)
                                    : juce::String (n) + " morceaux · "_t + juce::String ((int) lib.getAlbums ("added", 100000).size()) + " albums";
                homeSlot->refreshPlugins();
                p.topSlot = n > 0 ? homeSlot.get() : nullptr;
                p.ctaText = n == 0 ? juce::String ("Ajouter un dossier de musique"_t) : juce::String();
                p.ctaAction = [this] { navigate ({ PageId::Source, "local" }); };
                p.grid.maxRows = 2;
                p.grid.setAlbums (lib.getAlbums ("added", 48));
                p.list.setTracks (lib.getRecentlyAdded (12));
                p.heroTracks = p.list.getTracks();
            };
            break;
        case PageId::Library:
        case PageId::RecentlyAdded:
        case PageId::Albums:
        case PageId::Artists:
        case PageId::Songs:
        case PageId::Genre:
        {
            // One library view with the Audirvana-style pills: views, then quality filters.
            juce::String tab = r.page == PageId::RecentlyAdded ? "recents" : r.page == PageId::Albums ? "albums" : r.page == PageId::Artists ? "artistes"
                             : r.page == PageId::Songs ? "pistes" : r.page == PageId::Genre ? "genres" : (r.a.isNotEmpty() ? r.a : juce::String ("albums"));
            listing.loader = [this, &lib, tab, r] (ListingPage& p)
            {
                p.showPills = true;
                p.pills.setPills ({ "albums", "artistes", "pistes", "genres", "recents", "dxd", "dsd", "hires", "cd" },
                                  { "Albums", "Artistes"_t, "Pistes"_t, "Genres", "Ajouts récents"_t, "DXD", "DSD", "Hi-Res", "CD" }, tab);
                p.pills.setCounts ({ { "dxd", lib.countTracksFiltered (QualityFilter::Dxd) }, { "dsd", lib.countTracksFiltered (QualityFilter::Dsd) },
                                     { "hires", lib.countTracksFiltered (QualityFilter::HiRes) }, { "cd", lib.countTracksFiltered (QualityFilter::Cd) } });
                p.list.setTracks ({});
                p.grid.setAlbums ({});
                p.title = "Bibliothèque"_t;
                if (tab == "albums")
                {
                    auto albums = lib.getAlbums ("artist", 5000);
                    p.subtitle = juce::String ((int) albums.size()) + " albums";
                    p.grid.setAlbums (albums);
                }
                else if (tab == "recents")
                {
                    p.subtitle = "Ajouts récents"_t;
                    p.grid.setAlbums (lib.getAlbums ("added", 500));
                }
                else if (tab == "artistes")
                {
                    std::vector<AlbumInfo> cards;
                    juce::StringArray names;
                    for (auto& a : lib.getArtists())
                    {
                        AlbumInfo c; c.title = a.name; c.artist = juce::String (a.albumCount) + " albums · "_t + juce::String (a.trackCount) + " morceaux"_t;
                        c.artworkKey = a.artworkKey;
                        if (auto m = lib.getArtistMeta (a.name); m && m->imageUrl.isNotEmpty()) c.artworkKey = m->imageUrl;
                        cards.push_back (c);
                        names.add (a.name);
                    }
                    ctx.artistInfo().prefetch (names);     // photos arrive in the background (≈ 1 artist / s)
                    p.subtitle = juce::String ((int) cards.size()) + " artistes"_t;
                    p.grid.roundArt = true;
                    p.grid.libraryAlbums = false;
                    p.grid.setAlbums (cards);
                    p.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::ArtistDetail, a.title }); };
                    p.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getArtistTracks (a.title), 0); };
                }
                else if (tab == "pistes")
                {
                    auto tracks = lib.getTracks ("artist", 20000);
                    p.subtitle = juce::String ((int) tracks.size()) + " morceaux"_t;
                    p.list.setTracks (tracks);
                    p.heroTracks = tracks;
                }
                else if (tab == "genres")
                {
                    if (r.page == PageId::Genre && r.a.isNotEmpty())
                    {
                        p.title = r.a;
                        p.subtitle = "Genre"_t;
                        p.grid.setAlbums (lib.getAlbumsFiltered (QualityFilter::Any, r.a));
                        p.heroTracks = lib.getTracksFiltered (QualityFilter::Any, r.a);
                    }
                    else
                    {
                        std::vector<AlbumInfo> cards;
                        for (auto& gnr : lib.getGenres())
                        {
                            AlbumInfo c; c.title = gnr.name; c.artist = juce::String (gnr.albumCount) + " albums · "_t + juce::String (gnr.trackCount) + " morceaux"_t;
                            c.artworkKey = gnr.artworkKey;
                            cards.push_back (c);
                        }
                        p.subtitle = cards.empty() ? juce::String ("Aucun genre dans les tags"_t) : juce::String ((int) cards.size()) + " genres";
                        p.grid.libraryAlbums = false;
                        p.grid.setAlbums (cards);
                        p.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::Genre, a.title }); };
                        p.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getTracksFiltered (QualityFilter::Any, a.title), 0); };
                    }
                }
                else
                {
                    const auto q = tab == "dxd" ? QualityFilter::Dxd : tab == "dsd" ? QualityFilter::Dsd : tab == "hires" ? QualityFilter::HiRes : QualityFilter::Cd;
                    const juce::String names[] = { {}, "DXD (352,8 / 384 kHz)"_t, "DSD (natif, DoP ou converti)"_t, "Hi-Res (> 48 kHz ou > 16 bits)"_t, "Qualité CD (16 bits / 44,1 – 48 kHz)"_t };
                    auto albums = lib.getAlbumsFiltered (q);
                    p.subtitle = juce::String ((int) albums.size()) + " albums · "_t + names[(int) q];
                    p.grid.setAlbums (albums);
                    p.heroTracks = lib.getTracksFiltered (q);
                }
            };
            break;
        }
        case PageId::AlbumDetail:
            listing.loader = [this, &lib, r] (ListingPage& p)
            {
                auto tracks = lib.getAlbumTracks (r.a, r.b);
                p.hero = true;
                p.title = r.b;
                double dur = 0; int year = 0; juce::String badge, art, nas;
                for (auto& t : tracks) { dur += t.duration; year = juce::jmax (year, t.year); if (art.isEmpty()) art = t.artworkKey; if (t.source.startsWith ("upnp")) nas = t.source; }
                if (! tracks.empty()) badge = tracks.front().resolutionBadge();
                p.heroArtwork = art;
                p.heroArtist = r.a;
                p.subtitle = r.a + "\n" + (year > 0 ? juce::String (year) + " · "_t : juce::String()) + juce::String ((int) tracks.size()) + " morceaux · "_t
                             + style::formatTime (dur) + (badge.isNotEmpty() ? " · "_t + badge : juce::String())
                             + (nas.isNotEmpty() ? " · "_t + ctx.networkSourceLabel (nas) : juce::String());
                p.grid.setAlbums ({});
                p.list.setTracks (tracks, false, false, true);
                p.heroTracks = tracks;
            };
            break;
        case PageId::Favourites:
            listing.loader = [this, &lib] (ListingPage& p)
            {
                auto albums = lib.getFavouriteAlbums();
                auto tracks = lib.getFavouriteTracks();
                p.title = "Favoris"_t;
                p.subtitle = albums.empty() && tracks.empty() ? juce::String ("Cliquez sur le cœur d'un morceau, ou clic droit › Ajouter aux favoris, ou glissez des morceaux sur Favoris."_t)
                                                              : juce::String ((int) albums.size()) + " album(s) · "_t + juce::String ((int) tracks.size()) + " morceau(x)"_t;
                p.grid.setAlbums (albums);
                p.list.setTracks (tracks);
                p.heroTracks = tracks;
                p.list.onChanged = [this] { refreshCurrentPage(); };
            };
            break;
        case PageId::ArtistDetail:
            listing.loader = [&lib, r] (ListingPage& p)
            {
                std::vector<AlbumInfo> albums;
                for (auto& a : lib.getAlbums ("year", 5000)) if (a.artist == r.a) albums.push_back (a);
                const auto tracks = lib.getArtistTracks (r.a);
                p.artistName = r.a;
                p.title = r.a; p.subtitle = {};
                p.artistHeader.setArtist (r.a, (int) albums.size(), (int) tracks.size());
                p.grid.setAlbums (albums);
                p.list.setTracks (tracks);
                p.heroTracks = tracks;
            };
            break;
        case PageId::Playlists:
            listing.loader = [this, &lib] (ListingPage& p)
            {
                std::vector<AlbumInfo> cards;
                for (auto& pl : lib.getPlaylists())
                {
                    AlbumInfo c; c.title = pl.name; c.artist = juce::String (pl.trackCount) + " morceaux · "_t + pl.source; c.source = juce::String (pl.id);
                    auto tracks = lib.getPlaylistTracks (pl.id);
                    if (! tracks.empty()) c.artworkKey = tracks.front().artworkKey;
                    cards.push_back (c);
                }
                p.title = "Playlists"; p.subtitle = cards.empty() ? juce::String ("Importez vos playlists depuis une source, ou créez-en une."_t) : juce::String();
                p.grid.setAlbums (cards);
                p.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::PlaylistDetail, a.source }); };
                p.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getPlaylistTracks (a.source.getLargeIntValue()), 0); };
                p.list.setTracks ({});
            };
            break;
        case PageId::PlaylistDetail:
            listing.loader = [&lib, r] (ListingPage& p)
            {
                const auto id = r.a.getLargeIntValue();
                juce::String name;
                for (auto& pl : lib.getPlaylists()) if (pl.id == id) name = pl.name;
                auto tracks = lib.getPlaylistTracks (id);
                p.title = name; p.subtitle = juce::String ((int) tracks.size()) + " morceaux"_t;
                p.grid.setAlbums ({});
                p.list.setTracks (tracks);
                p.list.playlistContext = id;
                p.heroTracks = tracks;
            };
            break;
        case PageId::Search:
            listing.loader = [this, &lib, r] (ListingPage& p)
            {
                p.title = "Recherche"; p.subtitle = "« "_t + r.a + " »"_t + "  ·  bibliothèque + sources connectées"_t;
                // matching artists first: round cards that open the artist page
                {
                    std::vector<AlbumInfo> cards;
                    juce::StringArray names;
                    for (auto& a : lib.getArtists())
                        if (a.name.containsIgnoreCase (r.a) && cards.size() < 12)
                        {
                            AlbumInfo c; c.title = a.name; c.artist = juce::String (a.albumCount) + " albums"_t;
                            c.artworkKey = a.artworkKey;
                            if (auto m = lib.getArtistMeta (a.name); m && m->imageUrl.isNotEmpty()) c.artworkKey = m->imageUrl;
                            cards.push_back (c);
                            names.add (a.name);
                        }
                    ctx.artistInfo().prefetch (names);
                    p.grid.roundArt = ! cards.empty();
                    p.grid.libraryAlbums = cards.empty();
                    p.grid.setAlbums (cards);
                    if (! cards.empty())
                    {
                        p.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::ArtistDetail, a.title }); };
                        p.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getArtistTracks (a.title), 0); };
                    }
                }
                p.list.setTracks (lib.search (r.a, 200));
                p.heroTracks = p.list.getTracks();
                juce::Component::SafePointer<MainComponent> safe (this);
                const auto query = r.a;
                ctx.providers().search (query, [safe, query] (std::vector<TrackInfo> results)
                {
                    if (safe == nullptr || safe->current.page != PageId::Search || safe->current.a != query) return;
                    safe->listing.list.setTracks (results);
                    safe->listing.heroTracks = results;
                    safe->listing.layoutContent (safe->listing.getWidth() - 10);
                });
            };
            break;
        default: break;
    }
    listing.refresh();
    listing.scrollToTop();
    showPage (&listing);
    listing.scrollToTop();
}

void MainComponent::refreshCurrentPage()
{
    if (visiblePage == &listing) listing.refresh();
    else if (visiblePage == &sourcePage) sourcePage.refresh();
    else if (visiblePage == &marketplace) marketplace.refresh();
    sidebar.refresh();
}

void MainComponent::trackChanged()
{
    player.trackChanged();
    if (immersive.isVisible()) immersive.trackChanged();
    nowPlaying.trackChanged();
    queue.repaint();
    if (const auto* t = ctx.currentTrack()) background.setArtwork (t->artworkKey, t->album);
    listing.list.repaint();
}

void MainComponent::stateChanged()
{
    player.stateChanged();
    queue.repaint();
}

void MainComponent::themeChanged()
{
    background.themeChanged();
    resized();
    for (auto* p : std::initializer_list<juce::Component*> { &listing, &sourcePage }) p->resized();
    if (settingsWindow.isVisible()) settingsWindow.refresh();
    if (immersive.isVisible()) immersive.refreshConfig();
    if (current.page == PageId::Marketplace || current.page == PageId::UserPlugins) marketplace.refresh();
    repaint();
}

void MainComponent::pluginsChanged()
{
    player.refreshPlugins();
    sidebar.refreshPlugins();
    nowPlaying.refreshPlugins();
    homeSlot->refreshPlugins();
    if (current.page == PageId::Home) listing.refresh();
    if (settingsWindow.isVisible()) settingsWindow.refresh();
    if (immersive.isVisible()) immersive.refreshConfig();
    if (current.page == PageId::Marketplace || current.page == PageId::UserPlugins) marketplace.refresh();
}

void MainComponent::timerCallback()
{
    // page cross-fade (180 ms at animation speed 1)
    fadeProgress = juce::jmin (1.0f, fadeProgress + (1.0f / 60.0f) / 0.18f * ctx.theme().number ("--nx-animation-speed", 1.0f));
    if (visiblePage != nullptr) visiblePage->setAlpha (1.0f - (1.0f - fadeProgress) * (1.0f - fadeProgress));
    if (fadeProgress >= 1.0f) stopTimer();
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    auto& e = ctx.engine();
    if (k == juce::KeyPress::escapeKey)
    {
        if (settingsWindow.isVisible()) settingsWindow.close();
        else if (immersive.isVisible()) setImmersive (false);
        grabKeyboardFocus();                                         // leave the search field
        return true;
    }
    if (k == juce::KeyPress (',', juce::ModifierKeys::commandModifier, 0)) { openSettings ({}); return true; }
    if (onboarding.isVisible()) return false;
    if (k == juce::KeyPress::spaceKey) { e.togglePlayPause(); return true; }
    if (k == juce::KeyPress::rightKey) { e.seek (e.getPositionSeconds() + 10.0); return true; }
    if (k == juce::KeyPress::leftKey)  { e.seek (juce::jmax (0.0, e.getPositionSeconds() - 10.0)); return true; }
    if (k == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0)) { sidebar.getSearchField().grabKeyboardFocus(); return true; }
    if (k == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0)) { if (player.onToggleLyrics) player.onToggleLyrics(); return true; }
    if (k == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0)) { ctx.toggleMiniPlayer(); return true; }
    if (k == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0)) { e.next(); return true; }
    if (k == juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0)) { e.previous(); return true; }
    // Navigation history and sidebar
    if (k == juce::KeyPress ('[', juce::ModifierKeys::commandModifier, 0) || k == juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0)) { goBack(); return true; }
    if (k == juce::KeyPress (']', juce::ModifierKeys::commandModifier, 0) || k == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0)) { goForward(); return true; }
    if (k == juce::KeyPress ('b', juce::ModifierKeys::commandModifier, 0)) { setSidebarVisible (! sidebarVisible); return true; }
    // A-B loop: I = start, O = end + loop, L = off
    if (k.getModifiers().isAnyModifierKeyDown() == false)
    {
        const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (c == 'i') { player.markLoopA(); return true; }
        if (c == 'o') { player.markLoopB(); return true; }
        if (c == 'l') { player.clearLoop(); return true; }
        if (c == 'f') { setImmersive (! immersive.isVisible()); return true; }
    }
    return false;
}

} // namespace nx
