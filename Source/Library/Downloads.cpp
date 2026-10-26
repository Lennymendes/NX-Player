// NX Player — MIT License
#include "Downloads.h"
#include "../Core/Text.h"

namespace nx
{

DownloadManager::DownloadManager (Library& l) : juce::Thread ("NX downloads"), library (l)
{
    folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("NX Player");
    startThread (juce::Thread::Priority::low);
}

DownloadManager::~DownloadManager()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (8000);
}

void DownloadManager::setFolder (const juce::File& f)
{
    const std::lock_guard<std::mutex> l (lock);
    folder = f;
}

juce::File DownloadManager::getFolder() const
{
    const std::lock_guard<std::mutex> l (lock);
    return folder;
}

int DownloadManager::enqueue (const std::vector<TrackInfo>& tracks)
{
    int added = 0;
    {
        const std::lock_guard<std::mutex> l (lock);
        if (queue.empty() && currentKey.isEmpty()) sessionTotal = 0;
        for (auto& t : tracks)
        {
            if (! canDownload (t) || library.getOfflineCopy (t).isNotEmpty()) continue;
            const auto k = key (t);
            if (auto it = states.find (k); it != states.end() && (it->second == State::Queued || it->second == State::Downloading)) continue;
            states[k] = State::Queued;
            queue.push_back (t);
            ++sessionTotal;
            ++added;
        }
    }
    if (added > 0) { wake.signal(); sendChangeMessage(); }
    return added;
}

DownloadManager::State DownloadManager::getState (const TrackInfo& t) const
{
    if (t.source == "local") return library.isOfflineCopy (t) ? State::Done : State::None;
    {
        const std::lock_guard<std::mutex> l (lock);
        if (auto it = states.find (key (t)); it != states.end() && it->second != State::Done) return it->second;
    }
    return library.getOfflineCopy (t).isNotEmpty() ? State::Done : State::None;
}

float DownloadManager::getProgress (const TrackInfo& t) const
{
    const std::lock_guard<std::mutex> l (lock);
    return key (t) == currentKey ? currentProgress : 0.0f;
}

int DownloadManager::getPendingCount() const
{
    const std::lock_guard<std::mutex> l (lock);
    return (int) queue.size() + (currentKey.isNotEmpty() ? 1 : 0);
}

int DownloadManager::getSessionTotal() const     { const std::lock_guard<std::mutex> l (lock); return sessionTotal; }
float DownloadManager::getCurrentProgress() const { const std::lock_guard<std::mutex> l (lock); return currentProgress; }
juce::String DownloadManager::getCurrentTitle() const { const std::lock_guard<std::mutex> l (lock); return currentTitle; }

bool DownloadManager::waitUntilIdle (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (getPendingCount() > 0)
    {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep (10);
    }
    return true;
}

juce::String DownloadManager::extensionFor (const TrackInfo& t)
{
    // The file name in the URL first (".../01 Song.flac?x=1"), then the codec announced by the server.
    const auto path = t.uri.upToFirstOccurrenceOf ("?", false, false).fromLastOccurrenceOf ("/", false, false);
    const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
    static const juce::StringArray known { "flac", "wav", "aif", "aiff", "dsf", "dff", "m4a", "mp3", "ogg", "wv", "ape" };
    if (path.containsChar ('.') && known.contains (ext)) return ext;
    const auto c = t.codec.toUpperCase();
    if (c == "FLAC") return "flac";
    if (c == "WAV")  return "wav";
    if (c == "AIFF") return "aiff";
    if (c == "DSF")  return "dsf";
    if (c == "DFF")  return "dff";
    if (c == "ALAC") return "m4a";
    if (c == "MP3")  return "mp3";
    if (c == "VORBIS") return "ogg";
    return "flac";
}

juce::File DownloadManager::destinationFor (const juce::File& root, const TrackInfo& t)
{
    auto legal = [] (const juce::String& s, const juce::String& fallback)
    {
        auto v = juce::File::createLegalFileName (s.trim()).trimCharactersAtEnd (". ");
        return v.isEmpty() ? fallback : v.substring (0, 120);
    };
    const auto artist = legal (t.albumArtist.isNotEmpty() ? t.albumArtist : t.artist, "Artiste inconnu"_t);
    const auto album = legal (t.album, "Album inconnu");
    const auto title = legal (t.title, "Piste");
    const auto name = (t.trackNumber > 0 ? juce::String (t.trackNumber).paddedLeft ('0', 2) + " " : juce::String()) + title + "." + extensionFor (t);
    return root.getChildFile (artist).getChildFile (album).getChildFile (name);
}

void DownloadManager::post (const juce::String& message)
{
    std::weak_ptr<int> token = alive;
    juce::MessageManager::callAsync ([this, token, message] { if (! token.expired() && onMessage) onMessage (message); });
}

bool DownloadManager::downloadOne (const TrackInfo& t, juce::String& error)
{
    const auto dest = destinationFor (getFolder(), t);
    if (! dest.getParentDirectory().createDirectory()) { error = "dossier impossible à créer : "_t + dest.getParentDirectory().getFullPathName(); return false; }
    const auto part = dest.withFileExtension (dest.getFileExtension().substring (1) + ".part");

    // --- the audio file -------------------------------------------------------------------------
    juce::WebInputStream web (juce::URL (t.uri), false);
    web.withConnectionTimeout (15000);
    if (! web.connect (nullptr) || web.isError() || web.getStatusCode() < 200 || web.getStatusCode() >= 300)
    {
        error = "HTTP " + juce::String (web.getStatusCode());
        return false;
    }
    const auto total = web.getTotalLength();
    {
        part.deleteFile();
        juce::FileOutputStream out (part);
        if (! out.openedOk()) { error = "écriture impossible : "_t + part.getFullPathName(); return false; }
        juce::HeapBlock<char> buf (1 << 16);
        juce::int64 done = 0;
        auto lastNotify = juce::Time::getMillisecondCounter();
        while (! threadShouldExit())
        {
            const int n = web.read (buf.get(), 1 << 16);
            if (n <= 0) break;
            if (! out.write (buf.get(), (size_t) n)) { error = "disque plein ?"_t; return false; }
            done += n;
            if (total > 0)
            {
                { const std::lock_guard<std::mutex> l (lock); currentProgress = (float) ((double) done / (double) total); }
                if (juce::Time::getMillisecondCounter() - lastNotify > 150) { lastNotify = juce::Time::getMillisecondCounter(); sendChangeMessage(); }
            }
        }
        out.flush();
        if (threadShouldExit()) { part.deleteFile(); error = "interrompu"; return false; }
        if (total > 0 && done < total) { part.deleteFile(); error = "téléchargement incomplet ("_t + juce::String (done) + " / " + juce::String (total) + ")"; return false; }
    }
    dest.deleteFile();
    if (! part.moveFileTo (dest)) { error = "renommage impossible"_t; return false; }

    // --- the album artwork (once per album folder) ------------------------------------------------
    const auto cover = dest.getParentDirectory().getChildFile ("cover.jpg");
    if (! cover.existsAsFile() && t.artworkKey.startsWith ("http"))
    {
        juce::MemoryBlock img;
        if (juce::URL (t.artworkKey).readEntireBinaryStream (img) && img.getSize() > 64) cover.replaceWithData (img.getData(), img.getSize());
    }

    // --- into the library: tags of the file, completed by the server's metadata -------------------
    TrackInfo local;
    if (! readLocalTrack (dest, library, local))
    {
        local.source = "local";
        local.externalId = local.uri = dest.getFullPathName();
        local.codec = t.codec; local.sampleRate = t.sampleRate; local.bitsPerSample = t.bitsPerSample;
        local.channels = t.channels; local.isDsd = t.isDsd; local.lossy = t.lossy; local.duration = t.duration;
    }
    auto prefer = [] (juce::String& field, const juce::String& remote) { if (remote.isNotEmpty()) field = remote; };
    prefer (local.title, t.title);
    prefer (local.artist, t.artist);
    prefer (local.albumArtist, t.albumArtist);
    prefer (local.album, t.album);
    prefer (local.genre, t.genre);
    if (t.trackNumber > 0) local.trackNumber = t.trackNumber;
    if (t.year > 0 && local.year == 0) local.year = t.year;
    if (local.duration <= 0.0) local.duration = t.duration;
    if (local.artworkKey.isEmpty() && cover.existsAsFile())
    {
        juce::MemoryBlock img;
        if (cover.loadFileAsData (img)) local.artworkKey = library.storeArtwork (img, "image/jpeg");
    }
    local.added = 0;   // the library stamps it (milliseconds)
    library.upsert (local);
    library.markOffline (local.externalId, t);
    library.addFolder (getFolder());   // watched from now on (deletions / changes picked up by the scanner)
    library.notifyChanged();
    return true;
}

void DownloadManager::run()
{
    while (! threadShouldExit())
    {
        TrackInfo t;
        {
            const std::lock_guard<std::mutex> l (lock);
            if (! queue.empty())
            {
                t = queue.front();
                queue.pop_front();
                currentKey = key (t);
                currentTitle = t.title;
                currentProgress = 0.0f;
                states[currentKey] = State::Downloading;
            }
        }
        if (t.uri.isEmpty()) { wake.wait (500); continue; }
        sendChangeMessage();

        juce::String error;
        const bool ok = downloadOne (t, error);
        {
            const std::lock_guard<std::mutex> l (lock);
            states[key (t)] = ok ? State::Done : State::Failed;
            currentKey.clear();
            currentTitle.clear();
            currentProgress = 0.0f;
        }
        if (! ok && ! threadShouldExit()) post ("Échec du téléchargement de « "_t + t.title + " » : "_t + error);
        else if (ok && getPendingCount() == 0) post (juce::String (getSessionTotal()) + " morceau(x) disponible(s) hors-ligne"_t);
        sendChangeMessage();
    }
}

} // namespace nx
