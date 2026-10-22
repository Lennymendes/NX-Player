// NX Player — MIT License
// Library, search, sources and settings pages.
#pragma once

#include "Widgets.h"
#include "../Providers/MediaProvider.h"
#include "../Providers/UpnpProvider.h"

namespace nx
{

/** Title (or album hero) + optional album grid + optional track list. Most library views. */
class ListingPage : public ScrollPage, private juce::ChangeListener
{
public:
    explicit ListingPage (AppContext& c);
    ~ListingPage() override;
    void layoutContent (int width) override;
    void refresh() override { if (loader) loader (*this); layoutContent (getWidth() - viewport.getScrollBarThickness()); }

    std::function<void (ListingPage&)> loader;
    juce::String title, subtitle, listTitle, gridTitle;
    juce::String heroArtwork;           // album detail: big artwork on the left of the title
    bool hero = false;
    std::vector<TrackInfo> heroTracks;  // what "Lire" / "Aléatoire" play
    AlbumGrid grid;
    TrackList list;
    PluginSlot* topSlot = nullptr;
    juce::String ctaText;               // empty-state call to action
    std::function<void()> ctaAction;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { contentComp.repaint(); }
    struct Header : juce::Component { ListingPage& page; explicit Header (ListingPage& p) : page (p) {} void paint (juce::Graphics&) override; };
    Header header { *this };
    ArtworkView heroArt;
    juce::TextButton playAll { "Lire" }, shuffleAll { "Aléatoire"_t }, cta;
    std::unique_ptr<PluginSlot> homeSlot;
};

class SourcePage : public ScrollPage
{
public:
    explicit SourcePage (AppContext& c);
    void setSource (const juce::String& id);
    void layoutContent (int width) override;
    void refresh() override { setSource (sourceId); }

private:
    void buildLocal();
    void buildUpnp();
    void buildProvider (MediaProvider& p);
    void browseUpnp (const UpnpServer& server, const juce::String& objectId, const juce::String& title);
    juce::TextEditor* addField (const juce::String& label, const juce::String& key, bool password = false);
    juce::TextButton* addButton (const juce::String& text, std::function<void()> fn);
    void addText (const juce::String& text, bool dim = true);

    juce::String sourceId, heading, description;
    juce::OwnedArray<juce::Component> rows;
    std::vector<int> rowHeights;
    std::unique_ptr<TrackList> tracks;
    std::unique_ptr<AlbumGrid> albums;
    std::vector<UpnpEntry> browseEntries;
    UpnpServer browseServer;
    juce::StringArray breadcrumb;
};

class SettingsPage : public ScrollPage
{
public:
    explicit SettingsPage (AppContext& c);
    void layoutContent (int width) override;
    void refresh() override;

private:
    void section (const juce::String& title);
    void row (const juce::String& label, juce::Component* control, int height = 36, const juce::String& help = {});
    juce::OwnedArray<juce::Component> owned;
    struct Row { juce::String label, help; juce::Component* control = nullptr; int height = 36; bool isSection = false; };
    std::vector<Row> layoutRows;
    struct Labels : juce::Component { SettingsPage& p; explicit Labels (SettingsPage& s) : p (s) { setInterceptsMouseClicks (false, false); } void paint (juce::Graphics&) override; };
    Labels labels { *this };
    std::vector<std::pair<juce::Rectangle<int>, Row>> painted;
};

} // namespace nx
