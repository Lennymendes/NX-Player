// NX Player — MIT License
// Sidebar: three pillars (Local, Favoris, Playlists), the NAS / Jellyfin servers, Extensions & Thèmes and
// the user's playlists. Wide (labels) or compact (icons only, tooltips). Tracks and albums dropped on a
// playlist are appended to it; dropped on "Playlists" they start a new one; on "Favoris" they become favourites.
#pragma once

#include "Widgets.h"

namespace nx
{

class Sidebar : public juce::Component, public juce::DragAndDropTarget, public juce::TooltipClient,
                private juce::Timer, private juce::ChangeListener
{
public:
    explicit Sidebar (AppContext& c);
    ~Sidebar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; quickHover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void setActive (const PageRequest& r) { active = r; repaint(); }
    void refresh();
    void refreshPlugins() { pluginSlot.refreshPlugins(); resized(); }
    juce::TextEditor& getSearchField() { return search; }
    bool isCompact() const { return getWidth() < 120; }

    // Drag & drop of tracks / albums
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override { dropHover = -1; repaint(); }
    void itemDropped (const SourceDetails&) override;
    /** Drops ids on the item at y (tests / keyboard). Returns what happened. */
    juce::String dropTracks (const std::vector<juce::int64>& ids, int itemIndex);
    int findItem (PageId page, const juce::String& a = {}) const;

    juce::String getTooltip() override;

private:
    struct Item { bool header = false; juce::String label; style::Icon icon = style::Icon::Home; PageRequest target; juce::Colour dot; bool quickAdd = false; };
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { resized(); repaint(); }
    int itemAt (int y) const;
    int itemTop (int index) const;
    int bottomReserve() const;   // settings row (+ downloads progress)
    juce::Rectangle<int> quickAddBounds (int index) const;
    void addMusicFolder();
    bool isItemActive (const Item& it) const;

    AppContext& ctx;
    juce::TextEditor search;
    PluginSlot pluginSlot;
    std::vector<Item> items;
    PageRequest active;
    int hover = -1, dropHover = -1, listTop = 0, scroll = 0;
    bool quickHover = false;
    int itemHeight = 32, headerHeight = 34;
};

} // namespace nx
