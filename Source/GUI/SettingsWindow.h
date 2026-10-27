// NX Player — MIT License
// Settings as a modal window over the interface (Cider 2 style): dimmed glass backdrop, navigation on the
// left (Audio & Système, Bibliothèque & NAS, Interface, NX Remote, Outils Studio, À propos), content on the right.
#pragma once

#include "Pages.h"

namespace nx
{

class SettingsWindow : public juce::Component
{
public:
    explicit SettingsWindow (AppContext& c);
    void open (const juce::String& tab);
    void close();
    bool isOpen() const { return isVisible(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void refresh() { page.refresh(); repaint(); }
    juce::String getTab() const { return page.getTab(); }
    SettingsPage& getPage() { return page; }
    std::function<void()> onClose;

private:
    juce::Rectangle<int> panelBounds() const;
    juce::Rectangle<int> tabBounds (int i) const;
    AppContext& ctx;
    SettingsPage page;
    IconButton closeButton;
    int hoverTab = -1;
};

} // namespace nx
