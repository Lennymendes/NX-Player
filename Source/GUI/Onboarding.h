// NX Player — MIT License
// First-run assistant (Cider 2 style), also from Réglages › Interface › « Relancer l'assistant » :
//   1. language and theme   2. layout   3. detected audio device (+ optimal profile in one click)
//   4. music sources (local folder, NAS / Jellyfin)
#pragma once

#include "Widgets.h"

namespace nx
{

class OnboardingWizard : public juce::Component, private juce::Timer
{
public:
    explicit OnboardingWizard (AppContext& c);
    void start();
    void finish();
    int getStep() const noexcept { return step; }
    void setStep (int s);
    static constexpr int numSteps = 4;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }

    /** Tests: the clickable tiles of the current step (language, themes, layouts, profiles). */
    struct Tile { juce::String kind, id; juce::Rectangle<int> bounds; };
    const std::vector<Tile>& getTiles() const { return tiles; }
    void clickTile (const juce::String& kind, const juce::String& id);
    std::function<void()> onFinished;
    juce::StringArray addedFolders;

private:
    void timerCallback() override { phase += 1.0 / 30.0; repaint(); }
    juce::Rectangle<int> card() const;
    void layoutStep();
    void paintLanguage (juce::Graphics&, juce::Rectangle<int>);
    void paintLayout (juce::Graphics&, juce::Rectangle<int>);
    void paintDevice (juce::Graphics&, juce::Rectangle<int>);
    void paintSources (juce::Graphics&, juce::Rectangle<int>);
    void searchNas();

    AppContext& ctx;
    int step = 0;
    double phase = 0.0;
    std::vector<Tile> tiles;
    juce::TextButton skipButton, backButton, nextButton, folderButton, nasButton, nasConnect;
    juce::TextEditor nasAddress;
    juce::ComboBox deviceBox;
    juce::String nasStatus;
    bool nasSearching = false;
};

} // namespace nx
