// NX Player — MIT License
// Decoders deliver samples exactly as stored in the file:
//  - integer PCM  -> int32 left-justified (a 24-bit sample k becomes k << 8), never through float
//  - float PCM    -> double (32f / 64f, and lossy codecs)
//  - DSD          -> bytes, MSB first in time, interleaved by byte (L R L R...)
#pragma once

#include "../Core/Format.h"
#include <juce_core/juce_core.h>
#include <memory>

namespace nx
{

class AudioDecoder
{
public:
    virtual ~AudioDecoder() = default;

    virtual StreamFormat getFormat() const = 0;

    /** PCM: frames. DSD: bytes per channel (one byte = 8 DSD samples). */
    virtual juce::int64 getLength() const = 0;
    virtual juce::int64 getPosition() const = 0;
    virtual bool seek (juce::int64 position) = 0;

    /** Interleaved reads; return the number of frames (or DSD bytes per channel) produced, 0 at the end. */
    virtual int readInt (int32_t* /*interleaved*/, int /*maxFrames*/)     { return 0; }
    virtual int readDouble (double* /*interleaved*/, int /*maxFrames*/)   { return 0; }
    virtual int readDsd (uint8_t* /*interleaved*/, int /*maxBytes*/)      { return 0; }

    double getDurationSeconds() const
    {
        const auto f = getFormat();
        return f.isDsd() ? (double) getLength() * 8.0 / f.sampleRate : (double) getLength() / f.sampleRate;
    }

    juce::String lastError;
};

/** Opens any supported file (by content, the extension is only a hint). Takes ownership of the stream. */
std::unique_ptr<AudioDecoder> openDecoder (std::unique_ptr<juce::InputStream> stream,
                                           const juce::String& fileNameHint,
                                           juce::String& error);

std::unique_ptr<AudioDecoder> openDecoder (const juce::File& file, juce::String& error);

/** Extensions the library indexes. */
const juce::StringArray& supportedAudioExtensions();

//==============================================================================
namespace bytes
{
    inline uint16_t be16 (const uint8_t* p) noexcept { return (uint16_t) ((p[0] << 8) | p[1]); }
    inline uint32_t be32 (const uint8_t* p) noexcept { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
    inline uint64_t be64 (const uint8_t* p) noexcept { return ((uint64_t) be32 (p) << 32) | be32 (p + 4); }
    inline uint16_t le16 (const uint8_t* p) noexcept { return (uint16_t) (p[0] | (p[1] << 8)); }
    inline uint32_t le32 (const uint8_t* p) noexcept { return p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    inline uint64_t le64 (const uint8_t* p) noexcept { return le32 (p) | ((uint64_t) le32 (p + 4) << 32); }

    /** IEEE 754 80-bit extended (AIFF sample rate). */
    inline double extended80 (const uint8_t* p) noexcept
    {
        const int exponent = ((p[0] & 0x7f) << 8) | p[1];
        const uint64_t mantissa = be64 (p + 2);
        if (exponent == 0 && mantissa == 0) return 0.0;
        const double v = std::ldexp ((double) mantissa, exponent - 16383 - 63);
        return (p[0] & 0x80) ? -v : v;
    }

    inline bool readExactly (juce::InputStream& in, void* dest, int n)
    {
        return in.read (dest, n) == n;
    }
}

/** Reads a whole stream region into memory-safe chunks: helper used by the container parsers. */
struct ChunkHeader
{
    char id[5] {};
    juce::uint64 size = 0;
    juce::int64 dataStart = 0;
};

} // namespace nx
