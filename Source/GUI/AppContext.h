// NX Player — MIT License
// What every UI component can reach: the services and the navigation / playback actions.
#pragma once

#include "../Library/Library.h"
#include "../Engine/PlaybackEngine.h"
#include "../Ext/ThemeEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace nx
{

class UnifiedMediaProvider;
class PluginManager;
class AudioAnalyzer;
class ArtworkCache;
class LibraryScanner;

enum class PageId { Home, Search, RecentlyAdded, Albums, AlbumDetail, Artists, ArtistDetail, Songs, Playlists, PlaylistDetail,
                    Source, NowPlaying, Settings, Marketplace, UserPlugins };

struct PageRequest
{
    PageId page = PageId::Home;
    juce::String a, b;        // album artist / album, artist, playlist id, source id (+ NAS folder), search text, tab
    bool operator== (const PageRequest& o) const noexcept { return page == o.page && a == o.a && b == o.b; }
    bool operator!= (const PageRequest& o) const noexcept { return ! operator== (o); }
};

struct AppSettings
{
    juce::File file;
    juce::var root { new juce::DynamicObject() };

    void load()                { auto v = juce::JSON::parse (file); if (v.isObject()) root = v; }
    void save() const          { file.getParentDirectory().createDirectory(); file.replaceWithText (juce::JSON::toString (root)); }
    juce::var get (const juce::String& k, const juce::var& def = {}) const { return root.getProperty (k, def); }
    void set (const juce::String& k, const juce::var& v) { root.getDynamicObject()->setProperty (k, v); save(); }
};

class AppContext
{
public:
    virtual ~AppContext() = default;

    virtual Library& library() = 0;
    virtual PlaybackEngine& engine() = 0;
    virtual UnifiedMediaProvider& providers() = 0;
    virtual ThemeManager& themes() = 0;
    virtual PluginManager& plugins() = 0;
    virtual AudioAnalyzer& analyzer() = 0;
    virtual ArtworkCache& artwork() = 0;
    virtual LibraryScanner& scanner() = 0;
    virtual AppSettings& settings() = 0;

    const Theme& theme() { return themes().current(); }

    /** Plays a list (local, NAS, Qobuz / Tidal through the engine; Spotify / YouTube handed over). */
    virtual void playTracks (const std::vector<TrackInfo>& tracks, int startIndex) = 0;
    virtual void enqueue (const std::vector<TrackInfo>& tracks) = 0;
    virtual const TrackInfo* currentTrack() = 0;
    virtual void navigate (const PageRequest& request) = 0;
    virtual void toggleMiniPlayer() = 0;
    virtual void setShuffle (bool on) = 0;
    virtual void setRepeat (bool on) = 0;
    virtual bool isShuffle() const = 0;
    virtual bool isRepeat() const = 0;
    virtual void applyEngineSettings() = 0;
    virtual juce::StringArray getBackendNames() = 0;
    virtual juce::String getCurrentBackendName() = 0;
    virtual void selectBackend (const juce::String& name) = 0;
    virtual void selectDevice (const juce::String& name) = 0;
    virtual void applyTheme (const juce::String& id) = 0;
    virtual void showMessage (const juce::String& text) = 0;
    /** User data folder (settings, library, user themes and plug-ins) and bundled resources folder. */
    virtual juce::File userDataDirectory() = 0;
    virtual juce::File resourcesDirectory() = 0;
    /** Remembers the NAS servers found so far (they reconnect at the next launch without discovery). */
    virtual void saveNasServers() = 0;

    /** The blurred artwork behind the window (for glass panels), in main-window coordinates. */
    virtual const juce::Image& backdrop() = 0;
    virtual juce::Component* backdropOwner() = 0;
    /** Layout overrides requested by plug-ins / settings. */
    juce::var layout { new juce::DynamicObject() };
};

} // namespace nx
