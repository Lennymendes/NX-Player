// NX Player — MIT License
#include "Http.h"
#include "../Core/Text.h"

namespace nx
{

HttpResponse httpRequest (const juce::String& method, const juce::URL& url, const juce::StringPairArray& headers,
                          const juce::String& body, int timeoutMs)
{
    HttpResponse r;
    juce::String headerText;
    for (int i = 0; i < headers.size(); ++i)
        headerText << headers.getAllKeys()[i] << ": " << headers.getAllValues()[i] << "\r\n";
    if (! headers.containsKey ("User-Agent")) headerText << "User-Agent: NXPlayer/" NX_VERSION "\r\n";

    juce::URL u = body.isNotEmpty() ? url.withPOSTData (body) : url;
    auto opts = juce::URL::InputStreamOptions (body.isNotEmpty() ? juce::URL::ParameterHandling::inPostData
                                                                  : juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (timeoutMs)
                    .withExtraHeaders (headerText)
                    .withStatusCode (&r.status)
                    .withResponseHeaders (&r.headers)
                    .withHttpRequestCmd (method);
    if (auto stream = u.createInputStream (opts))
        r.body = stream->readEntireStreamAsString();
    return r;
}

juce::String formEncode (const juce::StringPairArray& fields)
{
    juce::StringArray parts;
    for (int i = 0; i < fields.size(); ++i)
        parts.add (juce::URL::addEscapeChars (fields.getAllKeys()[i], true) + "=" + juce::URL::addEscapeChars (fields.getAllValues()[i], true));
    return parts.joinIntoString ("&");
}

//==============================================================================
ProgressiveStream::ProgressiveStream (const juce::String& name)
    : juce::Thread (name), temp (".nxstream")
{
    writer = std::make_unique<juce::FileOutputStream> (temp.getFile());
    reader = std::make_unique<juce::FileInputStream> (temp.getFile());
}

ProgressiveStream::~ProgressiveStream()
{
    stopThread (5000);
    reader.reset();
    writer.reset();
}

void ProgressiveStream::begin() { startThread (juce::Thread::Priority::normal); }

bool ProgressiveStream::append (const void* data, size_t n)
{
    if (writer == nullptr || ! writer->write (data, n)) { fail ("écriture du cache impossible"_t); return false; }
    writer->flush();
    available += (juce::int64) n;
    return true;
}

void ProgressiveStream::fail (const juce::String& e)
{
    {
        const juce::ScopedLock l (errorLock);
        error = e;
    }
    failed = true;
}

juce::String ProgressiveStream::getError() const
{
    const juce::ScopedLock l (errorLock);
    return error;
}

void ProgressiveStream::run()
{
    const bool ok = produce();
    if (! ok && ! failed.load()) fail ("téléchargement interrompu"_t);
    if (writer) writer->flush();
    finished = true;
}

juce::int64 ProgressiveStream::getTotalLength()
{
    // Decoders ask for the length to find chunks: wait for the server's answer when we can.
    for (int i = 0; i < 1000 && expected.load() < 0 && ! finished.load(); ++i) juce::Thread::sleep (5);
    return expected.load() >= 0 ? expected.load() : (finished.load() ? available.load() : -1);
}

bool ProgressiveStream::isExhausted()
{
    return finished.load() && position >= available.load();
}

int ProgressiveStream::read (void* dest, int numBytes)
{
    // Wait until the bytes are there, the download ended, or the reader was asked to stop.
    while (! finished.load() && available.load() < position + numBytes && ! juce::Thread::currentThreadShouldExit())
        juce::Thread::sleep (2);

    const auto avail = available.load();
    const int n = (int) std::min<juce::int64> (numBytes, avail - position);
    if (n <= 0) return 0;
    const juce::ScopedLock l (readerLock);
    reader->setPosition (position);
    const int got = reader->read (dest, n);
    position += juce::jmax (0, got);
    return got;
}

//==============================================================================
HttpStream::HttpStream (juce::URL u, juce::String h)
    : ProgressiveStream ("NX http"), url (std::move (u)), headers (std::move (h))
{
    begin();
}

HttpStream::~HttpStream() { stopThread (5000); }

bool HttpStream::produce()
{
    juce::WebInputStream web (url, false);
    if (headers.isNotEmpty()) web.withExtraHeaders (headers);
    web.withConnectionTimeout (15000);
    if (! web.connect (nullptr) || web.isError())
    {
        fail ("connexion impossible : "_t + url.toString (false));
        return false;
    }
    const int status = web.getStatusCode();
    if (status < 200 || status >= 300)
    {
        fail ("HTTP " + juce::String (status));
        return false;
    }
    setExpectedLength (web.getTotalLength());

    juce::HeapBlock<char> buf (1 << 16);
    while (! threadShouldExit())
    {
        const int n = web.read (buf.get(), 1 << 16);
        if (n <= 0) break;
        if (! append (buf.get(), (size_t) n)) return false;
    }
    return ! web.isError();
}

} // namespace nx
