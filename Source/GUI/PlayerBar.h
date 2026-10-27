// NX Player — MIT License
#pragma once

#include "Widgets.h"

namespace nx
{

class PlayerBar : public juce::Component, private juce::Timer
{
public:
    explicit PlayerBar (AppContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void trackChanged();
    void stateChanged();
    void refreshPlugins() { pluginSlot.refreshPlugins(); resized(); }

    // A-B loop (button, keys I / O / L, markers on the seek bar)
    void markLoopA();
    void markLoopB();
    void clearLoop();

    std::function<void()> onToggleLyrics, onToggleQueue, onShowSignalPath, onOpenNowPlaying;
    bool shuffle = false, repeat = false;

    IconButton lyricsButton, queueButton;

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
    juce::String title, subtitle;
};

} // namespace nx
