// NX Player — MIT License
#include "LibraryScanner.h"
#include "../Audio/AudioDecoder.h"
#include <set>

namespace nx
{

LibraryScanner::LibraryScanner (Library& l) : juce::Thread ("NX library"), library (l) {}

LibraryScanner::~LibraryScanner() { stopThread (5000); }

void LibraryScanner::start (int pollMs)
{
    pollInterval = juce::jmax (500, pollMs);
    startThread (juce::Thread::Priority::low);
}

void LibraryScanner::requestFullScan()
{
    fullScanRequested = true;
    notify();
}

namespace
{
    bool isAudioFile (const juce::File& f)
    {
        const auto ext = f.getFileExtension().fromFirstOccurrenceOf (".", false, false).toLowerCase();
        return supportedAudioExtensions().contains (ext) && ! f.getFileName().startsWithChar ('.');
    }
}

bool LibraryScanner::needsUpdate (const juce::File& f)
{
    auto existing = library.findTrack ("local", f.getFullPathName());
    return ! existing.has_value()
        || existing->modified != f.getLastModificationTime().toMilliseconds()
        || existing->fileSize != f.getSize();
}

int LibraryScanner::scanDirectory (const juce::File& dir, bool recursive)
{
    int changed = 0;
    juce::StringArray present;
    std::vector<juce::File> toRead;

    for (const auto& entry : juce::RangedDirectoryIterator (dir, recursive, "*", juce::File::findFiles))
    {
        if (threadShouldExit()) return changed;
        const auto f = entry.getFile();
        if (! isAudioFile (f)) continue;
        present.add (f.getFullPathName());
        ++progress.seen;
        if (needsUpdate (f)) toRead.push_back (f);
    }

    size_t i = 0;
    while (i < toRead.size() && ! threadShouldExit())
    {
        SqliteDb::Transaction tx (library.getDb());
        for (int n = 0; n < 100 && i < toRead.size(); ++n, ++i)
        {
            TrackInfo t;
            if (readLocalTrack (toRead[i], library, t))
            {
                library.upsert (t);
                ++changed;
                ++progress.updated;
            }
        }
        tx.commit();
    }

    // Tracks of this directory (and below, when recursive) that disappeared.
    if (recursive)
    {
        const int removed = library.removeMissingUnder (dir.getFullPathName() + juce::File::getSeparatorString(), present);
        progress.removed += removed;
        changed += removed;
    }
    else
    {
        // only the direct children of this directory
        for (auto& path : library.getLocalPathsInDirectory (dir))
            if (! present.contains (path))
            {
                library.remove ("local", path);
                ++progress.removed;
                ++changed;
            }
    }
    return changed;
}

int LibraryScanner::scanFolderNow (const juce::File& folder)
{
    std::lock_guard<std::mutex> l (scanLock);
    progress.scanning = true;
    const int n = scanDirectory (folder, true);
    // Remember the directory times so the watcher only reacts to later changes.
    dirTimes[folder.getFullPathName()] = folder.getLastModificationTime().toMilliseconds();
    for (const auto& e : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findDirectories))
        dirTimes[e.getFile().getFullPathName()] = e.getFile().getLastModificationTime().toMilliseconds();
    const int covers = library.fillMissingArtwork();
    progress.scanning = false;
    if (n > 0 || covers > 0) library.notifyChanged();
    return n;
}

void LibraryScanner::pollFolder (const juce::File& root)
{
    if (! root.isDirectory()) return;
    std::vector<juce::File> dirs { root };
    for (const auto& e : juce::RangedDirectoryIterator (root, true, "*", juce::File::findDirectories))
        dirs.push_back (e.getFile());

    int changed = 0;
    std::set<juce::String> seen;
    for (auto& d : dirs)
    {
        if (threadShouldExit()) return;
        const auto key = d.getFullPathName();
        seen.insert (key);
        const auto t = d.getLastModificationTime().toMilliseconds();
        auto it = dirTimes.find (key);
        if (it == dirTimes.end() || it->second != t)
        {
            changed += scanDirectory (d, false);
            library.forgetCoverCheck (key);
            dirTimes[key] = t;
        }
    }
    // Directories that vanished (with all their files).
    for (auto it = dirTimes.begin(); it != dirTimes.end();)
    {
        if (it->first.startsWith (root.getFullPathName()) && seen.count (it->first) == 0)
        {
            changed += library.removeMissingUnder (it->first + juce::File::getSeparatorString(), {});
            it = dirTimes.erase (it);
        }
        else ++it;
    }
    changed += library.fillMissingArtwork();
    if (changed > 0) library.notifyChanged();
}

void LibraryScanner::pollNow()
{
    std::lock_guard<std::mutex> l (scanLock);
    for (auto& path : library.getFolders()) pollFolder (juce::File (path));
}

void LibraryScanner::run()
{
    while (! threadShouldExit())
    {
        const auto now = juce::Time::currentTimeMillis();
        if (fullScanRequested.exchange (false) || now - lastFullScan > 10 * 60 * 1000)
        {
            lastFullScan = now;
            for (auto& path : library.getFolders())
                if (! threadShouldExit()) scanFolderNow (juce::File (path));
        }
        else
        {
            pollNow();
        }
        wait (pollInterval);
    }
}

} // namespace nx
