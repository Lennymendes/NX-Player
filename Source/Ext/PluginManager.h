// NX Player — MIT License
// Community plug-ins.
//
//  UI plug-ins  (Plugins/UI/<name>/manifest.json + plugin.js): JavaScript (QuickJS through JUCE),
//               drawn in slots of the interface ("nowplaying.side", "playerbar.right", "sidebar.bottom",
//               "home.top"), can change the layout through nx.ui.setLayout(). Hot-reloaded.
//  DSP plug-ins (Plugins/DSP/<name>/manifest.json + shared library): C ABI (nxplayer_dsp.h),
//               inserted in the listening chain (PCM only, takes the stream out of bit-perfect).
// See docs/PLUGINS.md for the full API.
#pragma once

#include "../Engine/DspChain.h"
#include "nxplayer_dsp.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_javascript/juce_javascript.h>
#include <map>

namespace nx
{

struct UiPluginInfo
{
    juce::String id, name, version, author, description;
    juce::File folder, script;
    juce::StringArray slots;
    int fps = 30;
    int preferredHeight = 120;
    juce::var layout;                 // optional layout applied when enabled
    bool enabled = false;
    juce::String error;
};

struct DspPluginInfo
{
    juce::String id, name, author, description;
    juce::File folder, library;
    std::vector<nx_dsp_param> params;
    bool enabled = false;
    juce::String error;
};

class PluginManager : private juce::Timer
{
public:
    PluginManager();
    ~PluginManager() override;

    void setSearchFolders (juce::Array<juce::File> folders);
    void rescan();

    //== UI ===================================================================
    std::vector<UiPluginInfo> getUiPlugins() const;
    void setUiPluginEnabled (const juce::String& id, bool enabled);
    juce::StringArray getEnabledForSlot (const juce::String& slot) const;
    int getPreferredHeight (const juce::String& id) const;
    int getFps (const juce::String& id) const;
    juce::String getUiError (const juce::String& id) const;

    /** Message thread: state shared with every script (nx.player, nx.audio, nx.theme...). */
    void setState (const juce::String& key, const juce::var& value);
    /** Runs render(g, w, h, nx, slot) of a plug-in into g. Returns false (and shows the error) on failure.
        preview: also for a disabled plug-in, in its own script instance (gallery cards). */
    bool render (const juce::String& id, const juce::String& slot, juce::Graphics& g, juce::Rectangle<float> area, bool preview = false);
    /** Scripts of this manager see the same nx.* state as another one (catalogue previews). */
    void shareStateWith (const PluginManager& other) { state = other.state; ownsSharedObjects = false; }
    /** Where a plug-in lives: bundled (next to the app) or installed by the user. */
    juce::File getPluginFolder (const juce::String& id) const;

    /** nx.ui.setLayout({...}) from a script, or a manifest "layout". */
    std::function<void (const juce::var& layout)> onLayoutRequest;
    std::function<void()> onPluginsChanged;

    //== DSP ==================================================================
    std::vector<DspPluginInfo> getDspPlugins() const;
    void setDspPluginEnabled (const juce::String& id, bool enabled);
    void setDspParameter (const juce::String& id, int index, double value);
    double getDspParameter (const juce::String& id, int index) const;
    /** New chain with every enabled DSP plug-in (the engine prepares and swaps it in). */
    std::unique_ptr<DspChain> buildDspChain();

    void checkForChanges() { timerCallback(); }   // tests

private:
    struct UiRuntime;
    void timerCallback() override;
    bool loadScript (UiRuntime& rt);
    juce::int64 fingerprint (const juce::File& folder) const;

    juce::Array<juce::File> folders;
    std::vector<UiPluginInfo> uiPlugins;
    std::vector<DspPluginInfo> dspPlugins;
    std::map<juce::String, std::unique_ptr<UiRuntime>> runtimes;
    std::map<juce::String, juce::int64> fingerprints;
    std::map<juce::String, std::vector<double>> dspValues;
    std::map<juce::String, std::unique_ptr<juce::DynamicLibrary>> libraries;
    std::vector<std::unique_ptr<juce::DynamicLibrary>> retiredLibraries;
    juce::DynamicObject::Ptr state;
    bool ownsSharedObjects = true;   // false: nx.ui / nx.console belong to the manager we share state with
    std::shared_ptr<int> alive = std::make_shared<int> (0);
};

} // namespace nx
