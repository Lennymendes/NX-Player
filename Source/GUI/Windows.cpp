// NX Player — MIT License
#include "Windows.h"

namespace nx
{

MiniPlayer::MiniPlayer (AppContext& c)
    : ctx (c), artwork (c, 256),
      prev (style::Icon::Previous, "Précédent"_t, c), play (style::Icon::Play, "Lecture / pause"_t, c),
      next (style::Icon::Next, "Suivant"_t, c), expand (style::Icon::Expand, "Fenêtre principale"_t, c), seek (c)
{
    artwork.shadow = false;
    artwork.cornerRadius = 8.0f;
    play.primary = true;
    seek.showTimes = false;
    for (auto* comp : std::initializer_list<juce::Component*> { &artwork, &prev, &play, &next, &expand, &seek }) addAndMakeVisible (comp);
    prev.onClick = [this] { ctx.engine().previous(); };
    next.onClick = [this] { ctx.engine().next(); };
    play.onClick = [this] { ctx.engine().togglePlayPause(); };
    expand.onClick = [this] { ctx.toggleMiniPlayer(); };
    seek.onSeek = [this] (double s) { ctx.engine().seek (s); };
    startTimerHz (20);
    trackChanged();
}

void MiniPlayer::trackChanged()
{
    const auto* t = ctx.currentTrack();
    artwork.setArtwork (t ? t->artworkKey : juce::String(), t ? t->album : juce::String ("NX"));
    blurred = {};
    if (t != nullptr)
    {
        auto art = ctx.artwork().loadNow (t->artworkKey, 128);
        if (art.isValid())
        {
            blurred = imaging::coverFit (art, 96, 36).convertedToFormat (juce::Image::RGB);
            imaging::blur (blurred, 8);
        }
    }
    repaint();
}

void MiniPlayer::timerCallback()
{
    perf::retune (*this, 20, 4);
    play.setIcon (ctx.engine().getState() == PlaybackEngine::State::Playing ? style::Icon::Pause : style::Icon::Play);
    const auto* t = ctx.currentTrack();
    seek.setPosition (ctx.engine().getPositionSeconds(), t ? t->duration : 0.0);
}

void MiniPlayer::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    g.fillAll (t.bg());
    if (blurred.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (blurred, getLocalBounds().toFloat());
    }
    g.setColour (t.colour ("--nx-glass-strong", juce::Colour (0xcc0a0d10)));
    g.fillAll();
    auto r = getLocalBounds().reduced (12);
    r.removeFromLeft (r.getHeight() + 12);
    auto text = r.removeFromTop (r.getHeight() / 2);
    const auto* tr = ctx.currentTrack();
    g.setColour (t.textCol());
    g.setFont (style::font (14.0f, true));
    g.drawText (tr ? tr->title : juce::String ("NX Player"), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour (t.textDim());
    g.setFont (style::font (12.5f));
    g.drawText (tr ? tr->displayArtist() : juce::String(), text, juce::Justification::topLeft, true);
}

void MiniPlayer::resized()
{
    auto r = getLocalBounds().reduced (12);
    artwork.setBounds (r.removeFromLeft (r.getHeight()));
    r.removeFromLeft (12);
    r.removeFromTop (r.getHeight() / 2);
    seek.setBounds (r.removeFromBottom (10));
    auto controls = r;
    const int b = juce::jmin (36, controls.getHeight());
    expand.setBounds (controls.removeFromRight (b));
    next.setBounds (controls.removeFromRight (b));
    play.setBounds (controls.removeFromRight (b));
    prev.setBounds (controls.removeFromRight (b));
}

//==============================================================================
MainWindow::MainWindow (AppContext& ctx, juce::LookAndFeel& lf)
    : juce::DocumentWindow ("NX Player", ctx.theme().bg(), juce::DocumentWindow::allButtons)
{
    setLookAndFeel (&lf);
    setUsingNativeTitleBar (true);
    main = new MainComponent (ctx);
    setContentOwned (main, false);
    setResizable (true, true);
    setResizeLimits (980, 640, 10000, 10000);
    const auto saved = ctx.settings().get ("windowBounds").toString();
    if (saved.isNotEmpty()) restoreWindowStateFromString (saved);
    else centreWithSize (1360, 860);
    setVisible (true);
    main->grabKeyboardFocus();   // space = play / pause straight away
}

MiniWindow::MiniWindow (AppContext& ctx)
    : juce::DocumentWindow ("NX Player — mini"_t, ctx.theme().bg(), 0)
{
    setUsingNativeTitleBar (false);
    setTitleBarHeight (0);
    mini = new MiniPlayer (ctx);
    setContentOwned (mini, false);
    setSize (380, 96);
    setAlwaysOnTop (true);
    setResizable (false, false);
}

} // namespace nx
