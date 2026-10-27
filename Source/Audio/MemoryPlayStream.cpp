// NX Player — MIT License
#include "MemoryPlayStream.h"

namespace nx
{

namespace
{
    std::atomic<juce::int64> resident { 0 };
    std::atomic<int> completed { 0 };
}

juce::int64 MemoryPlayStream::residentBytes() noexcept { return resident.load(); }
int MemoryPlayStream::completedLoads() noexcept { return completed.load(); }

MemoryPlayStream::Shared::Shared (juce::int64 n) : size (n)
{
    data.allocate ((size_t) juce::jmax ((juce::int64) 1, n), false);
    if (data != nullptr) resident += n;
}

MemoryPlayStream::Shared::~Shared()
{
    if (data != nullptr) resident -= size;
}

std::unique_ptr<MemoryPlayStream> MemoryPlayStream::open (const juce::File& file, juce::int64 maxBytes)
{
    const auto size = file.getSize();
    if (size <= 0 || size > maxBytes || ! file.existsAsFile()) return nullptr;

    std::shared_ptr<Shared> shared;
    try { shared = std::make_shared<Shared> (size); }
    catch (...) { return nullptr; }
    if (shared->data == nullptr) return nullptr;

    // Opened here so that a missing / locked file fails at once (the caller falls back to the disk).
    auto in = std::make_shared<juce::FileInputStream> (file);
    if (! in->openedOk()) return nullptr;

    std::weak_ptr<Shared> weak = shared;
    juce::Thread::launch ([weak, in]
    {
        constexpr int chunk = 1 << 20;
        juce::int64 done = 0;
        while (true)
        {
            auto s = weak.lock();
            if (s == nullptr || s->cancel.load()) return;                  // the stream was closed: stop loading
            if (done >= s->size) break;
            const int n = (int) juce::jmin ((juce::int64) chunk, s->size - done);
            const int got = in->read (s->data.get() + done, n);
            if (got <= 0) { s->failed = true; return; }
            done += got;
            s->loaded.store (done, std::memory_order_release);
        }
        ++completed;
    });
    return std::unique_ptr<MemoryPlayStream> (new MemoryPlayStream (std::move (shared)));
}

MemoryPlayStream::~MemoryPlayStream()
{
    shared->cancel = true;
}

int MemoryPlayStream::read (void* dest, int numBytes)
{
    const auto end = juce::jmin (shared->size, position + (juce::int64) juce::jmax (0, numBytes));
    if (end <= position) return 0;
    // Wait for the loader (only at the very start of a file, or after a seek far ahead).
    for (int spins = 0; shared->loaded.load (std::memory_order_acquire) < end; ++spins)
    {
        if (shared->failed.load() || shared->cancel.load()) break;
        juce::Thread::sleep (spins < 50 ? 1 : 5);
    }
    const auto available = juce::jmin (end, shared->loaded.load (std::memory_order_acquire));
    const int n = (int) juce::jmax ((juce::int64) 0, available - position);
    if (n > 0) std::memcpy (dest, shared->data.get() + position, (size_t) n);
    position += n;
    return n;
}

} // namespace nx
