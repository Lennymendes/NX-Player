// NX Player — MIT License
// A small UPnP / DLNA media server for the tests: answers unicast M-SEARCH over UDP, serves its device
// description, a ContentDirectory (Browse with a capped page size, like many real servers, and SOAP faults)
// and the media files over HTTP. Everything runs on 127.0.0.1.
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <map>
#include <vector>

namespace nxtest
{

class FakeUpnpServer : public juce::Thread
{
public:
    struct Node
    {
        juce::String id, parent, title, artist, album, art;   // art: file name served under /art/
        bool container = false;
        juce::String file, mime = "audio/wav";                 // items: file name served under /media/
        double rate = 44100.0; int bits = 16; double seconds = 0.2;
    };

    FakeUpnpServer (juce::File mediaFolder, juce::String name = "NAS de test")
        : juce::Thread ("fake upnp"), media (std::move (mediaFolder)), friendlyName (std::move (name))
    {
        for (port = 38400; port < 38500; ++port)
            if (listener.createListener (port, "127.0.0.1")) break;
        udp.bindToPort (0, "127.0.0.1");
        udpPort = udp.getBoundPort();
        startThread();
    }

    ~FakeUpnpServer() override
    {
        signalThreadShouldExit();
        listener.close();
        udp.shutdown();
        stopThread (3000);
        for (int i = 0; i < 500 && activeConnections.load() > 0; ++i) juce::Thread::sleep (10);
    }

    juce::String base() const      { return "http://127.0.0.1:" + juce::String (port); }
    juce::String location() const  { return base() + "/desc.xml"; }

    void add (const Node& n) { nodes.push_back (n); }

    int port = 0, udpPort = 0;
    int pageCap = 2;                         // the server never returns more than this per Browse
    std::atomic<int> browseRequests { 0 }, searchAnswers { 0 }, activeConnections { 0 };

private:
    static juce::String esc (const juce::String& s)
    {
        return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;");
    }

    static juce::String between (const juce::String& s, const juce::String& a, const juce::String& b)
    {
        return s.fromFirstOccurrenceOf (a, false, false).upToFirstOccurrenceOf (b, false, false);
    }

    juce::String didl (const std::vector<const Node*>& list) const
    {
        juce::String d = "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
                         "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">";
        for (auto* n : list)
        {
            if (n->container)
            {
                int children = 0;
                for (auto& c : nodes) if (c.parent == n->id) ++children;
                d << "<container id=\"" << esc (n->id) << "\" parentID=\"" << esc (n->parent) << "\" childCount=\"" << children << "\" restricted=\"1\">"
                  << "<dc:title>" << esc (n->title) << "</dc:title>"
                  << (n->artist.isNotEmpty() ? "<upnp:artist>" + esc (n->artist) + "</upnp:artist>" : juce::String())
                  << (n->art.isNotEmpty() ? "<upnp:albumArtURI>" + esc (base() + "/art/" + n->art) + "</upnp:albumArtURI>" : juce::String())
                  << "<upnp:class>object.container.album.musicAlbum</upnp:class></container>";
            }
            else
            {
                const int secs = (int) n->seconds;
                const auto dur = "0:00:" + juce::String (secs).paddedLeft ('0', 2) + "." + juce::String ((int) ((n->seconds - secs) * 1000)).paddedLeft ('0', 3);
                d << "<item id=\"" << esc (n->id) << "\" parentID=\"" << esc (n->parent) << "\" restricted=\"1\">"
                  << "<dc:title>" << esc (n->title) << "</dc:title><upnp:artist>" << esc (n->artist) << "</upnp:artist>"
                  << "<upnp:album>" << esc (n->album) << "</upnp:album><upnp:class>object.item.audioItem.musicTrack</upnp:class>"
                  << (n->art.isNotEmpty() ? "<upnp:albumArtURI>" + esc (base() + "/art/" + n->art) + "</upnp:albumArtURI>" : juce::String())
                  << "<res protocolInfo=\"http-get:*:audio/mpeg:*\" duration=\"" << dur << "\">" << esc (base() + "/media/" + n->file + ".mp3") << "</res>"
                  << "<res protocolInfo=\"http-get:*:" << n->mime << ":*\" duration=\"" << dur << "\" sampleFrequency=\"" << (int) n->rate
                  << "\" bitsPerSample=\"" << n->bits << "\" nrAudioChannels=\"2\">" << esc (base() + "/media/" + n->file) << "</res></item>";
            }
        }
        return d + "</DIDL-Lite>";
    }

    juce::String soapAnswer (const juce::String& body, int& status)
    {
        status = 200;
        const auto envelope = [] (const juce::String& inner)
        {
            return "<?xml version=\"1.0\" encoding=\"utf-8\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                   "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>" + inner + "</s:Body></s:Envelope>";
        };
        const auto objectId = between (body, "<ObjectID>", "</ObjectID>");
        const int start = between (body, "<StartingIndex>", "</StartingIndex>").getIntValue();
        const int count = between (body, "<RequestedCount>", "</RequestedCount>").getIntValue();
        ++browseRequests;

        bool exists = objectId == "0";
        for (auto& n : nodes) if (n.id == objectId && n.container) exists = true;
        if (! exists)
        {
            status = 500;
            return envelope ("<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
                             "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>701</errorCode>"
                             "<errorDescription>No such object</errorDescription></UPnPError></detail></s:Fault>");
        }
        std::vector<const Node*> children;
        for (auto& n : nodes) if (n.parent == objectId) children.push_back (&n);
        const int total = (int) children.size();
        const int n = juce::jlimit (0, juce::jmax (0, total - start), juce::jmin (count > 0 ? count : total, pageCap));
        std::vector<const Node*> page (children.begin() + juce::jmin (start, total), children.begin() + juce::jmin (start, total) + n);
        return envelope ("<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>" + esc (didl (page))
                         + "</Result><NumberReturned>" + juce::String (n) + "</NumberReturned><TotalMatches>" + juce::String (total)
                         + "</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse>");
    }

    static void writeAll (juce::StreamingSocket& c, const void* data, size_t size)
    {
        size_t sent = 0;
        for (int stalls = 0; sent < size && stalls < 200;)
        {
            const int w = c.write ((const char*) data + sent, (int) std::min<size_t> (65536, size - sent));
            if (w > 0) { sent += (size_t) w; stalls = 0; continue; }
            if (c.waitUntilReady (false, 50) < 0) break;
            ++stalls;
        }
    }

    void serve (juce::StreamingSocket& c)
    {
        juce::MemoryBlock in;
        juce::String head;
        for (int waited = 0; waited < 5000;)
        {
            const int r = c.waitUntilReady (true, 50);
            if (r < 0) return;
            if (r == 0) { waited += 50; continue; }
            char buf[8192];
            const int n = c.read (buf, sizeof (buf), false);
            if (n <= 0) break;
            in.append (buf, (size_t) n);
            head = in.toString();
            if (! head.contains ("\r\n\r\n")) continue;
            const auto lenText = head.fromFirstOccurrenceOf ("Content-Length:", false, true).upToFirstOccurrenceOf ("\r\n", false, false).trim();
            const auto bodyStart = head.indexOf ("\r\n\r\n") + 4;
            if ((int) in.getSize() - bodyStart >= lenText.getIntValue()) break;
        }
        const auto method = head.upToFirstOccurrenceOf (" ", false, false);
        const auto path = between (head, " ", " ");
        const auto body = head.fromFirstOccurrenceOf ("\r\n\r\n", false, false);

        int status = 200;
        juce::MemoryBlock content;
        juce::String type = "text/xml; charset=\"utf-8\"";
        if (method == "GET" && path == "/desc.xml")
        {
            content.append (desc().toRawUTF8(), desc().getNumBytesAsUTF8());
        }
        else if (method == "POST" && path == "/ctl")
        {
            const auto xml = soapAnswer (body, status);
            content.append (xml.toRawUTF8(), xml.getNumBytesAsUTF8());
        }
        else if (method == "GET" && (path.startsWith ("/media/") || path.startsWith ("/art/")))
        {
            const auto f = media.getChildFile (juce::URL::removeEscapeChars (path.fromLastOccurrenceOf ("/", false, false)));
            type = "application/octet-stream";
            if (! f.existsAsFile() || ! f.loadFileAsData (content)) status = 404;
        }
        else status = 404;

        const juce::String h = "HTTP/1.1 " + juce::String (status) + (status == 200 ? " OK" : status == 500 ? " Internal Server Error" : " Not Found")
                               + "\r\nContent-Type: " + type + "\r\nContent-Length: " + juce::String ((juce::int64) content.getSize())
                               + "\r\nConnection: close\r\n\r\n";
        writeAll (c, h.toRawUTF8(), h.getNumBytesAsUTF8());
        if (method != "HEAD") writeAll (c, content.getData(), content.getSize());
        for (int waited = 0; waited < 2000;)   // let the client close first
        {
            const int r = c.waitUntilReady (true, 50);
            if (r < 0) break;
            if (r == 0) { waited += 50; continue; }
            char sink[1024];
            if (c.read (sink, sizeof (sink), false) <= 0) break;
        }
    }

    juce::String desc() const
    {
        return "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
               "<device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>" + esc (friendlyName) + "</friendlyName>"
               "<UDN>uuid:nx-test-" + juce::String (port) + "</UDN><iconList><icon><mimetype>image/jpeg</mimetype><width>120</width><height>120</height>"
               "<depth>24</depth><url>/art/icon.jpg</url></icon></iconList><serviceList><service>"
               "<serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><controlURL>/cm</controlURL></service><service>"
               "<serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>"
               "<controlURL>/ctl</controlURL><eventSubURL>/evt</eventSubURL><SCPDURL>/cd.xml</SCPDURL></service></serviceList></device></root>";
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            // SSDP over unicast UDP
            if (udp.waitUntilReady (true, 20) == 1)
            {
                char buf[2048];
                juce::String sender;
                int senderPort = 0;
                const int n = udp.read (buf, sizeof (buf) - 1, false, sender, senderPort);
                if (n > 0 && juce::String::fromUTF8 (buf, n).startsWith ("M-SEARCH"))
                {
                    const auto st = between (juce::String::fromUTF8 (buf, n), "\r\nST: ", "\r\n");
                    const juce::String answer = "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nEXT:\r\nLOCATION: " + location()
                                              + "\r\nSERVER: Test/1.0 UPnP/1.0 FakeNAS/1.0\r\nST: " + st + "\r\nUSN: uuid:nx-test::" + st + "\r\n\r\n";
                    udp.write (sender, senderPort, answer.toRawUTF8(), (int) answer.getNumBytesAsUTF8());
                    ++searchAnswers;
                }
            }
            if (listener.waitUntilReady (true, 30) != 1) continue;
            std::shared_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
            if (c == nullptr) continue;
            // One thread per connection, like a real NAS (artwork, browsing and streaming at the same time).
            ++activeConnections;
            juce::Thread::launch ([this, c] { serve (*c); --activeConnections; });
        }
    }

    juce::File media;
    juce::String friendlyName;
    std::vector<Node> nodes;
    juce::StreamingSocket listener;
    juce::DatagramSocket udp { false };
};

} // namespace nxtest
