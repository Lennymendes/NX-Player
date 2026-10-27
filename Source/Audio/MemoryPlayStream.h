// NX Player — MIT License
// Memory Play: the whole file is copied into RAM before / while it plays, so the disk (or the NAS share)
// is no longer touched during playback. Loading runs on a background thread and starts at once: reads
// block only until the bytes they need are in memory. The data are the file's bytes, untouched — what
// the decoder reads is identical to the file (bit-perfect).
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace nx
{

class MemoryPlayStream : public juce::InputStream
{
public:
    /** nullptr if the file cannot be opened, is larger than maxBytes or the memory cannot be reserved. */
    static std::unique_ptr<MemoryPlayStream> open (const juce::File& file, juce::int64 maxBytes);
    ~MemoryPlayStream() override;

    juce::int64 getTotalLength() override { return shared->size; }
    bool isExhausted() override { return position >= shared->size; }
    int read (void* dest, int numBytes) override;
    juce::int64 getPosition() override { return position; }
    bool setPosition (juce::int64 pos) override { position = juce::jlimit ((juce::int64) 0, shared->size, pos); return true; }

    bool isFullyLoaded() const noexcept { return shared->loaded.load() >= shared->size; }
    juce::int64 getLoadedBytes() const noexcept { return shared->loaded.load(); }

    /** Bytes of audio files currently held in RAM by Memory Play (all streams). */
    static juce::int64 residentBytes() noexcept;
    /** Files fully loaded since start (statistics, tests). */
    static int completedLoads() noexcept;

private:
    struct Shared
    {
        explicit Shared (juce::int64 n);
        ~Shared();
        juce::HeapBlock<char> data;
        juce::int64 size = 0;
        std::atomic<juce::int64> loaded { 0 };
        std::atomic<bool> cancel { false }, failed { false };
    };
    explicit MemoryPlayStream (std::shared_ptr<Shared> s) : shared (std::move (s)) {}
    std::shared_ptr<Shared> shared;
    juce::int64 position = 0;
};

} // namespace nx
