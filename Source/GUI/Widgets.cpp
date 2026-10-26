// NX Player — MIT License
#include "Widgets.h"
#include "../Ext/PluginManager.h"

namespace nx
{

//==============================================================================
void IconButton::paintButton (juce::Graphics& g, bool hi, bool down)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat();
    if (primary)
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight()) - 2.0f;
        auto c = r.withSizeKeepingCentre (d, d);
        g.setColour (t.textCol().withAlpha (down ? 0.85f : 1.0f));
        g.fillEllipse (c);
        style::drawIcon (g, icon, c.reduced (d * 0.28f).translated (icon == style::Icon::Play ? d * 0.03f : 0.0f, 0.0f), t.bg());
        return;
    }
    if ((hi || down) && isEnabled())
    {
        const float d = juce::jmin (r.getWidth(), r.getHeight());
        g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.08f));
        g.fillEllipse (r.withSizeKeepingCentre (d, d));
    }
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.62f;
    style::drawIcon (g, icon, r.withSizeKeepingCentre (s, s),
                     ! isEnabled() ? t.textCol().withAlpha (0.28f) : active ? t.accent() : (hi ? t.textCol() : t.textCol().withAlpha (0.82f)));
    if (active)
    {
        g.setColour (t.accent());
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getCentreX(), r.getBottom() - 3.0f }));
    }
}

//==============================================================================
ArtworkView::ArtworkView (AppContext& c, int px) : ctx (c), size (px)
{
    ctx.artwork().addChangeListener (this);
    setInterceptsMouseClicks (false, false);
}

ArtworkView::~ArtworkView() { ctx.artwork().removeChangeListener (this); }

void ArtworkView::setArtwork (const juce::String& k, const juce::String& text)
{
    if (k == key && text == placeholderText) return;
    key = k;
    placeholderText = text;
    placeholderImage = {};
    repaint();
}

void ArtworkView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (d, d).reduced (shadow ? 6.0f : 0.0f);
    if (shadow)
    {
        juce::DropShadow ds (juce::Colours::black.withAlpha (0.55f), 18, { 0, 8 });
        juce::Path p;
        p.addRoundedRectangle (r, cornerRadius);
        ds.drawForPath (g, p);
    }
    auto img = ctx.artwork().get (key, size);
    if (! img.isValid())
    {
        if (! placeholderImage.isValid())
            placeholderImage = imaging::placeholder (placeholderText, 256, ctx.theme().accent(), ctx.theme().accent2());
        img = placeholderImage;
    }
    juce::Path clip;
    clip.addRoundedRectangle (r, cornerRadius);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setOpacity (1.0f);   // drawImage uses the current colour's alpha (left at the shadow's)
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (img, r);
    g.restoreState();
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r, cornerRadius, 1.0f);
}

//==============================================================================
void QualityBadge::paint (juce::Graphics& g)
{
    if (label.isEmpty()) return;
    const auto c = style::qualityColour (ctx.theme(), quality);
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (c.withAlpha (isMouseOver() ? 0.24f : 0.14f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c.withAlpha (0.55f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    auto icon = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    style::drawIcon (g, style::Icon::Signal, icon, c);
    g.setColour (c);
    g.setFont (style::font (12.0f, true));
    g.drawText (label, r.withTrimmedRight (8.0f), juce::Justification::centredLeft);
}

//==============================================================================
double SeekBar::xToTime (float x) const
{
    const auto bar = getLocalBounds().toFloat().reduced (showTimes ? 46.0f : 0.0f, 0.0f);
    return length * juce::jlimit (0.0, 1.0, (double) ((x - bar.getX()) / juce::jmax (1.0f, bar.getWidth())));
}

void SeekBar::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat();
    if (showTimes)
    {
        g.setColour (t.textDim());
        g.setFont (style::font (11.5f));
        g.drawText (style::formatTime (position), r.removeFromLeft (46.0f), juce::Justification::centred);
        g.drawText (length > 0 ? "-" + style::formatTime (length - position) : juce::String ("--:--"), r.removeFromRight (46.0f), juce::Justification::centred);
    }
    const bool over = isMouseOver() || dragging;
    const float th = over ? 6.0f : 4.0f;
    auto track = r.withSizeKeepingCentre (r.getWidth(), th);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillRoundedRectangle (track, th * 0.5f);
    const float frac = length > 0 ? (float) juce::jlimit (0.0, 1.0, position / length) : 0.0f;
    g.setGradientFill (juce::ColourGradient (t.accent(), track.getX(), 0, t.accent2(), track.getRight(), 0, false));
    g.fillRoundedRectangle (track.withWidth (track.getWidth() * frac), th * 0.5f);
    if (over && length > 0)
    {
        g.setColour (t.textCol());
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ track.getX() + track.getWidth() * frac, track.getCentreY() }));
    }
}

void SeekBar::mouseDown (const juce::MouseEvent& e) { if (length > 0) { dragging = true; position = xToTime ((float) e.x); repaint(); } }
void SeekBar::mouseDrag (const juce::MouseEvent& e) { if (dragging) { position = xToTime ((float) e.x); repaint(); } }
void SeekBar::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging) return;
    dragging = false;
    position = xToTime ((float) e.x);
    if (onSeek) onSeek (position);
}

//==============================================================================
PluginSlot::PluginSlot (AppContext& c, juce::String s, bool h) : ctx (c), slot (std::move (s)), horizontal (h)
{
    setInterceptsMouseClicks (false, false);
    refreshPlugins();
}

void PluginSlot::refreshPlugins()
{
    ids = ctx.plugins().getEnabledForSlot (slot);
    int fps = 0;
    for (auto& id : ids) fps = juce::jmax (fps, ctx.plugins().getFps (id));
    if (fps > 0) startTimerHz (fps);
    else stopTimer();
    repaint();
}

int PluginSlot::getPreferredHeight() const
{
    int h = 0;
    for (auto& id : ids) h += ctx.plugins().getPreferredHeight (id) + 8;
    return h;
}

void PluginSlot::paint (juce::Graphics& g)
{
    if (ids.isEmpty()) return;
    auto r = getLocalBounds().toFloat();
    if (horizontal)
    {
        const float w = r.getWidth() / (float) ids.size();
        for (auto& id : ids) ctx.plugins().render (id, slot, g, r.removeFromLeft (w).reduced (2.0f));
        return;
    }
    for (auto& id : ids)
    {
        const float h = (float) ctx.plugins().getPreferredHeight (id);
        ctx.plugins().render (id, slot, g, r.removeFromTop (h));
        r.removeFromTop (8.0f);
    }
}

//==============================================================================
AlbumGrid::AlbumGrid (AppContext& c) : ctx (c) { ctx.artwork().addChangeListener (this); }
AlbumGrid::~AlbumGrid() { ctx.artwork().removeChangeListener (this); }

void AlbumGrid::setAlbums (std::vector<AlbumInfo> a) { albums = std::move (a); hover = -1; repaint(); }

int AlbumGrid::columns() const
{
    const float card = ctx.theme().number ("--nx-card-size", 176.0f);
    return juce::jmax (1, (int) ((float) (getWidth() + 20) / (card + 20.0f)));
}

juce::Rectangle<int> AlbumGrid::cardBounds (int i) const
{
    const int cols = columns();
    const int gap = 20;
    const int w = (getWidth() - gap * (cols - 1)) / cols;
    const int h = w + 52;
    return { (i % cols) * (w + gap), (i / cols) * (h + gap), w, h };
}

int AlbumGrid::heightForWidth (int width) const
{
    if (albums.empty()) return 0;
    const float card = ctx.theme().number ("--nx-card-size", 176.0f);
    const int cols = juce::jmax (1, (int) ((float) (width + 20) / (card + 20.0f)));
    int rows = ((int) albums.size() + cols - 1) / cols;
    if (maxRows > 0) rows = juce::jmin (rows, maxRows);
    const int w = (width - 20 * (cols - 1)) / cols;
    return rows * (w + 52 + 20);
}

int AlbumGrid::indexAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) albums.size(); ++i)
        if (cardBounds (i).contains (p)) return i;
    return -1;
}

void AlbumGrid::mouseMove (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i != hover) { hover = i; repaint(); }
}

void AlbumGrid::mouseUp (const juce::MouseEvent& e)
{
    const int i = indexAt (e.getPosition());
    if (i < 0) return;
    const auto art = cardBounds (i).removeFromTop (cardBounds (i).getWidth());
    const auto playButton = juce::Rectangle<int> (40, 40).withPosition (art.getRight() - 50, art.getBottom() - 50);
    if (playButton.contains (e.getPosition()) && onPlay) onPlay (albums[(size_t) i]);
    else if (onOpen) onOpen (albums[(size_t) i]);
}

void AlbumGrid::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    const int cols = columns();
    const int limit = maxRows > 0 ? juce::jmin ((int) albums.size(), maxRows * cols) : (int) albums.size();
    const auto clip = g.getClipBounds();
    for (int i = 0; i < limit; ++i)
    {
        auto card = cardBounds (i);
        if (! card.intersects (clip)) continue;
        const auto& a = albums[(size_t) i];
        auto art = card.removeFromTop (card.getWidth()).toFloat();
        const bool hot = i == hover;
        if (hot) art = art.expanded (3.0f);

        juce::Path p;
        p.addRoundedRectangle (art, t.number ("--nx-radius-small", 8.0f));
        juce::DropShadow (juce::Colours::black.withAlpha (hot ? 0.55f : 0.35f), hot ? 22 : 12, { 0, 6 }).drawForPath (g, p);
        auto img = ctx.artwork().get (a.artworkKey, 320);
        if (! img.isValid()) img = imaging::placeholder (a.title, 128, t.accent(), t.accent2());
        g.saveState();
        g.reduceClipRegion (p);
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (img, art);
        if (hot)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (art);
        }
        g.restoreState();

        if (hot)
        {
            auto pb = juce::Rectangle<float> (40, 40).withPosition (art.getRight() - 50, art.getBottom() - 50);
            g.setColour (t.accent());
            g.fillEllipse (pb);
            style::drawIcon (g, style::Icon::Play, pb.reduced (11.0f).translated (1.5f, 0), t.colour ("--nx-on-accent", juce::Colours::black));
        }

        // badge on the artwork
        const auto badge = a.badge();
        if (badge.isNotEmpty() && ! a.lossy)
        {
            const float bw = (float) badge.length() * 7.0f + 14.0f;
            auto br = juce::Rectangle<float> (bw, 18.0f).withPosition (art.getX() + 8, art.getY() + 8);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (br, 6.0f);
            g.setColour (a.hasDsd ? style::qualityColour (t, PathQuality::BitPerfect) : t.accent());
            g.setFont (style::font (11.0f, true));
            g.drawText (badge, br, juce::Justification::centred);
        }

        card.removeFromTop (8);
        g.setColour (t.textCol());
        g.setFont (style::font (14.0f, true));
        g.drawText (a.title, card.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText (a.artist + (a.year > 0 ? " · "_t + juce::String (a.year) : juce::String()), card.removeFromTop (18), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
TrackList::TrackList (AppContext& c) : ctx (c) { ctx.artwork().addChangeListener (this); }
TrackList::~TrackList() { ctx.artwork().removeChangeListener (this); }

void TrackList::setTracks (std::vector<TrackInfo> t, bool art, bool alb, bool num)
{
    tracks = std::move (t);
    artwork = art; album = alb; numbered = num;
    hover = selected = -1;
    repaint();
}

void TrackList::mouseMove (const juce::MouseEvent& e)
{
    const int i = e.y / rowHeight;
    if (i != hover) { hover = i < (int) tracks.size() ? i : -1; repaint(); }
}

void TrackList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = e.y / rowHeight;
    if (i >= 0 && i < (int) tracks.size()) ctx.playTracks (tracks, i);
}

void TrackList::mouseUp (const juce::MouseEvent& e)
{
    const int i = e.y / rowHeight;
    if (i < 0 || i >= (int) tracks.size()) return;
    selected = i;
    repaint();
    if (! e.mods.isPopupMenu()) return;

    const auto track = tracks[(size_t) i];
    juce::PopupMenu m;
    m.addItem ("Lire", [this, i] { ctx.playTracks (tracks, i); });
    m.addItem ("Ajouter à la file d'attente"_t, [this, track] { ctx.enqueue ({ track }); });
    m.addItem ("Voir l'album", [this, track] { ctx.navigate ({ PageId::AlbumDetail, track.albumArtist.isNotEmpty() ? track.albumArtist : track.artist, track.album }); });
    m.addItem ("Voir l'artiste", [this, track] { ctx.navigate ({ PageId::ArtistDetail, track.displayArtist() }); });
    if (track.id > 0)
        m.addItem (track.favourite ? "Retirer des favoris" : "Ajouter aux favoris", [this, i]
        {
            auto& tr = tracks[(size_t) i];
            tr.favourite = ! tr.favourite;
            ctx.library().setFavourite (tr.id, tr.favourite);
            repaint();
        });
    if (track.credits.isNotEmpty())
    {
        juce::PopupMenu credits;
        for (auto& line : juce::StringArray::fromLines (track.credits)) credits.addItem (line, false, false, nullptr);
        m.addSubMenu ("Crédits"_t, credits);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void TrackList::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    const auto* playing = ctx.currentTrack();
    const auto clip = g.getClipBounds();
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        auto row = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);
        if (! row.intersects (clip)) continue;
        const auto& tr = tracks[(size_t) i];
        const bool isPlaying = playing != nullptr && playing->source == tr.source && playing->externalId == tr.externalId;
        if (i == hover || i == selected)
        {
            g.setColour (juce::Colours::white.withAlpha (i == selected ? 0.08f : 0.05f));
            g.fillRoundedRectangle (row.toFloat().reduced (0, 2), 8.0f);
        }
        auto r = row.reduced (10, 6);

        if (numbered)
        {
            auto n = r.removeFromLeft (30);
            if (isPlaying)
            {
                // three animated bars
                const double time = juce::Time::getMillisecondCounterHiRes() / 1000.0;
                for (int b = 0; b < 3; ++b)
                {
                    const float h = 6.0f + 8.0f * (float) (0.5 + 0.5 * std::sin (time * (5.0 + b * 1.7) + b));
                    g.setColour (t.accent());
                    g.fillRoundedRectangle ((float) n.getX() + 4.0f + b * 5.0f, (float) n.getCentreY() + 7.0f - h, 3.0f, h, 1.5f);
                }
            }
            else
            {
                g.setColour (t.textDim());
                g.setFont (style::font (13.0f));
                g.drawText (tr.trackNumber > 0 ? juce::String (tr.trackNumber) : juce::String (i + 1), n, juce::Justification::centredLeft);
            }
        }
        if (artwork)
        {
            auto a = r.removeFromLeft (r.getHeight()).toFloat();
            auto img = ctx.artwork().get (tr.artworkKey, 96);
            if (! img.isValid()) img = imaging::placeholder (tr.album.isNotEmpty() ? tr.album : tr.title, 64, t.accent(), t.accent2());
            juce::Path p;
            p.addRoundedRectangle (a, 5.0f);
            g.saveState();
            g.reduceClipRegion (p);
            g.setOpacity (1.0f);
            g.drawImage (img, a);
            g.restoreState();
            r.removeFromLeft (12);
        }

        auto right = r.removeFromRight (64);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText (style::formatTime (tr.duration), right, juce::Justification::centredRight);

        const auto badge = tr.resolutionBadge();
        if (badge.isNotEmpty())
        {
            auto b = r.removeFromRight (78).withSizeKeepingCentre (70, 18).toFloat();
            const auto c = tr.lossy ? style::qualityColour (t, PathQuality::LowQuality)
                         : tr.isDsd ? style::qualityColour (t, PathQuality::BitPerfect) : t.accent();
            style::paintBadge (g, b, badge, c);
        }
        if (tr.source != "local")
        {
            auto s = r.removeFromRight (90);
            g.setColour (t.textDim().withAlpha (0.8f));
            g.setFont (style::font (11.5f));
            const auto src = tr.source.startsWith ("upnp") ? juce::String ("NAS") : tr.source.substring (0, 1).toUpperCase() + tr.source.substring (1);
            g.drawText (src, s, juce::Justification::centredRight);
        }
        juce::Rectangle<int> albumCol;
        if (album && r.getWidth() > 420) albumCol = r.removeFromRight (r.getWidth() / 3);

        g.setColour (isPlaying ? t.accent() : t.textCol());
        g.setFont (style::font (14.0f, true));
        g.drawText (tr.title, r.removeFromTop (r.getHeight() / 2).withTrimmedRight (12), juce::Justification::bottomLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText (tr.displayArtist(), r.withTrimmedRight (12), juce::Justification::topLeft, true);
        if (! albumCol.isEmpty())
            g.drawText (tr.album, albumCol, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
ScrollPage::ScrollPage (AppContext& c) : ctx (c)
{
    viewport.setViewedComponent (&contentComp, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

void ScrollPage::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutContent (getWidth() - viewport.getScrollBarThickness());
}

void ScrollPage::setContentHeight (int h)
{
    contentComp.setSize (getWidth() - viewport.getScrollBarThickness(), juce::jmax (h, getHeight()));
}

void drawPageTitle (juce::Graphics& g, const Theme& t, juce::Rectangle<int> r, const juce::String& title, const juce::String& subtitle)
{
    g.setColour (t.textCol());
    g.setFont (style::font (32.0f, true));
    g.drawText (title, r.removeFromTop (44), juce::Justification::bottomLeft, true);
    if (subtitle.isNotEmpty())
    {
        g.setColour (t.textDim());
        g.setFont (style::font (14.0f));
        g.drawText (subtitle, r.removeFromTop (24), juce::Justification::centredLeft, true);
    }
}

} // namespace nx
