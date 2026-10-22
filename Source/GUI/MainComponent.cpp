// NX Player — MIT License
#include "MainComponent.h"
#include "../Ext/PluginManager.h"

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
        g.drawText ("Vide", r.removeFromTop (30), juce::Justification::centredLeft);
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
    : background (c), sidebar (c), player (c), ctx (c), listing (c), nowPlaying (c), sourcePage (c), settingsPage (c), queue (c), toast (c)
{
    homeSlot = std::make_unique<PluginSlot> (c, "home.top");
    addAndMakeVisible (background);
    addAndMakeVisible (sidebar);
    addChildComponent (listing);
    addChildComponent (nowPlaying);
    addChildComponent (sourcePage);
    addChildComponent (settingsPage);
    addAndMakeVisible (player);
    addChildComponent (queue);
    addChildComponent (toast);
    setWantsKeyboardFocus (true);

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

    navigate ({ PageId::Home });
}

MainComponent::~MainComponent() = default;

void MainComponent::showSignalPath()
{
    auto view = std::make_unique<SignalPathView> (ctx, false);
    view->setSize (380, juce::jmax (140, view->getIdealHeight()));
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (view), player.getBounds().withWidth (player.getWidth() * 2 / 3).withX (player.getWidth() / 2), this);
    box.setArrowSize (10.0f);
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

    const int sideW = (int) ctx.layout.getProperty ("sidebarWidth", t.number ("--nx-sidebar-width", 236.0f));
    sidebar.setBounds (r.removeFromLeft (sideW));
    r.removeFromLeft (margin);

    if (queueVisible) queue.setBounds (r.removeFromRight (juce::jmin (340, r.getWidth() / 3)).withTrimmedLeft (0));
    if (queueVisible) r.removeFromRight (margin);

    for (auto* p : std::initializer_list<juce::Component*> { &listing, &nowPlaying, &sourcePage, &settingsPage }) p->setBounds (r);
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

void MainComponent::navigate (const PageRequest& r)
{
    current = r;
    sidebar.setActive (r);
    player.lyricsButton.active = r.page == PageId::NowPlaying && nowPlaying.showLyrics;
    player.lyricsButton.repaint();

    if (r.page == PageId::NowPlaying) { nowPlaying.trackChanged(); showPage (&nowPlaying); return; }
    if (r.page == PageId::Source) { sourcePage.setSource (r.a); showPage (&sourcePage); return; }
    if (r.page == PageId::Settings) { settingsPage.refresh(); showPage (&settingsPage); return; }

    auto& lib = ctx.library();
    listing.hero = false;
    listing.heroArtwork = {};
    listing.heroTracks.clear();
    listing.topSlot = nullptr;
    listing.ctaText = {};
    listing.ctaAction = nullptr;
    listing.grid.maxRows = 0;
    listing.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::AlbumDetail, a.artist, a.title }); };
    listing.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getAlbumTracks (a.artist, a.title), 0); };
    if (homeSlot->getParentComponent() != nullptr) homeSlot->getParentComponent()->removeChildComponent (homeSlot.get());

    switch (r.page)
    {
        case PageId::Home:
            listing.loader = [this, &lib] (ListingPage& p)
            {
                const int hour = juce::Time::getCurrentTime().getHours();
                p.title = hour < 6 || hour >= 18 ? "Bonsoir" : "Bonjour";
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
        case PageId::RecentlyAdded:
            listing.loader = [&lib] (ListingPage& p)
            {
                p.title = "Ajouts récents"_t; p.subtitle = {};
                p.grid.setAlbums (lib.getAlbums ("added", 500));
                p.list.setTracks ({});
            };
            break;
        case PageId::Albums:
            listing.loader = [&lib] (ListingPage& p)
            {
                auto albums = lib.getAlbums ("artist", 5000);
                p.title = "Albums"; p.subtitle = juce::String ((int) albums.size()) + " albums";
                p.grid.setAlbums (albums);
                p.list.setTracks ({});
            };
            break;
        case PageId::AlbumDetail:
            listing.loader = [&lib, r] (ListingPage& p)
            {
                auto tracks = lib.getAlbumTracks (r.a, r.b);
                p.hero = true;
                p.title = r.b;
                double dur = 0; int year = 0; juce::String badge, art;
                for (auto& t : tracks) { dur += t.duration; year = juce::jmax (year, t.year); if (art.isEmpty()) art = t.artworkKey; }
                if (! tracks.empty()) badge = tracks.front().resolutionBadge();
                p.heroArtwork = art;
                p.subtitle = r.a + "\n" + (year > 0 ? juce::String (year) + " · "_t : juce::String()) + juce::String ((int) tracks.size()) + " morceaux · "_t
                             + style::formatTime (dur) + (badge.isNotEmpty() ? " · "_t + badge : juce::String());
                p.grid.setAlbums ({});
                p.list.setTracks (tracks, false, false, true);
                p.heroTracks = tracks;
            };
            break;
        case PageId::Artists:
            listing.loader = [this, &lib] (ListingPage& p)
            {
                std::vector<AlbumInfo> cards;
                for (auto& a : lib.getArtists())
                {
                    AlbumInfo c; c.title = a.name; c.artist = juce::String (a.albumCount) + " albums · "_t + juce::String (a.trackCount) + " morceaux"; c.artworkKey = a.artworkKey;
                    cards.push_back (c);
                }
                p.title = "Artistes"; p.subtitle = juce::String ((int) cards.size()) + " artistes";
                p.grid.setAlbums (cards);
                p.grid.onOpen = [this] (const AlbumInfo& a) { navigate ({ PageId::ArtistDetail, a.title }); };
                p.grid.onPlay = [this] (const AlbumInfo& a) { ctx.playTracks (ctx.library().getArtistTracks (a.title), 0); };
                p.list.setTracks ({});
            };
            break;
        case PageId::ArtistDetail:
            listing.loader = [&lib, r] (ListingPage& p)
            {
                std::vector<AlbumInfo> albums;
                for (auto& a : lib.getAlbums ("year", 5000)) if (a.artist == r.a) albums.push_back (a);
                const auto tracks = lib.getArtistTracks (r.a);
                p.title = r.a; p.subtitle = juce::String ((int) albums.size()) + " albums · "_t + juce::String ((int) tracks.size()) + " morceaux";
                p.grid.setAlbums (albums);
                p.list.setTracks (tracks);
                p.heroTracks = tracks;
            };
            break;
        case PageId::Songs:
            listing.loader = [&lib] (ListingPage& p)
            {
                auto tracks = lib.getTracks ("artist", 20000);
                p.title = "Morceaux"; p.subtitle = juce::String ((int) tracks.size()) + " morceaux";
                p.grid.setAlbums ({});
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
                p.title = name; p.subtitle = juce::String ((int) tracks.size()) + " morceaux";
                p.grid.setAlbums ({});
                p.list.setTracks (tracks);
                p.heroTracks = tracks;
            };
            break;
        case PageId::Search:
            listing.loader = [this, &lib, r] (ListingPage& p)
            {
                p.title = "Recherche"; p.subtitle = "« "_t + r.a + " »"_t + "  ·  bibliothèque + sources connectées"_t;
                p.grid.setAlbums ({});
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
    showPage (&listing);
}

void MainComponent::refreshCurrentPage()
{
    if (visiblePage == &listing) listing.refresh();
    else if (visiblePage == &sourcePage) sourcePage.refresh();
    sidebar.refresh();
}

void MainComponent::trackChanged()
{
    player.trackChanged();
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
    settingsPage.refresh();
    repaint();
}

void MainComponent::pluginsChanged()
{
    player.refreshPlugins();
    sidebar.refreshPlugins();
    nowPlaying.refreshPlugins();
    homeSlot->refreshPlugins();
    if (current.page == PageId::Home) listing.refresh();
    if (current.page == PageId::Settings) settingsPage.refresh();
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
    if (k == juce::KeyPress::spaceKey) { e.togglePlayPause(); return true; }
    if (k == juce::KeyPress::rightKey) { e.seek (e.getPositionSeconds() + 10.0); return true; }
    if (k == juce::KeyPress::leftKey)  { e.seek (juce::jmax (0.0, e.getPositionSeconds() - 10.0)); return true; }
    if (k == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0)) { sidebar.getSearchField().grabKeyboardFocus(); return true; }
    if (k == juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0)) { if (player.onToggleLyrics) player.onToggleLyrics(); return true; }
    if (k == juce::KeyPress ('m', juce::ModifierKeys::commandModifier, 0)) { ctx.toggleMiniPlayer(); return true; }
    if (k == juce::KeyPress (juce::KeyPress::rightKey, juce::ModifierKeys::commandModifier, 0)) { e.next(); return true; }
    if (k == juce::KeyPress (juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0)) { e.previous(); return true; }
    return false;
}

} // namespace nx
