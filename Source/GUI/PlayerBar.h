// NX Player — MIT License
#pragma once

#include "Widgets.h"

namespace nx
{

/** Player bar chip: miniature of the DAC (Merging Anubis, RME ADI-2, generic) + its active profile; click = profile menu. */
class DeviceChip : public juce::Component
{
public:
    explicit DeviceChip (AppContext& c) : ctx (c) { setRepaintsOnMouseActivity (true); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { showMenu(); }
    void showMenu();
    /** The profile entries of the menu (tests): name + whether it is the active one. */
    std::vector<std::pair<juce::String, bool>> menuEntries() const;
    void selectEntry (int index);
private:
    AppContext& ctx;
};

class PlayerBar : public juce::Component, private juce::Timer
{
public:
    explicit PlayerBar (AppContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::NormalCursor); }
    void trackChanged();
    void stateChanged();
    void refreshPlugins() { pluginSlot.refreshPlugins(); resized(); }

    // A-B loop (button, keys I / O / L, markers on the seek bar)
    void markLoopA();
    void markLoopB();
    void clearLoop();

    /** Volume slider position 0..1 (NX Remote, keyboard). */
    void setVolumeLevel (double v);
    void showStudioMenu();

    std::function<void()> onToggleLyrics, onToggleQueue, onShowSignalPath, onOpenNowPlaying, onImmersive;
    bool shuffle = false, repeat = false;

    IconButton lyricsButton, queueButton, studioButton, immersiveButton;
    DeviceChip deviceChip;

private:
    void timerCallback() override;
    AppContext& ctx;
    ArtworkView artwork;
    IconButton shuffleButton, prevButton, playButton, nextButton, repeatButton, miniButton, volumeButton;
    SeekBar seek;
    LoopButton loopButton;
    double pendingLoopA = -1.0;
    void syncLoop();
    QualityBadge badge;
    juce::Slider volume;
    PluginSlot pluginSlot;
    juce::Rectangle<int> textArea;
    juce::String title, subtitle, artistText, albumText;
    juce::Rectangle<int> artistRect;     // clickable artist name (opens the artist page)
};

} // namespace nx
