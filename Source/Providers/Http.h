// NX Player — MIT License
// HTTP helpers for the providers, and progressive streams that let the decoders start reading a
// remote file (NAS / UPnP, Qobuz, Tidal) while it is still downloading.
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

namespace nx
{

struct HttpResponse
{
    int status = 0;
    juce::String body;
    juce::StringPairArray headers;
    bool ok() const noexcept { return status >= 200 && status < 300; }
    juce::var json() const { return juce::JSON::parse (body); }
};

/** Blocking request. method: GET, POST, PUT, DELETE. body: raw (JSON or form-encoded). */
HttpResponse httpRequest (const juce::String& method, const juce::URL& url, const juce::StringPairArray& headers = {},
                          const juce::String& body = {}, int timeoutMs = 15000);

juce::String formEncode (const juce::StringPairArray& fields);

/** InputStream filled by a background thread into a temporary file. Reads block until the bytes they
    need have arrived (or the download failed). Seeking is supported anywhere in what will be downloaded. */
class ProgressiveStream : public juce::InputStream, protected juce::Thread
{
public:
    ~ProgressiveStream() override;

    juce::int64 getTotalLength() override;
    bool isExhausted() override;
    int read (void* dest, int numBytes) override;
    juce::int64 getPosition() override { return position; }
    bool setPosition (juce::int64 pos) override { position = std::max<juce::int64> (0, pos); return true; }

    juce::String getError() const;
    juce::int64 getBytesAvailable() const noexcept { return available.load(); }
    bool isComplete() const noexcept { return finished.load(); }

protected:
    explicit ProgressiveStream (const juce::String& threadName);
    /** Starts the download thread. Call from the derived constructor. */
    void begin();
    /** Implemented by the derived class on the download thread: call append() as data arrives. */
    virtual bool produce() = 0;
    bool append (const void* data, size_t n);
    void setExpectedLength (juce::int64 n) { expected = n; }
    void fail (const juce::String& e);

private:
    void run() override;

    juce::TemporaryFile temp;
    std::unique_ptr<juce::FileOutputStream> writer;
    std::unique_ptr<juce::FileInputStream> reader;
    juce::CriticalSection readerLock;
    std::atomic<juce::int64> available { 0 }, expected { -1 };
    std::atomic<bool> finished { false }, failed { false };
    juce::int64 position = 0;
    juce::String error;
    mutable juce::CriticalSection errorLock;
};

/** Plain HTTP(S) download. */
class HttpStream final : public ProgressiveStream
{
public:
    HttpStream (juce::URL url, juce::String extraHeaders = {});
    ~HttpStream() override;

private:
    bool produce() override;
    juce::URL url;
    juce::String headers;
};

} // namespace nx
