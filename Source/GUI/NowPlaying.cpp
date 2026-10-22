// NX Player — MIT License
#include "NowPlaying.h"
#include "../Ext/PluginManager.h"

namespace nx
{

//==============================================================================
SignalPathView::SignalPathView (AppContext& c, bool comp) : ctx (c), compact (comp)
{
    setInterceptsMouseClicks (false, false);
    refresh();
    startTimerHz (2);
}

int SignalPathView::getIdealHeight() const
{
    return 56 + (int) path.nodes.size() * (compact ? 50 : 54);
}

void SignalPathView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    const auto overall = style::qualityColour (t, path.overall);

    g.setColour (t.textDim());
    g.setFont (style::font (11.5f, true));
    g.drawText ("CHEMIN DU SIGNAL", r.removeFromTop (18), juce::Justification::centredLeft);
    if (path.nodes.empty())
    {
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText ("Aucune lecture en cours", r.removeFromTop (24), juce::Justification::centredLeft);
        return;
    }
    auto head = r.removeFromTop (28);
    g.setColour (overall);
    g.fillEllipse (head.removeFromLeft (12).withSizeKeepingCentre (10, 10));
    head.removeFromLeft (8);
    g.setFont (style::font (15.0f, true));
    g.drawText (toString (path.overall), head, juce::Justification::centredLeft);
    r.removeFromTop (6);

    const float step = compact ? 50.0f : 54.0f;
    const float x = r.getX() + 6.0f;
    for (size_t i = 0; i < path.nodes.size(); ++i)
    {
        const auto& n = path.nodes[i];
        auto row = r.removeFromTop (step);
        const auto c = style::qualityColour (t, n.quality);
        if (i + 1 < path.nodes.size())
        {
            g.setColour (c.withAlpha (0.45f));
            g.fillRect (x - 1.0f, row.getY() + 14.0f, 2.0f, step - 8.0f);
        }
        g.setColour (c);
        g.fillEllipse (x - 5.0f, row.getY() + 4.0f, 10.0f, 10.0f);
        auto text = row.withTrimmedLeft (22.0f);
        g.setColour (t.textDim());
        g.setFont (style::font (11.0f, true));
        g.drawText (style::upper (n.stage), text.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (t.textCol());
        g.setFont (style::font (13.5f, true));
        g.drawText (n.title, text.removeFromTop (17), juce::Justification::centredLeft, true);
        if (! compact || n.detail.length() < 48)
        {
            g.setColour (t.textDim());
            g.setFont (style::font (12.0f));
            g.drawText (n.detail, text.removeFromTop (16), juce::Justification::centredLeft, true);
        }
    }
}

//==============================================================================
LyricsView::LyricsView (AppContext& c) : ctx (c) { startTimerHz (60); }

void LyricsView::setLyrics (const Lyrics& l)
{
    lyrics = l;
    current = -1;
    scroll = targetScroll = manualOffset = 0.0f;
    layoutLines();
    repaint();
}

void LyricsView::layoutLines()
{
    lineBounds.clear();
    const auto& t = ctx.theme();
    const float size = t.number ("--nx-lyrics-size", 30.0f);
    const float w = (float) getWidth() - 20.0f;
    if (w <= 0) return;
    float y = 0.0f;
    auto measure = [&] (const juce::String& text, float fontSize)
    {
        juce::AttributedString as;
        as.append (text.isEmpty() ? juce::String::fromUTF8 ("\xe2\x99\xaa") : text, style::font (fontSize, true));
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (as, w);
        return tl.getHeight();
    };
    if (lyrics.synced)
        for (auto& l : lyrics.lines)
        {
            const float h = measure (l.text, size) + size * 0.55f;
            lineBounds.push_back ({ 10.0f, y, w, h });
            y += h;
        }
    else if (lyrics.plain.isNotEmpty())
        lineBounds.push_back ({ 10.0f, 0.0f, w, measure (lyrics.plain, size * 0.62f) + 40.0f });
}

void LyricsView::timerCallback()
{
    if (! isVisible() || getWidth() <= 0) return;
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (lyrics.synced && ! lineBounds.empty())
    {
        const int idx = lyrics.indexAt (ctx.engine().getPositionSeconds() + 0.15);
        if (idx != current) current = idx;
        const int c = juce::jmax (0, current);
        if (now - lastManual > 3.0) manualOffset *= 0.9f;
        targetScroll = lineBounds[(size_t) c].getCentreY() - (float) getHeight() * 0.36f + manualOffset;
    }
    else
    {
        targetScroll = manualOffset;
    }
    const float speed = 0.16f * ctx.theme().number ("--nx-animation-speed", 1.0f);
    if (std::abs (targetScroll - scroll) > 0.3f)
    {
        scroll += (targetScroll - scroll) * juce::jlimit (0.02f, 1.0f, speed);
        repaint();
    }
    else if (lyrics.synced) repaint();
}

void LyricsView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    manualOffset -= w.deltaY * 220.0f;
    lastManual = juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

void LyricsView::mouseUp (const juce::MouseEvent& e)
{
    if (! lyrics.synced) return;
    for (size_t i = 0; i < lineBounds.size(); ++i)
        if (lineBounds[i].translated (0, -scroll).contains (e.position))
        {
            ctx.engine().seek (lyrics.lines[i].time);
            manualOffset = 0.0f;
            return;
        }
}

void LyricsView::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    const float size = t.number ("--nx-lyrics-size", 30.0f);
    if (lyrics.isEmpty())
    {
        g.setColour (t.textDim());
        g.setFont (style::font (18.0f, true));
        g.drawFittedText (statusText.isNotEmpty() ? statusText : juce::String ("Pas de paroles pour ce morceau"),
                          getLocalBounds().reduced (20), juce::Justification::centredLeft, 3);
        return;
    }
    const auto active = t.colour ("--nx-lyrics-active", t.textCol());
    const auto inactive = t.colour ("--nx-lyrics-inactive", t.textCol().withAlpha (0.3f));

    if (! lyrics.synced)
    {
        juce::AttributedString as;
        as.append (lyrics.plain, style::font (size * 0.62f, true), active.withAlpha (0.85f));
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (as, (float) getWidth() - 20.0f);
        tl.draw (g, { 10.0f, 20.0f - scroll, (float) getWidth() - 20.0f, tl.getHeight() });
        return;
    }

    for (size_t i = 0; i < lineBounds.size(); ++i)
    {
        auto b = lineBounds[i].translated (0, -scroll);
        if (b.getBottom() < -50 || b.getY() > (float) getHeight() + 50) continue;
        const int dist = std::abs ((int) i - current);
        const bool isCurrent = (int) i == current;
        auto colour = isCurrent ? active : inactive.withMultipliedAlpha (juce::jmax (0.35f, 1.0f - (float) dist * 0.12f));
        const auto text = lyrics.lines[i].text.isEmpty() ? juce::String::fromUTF8 ("\xe2\x99\xaa") : lyrics.lines[i].text;
        juce::AttributedString as;
        as.append (text, style::font (size, true), colour);
        as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (as, b.getWidth());
        if (isCurrent)
        {
            // soft glow behind the active line
            g.setColour (t.accent().withAlpha (0.10f));
            g.fillRoundedRectangle (b.withHeight (tl.getHeight() + 12.0f).translated (-8.0f, -6.0f).withWidth (juce::jmin (b.getWidth(), tl.getWidth() + 24.0f)), 12.0f);
        }
        tl.draw (g, b.withHeight (tl.getHeight()));
    }
}

//==============================================================================
NowPlayingPage::NowPlayingPage (AppContext& c)
    : ctx (c), artwork (c, 1024), lyricsView (c), signalPath (c, true), pluginSlot (c, "nowplaying.side")
{
    artwork.cornerRadius = 16.0f;
    addAndMakeVisible (artwork);
    addAndMakeVisible (lyricsView);
    addAndMakeVisible (signalPath);
    addAndMakeVisible (pluginSlot);
    trackChanged();
}

void NowPlayingPage::trackChanged()
{
    if (const auto* t = ctx.currentTrack())
        artwork.setArtwork (t->artworkKey, t->album.isNotEmpty() ? t->album : t->title);
    else
        artwork.setArtwork ({}, "NX");
    signalPath.refresh();
    repaint();
}

void NowPlayingPage::resized()
{
    auto r = getLocalBounds().reduced (32, 24);
    auto left = r.removeFromLeft (juce::jmin (r.getWidth() * 40 / 100, 520));
    r.removeFromLeft (40);

    // left: artwork, title block, signal path
    const int pathH = juce::jmin (signalPath.getIdealHeight(), 300);
    const int art = juce::jmax (140, juce::jmin (left.getWidth(), left.getHeight() - 84 - pathH - 16));
    artwork.setBounds (left.removeFromTop (art).withSizeKeepingCentre (art, art));
    left.removeFromTop (10);
    infoArea = left.removeFromTop (74);
    left.removeFromTop (12);
    signalPath.setVisible (true);
    signalPath.setBounds (left.removeFromTop (juce::jmin (left.getHeight(), pathH)));

    // right: lyrics, plug-ins below
    const int ph = pluginSlot.isEmpty() ? 0 : juce::jmin (pluginSlot.getPreferredHeight(), r.getHeight() / 3);
    pluginSlot.setVisible (ph > 0);
    pluginSlot.setBounds (r.removeFromBottom (ph));
    if (ph > 0) r.removeFromBottom (16);
    lyricsView.setVisible (showLyrics);
    lyricsView.setBounds (r);
}

void NowPlayingPage::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = infoArea;
    if (const auto* tr = ctx.currentTrack())
    {
        g.setColour (t.textCol());
        g.setFont (style::font (24.0f, true));
        g.drawText (tr->title, r.removeFromTop (32), juce::Justification::centred, true);
        g.setColour (t.textDim());
        g.setFont (style::font (16.0f));
        g.drawText (tr->displayArtist() + (tr->album.isNotEmpty() ? " — "_t + tr->album : juce::String()), r.removeFromTop (22), juce::Justification::centred, true);
        const auto plan = ctx.engine().getCurrentPlan();
        g.setFont (style::font (13.0f));
        g.setColour (t.accent());
        if (plan.source.sampleRate > 0) g.drawText (plan.source.describe(), r.removeFromTop (20), juce::Justification::centred, true);
    }
    else
    {
        g.setColour (t.textDim());
        g.setFont (style::font (18.0f, true));
        g.drawText ("Rien en lecture", r, juce::Justification::centred);
    }
}

} // namespace nx
