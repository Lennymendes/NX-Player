// NX Player — MIT License
#pragma once

#include "Background.h"
#include "Sidebar.h"
#include "PlayerBar.h"
#include "NowPlaying.h"
#include "Pages.h"

namespace nx
{

class QueuePanel : public juce::Component
{
public:
    explicit QueuePanel (AppContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jmax (0.0f, scroll - w.deltaY * 200.0f); repaint(); }
private:
    AppContext& ctx;
    float scroll = 0.0f;
};

class Toast : public juce::Component, private juce::Timer
{
public:
    explicit Toast (AppContext& c) : ctx (c) { setInterceptsMouseClicks (false, false); }
    void show (const juce::String& text);
    void paint (juce::Graphics&) override;
    juce::String message;
private:
    void timerCallback() override;
    AppContext& ctx;
    float alpha = 0.0f;
    double shownAt = 0.0;
};

class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AppContext& ctx);
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override {}
    bool keyPressed (const juce::KeyPress&) override;

    void navigate (const PageRequest& r);
    void refreshCurrentPage();
    void trackChanged();
    void stateChanged();
    void themeChanged();
    void pluginsChanged();
    void showMessage (const juce::String& text) { toast.show (text); }
    void setLyrics (const Lyrics& l, const juce::String& status) { nowPlaying.setLyrics (l, status); }
    void showSignalPath();
    PageRequest getCurrentPage() const { return current; }

    BackgroundComponent background;
    Sidebar sidebar;
    PlayerBar player;

private:
    void showPage (juce::Component* page);
    void timerCallback() override;
    float fadeProgress = 1.0f;
    AppContext& ctx;
    ListingPage listing;
    NowPlayingPage nowPlaying;
    SourcePage sourcePage;
    SettingsPage settingsPage;
    QueuePanel queue;
    Toast toast;
    std::unique_ptr<PluginSlot> homeSlot;
    juce::Component* visiblePage = nullptr;
    PageRequest current;
    bool queueVisible = false;
};

} // namespace nx
