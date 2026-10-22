// NX Player — MIT License
// Now Playing: large artwork, time-synced lyrics, Roon-style signal path, plug-in slot.
#pragma once

#include "Widgets.h"
#include "../Library/Lyrics.h"

namespace nx
{

class SignalPathView : public juce::Component, private juce::Timer
{
public:
    explicit SignalPathView (AppContext& c, bool compact = false);
    void paint (juce::Graphics&) override;
    int getIdealHeight() const;
    void refresh() { path = ctx.engine().getSignalPath(); status = ctx.engine().getOutputStatus(); repaint(); }
private:
    void timerCallback() override { refresh(); }
    AppContext& ctx;
    bool compact;
    SignalPath path;
    OutputStatus status;
};

class LyricsView : public juce::Component, private juce::Timer
{
public:
    explicit LyricsView (AppContext& c);
    void setLyrics (const Lyrics& l);
    void paint (juce::Graphics&) override;
    void resized() override { layoutLines(); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool hasLyrics() const { return ! lyrics.isEmpty(); }
    juce::String statusText;   // "Recherche des paroles…", "Pas de paroles"
private:
    void timerCallback() override;
    void layoutLines();
    AppContext& ctx;
    Lyrics lyrics;
    std::vector<juce::Rectangle<float>> lineBounds;   // at scroll 0
    float scroll = 0.0f, targetScroll = 0.0f, manualOffset = 0.0f;
    int current = -1;
    double lastManual = 0.0;
};

class NowPlayingPage : public juce::Component
{
public:
    explicit NowPlayingPage (AppContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void trackChanged();
    void setLyrics (const Lyrics& l, const juce::String& status = {}) { lyricsView.statusText = status; lyricsView.setLyrics (l); resized(); }
    void refreshPlugins() { pluginSlot.refreshPlugins(); resized(); }
    bool showLyrics = true;
private:
    AppContext& ctx;
    ArtworkView artwork;
    LyricsView lyricsView;
    SignalPathView signalPath;
    PluginSlot pluginSlot;
    juce::Rectangle<int> infoArea;
};

} // namespace nx
