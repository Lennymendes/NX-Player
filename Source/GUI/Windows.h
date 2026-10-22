// NX Player — MIT License
#pragma once

#include "MainComponent.h"

namespace nx
{

class MiniPlayer : public juce::Component, private juce::Timer
{
public:
    explicit MiniPlayer (AppContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void trackChanged();
    void mouseDown (const juce::MouseEvent& e) override { dragger.startDraggingComponent (getTopLevelComponent(), e.getEventRelativeTo (getTopLevelComponent())); }
    void mouseDrag (const juce::MouseEvent& e) override { dragger.dragComponent (getTopLevelComponent(), e.getEventRelativeTo (getTopLevelComponent()), nullptr); }
    void mouseDoubleClick (const juce::MouseEvent&) override { ctx.toggleMiniPlayer(); }
private:
    void timerCallback() override;
    AppContext& ctx;
    ArtworkView artwork;
    IconButton prev, play, next, expand;
    SeekBar seek;
    juce::Image blurred;
    juce::ComponentDragger dragger;
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (AppContext& ctx, juce::LookAndFeel& lf);
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    MainComponent& getMain() { return *main; }
private:
    MainComponent* main = nullptr;
};

class MiniWindow : public juce::DocumentWindow
{
public:
    explicit MiniWindow (AppContext& ctx);
    void closeButtonPressed() override { setVisible (false); }
    MiniPlayer& getMini() { return *mini; }
private:
    MiniPlayer* mini = nullptr;
};

} // namespace nx
