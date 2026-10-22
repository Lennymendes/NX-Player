// NX Player — MIT License
#include "PlayerBar.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Ext/PluginManager.h"

namespace nx
{

PlayerBar::PlayerBar (AppContext& c)
    : lyricsButton (style::Icon::Lyrics, "Paroles", c),
      queueButton (style::Icon::Queue, "File d'attente"_t, c),
      ctx (c), artwork (c, 128),
      shuffleButton (style::Icon::Shuffle, "Aléatoire"_t, c),
      prevButton (style::Icon::Previous, "Précédent"_t, c),
      playButton (style::Icon::Play, "Lecture / pause", c),
      nextButton (style::Icon::Next, "Suivant", c),
      repeatButton (style::Icon::Repeat, "Répéter"_t, c),
      miniButton (style::Icon::Mini, "Mini-lecteur"_t, c),
      volumeButton (style::Icon::Volume, "Volume (100 % = bit-perfect)", c),
      seek (c), badge (c), pluginSlot (c, "playerbar.right", true)
{
    artwork.cornerRadius = 8.0f;
    artwork.shadow = false;
    playButton.primary = true;
    for (auto* b : { &shuffleButton, &prevButton, &playButton, &nextButton, &repeatButton, &lyricsButton, &queueButton, &miniButton, &volumeButton })
        addAndMakeVisible (b);
    addAndMakeVisible (artwork);
    addAndMakeVisible (seek);
    addAndMakeVisible (badge);
    addAndMakeVisible (pluginSlot);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.setValue (1.0, juce::dontSendNotification);
    volume.setTooltip ("Volume numérique 64 bits. À 100 %, le flux reste bit-perfect."_t);
    addAndMakeVisible (volume);

    playButton.onClick = [this] { ctx.engine().togglePlayPause(); };
    nextButton.onClick = [this] { ctx.engine().next(); };
    prevButton.onClick = [this] { ctx.engine().previous(); };
    shuffleButton.onClick = [this] { ctx.setShuffle (! ctx.isShuffle()); shuffleButton.active = ctx.isShuffle(); shuffleButton.repaint(); };
    repeatButton.onClick = [this] { ctx.setRepeat (! ctx.isRepeat()); repeatButton.active = ctx.isRepeat(); repeatButton.repaint(); };
    lyricsButton.onClick = [this] { if (onToggleLyrics) onToggleLyrics(); };
    queueButton.onClick = [this] { if (onToggleQueue) onToggleQueue(); };
    miniButton.onClick = [this] { ctx.toggleMiniPlayer(); };
    volumeButton.onClick = [this] { volume.setValue (volume.getValue() > 0.0 ? 0.0 : 1.0); };
    seek.onSeek = [this] (double s) { ctx.engine().seek (s); };
    badge.onClick = [this] { if (onShowSignalPath) onShowSignalPath(); };

    volume.onValueChange = [this]
    {
        const double v = volume.getValue();
        if (v >= 0.999)
        {
            ctx.engine().setVolume (1.0);
            ctx.engine().setFixedVolume (true);      // bit-perfect again
        }
        else
        {
            // perceptual taper: 0 -> mute, 0.5 -> about -18 dB
            const double db = v <= 0.0 ? -120.0 : 60.0 * std::log10 (v);
            ctx.engine().setFixedVolume (false);
            ctx.engine().setVolume (juce::Decibels::decibelsToGain (db, -120.0));
        }
        volumeButton.setIcon (v <= 0.0 ? style::Icon::VolumeMute : style::Icon::Volume);
        ctx.settings().set ("volume", v);
    };
    volume.setValue ((double) ctx.settings().get ("volume", 1.0), juce::sendNotificationSync);

    startTimerHz (30);
    trackChanged();
}

void PlayerBar::paint (juce::Graphics& g)
{
    style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), ctx.theme().radius(), true);
    const auto& t = ctx.theme();
    auto r = textArea;
    g.setColour (t.textCol());
    g.setFont (style::font (14.5f, true));
    g.drawText (title, r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour (t.textDim());
    g.setFont (style::font (13.0f));
    g.drawText (subtitle, r, juce::Justification::topLeft, true);
}

void PlayerBar::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    artwork.setBounds (r.removeFromLeft (r.getHeight()));
    r.removeFromLeft (12);
    const int third = getWidth() / 3;
    textArea = r.removeFromLeft (juce::jmax (120, third - r.getHeight() - 40));

    auto right = r.removeFromRight (juce::jmin (third - 20, 380));
    auto center = r.reduced (8, 0);

    // center: transport row + seek bar
    auto row = center.removeFromTop (center.getHeight() * 55 / 100);
    const int b = row.getHeight();
    auto controls = row.withSizeKeepingCentre (b * 5 + 40, b);
    shuffleButton.setBounds (controls.removeFromLeft (b).reduced (6));
    controls.removeFromLeft (10);
    prevButton.setBounds (controls.removeFromLeft (b).reduced (4));
    controls.removeFromLeft (10);
    playButton.setBounds (controls.removeFromLeft (b).reduced (1));
    controls.removeFromLeft (10);
    nextButton.setBounds (controls.removeFromLeft (b).reduced (4));
    controls.removeFromLeft (10);
    repeatButton.setBounds (controls.removeFromLeft (b).reduced (6));
    seek.setBounds (center.withSizeKeepingCentre (juce::jmin (center.getWidth(), 620), center.getHeight()));

    // right: badge / plug-ins / buttons / volume
    auto top = right.removeFromTop (right.getHeight() / 2);
    badge.setBounds (top.removeFromLeft (juce::jmin (top.getWidth() - 96, 190)).reduced (0, 4));
    top.removeFromLeft (6);
    pluginSlot.setVisible (! pluginSlot.isEmpty());
    pluginSlot.setBounds (top.reduced (0, 2));
    const int ib = right.getHeight();
    miniButton.setBounds (right.removeFromRight (ib));
    queueButton.setBounds (right.removeFromRight (ib));
    lyricsButton.setBounds (right.removeFromRight (ib));
    volumeButton.setBounds (right.removeFromLeft (ib));
    volume.setBounds (right.reduced (4, 0));
}

void PlayerBar::mouseUp (const juce::MouseEvent& e)
{
    if ((artwork.getBounds().contains (e.getPosition()) || textArea.contains (e.getPosition())) && onOpenNowPlaying)
        onOpenNowPlaying();
}

void PlayerBar::trackChanged()
{
    if (const auto* t = ctx.currentTrack())
    {
        title = t->title;
        subtitle = t->displayArtist() + (t->album.isNotEmpty() ? " — "_t + t->album : juce::String());
        artwork.setArtwork (t->artworkKey, t->album.isNotEmpty() ? t->album : t->title);
    }
    else
    {
        title = "NX Player";
        subtitle = "Choisissez un morceau"_t;
        artwork.setArtwork ({}, "NX");
    }
    stateChanged();
    repaint();
}

void PlayerBar::stateChanged()
{
    const auto st = ctx.engine().getState();
    playButton.setIcon (st == PlaybackEngine::State::Playing ? style::Icon::Pause : style::Icon::Play);
    const auto path = ctx.engine().getSignalPath();
    const auto plan = ctx.engine().getCurrentPlan();
    if (path.nodes.empty())
        badge.update ({}, PathQuality::BitPerfect);
    else
    {
        juce::String text;
        if (plan.source.isDsd()) text = "DSD" + juce::String (plan.source.dsdMultiple());
        else
        {
            const auto khz = plan.source.sampleRate / 1000.0;
            text = juce::String (plan.source.bitsPerSample) + "/" + (std::fmod (khz, 1.0) == 0.0 ? juce::String ((int) khz) : juce::String (khz, 1).replaceCharacter ('.', ','));
            if (plan.source.lossy) text = plan.source.codec;
        }
        text << "  " << toString (path.overall);
        badge.update (text, path.overall);
    }
    lyricsButton.repaint();
}

void PlayerBar::timerCallback()
{
    const auto& e = ctx.engine();
    double dur = e.getDurationSeconds();
    if (const auto* t = ctx.currentTrack()) if (dur <= 0) dur = t->duration;
    seek.setPosition (e.getPositionSeconds(), dur);
}

} // namespace nx
