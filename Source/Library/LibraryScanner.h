// NX Player — MIT License
// Indexes the library folders (local disks, mounted SMB / NFS shares) in the background and keeps
// them in sync: directory modification times are polled every few seconds (cheap, works on network
// shares where file-system events are unreliable), only changed directories are re-read, and a full
// pass catches tag edits every ten minutes.
#pragma once

#include "Library.h"
#include <map>

namespace nx
{

class LibraryScanner : private juce::Thread
{
public:
    explicit LibraryScanner (Library& library);
    ~LibraryScanner() override;

    /** Starts the watcher thread. pollMs: directory polling period. */
    void start (int pollMs = 3000);
    void requestFullScan();
    /** Synchronous scan of one folder (tests, first import). Returns the number of tracks added or updated. */
    int scanFolderNow (const juce::File& folder);
    /** One polling pass, synchronously (tests). */
    void pollNow();

    struct Progress
    {
        std::atomic<int> seen { 0 }, updated { 0 }, removed { 0 };
        std::atomic<bool> scanning { false };
    };
    const Progress& getProgress() const noexcept { return progress; }

private:
    void run() override;
    void pollFolder (const juce::File& root);
    int scanDirectory (const juce::File& dir, bool recursive);
    bool needsUpdate (const juce::File& f);

    Library& library;
    int pollInterval = 3000;
    std::atomic<bool> fullScanRequested { true };
    std::map<juce::String, juce::int64> dirTimes;
    std::mutex scanLock;
    juce::int64 lastFullScan = 0;
    Progress progress;
};

} // namespace nx
