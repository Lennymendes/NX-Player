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
    TrackInfo track;      // items only
};

class UpnpProvider final : public MediaProvider
{
public:
    juce::String getId() const override          { return "upnp"; }
    juce::String getDisplayName() const override { return "NAS (UPnP / DLNA)"; }
    bool isConfigured() const override           { return true; }
    bool isSignedIn() const override;
    juce::String getStatusText() const override;

    /** Blocking: multicast M-SEARCH, collects answers for timeoutMs. */
    std::vector<UpnpServer> discover (int timeoutMs = 2500);
    std::vector<UpnpServer> getServers() const;
    void addServerFromLocation (const juce::String& location);

    std::vector<UpnpEntry> browse (const UpnpServer& server, const juce::String& objectId, int start = 0, int count = 500);
    std::vector<TrackInfo> searchTracks (const juce::String& query, int limit) override;
    /** Imports every track below objectId into the library (recursive). Returns the count. */
    int importContainer (Library& library, const UpnpServer& server, const juce::String& objectId, int maxTracks = 20000);

    // Parsers (tests)
    static juce::String parseSsdpLocation (const juce::String& response);
    static bool parseDeviceDescription (const juce::String& xml, const juce::String& location, UpnpServer& out);
    static std::vector<UpnpEntry> parseDidl (const juce::String& didl, const juce::String& serverUdn);
    static double parseDuration (const juce::String& hhmmss);

private:
    std::vector<UpnpEntry> soapCall (const UpnpServer& server, const juce::String& action, const juce::String& args);
    std::vector<UpnpServer> servers;
    mutable juce::CriticalSection lock;
};

} // namespace nx
