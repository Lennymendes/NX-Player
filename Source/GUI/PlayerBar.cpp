// NX Player — MIT License
#include "PlayerBar.h"
#include "../Engine/DeviceProfiles.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Ext/PluginManager.h"
#include "../DSP/StudioTools.h"
#include "DeviceArt.h"

namespace nx
{

PlayerBar::PlayerBar (AppContext& c)
    : lyricsButton (style::Icon::Lyrics, "Paroles"_t, c),
      queueButton (style::Icon::Queue, "File d'attente"_t, c),
      studioButton (style::Icon::Studio, "Outils Studio : mono, Mid / Side, phase, crossfeed"_t, c),
      immersiveButton (style::Icon::Fullscreen, "Vue immersive (F)"_t, c),
      deviceChip (c),
      ctx (c), artwork (c, 128),
      shuffleButton (style::Icon::Shuffle, "Aléatoire"_t, c),
      prevButton (style::Icon::Previous, "Précédent"_t, c),
      playButton (style::Icon::Play, "Lecture / pause"_t, c),
      nextButton (style::Icon::Next, "Suivant"_t, c),
      repeatButton (style::Icon::Repeat, "Répéter"_t, c),
      miniButton (style::Icon::Mini, "Mini-lecteur"_t, c),
      volumeButton (style::Icon::Volume, "Volume (100 % = bit-perfect)"_t, c),
      waveButton (style::Icon::Waveform, "Forme d'onde à la place de la barre de progression"_t, c),
      seek (c), loopButton (c), badge (c), pluginSlot (c, "playerbar.right", true)
{
    artwork.cornerRadius = 8.0f;
    artwork.shadow = false;
    playButton.primary = true;
    for (auto* b : { &shuffleButton, &prevButton, &playButton, &nextButton, &repeatButton, &lyricsButton, &queueButton, &miniButton, &volumeButton,
                     &studioButton, &immersiveButton, &waveButton })
        addAndMakeVisible (b);
    waveButton.onClick = [this] { setWaveformShown (! waveButton.active); };
    ctx.trackAnalyzer().addChangeListener (this);
    addAndMakeVisible (deviceChip);
    studioButton.onClick = [this] { showStudioMenu(); };
    immersiveButton.onClick = [this] { if (onImmersive) onImmersive(); };
    addAndMakeVisible (artwork);
    addAndMakeVisible (seek);
    addAndMakeVisible (loopButton);
    loopButton.onClick = [this]
    {
        if (loopButton.stage == 0) markLoopA();
        else if (loopButton.stage == 1) markLoopB();
        else clearLoop();
    };
    seek.onLoopEdited = [this] (double a, double b)
    {
        if (loopButton.stage == 2 && b > a) ctx.engine().setLoop (a, b);
        else if (loopButton.stage == 1) pendingLoopA = a;
    };
    addAndMakeVisible (badge);
    addAndMakeVisible (pluginSlot);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.setValue (1.0, juce::dontSendNotification);
    volume.setTooltip ("Volume numérique 64 bits. À 100 %, le flux reste bit-perfect."_t);
    addAndMakeVisible (volume);
    volumeLock.setJustificationType (juce::Justification::centred);
    volumeLock.setFont (style::font (12.0f, true));
    volumeLock.setColour (juce::Label::textColourId, ctx.theme().textCol());
    volumeLock.setInterceptsMouseClicks (false, false);
    addChildComponent (volumeLock);

    playButton.onClick = [this] { ctx.engine().togglePlayPause(); };
    nextButton.onClick = [this] { ctx.engine().next(); };
    prevButton.onClick = [this] { ctx.engine().previous(); };
    shuffleButton.onClick = [this] { ctx.setShuffle (! ctx.isShuffle()); shuffleButton.active = ctx.isShuffle(); shuffleButton.repaint(); };
    repeatButton.onClick = [this] { ctx.setRepeat (! ctx.isRepeat()); repeatButton.active = ctx.isRepeat(); repeatButton.repaint(); };
    lyricsButton.onClick = [this] { if (onToggleLyrics) onToggleLyrics(); };
    queueButton.onClick = [this] { if (onToggleQueue) onToggleQueue(); };
    miniButton.onClick = [this] { ctx.toggleMiniPlayer(); };
    volumeButton.onClick = [this] { if (volume.isEnabled()) volume.setValue (volume.getValue() > 0.0 ? 0.0 : 1.0); };
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
    setWaveformShown ((bool) ctx.settings().get ("playerWaveform", false));
    trackChanged();
}

PlayerBar::~PlayerBar() { ctx.trackAnalyzer().removeChangeListener (this); }

void PlayerBar::setWaveformShown (bool on)
{
    waveButton.active = on;
    waveButton.repaint();
    if ((bool) ctx.settings().get ("playerWaveform", false) != on) ctx.settings().set ("playerWaveform", on);
    seek.setWaveform (on, ctx.currentAnalysis());
    resized();
}

void PlayerBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    seek.setWaveform (waveButton.active, ctx.currentAnalysis());
    repaint();   // Peak / RMS / DR line
}

void PlayerBar::paint (juce::Graphics& g)
{
    style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), ctx.theme().radius(), true);
    const auto& t = ctx.theme();
    auto r = textArea;
    // technical line: channels (file header) + Peak / RMS / DR once the background scan is done
    {
        juce::String info;
        if (const auto* tr = ctx.currentTrack()) info = channelBadge (tr->channels);
        if (auto an = ctx.currentAnalysis()) info << (info.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") : juce::String()) << an->summary();
        if (info.isNotEmpty() && r.getHeight() >= 46)
        {
            g.setColour (t.textDim().withAlpha (0.85f));
            g.setFont (style::font (11.0f));
            g.drawText (info, r.removeFromBottom (13), juce::Justification::bottomLeft, true);
        }
    }
    g.setColour (t.textCol());
    g.setFont (style::font (14.5f, true));
    g.drawText (title, r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setFont (style::font (13.0f));
    artistRect = {};
    if (artistText.isNotEmpty())
    {
        const int aw = juce::jmin (r.getWidth(), juce::roundToInt (style::textWidth (style::font (13.0f), artistText)) + 2);
        artistRect = r.withWidth (aw).withHeight (juce::jmin (r.getHeight(), 20));
        g.setColour (t.accent());
        g.drawText (artistText, artistRect, juce::Justification::topLeft, true);
        g.setColour (t.textDim());
        if (albumText.isNotEmpty() && r.getWidth() > aw + 12)
            g.drawText (" — "_t + albumText, r.withTrimmedLeft (aw), juce::Justification::topLeft, true);
    }
    else
    {
        g.setColour (t.textDim());
        g.drawText (subtitle, r, juce::Justification::topLeft, true);
    }
}

void PlayerBar::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (artistRect.contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PlayerBar::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    artwork.setBounds (r.removeFromLeft (r.getHeight()));
    r.removeFromLeft (12);
    const int third = getWidth() / 3;
    textArea = r.removeFromLeft (juce::jmax (120, third - r.getHeight() - 40));

    auto right = r.removeFromRight (juce::jmin (third + 40, 470));
    auto center = r.reduced (8, 0);

    // center: transport row + seek bar
    // waveform mode gives the seek area more height (Audirvana style)
    auto row = center.removeFromTop (center.getHeight() * (waveButton.active ? 44 : 55) / 100);
    const int b = row.getHeight();
    auto controls = row.withSizeKeepingCentre (b * 5 + 40 + 10 + 44, b);
    shuffleButton.setBounds (controls.removeFromLeft (b).reduced (6));
    controls.removeFromLeft (10);
    prevButton.setBounds (controls.removeFromLeft (b).reduced (4));
    controls.removeFromLeft (10);
    playButton.setBounds (controls.removeFromLeft (b).reduced (1));
    controls.removeFromLeft (10);
    nextButton.setBounds (controls.removeFromLeft (b).reduced (4));
    controls.removeFromLeft (10);
    repeatButton.setBounds (controls.removeFromLeft (b).reduced (6));
    controls.removeFromLeft (10);
    loopButton.setBounds (controls.removeFromLeft (44));
    auto seekArea = center.withSizeKeepingCentre (juce::jmin (center.getWidth(), 646), center.getHeight());
    waveButton.setBounds (seekArea.removeFromLeft (26).withSizeKeepingCentre (22, 22));   // left of the elapsed time
    seek.setBounds (seekArea);

    // right: badge + device chip / plug-ins, then buttons / volume
    auto top = right.removeFromTop (right.getHeight() / 2);
    {
        // wide enough for the pipeline label ("DSD64 → DXD 352,8 kHz [Anubis Ref]")
        const int wanted = juce::roundToInt (juce::GlyphArrangement::getStringWidth (style::font (12.0f, true), badge.getLabel())) + 48;
        const int chipW = juce::jmin (150, juce::jmax (100, top.getWidth() / 3));
        deviceChip.setBounds (top.removeFromRight (chipW).reduced (0, 3));
        top.removeFromRight (6);
        badge.setBounds (top.removeFromLeft (juce::jlimit (120, juce::jmax (120, top.getWidth()), juce::jmax (170, wanted))).reduced (0, 4));
    }
    top.removeFromLeft (6);
    pluginSlot.setVisible (! pluginSlot.isEmpty() && top.getWidth() > 40);
    pluginSlot.setBounds (top.reduced (0, 2));
    const int ib = right.getHeight();
    miniButton.setBounds (right.removeFromRight (ib));
    immersiveButton.setBounds (right.removeFromRight (ib));
    queueButton.setBounds (right.removeFromRight (ib));
    lyricsButton.setBounds (right.removeFromRight (ib));
    studioButton.setBounds (right.removeFromRight (ib));
    volumeButton.setBounds (right.removeFromLeft (ib));
    volume.setBounds (right.reduced (4, 0));
    volumeLock.setBounds (volumeButton.getBounds().getUnion (volume.getBounds()));
}

void PlayerBar::mouseUp (const juce::MouseEvent& e)
{
    if (artistRect.contains (e.getPosition()) && artistText.isNotEmpty())
    {
        ctx.navigate ({ PageId::ArtistDetail, artistText });
        return;
    }
    if ((artwork.getBounds().contains (e.getPosition()) || textArea.contains (e.getPosition())) && onOpenNowPlaying)
        onOpenNowPlaying();
}

void PlayerBar::markLoopA()
{
    auto& e = ctx.engine();
    if (e.getCurrentIndex() < 0) return;
    if (e.isLoopActive()) e.clearLoop();
    pendingLoopA = e.getPositionSeconds();
    loopButton.stage = 1;
    loopButton.repaint();
    seek.setLoop (pendingLoopA, -1.0);
    ctx.showMessage ("Boucle : début A à "_t + style::formatTime (pendingLoopA) + " (O pour la fin B)"_t);
}

void PlayerBar::markLoopB()
{
    auto& e = ctx.engine();
    if (e.getCurrentIndex() < 0) return;
    const double a = pendingLoopA >= 0.0 ? pendingLoopA : (e.isLoopActive() ? e.getLoopA() : 0.0);
    const double b = e.getPositionSeconds();
    if (b - a < 0.1) { ctx.showMessage ("Boucle : la fin B doit être après le début A"_t); return; }
    e.setLoop (a, b);
    pendingLoopA = -1.0;
    loopButton.stage = 2;
    loopButton.repaint();
    seek.setLoop (a, b);
    ctx.showMessage ("Boucle A-B : "_t + style::formatTime (a) + " → "_t + style::formatTime (b) + " (L pour arrêter)"_t);
}

void PlayerBar::clearLoop()
{
    ctx.engine().clearLoop();
    pendingLoopA = -1.0;
    loopButton.stage = 0;
    loopButton.repaint();
    seek.setLoop (-1.0, -1.0);
}

void PlayerBar::syncLoop()
{
    auto& e = ctx.engine();
    if (e.isLoopActive() && e.getLoopIndex() != e.getCurrentIndex())
        e.clearLoop (false);                         // another track: the loop belonged to the previous one
    if (e.isLoopActive()) { loopButton.stage = 2; seek.setLoop (e.getLoopA(), e.getLoopB()); }
    else if (loopButton.stage == 2) { loopButton.stage = 0; seek.setLoop (-1.0, -1.0); }
    loopButton.repaint();
}

void PlayerBar::trackChanged()
{
    if (loopButton.stage == 1) { pendingLoopA = -1.0; loopButton.stage = 0; seek.setLoop (-1.0, -1.0); }
    syncLoop();
    if (const auto* t = ctx.currentTrack())
    {
        title = t->title;
        artistText = t->displayArtist();
        albumText = t->album;
        subtitle = t->displayArtist() + (t->album.isNotEmpty() ? " — "_t + t->album : juce::String());
        artwork.setArtwork (t->artworkKey, t->album.isNotEmpty() ? t->album : t->title);
    }
    else
    {
        title = "NX Player";
        subtitle = "Choisissez un morceau"_t;
        artistText = albumText = {};
        artwork.setArtwork ({}, "NX");
    }
    seek.setWaveform (waveButton.active, ctx.currentAnalysis());
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
        // The active pipeline: "DSD64 → DXD 352,8 kHz [Anubis Ref]", "PCM 44,1 → DXD [Warm]", "DSD64 DoP [Bit-Perfect]"
        badge.update (pipelineLabel (plan, toString (path.overall)), path.overall);
        resized();
    }
    {
        // DoP is a bit-exact DSD carrier (a gain would turn it into noise), and the RME "DSD Direct" profile
        // holds the software volume at 100 %: in both cases the control is greyed out and inert.
        const bool dop = ! path.nodes.empty() && plan.wire == WireFormat::DoP;
        const bool profile = ctx.engine().isVolumeLockedByProfile();
        const bool locked = dop || profile;
        volume.setEnabled (! locked);
        volume.setInterceptsMouseClicks (! locked, ! locked);   // no click, drag or wheel
        volume.setAlpha (locked ? 0.25f : 1.0f);
        volumeButton.setEnabled (! locked);
        volumeButton.setAlpha (locked ? 0.4f : 1.0f);
        volumeLock.setVisible (locked);
        volumeLock.setText (dop ? "Volume Fixe / DoP"_t : "Volume Fixe"_t, juce::dontSendNotification);
        volume.setTooltip (dop ? juce::String ("Volume fixe en DoP : le flux DSD est transmis tel quel, réglez le niveau en analogique."_t)
                         : profile ? juce::String ("Volume verrouillé à 100 % par le profil RME (DSD Direct) : réglez le niveau en analogique."_t)
                                   : juce::String ("Volume numérique 64 bits. À 100 %, le flux reste bit-perfect."_t));
    }
    lyricsButton.repaint();
    studioButton.active = ctx.studioTools().anyActive();
    studioButton.repaint();
    deviceChip.repaint();
}

void PlayerBar::timerCallback()
{
    const auto& e = ctx.engine();
    double dur = e.getDurationSeconds();
    if (const auto* t = ctx.currentTrack()) if (dur <= 0) dur = t->duration;
    seek.setPosition (e.getPositionSeconds(), dur);
}

void PlayerBar::setVolumeLevel (double v)
{
    if (volume.isEnabled()) volume.setValue (juce::jlimit (0.0, 1.0, v), juce::sendNotificationSync);
}

void PlayerBar::showStudioMenu()
{
    auto& st = ctx.studioTools();
    using M = StudioToolsState::Monitor;
    juce::PopupMenu m;
    m.addSectionHeader ("Outils Studio (64 bits)"_t);
    auto setMonitor = [this] (M mode) { auto& s = ctx.studioTools(); s.monitor = s.monitor.load() == mode ? M::Stereo : mode; ctx.applyStudioTools(); studioButton.active = s.anyActive(); studioButton.repaint(); };
    m.addItem ("Mono (L+R)"_t, true, st.monitor.load() == M::Mono, [setMonitor] { setMonitor (M::Mono); });
    m.addItem ("Solo Mid (M)", true, st.monitor.load() == M::MidSolo, [setMonitor] { setMonitor (M::MidSolo); });
    m.addItem ("Solo Side (S)", true, st.monitor.load() == M::SideSolo, [setMonitor] { setMonitor (M::SideSolo); });
    m.addSeparator();
    m.addItem (juce::String::fromUTF8 ("Inversion de phase (\xc3\x98)"), true, st.invertPhase.load(), [this]
    {
        auto& s = ctx.studioTools(); s.invertPhase = ! s.invertPhase.load(); ctx.applyStudioTools(); studioButton.active = s.anyActive(); studioButton.repaint();
    });
    m.addItem ("Smart Crossfeed (casque)"_t, true, st.crossfeed.load(), [this]
    {
        auto& s = ctx.studioTools(); s.crossfeed = ! s.crossfeed.load(); ctx.applyStudioTools(); studioButton.active = s.anyActive(); studioButton.repaint();
    });
    m.addSeparator();
    m.addItem ("Tout désactiver (retour au bit-perfect)"_t, st.anyActive(), false, [this]
    {
        auto& s = ctx.studioTools(); s.monitor = M::Stereo; s.invertPhase = false; s.crossfeed = false; ctx.applyStudioTools(); studioButton.active = false; studioButton.repaint();
    });
    if (const auto* t = ctx.currentTrack())
    {
        m.addSeparator();
        const auto track = *t;
        m.addItem ("Analyser / Convertir dans AuraEngine X"_t, [this, track] { ctx.sendToAuraEngine ({ track }); });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&studioButton));
}

//==============================================================================
std::vector<std::pair<juce::String, bool>> DeviceChip::menuEntries() const
{
    std::vector<std::pair<juce::String, bool>> out;
    const auto s = ctx.engine().getSettings();
    const auto family = activeFamily (s, ctx.engine().getDeviceName());
    if (family == DeviceFamily::None)
    {
        const bool convert = s.dsdMode == DsdMode::ConvertToPcm;
        out.push_back ({ "Bit-Perfect (DSD natif / DoP)"_t, ! convert && ! s.upsampleToDxd });
        out.push_back ({ "DSD -> DXD 352,8 kHz"_t, convert });
        out.push_back ({ "Suréchantillonnage DXD"_t, s.upsampleToDxd });
        return out;
    }
    const int current = familyCurrentProfile (s, family);
    for (int i = 0; i < familyProfileCount (family); ++i) out.push_back ({ profileName (family, i), i == current });
    return out;
}

void DeviceChip::selectEntry (int index)
{
    auto s = ctx.engine().getSettings();
    const auto family = activeFamily (s, ctx.engine().getDeviceName());
    if (family == DeviceFamily::None)
    {
        s.dsdMode = index == 1 ? DsdMode::ConvertToPcm : DsdMode::Auto;
        s.upsampleToDxd = index == 2;
    }
    else familySetProfile (s, family, index);
    ctx.engine().setSettings (s);
    ctx.applyEngineSettings();
    const auto entries = menuEntries();
    if (index >= 0 && index < (int) entries.size()) ctx.showMessage ("Profil : "_t + entries[(size_t) index].first);
    repaint();
}

void DeviceChip::showMenu()
{
    const auto device = ctx.engine().getDeviceName();
    const auto family = activeFamily (ctx.engine().getSettings(), device);
    juce::PopupMenu m;
    m.addSectionHeader (device.isNotEmpty() ? device : juce::String ("Aucun périphérique"_t));
    const auto entries = menuEntries();
    for (int i = 0; i < (int) entries.size(); ++i)
        m.addItem (entries[(size_t) i].first, true, entries[(size_t) i].second, [this, i] { selectEntry (i); });
    m.addSeparator();
    m.addItem ("Réglages audio…"_t, [this] { ctx.navigate ({ PageId::Settings, "audio" }); });
    juce::ignoreUnused (family);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void DeviceChip::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto device = ctx.engine().getDeviceName();
    const auto s = ctx.engine().getSettings();
    const auto family = familyFromName (device) != DeviceFamily::None ? familyFromName (device) : activeFamily (s, device);
    g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.12f : 0.06f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (t.border().withMultipliedAlpha (1.5f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    auto glyph = r.removeFromLeft (r.getHeight() * 1.4f).reduced (6.0f, 3.0f);
    deviceart::drawGlyph (g, family, glyph, t.accent());
    style::drawIcon (g, style::Icon::ChevronDown, r.removeFromRight (r.getHeight()).reduced (r.getHeight() * 0.3f), t.textDim());
    const auto activeFam = activeFamily (s, device);
    juce::String text;
    if (activeFam != DeviceFamily::None) text = profileTag (activeFam, familyCurrentProfile (s, activeFam));
    else text = s.dsdMode == DsdMode::ConvertToPcm ? juce::String ("DSD -> DXD") : s.upsampleToDxd ? juce::String ("DXD") : juce::String ("Bit-Perfect");
    g.setColour (t.textCol());
    g.setFont (style::font (11.5f, true));
    g.drawText (text, r, juce::Justification::centredLeft, true);
}

} // namespace nx
