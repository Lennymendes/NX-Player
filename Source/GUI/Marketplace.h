// NX Player — MIT License
// "Extensions & Thèmes" (a Spicetify-style gallery: every theme and plug-in as a card, switched on or off
// in one click, with live previews) and "Plugins utilisateur" (the catalogue of extra plug-ins shipped with
// NX Player, installed into the user's folder in one click, plus the user's own imported plug-ins).
#pragma once

#include "Widgets.h"
#include "../Ext/PluginManager.h"

namespace nx
{

class ToggleSwitch : public juce::Component
{
public:
    explicit ToggleSwitch (AppContext& c) : ctx (c) { setRepaintsOnMouseActivity (true); }
    void setOn (bool v) { on = v; repaint(); }
    bool isOn() const noexcept { return on; }
    std::function<void (bool)> onToggle;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override;
private:
    AppContext& ctx;
    bool on = false;
};

class MarketCard : public juce::Component
{
public:
    enum class Kind { Theme, UiPlugin, DspPlugin };
    struct Data
    {
        Kind kind = Kind::Theme;
        juce::String id, name, author, version, description, slot;
        bool enabled = false;          // theme: active
        bool installed = true;         // catalogue: present in the user's folder
        bool catalogue = false;
        bool removable = false;        // lives in the user's folder
        juce::String unavailable;      // DSP library missing...
        int paramCount = 0;
        Theme theme;                   // themes: colours for the mock-up
        PluginManager* renderer = nullptr;
    };

    MarketCard (AppContext& c, Data d);
    const Data& getData() const noexcept { return data; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Rectangle<int> previewArea() const { return getLocalBounds().removeFromTop (previewHeight); }

    std::function<void (bool)> onToggle;
    std::function<void()> onInstall, onRemove;
    static constexpr int previewHeight = 132, cardHeight = 282;

private:
    void paintThemeMock (juce::Graphics&, juce::Rectangle<float>);
    void paintDspMock (juce::Graphics&, juce::Rectangle<float>);
    AppContext& ctx;
    Data data;
    ToggleSwitch toggle;
    juce::TextButton installButton;
    IconButton removeButton;
};

class MarketplacePage : public ScrollPage, private juce::Timer
{
public:
    enum class Mode { Gallery, UserPlugins };
    explicit MarketplacePage (AppContext& c);
    ~MarketplacePage() override;

    /** tab: Gallery "tout" | "themes" | "interface" | "dsp"; UserPlugins "catalogue" | "installes". */
    void setMode (Mode m, const juce::String& tab);
    void refresh() override;
    void layoutContent (int width) override;

    /** Installs a catalogue plug-in into the user's folder and enables it (also used by the tests). */
    bool installFromCatalogue (const juce::String& id, juce::String& error);
    bool removeUserPlugin (const juce::String& id, juce::String& error);
    PluginManager& getCatalogue() { return *catalogue; }
    int getNumCards() const { return cards.size(); }

private:
    void timerCallback() override;
    void build();
    void importPlugin();
    juce::File userPluginsRoot() const { return ctx.userDataDirectory().getChildFile ("Plugins"); }
    bool isUserFolder (const juce::File& f) const;
    juce::StringArray tabIds() const;
    juce::StringArray tabLabels() const;

    struct Header : juce::Component
    {
        MarketplacePage& page;
        explicit Header (MarketplacePage& p) : page (p) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
    };

    Mode mode = Mode::Gallery;
    juce::String tab;
    Header header { *this };
    juce::OwnedArray<juce::TextButton> tabButtons;
    juce::TextEditor filter;
    juce::TextButton importButton, folderButton;
    juce::OwnedArray<MarketCard> cards;
    juce::String emptyText;
    juce::Label emptyLabel;
    std::unique_ptr<PluginManager> catalogue;
    int gridTop = 0;
};

} // namespace nx
