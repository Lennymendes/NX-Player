// NX Player — MIT License
// Owns every service and the windows; implements AppContext for the interface.
#pragma once

#include "AppContext.h"
#include <random>
#include <algorithm>
#include "LookAndFeel.h"
#include "Artwork.h"
#include "../Library/Library.h"
#include "../Library/LibraryScanner.h"
#include "../Library/Lyrics.h"
#include "../Library/Downloads.h"
#include "../Platform/MediaControls.h"
#include "../Providers/MediaProvider.h"
#include "../Ext/ThemeEngine.h"
#include "../Ext/PluginManager.h"
#include "../Ext/AudioAnalyzer.h"
#include "../Library/ArtistInfo.h"
#include "../Remote/RemoteServer.h"
#include "../DSP/StudioTools.h"
#include "../Platform/SystemMonitor.h"

namespace nx
{

class MainWindow;
class MiniWindow;
class MainComponent;

class AppController final : public AppContext, private juce::Timer
{
public:
    struct Options
    {
        juce::File dataDir;                          // empty = user data folder
        juce::File resourcesDir;                     // empty = next to the executable / bundle Resources
        std::unique_ptr<OutputBackend> backend;      // tests: virtual DAC
        bool createWindow = true;
    };

    AppController();
    explicit AppController (Options options);
    ~AppController() override;

    void openFiles (const juce::StringArray& paths);

    //== AppContext ===========================================================
    Library& library() override                { return lib; }
    PlaybackEngine& engine() override          { return *eng; }
    UnifiedMediaProvider& providers() override { return *unified; }
    ThemeManager& themes() override            { return themeManager; }
    PluginManager& plugins() override          { return pluginManager; }
    AudioAnalyzer& analyzer() override         { return *audioAnalyzer; }
    ArtworkCache& artwork() override           { return *artworkCache; }
    LibraryScanner& scanner() override         { return *libScanner; }
    AppSettings& settings() override           { return appSettings; }
    DownloadManager& downloads() override      { return *downloadManager; }
    ArtistInfoService& artistInfo() override   { return *artistInfoService; }
    RemoteServer& remote() override            { return *remoteServer; }
    StudioToolsState& studioTools() override   { return *studio; }

    void playTracks (const std::vector<TrackInfo>& tracks, int startIndex) override;
    void enqueue (const std::vector<TrackInfo>& tracks) override;
    void playCollection (const juce::String& scope, bool shuffle) override;
    const TrackInfo* currentTrack() override;
    void navigate (const PageRequest& r) override;
    void toggleMiniPlayer() override;
    void setShuffle (bool on) override;
    void setRepeat (bool on) override       { repeatOn = on; }
    bool isShuffle() const override         { return shuffleOn; }
    bool isRepeat() const override          { return repeatOn; }
    void applyEngineSettings() override;
    juce::StringArray getBackendNames() override;
    juce::String getCurrentBackendName() override;
    void selectBackend (const juce::String& name) override;
    void selectDevice (const juce::String& name) override;
    void applyTheme (const juce::String& id) override;
    juce::String selectedThemeId() override { return appSettings.get ("theme", themeManager.current().id).toString(); }
    /** Immediate re-skin (tests only: the UI never calls it). */
    void applyThemeNow (const juce::String& id);
    /** Relaunches the application once this process has quit. */
    void restartApplication();
    void showMessage (const juce::String& text) override;
    juce::File userDataDirectory() override  { return dataDir; }
    juce::File resourcesDirectory() override { return resourcesDir; }
    void saveNasServers() override;
    const juce::Image& backdrop() override;
    juce::Component* backdropOwner() override;
    void applyStudioTools() override;
    void setUserVolume (double v) override;
    double getUserVolume() override;
    bool setRemoteEnabled (bool on) override;
    void setLanguage (const juce::String& code) override;
    void setLayoutOption (const juce::String& key, const juce::var& value) override;
    bool isSleepBlocked() override { return sleepBlocker.isActive(); }
    void showOnboarding() override;
    void setImmersive (bool on) override;
    void sendToAuraEngine (const std::vector<TrackInfo>& tracks) override;
    juce::String networkSourceLabel (const juce::String& source) override;

    /** Remote commands (also used by the tests). Message thread. */
    void handleRemoteCommand (const juce::String& command, const juce::StringPairArray& params);
    juce::String buildRemoteState();
    juce::String buildRemoteLibrary (const juce::StringPairArray& params);

    MainComponent* getMainComponent();
    MainWindow* getMainWindow() { return mainWindow.get(); }
    juce::File getDataDir() const { return dataDir; }
    static juce::File defaultDataDir();
    static juce::File defaultResourcesDir();

private:
    void timerCallback() override;
    void onTrackChanged();
    void loadLyrics (const TrackInfo& t);
    void saveEngineSettings();
    void updateSystemNowPlaying (bool force);
    void loadEngineSettings();
    void rebuildDspChain();
    void applyLayoutSettings();
    std::vector<TrackInfo> collectionTracks (const juce::String& scope);
    void extendShuffle();

    juce::File dataDir, resourcesDir;
    AppSettings appSettings;
    Library lib;
    std::unique_ptr<LibraryScanner> libScanner;
    std::unique_ptr<CredentialStore> credentials;
    std::unique_ptr<UnifiedMediaProvider> unified;
    std::unique_ptr<PlaybackEngine> eng;
    std::unique_ptr<AudioAnalyzer> audioAnalyzer;
    std::unique_ptr<ArtworkCache> artworkCache;
    std::unique_ptr<DownloadManager> downloadManager;
    std::unique_ptr<MediaControls> mediaControls;
    std::unique_ptr<ArtistInfoService> artistInfoService;
    std::unique_ptr<RemoteServer> remoteServer;
    std::shared_ptr<StudioToolsState> studio = std::make_shared<StudioToolsState>();
    SleepBlocker sleepBlocker;
    double lastRemoteState = 0.0;
    double lastNowPlayingUpdate = 0.0;
    juce::String nowPlayingArtworkSent;
    ThemeManager themeManager;
    PluginManager pluginManager;
    NXLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<MiniWindow> miniWindow;
    std::unique_ptr<MainComponent> headlessMain;   // tests without a window

    std::vector<TrackInfo> queueTracks;            // parallel to the engine queue
    TrackInfo externalTrack;                        // Spotify / YouTube hand-off
    bool externalPlaying = false;
    bool shuffleOn = false, repeatOn = false;
    bool restartPromptOpen = false;
    // Endless shuffle: shuffled passes over the whole collection, queued a chunk at a time.
    juce::String shuffleScope;                      // empty = no endless shuffle running
    std::vector<TrackInfo> shufflePass;             // current pass, already shuffled
    size_t shuffleNext = 0;                         // next track of the pass to queue
    juce::int64 lastQueuedKey = 0;
    std::mt19937 rng { std::random_device{}() };
    juce::String backendName;
    double lastFrame = 0.0;
    int lyricsGeneration = 0;
    juce::Image emptyImage;
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    std::atomic<bool> nasDiscoveryRunning { false };
};

} // namespace nx
