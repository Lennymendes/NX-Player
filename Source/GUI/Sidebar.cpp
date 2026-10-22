// NX Player — MIT License
#include "Sidebar.h"
#include "../Providers/MediaProvider.h"
#include "../Ext/PluginManager.h"

namespace nx
{

Sidebar::Sidebar (AppContext& c) : ctx (c), pluginSlot (c, "sidebar.bottom")
{
    search.setTextToShowWhenEmpty ("Rechercher partout", ctx.theme().textDim());
    search.setFont (style::font (14.0f));
    search.setIndents (34, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.onReturnKey = [this] { if (search.getText().trim().isNotEmpty()) ctx.navigate ({ PageId::Search, search.getText().trim() }); };
    search.onTextChange = [this] { startTimer (350); };
    addAndMakeVisible (search);
    addAndMakeVisible (pluginSlot);
    refresh();
}

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
    auto item = [this] (const juce::String& s, style::Icon icon, PageRequest target, juce::Colour dot = {}) { Item i; i.label = s; i.icon = icon; i.target = target; i.dot = dot; items.push_back (i); };

    header ("Écouter"_t);
    item ("Accueil", style::Icon::Home, { PageId::Home });
    item ("En cours", style::Icon::Expand, { PageId::NowPlaying });
    header ("Bibliothèque"_t);
    item ("Ajouts récents"_t, style::Icon::Clock, { PageId::RecentlyAdded });
    item ("Albums", style::Icon::Album, { PageId::Albums });
    item ("Artistes", style::Icon::Artist, { PageId::Artists });
    item ("Morceaux", style::Icon::Song, { PageId::Songs });
    item ("Playlists", style::Icon::Playlist, { PageId::Playlists });
    header ("Sources");
    item ("Fichiers locaux", style::Icon::Folder, { PageId::Source, "local" });
    const auto ok = juce::Colour (0xff00f5a0), off = juce::Colours::grey;
    for (auto& p : ctx.providers().getProviders())
    {
        const auto icon = p->getId() == "upnp" ? style::Icon::Network : style::Icon::Cloud;
        item (p->getDisplayName(), icon, { PageId::Source, p->getId() }, p->isSignedIn() ? ok : off);
    }
    const auto playlists = ctx.library().getPlaylists();
    if (! playlists.empty())
    {
        header ("Mes playlists");
        for (auto& pl : playlists) item (pl.name, style::Icon::Playlist, { PageId::PlaylistDetail, juce::String (pl.id) });
    }
    repaint();
}

void Sidebar::resized()
{
    auto r = getLocalBounds().reduced (14, 14);
    r.removeFromTop (44);   // logo
    search.setBounds (r.removeFromTop (34));
    r.removeFromTop (8);
    listTop = r.getY();
    auto bottom = r.removeFromBottom (36);
    juce::ignoreUnused (bottom);
    const int ph = pluginSlot.isEmpty() ? 0 : juce::jmin (pluginSlot.getPreferredHeight(), 140);
    pluginSlot.setVisible (ph > 0);
    pluginSlot.setBounds (r.removeFromBottom (ph));
}

int Sidebar::itemAt (int y) const
{
    int pos = listTop - scroll;
    const int maxY = getHeight() - 50 - (pluginSlot.isVisible() ? pluginSlot.getHeight() : 0);
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const int h = items[(size_t) i].header ? headerHeight : itemHeight;
        if (y >= pos && y < pos + h && pos + h <= maxY) return items[(size_t) i].header ? -1 : i;
        pos += h;
    }
    // settings button at the bottom
    if (y >= getHeight() - 50) return -2;
    return -1;
}

void Sidebar::mouseMove (const juce::MouseEvent& e)
{
    const int i = itemAt (e.y);
    if (i != hover) { hover = i; repaint(); }
}

void Sidebar::mouseUp (const juce::MouseEvent& e)
{
    const int i = itemAt (e.y);
    if (i == -2) ctx.navigate ({ PageId::Settings });
    else if (i >= 0) ctx.navigate (items[(size_t) i].target);
}

void Sidebar::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), t.radius());

    // logo
    auto logo = getLocalBounds().reduced (16, 14).removeFromTop (40);
    auto mark = logo.removeFromLeft (30).toFloat().withSizeKeepingCentre (28, 28);
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
    logo.removeFromLeft (8);
    g.setColour (t.textCol());
    g.setFont (style::font (19.0f, true));
    g.drawText ("NX Player", logo, juce::Justification::centredLeft);

    style::drawIcon (g, style::Icon::Search, search.getBounds().toFloat().removeFromLeft (34).withSizeKeepingCentre (16, 16), t.textDim());

    // items
    g.saveState();
    const int maxY = getHeight() - 50 - (pluginSlot.isVisible() ? pluginSlot.getHeight() : 0);
    g.reduceClipRegion (0, listTop, getWidth(), maxY - listTop);
    int y = listTop - scroll;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& it = items[(size_t) i];
        if (it.header)
        {
            g.setColour (t.textDim());
            g.setFont (style::font (11.5f, true));
            g.drawText (style::upper (it.label), 16, y + 12, getWidth() - 32, 20, juce::Justification::centredLeft);
            y += headerHeight;
            continue;
        }
        auto row = juce::Rectangle<int> (10, y, getWidth() - 20, itemHeight);
        const bool isActive = it.target.page == active.page && (it.target.a == active.a || it.target.a.isEmpty() && active.a.isEmpty());
        if (isActive)
        {
            g.setColour (t.accent().withAlpha (0.16f));
            g.fillRoundedRectangle (row.toFloat(), 8.0f);
        }
        else if (i == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (row.toFloat(), 8.0f);
        }
        auto ir = row.reduced (8, 0);
        style::drawIcon (g, it.icon, ir.removeFromLeft (20).toFloat().withSizeKeepingCentre (18, 18), isActive ? t.accent() : t.textCol().withAlpha (0.8f));
        ir.removeFromLeft (10);
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

    // settings
    auto s = juce::Rectangle<int> (10, getHeight() - 46, getWidth() - 20, 34);
    if (hover == -2 || active.page == PageId::Settings)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (s.toFloat(), 8.0f);
    }
    auto sr = s.reduced (8, 0);
    style::drawIcon (g, style::Icon::Settings, sr.removeFromLeft (20).toFloat().withSizeKeepingCentre (18, 18), t.textCol().withAlpha (0.8f));
    sr.removeFromLeft (10);
    g.setColour (t.textCol());
    g.setFont (style::font (14.0f));
    g.drawText ("Réglages"_t, sr, juce::Justification::centredLeft);
}

} // namespace nx
