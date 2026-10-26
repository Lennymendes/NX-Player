// NX Player — MIT License
// UPnP / DLNA media servers on the local network (MinimServer, Asset, Roon Server's DLNA, Synology,
// QNAP, JRiver...): SSDP discovery, ContentDirectory Browse / Search, DIDL-Lite parsing. Tracks are
// streamed over HTTP into NX Player's own decoders (bit-perfect). SMB / NFS shares are simply library
// folders (mounted volumes) handled by the LibraryScanner.
#pragma once

#include "MediaProvider.h"

namespace nx
{

struct UpnpServer
{
    juce::String udn, name, location, controlUrl, iconUrl;
};

struct UpnpEntry
{
    bool isContainer = false;
    juce::String id, parentId, title;
    int childCount = 0;
    TrackInfo track;      // items; for containers only artist / album / artworkKey are filled
};

struct UpnpBrowsePage
{
    bool ok = false;
    std::vector<UpnpEntry> entries;
    int numberReturned = 0, totalMatches = -1;
    juce::String error;
};

class UpnpProvider final : public MediaProvider
{
public:
    UpnpProvider() = default;
    /** Background threads (discovery, browsing) may still be using the provider: wait for them. */
    ~UpnpProvider() override { for (int i = 0; i < 600 && busy.load() > 0; ++i) juce::Thread::sleep (10); }
    juce::String getId() const override          { return "upnp"; }
    juce::String getDisplayName() const override { return "NAS (UPnP / DLNA)"; }
    bool isConfigured() const override           { return true; }
    bool isSignedIn() const override;
    juce::String getStatusText() const override;

    /** Blocking: multicast M-SEARCH (MediaServer + ContentDirectory, sent 3 times) while also listening to
        the servers' NOTIFY announcements, for timeoutMs. Known servers are kept. */
    std::vector<UpnpServer> discover (int timeoutMs = 3000);
    /** Unicast M-SEARCH to one host (works when multicast is filtered: VPN, other subnet, Wi-Fi isolation). */
    std::vector<UpnpServer> discoverHost (const juce::String& host, int port = 1900, int timeoutMs = 2500);
    std::vector<UpnpServer> getServers() const;
    bool addServerFromLocation (const juce::String& location);
    juce::StringArray getKnownLocations() const;

    /** One page of Browse (BrowseDirectChildren). */
    UpnpBrowsePage browsePage (const UpnpServer& server, const juce::String& objectId, int start, int count);
    /** Every child of a container: follows NumberReturned / TotalMatches (servers cap their pages). */
    std::vector<UpnpEntry> browseAll (const UpnpServer& server, const juce::String& objectId, int maxEntries = 20000);
    std::vector<UpnpEntry> browse (const UpnpServer& server, const juce::String& objectId) { return browseAll (server, objectId); }
    /** Every track below a container (recursive). */
    std::vector<TrackInfo> collectTracks (const UpnpServer& server, const juce::String& objectId, int maxTracks = 20000);
    std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) override;
    /** Imports every track below objectId into the library (recursive). Returns the count. */
    int importContainer (Library& library, const UpnpServer& server, const juce::String& objectId, int maxTracks = 20000);

    // Parsers (tests)
    static juce::String parseSsdpLocation (const juce::String& response);
    static juce::String parseSsdpHeader (const juce::String& response, const juce::String& name);
    /** True for an M-SEARCH answer or an ssdp:alive NOTIFY that announces a media server. */
    static bool isMediaServerAnnouncement (const juce::String& response);
    static bool parseDeviceDescription (const juce::String& xml, const juce::String& location, UpnpServer& out);
    static std::vector<UpnpEntry> parseDidl (const juce::String& didl, const juce::String& serverUdn);
    static double parseDuration (const juce::String& hhmmss);
    static juce::String mSearch (const juce::String& searchTarget, const juce::String& host = "239.255.255.250:1900");

private:
    UpnpBrowsePage soapCall (const UpnpServer& server, const juce::String& action, const juce::String& args);
    void collectLocations (juce::DatagramSocket& socket, juce::StringArray& locations, int waitMs);
    std::vector<UpnpServer> servers;
    mutable juce::CriticalSection lock;
    std::atomic<int> busy { 0 };
    struct Busy { std::atomic<int>& c; explicit Busy (std::atomic<int>& x) : c (x) { ++c; } ~Busy() { --c; } };
};

} // namespace nx
