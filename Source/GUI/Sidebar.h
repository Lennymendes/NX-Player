// NX Player — MIT License
#pragma once

#include "Widgets.h"

namespace nx
{

class Sidebar : public juce::Component, private juce::Timer
{
public:
    explicit Sidebar (AppContext& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void setActive (const PageRequest& r) { active = r; repaint(); }
    void refresh();
    void refreshPlugins() { pluginSlot.refreshPlugins(); resized(); }
    juce::TextEditor& getSearchField() { return search; }

private:
    struct Item { bool header = false; juce::String label; style::Icon icon = style::Icon::Home; PageRequest target; juce::Colour dot; };
    void timerCallback() override;
    int itemAt (int y) const;

    AppContext& ctx;
    juce::TextEditor search;
    PluginSlot pluginSlot;
    std::vector<Item> items;
    PageRequest active;
    int hover = -1, listTop = 0, scroll = 0;
    static constexpr int itemHeight = 32, headerHeight = 34;
};

} // namespace nx
