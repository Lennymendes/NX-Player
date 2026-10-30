// NX Player — MIT License
#include "Sidebar.h"
#include "LibraryActions.h"
#include "../Providers/MediaProvider.h"
#include "../Ext/PluginManager.h"
#include "../Library/Downloads.h"
#include "../Library/LibraryScanner.h"

namespace nx
{

Sidebar::Sidebar (AppContext& c) : ctx (c), pluginSlot (c, "sidebar.bottom")
{
    search.setTextToShowWhenEmpty ("Rechercher partout"_t, ctx.theme().textDim());
    search.setFont (style::font (14.0f));
    search.setIndents (34, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onReturnKey = [this] { if (search.getText().trim().isNotEmpty()) ctx.navigate ({ PageId::Search, search.getText().trim() }); };
    search.onTextChange = [this] { startTimer (350); };
    search.onEscapeKey = [this] { if (auto* p = getParentComponent()) p->grabKeyboardFocus(); };
    addAndMakeVisible (search);
    addAndMakeVisible (pluginSlot);
    ctx.downloads().addChangeListener (this);
    refresh();
}

Sidebar::~Sidebar() { ctx.downloads().removeChangeListener (this); }

int Sidebar::bottomReserve() const { return 50 + (ctx.downloads().getPendingCount() > 0 && ! isCompact() ? 46 : 0); }

void Sidebar::timerCallback()
{
    stopTimer();
    const auto q = search.getText().trim();
    if (q.length() >= 2) ctx.navigate ({ PageId::Search, q });
}

void Sidebar::refresh()
{
    items.clear();
    auto header = [this] (const juce::String& s) { Item i; i.header = true; i.label = s; items.push_back (i); };
    auto item = [this] (const juce::String& s, style::Icon icon, PageRequest target, juce::Colour dot = {}, bool quick = false)
    {
        Item i; i.label = s; i.icon = icon; i.target = target; i.dot = dot; i.quickAdd = quick; items.push_back (i);
    };

    search.setTextToShowWhenEmpty ("Rechercher partout"_t, ctx.theme().textDim());
    item ("Accueil"_t, style::Icon::Home, { PageId::Home });
    item ("En cours"_t, style::Icon::Expand, { PageId::NowPlaying });
    header ("Bibliothèque"_t);
    item ("Local"_t, style::Icon::Folder, { PageId::Library, "albums", "sidebar" }, {}, true);
    item ("Favoris"_t, style::Icon::Heart, { PageId::Favourites });
    item ("Playlists"_t, style::Icon::Playlist, { PageId::Playlists });
    header ("Réseau"_t);
    const auto ok = juce::Colour (0xff00f5a0), off = juce::Colours::grey;
    for (auto& p : ctx.providers().getProviders())
    {
        const bool nas = p->getId() == "upnp";
        item (nas ? "NAS / Jellyfin"_t : p->getDisplayName(), nas ? style::Icon::Network : style::Icon::Cloud, { PageId::Source, p->getId() },
              p->isSignedIn() ? ok : off);
    }
    header ("Personnaliser"_t);
    item ("Extensions & Thèmes"_t, style::Icon::Grid, { PageId::Marketplace });
    const auto playlists = ctx.library().getPlaylists();
    if (! playlists.empty())
    {
        header ("Mes playlists"_t);
        for (auto& pl : playlists) item (pl.name, style::Icon::Playlist, { PageId::PlaylistDetail, juce::String (pl.id) });
    }
    resized();
    repaint();
}

void Sidebar::resized()
{
    const bool compact = isCompact();
    itemHeight = compact ? 40 : 32;
    headerHeight = compact ? 14 : 34;
    auto r = getLocalBounds().reduced (compact ? 8 : 14, 14);
    r.removeFromTop (44);   // logo
    search.setVisible (! compact);
    if (compact) r.removeFromTop (36);                    // search icon (opens the wide sidebar)
    else search.setBounds (r.removeFromTop (34));
    r.removeFromTop (8);
    listTop = r.getY();
    r.removeFromBottom (36 + (ctx.downloads().getPendingCount() > 0 && ! compact ? 46 : 0));
    const int ph = pluginSlot.isEmpty() || compact ? 0 : juce::jmin (pluginSlot.getPreferredHeight(), 140);
    pluginSlot.setVisible (ph > 0);
    pluginSlot.setBounds (r.removeFromBottom (ph));
}

int Sidebar::itemTop (int index) const
{
    int pos = listTop - scroll;
    for (int i = 0; i < index && i < (int) items.size(); ++i) pos += items[(size_t) i].header ? headerHeight : itemHeight;
    return pos;
}

int Sidebar::itemAt (int y) const
{
    if (isCompact() && y >= 58 && y < 58 + 36) return -3;                                  // search icon
    int pos = listTop - scroll;
    const int maxY = getHeight() - bottomReserve() - (pluginSlot.isVisible() ? pluginSlot.getHeight() : 0);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const int h = items[(size_t) i].header ? headerHeight : itemHeight;
        if (y >= pos && y < pos + h && pos + h <= maxY && y >= listTop) return items[(size_t) i].header ? -1 : i;
        pos += h;
    }
    if (y >= getHeight() - 50) return -2;    // settings
    return -1;
}

int Sidebar::findItem (PageId page, const juce::String& a) const
{
    for (int i = 0; i < (int) items.size(); ++i)
        if (! items[(size_t) i].header && items[(size_t) i].target.page == page && (a.isEmpty() || items[(size_t) i].target.a == a)) return i;
    return -1;
}

juce::Rectangle<int> Sidebar::quickAddBounds (int index) const
{
    if (isCompact() || index < 0 || index >= (int) items.size() || ! items[(size_t) index].quickAdd) return {};
    return { getWidth() - 10 - 34, itemTop (index) + (itemHeight - 26) / 2, 26, 26 };
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int i = itemAt (e.y);
    const bool q = i >= 0 && quickAddBounds (i).contains (e.getPosition());
    if (i != hover || q != quickHover) { hover = i; quickHover = q; repaint(); }
}

void Sidebar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    int total = 0;
    for (auto& it : items) total += it.header ? headerHeight : itemHeight;
    const int visible = getHeight() - bottomReserve() - (pluginSlot.isVisible() ? pluginSlot.getHeight() : 0) - listTop;
    scroll = juce::jlimit (0, juce::jmax (0, total - visible), scroll - juce::roundToInt (w.deltaY * 240.0f));
    repaint();
}

void Sidebar::addMusicFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Dossier de musique"_t, juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    auto* app = &ctx;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [app, chooser] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (! dir.isDirectory()) return;
        app->library().addFolder (dir);
        app->scanner().requestFullScan();
        app->showMessage ("Indexation de "_t + dir.getFileName() + "…"_t);
    });
}

void Sidebar::mouseUp (const juce::MouseEvent& e)
{
    const int i = itemAt (e.y);
    if (i == -2) { ctx.navigate ({ PageId::Settings }); return; }
    if (i == -3) { ctx.setLayoutOption ("sidebar", "wide"); juce::MessageManager::callAsync ([this] { search.grabKeyboardFocus(); }); return; }
    if (i < 0) return;
    if (quickAddBounds (i).contains (e.getPosition())) { addMusicFolder(); return; }
    const auto& it = items[(size_t) i];
    if (e.mods.isPopupMenu() && it.target.page == PageId::PlaylistDetail)
    {
        const auto id = it.target.a.getLargeIntValue();
        juce::PopupMenu m;
        auto* app = &ctx;
        m.addItem ("Lire"_t, [app, id] { app->playTracks (app->library().getPlaylistTracks (id), 0); });
        m.addItem ("Supprimer la playlist"_t, [app, id, name = it.label]
        {
            actions::confirm (*app, "Supprimer la playlist"_t, "« "_t + name + " » sera supprimée (les morceaux restent dans la bibliothèque)."_t, "Supprimer"_t,
                              [app, id] { app->library().deletePlaylist (id); app->library().notifyChanged(); app->navigate ({ PageId::Playlists }); });
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    ctx.navigate (it.target);
}

bool Sidebar::isItemActive (const Item& it) const
{
    const auto p = active.page;
    if (it.target.page == PageId::Library)
        return p == PageId::Library || p == PageId::Albums || p == PageId::Artists || p == PageId::Songs || p == PageId::RecentlyAdded
            || p == PageId::Genre || p == PageId::AlbumDetail || p == PageId::ArtistDetail;
    if (it.target.page == PageId::Marketplace) return p == PageId::Marketplace || p == PageId::UserPlugins;
    if (it.target.page == PageId::Source) return p == PageId::Source && it.target.a == active.a;
    if (it.target.page == PageId::PlaylistDetail) return p == PageId::PlaylistDetail && it.target.a == active.a;
    return it.target.page == p;
}

//== Drag & drop =================================================================
bool Sidebar::isInterestedInDragSource (const SourceDetails& d)
{
    return ! actions::idsFromPayload (d.description).empty();
}

void Sidebar::itemDragMove (const SourceDetails& d)
{
    const int i = itemAt (d.localPosition.getY());
    const bool ok = i >= 0 && (items[(size_t) i].target.page == PageId::PlaylistDetail || items[(size_t) i].target.page == PageId::Playlists
                               || items[(size_t) i].target.page == PageId::Favourites);
    const int target = ok ? i : -1;
    if (target != dropHover) { dropHover = target; repaint(); }
}

juce::String Sidebar::dropTracks (const std::vector<juce::int64>& ids, int index)
{
    if (index < 0 || index >= (int) items.size() || ids.empty()) return {};
    const auto& it = items[(size_t) index];
    if (it.target.page == PageId::PlaylistDetail)
    {
        actions::addToPlaylist (ctx, it.target.a.getLargeIntValue(), ids);
        return "playlist";
    }
    if (it.target.page == PageId::Playlists)
    {
        actions::promptNewPlaylist (ctx, ids);
        return "new";
    }
    if (it.target.page == PageId::Favourites)
    {
        for (auto id : ids) ctx.library().setFavourite (id, true);
        ctx.library().notifyChanged();
        ctx.showMessage (juce::String ((int) ids.size()) + " morceau(x) ajouté(s) aux favoris"_t);
        return "favourites";
    }
    return {};
}

void Sidebar::itemDropped (const SourceDetails& d)
{
    const int i = itemAt (d.localPosition.getY());
    dropHover = -1;
    dropTracks (actions::idsFromPayload (d.description), i);
    refresh();
}

juce::String Sidebar::getTooltip()
{
    if (! isCompact()) return {};
    const auto pos = getMouseXYRelative();
    const int i = itemAt (pos.y);
    if (i >= 0) return items[(size_t) i].label;
    if (i == -2) return "Réglages"_t;
    if (i == -3) return "Rechercher"_t;
    return {};
}

//== Paint =======================================================================
void Sidebar::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    const bool compact = isCompact();
    style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), t.radius());

    // logo
    auto logo = getLocalBounds().reduced (compact ? 0 : 16, 14).removeFromTop (40);
    auto mark = (compact ? logo : logo.removeFromLeft (30)).toFloat().withSizeKeepingCentre (28, 28);
    g.setGradientFill (juce::ColourGradient (t.accent(), mark.getX(), mark.getY(), t.accent2(), mark.getRight(), mark.getBottom(), false));
    g.drawEllipse (mark.reduced (2), 3.0f);
    juce::Path wave;
    for (int k = 0; k <= 20; ++k)
    {
        const float x = mark.getX() + 7.0f + (float) k * 0.7f;
        const float y = mark.getCentreY() - 5.0f * std::sin ((float) k / 20.0f * juce::MathConstants<float>::twoPi);
        if (k == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (! compact)
    {
        logo.removeFromLeft (8);
        g.setColour (t.textCol());
        g.setFont (style::font (19.0f, true));
        g.drawText ("NX Player", logo, juce::Justification::centredLeft);
        style::drawIcon (g, style::Icon::Search, search.getBounds().toFloat().removeFromLeft (34).withSizeKeepingCentre (16, 16), t.textDim());
    }
    else
    {
        auto s = juce::Rectangle<float> (0, 58, (float) getWidth(), 36);
        if (hover == -3) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (s.reduced (8, 2), 8.0f); }
        style::drawIcon (g, style::Icon::Search, s.withSizeKeepingCentre (18, 18), t.textCol().withAlpha (0.8f));
    }

    // items
    g.saveState();
    const int maxY = getHeight() - bottomReserve() - (pluginSlot.isVisible() ? pluginSlot.getHeight() : 0);
    g.reduceClipRegion (0, listTop, getWidth(), maxY - listTop);
    int y = listTop - scroll;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        if (it.header)
        {
            if (compact)
            {
                g.setColour (t.border());
                g.fillRect (16, y + headerHeight / 2, getWidth() - 32, 1);
            }
            else
            {
                g.setColour (t.textDim());
                g.setFont (style::font (11.5f, true));
                g.drawText (style::upper (it.label), 16, y + 12, getWidth() - 32, 20, juce::Justification::centredLeft);
            }
            y += headerHeight;
            continue;
        }
        auto row = juce::Rectangle<int> (compact ? 8 : 10, y, getWidth() - (compact ? 16 : 20), itemHeight);
        const bool isActive = isItemActive (it);
        if (i == dropHover)
        {
            g.setColour (t.accent().withAlpha (0.28f));
            g.fillRoundedRectangle (row.toFloat(), 8.0f);
            g.setColour (t.accent());
            g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 8.0f, 1.5f);
        }
        else if (isActive)
        {
            g.setColour (t.accent().withAlpha (0.16f));
            g.fillRoundedRectangle (row.toFloat(), 8.0f);
        }
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (row.toFloat(), 8.0f);
        }
        const auto iconColour = isActive ? t.accent() : (it.icon == style::Icon::Heart ? t.accent().withAlpha (0.8f) : t.textCol().withAlpha (0.8f));
        if (compact)
        {
            style::drawIcon (g, it.icon, row.toFloat().withSizeKeepingCentre (20, 20), iconColour);
            if (! it.dot.isTransparent())
            {
                g.setColour (it.dot);
                g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ (float) row.getRight() - 8.0f, (float) row.getY() + 8.0f }));
            }
            y += itemHeight;
            continue;
        }
        auto ir = row.reduced (8, 0);
        style::drawIcon (g, it.icon, ir.removeFromLeft (20).toFloat().withSizeKeepingCentre (18, 18), iconColour);
        ir.removeFromLeft (10);
        if (it.quickAdd)
        {
            auto q = quickAddBounds (i);
            if (i == hover)
            {
                g.setColour (juce::Colours::white.withAlpha (quickHover ? 0.16f : 0.08f));
                g.fillRoundedRectangle (q.toFloat(), 6.0f);
            }
            style::drawIcon (g, style::Icon::FolderPlus, q.toFloat().reduced (4.0f), quickHover ? t.accent() : t.textDim());
            ir.removeFromRight (34);
        }
        if (! it.dot.isTransparent())
        {
            g.setColour (it.dot);
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ (float) ir.getRight() - 6.0f, (float) ir.getCentreY() }));
            ir.removeFromRight (16);
        }
        g.setColour (isActive ? t.textCol() : t.textCol().withAlpha (0.88f));
        g.setFont (style::font (14.0f, isActive));
        g.drawText (it.label, ir, juce::Justification::centredLeft, true);
        y += itemHeight;
    }
    g.restoreState();

    // downloads in progress
    auto& dl = ctx.downloads();
    if (const int pending = dl.getPendingCount(); pending > 0 && ! compact)
    {
        auto box = juce::Rectangle<int> (16, getHeight() - 46 - 44, getWidth() - 32, 38);
        const int total = juce::jmax (pending, dl.getSessionTotal());
        const int doneCount = total - pending;
        const float overall = ((float) doneCount + dl.getCurrentProgress()) / (float) total;
        style::drawIcon (g, style::Icon::Download, box.removeFromLeft (20).toFloat().withSizeKeepingCentre (16, 16), t.accent());
        box.removeFromLeft (8);
        g.setColour (t.textCol());
        g.setFont (style::font (12.5f, true));
        g.drawText ("Téléchargement "_t + juce::String (doneCount + 1) + "/" + juce::String (total) + "  ·  "_t
                        + juce::String (juce::roundToInt (overall * 100.0f)) + " %",
                    box.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (11.5f));
        g.drawText (dl.getCurrentTitle(), box.removeFromTop (14), juce::Justification::centredLeft, true);
        auto bar = juce::Rectangle<float> (16.0f, (float) getHeight() - 50.0f, (float) getWidth() - 32.0f, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (t.accent());
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, overall)), 1.5f);
    }

    // settings (opens the settings window)
    auto s = juce::Rectangle<int> (compact ? 8 : 10, getHeight() - 46, getWidth() - (compact ? 16 : 20), 34);
    if (hover == -2)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (s.toFloat(), 8.0f);
    }
    if (compact)
    {
        style::drawIcon (g, style::Icon::Settings, s.toFloat().withSizeKeepingCentre (18, 18), t.textCol().withAlpha (0.8f));
        return;
    }
    auto sr = s.reduced (8, 0);
    style::drawIcon (g, style::Icon::Settings, sr.removeFromLeft (20).toFloat().withSizeKeepingCentre (18, 18), t.textCol().withAlpha (0.8f));
    sr.removeFromLeft (10);
    g.setColour (t.textCol());
    g.setFont (style::font (14.0f));
    g.drawText ("Réglages"_t, sr, juce::Justification::centredLeft);
}

} // namespace nx
