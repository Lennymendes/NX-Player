// NX Player — MIT License
// Owns every service and the windows; implements AppContext for the interface.
#pragma once

#include "AppContext.h"
#include "LookAndFeel.h"
#include "Artwork.h"
#include "../Library/Library.h"
#include "../Library/LibraryScanner.h"
#include "../Library/Lyrics.h"
#include "../Providers/MediaProvider.h"
#include "../Ext/ThemeEngine.h"
#include "../Ext/PluginManager.h"
#include "../Ext/AudioAnalyzer.h"

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

    void playTracks (const std::vector<TrackInfo>& tracks, int startIndex) override;
    void enqueue (const std::vector<TrackInfo>& tracks) override;
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
    void showMessage (const juce::String& text) override;
    juce::File userDataDirectory() override  { return dataDir; }
    juce::File resourcesDirectory() override { return resourcesDir; }
    void saveNasServers() override;
    const juce::Image& backdrop() override;
    juce::Component* backdropOwner() override;

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
    void loadEngineSettings();

    juce::File dataDir, resourcesDir;
    AppSettings appSettings;
    Library lib;
    std::unique_ptr<LibraryScanner> libScanner;
    std::unique_ptr<CredentialStore> credentials;
    std::unique_ptr<UnifiedMediaProvider> unified;
    std::unique_ptr<PlaybackEngine> eng;
    std::unique_ptr<AudioAnalyzer> audioAnalyzer;
    std::unique_ptr<ArtworkCache> artworkCache;
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
    juce::String backendName;
    double lastFrame = 0.0;
    int lyricsGeneration = 0;
    juce::Image emptyImage;
    std::shared_ptr<int> alive = std::make_shared<int> (0);
    std::atomic<bool> nasDiscoveryRunning { false };
};

} // namespace nx
