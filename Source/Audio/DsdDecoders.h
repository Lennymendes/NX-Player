// NX Player — MIT License
// DSD file readers: DSF (Sony) and DSDIFF / DFF (Philips). Both deliver MSB-first bytes interleaved
// by byte, the layout DoP and ASIO DSD (MSB1) expect. DST-compressed DFF files are refused.
#pragma once

#include "AudioDecoder.h"

namespace nx
{

class DsfDecoder final : public AudioDecoder
{
public:
    static std::unique_ptr<AudioDecoder> open (std::unique_ptr<juce::InputStream>& s, juce::String& error);

    StreamFormat getFormat() const override { return format; }
    juce::int64 getLength() const override   { return lengthBytes; }
    juce::int64 getPosition() const override { return position; }
    bool seek (juce::int64 bytePos) override;
    int readDsd (uint8_t* dest, int maxBytes) override;

    juce::int64 metadataOffset = 0;   // ID3v2 tag at the end of the file (0 = none)

private:
    bool loadBlockGroup (juce::int64 groupIndex);

    std::unique_ptr<juce::InputStream> stream;
    StreamFormat format;
    juce::int64 dataStart = 0, lengthBytes = 0, position = 0;
    int blockSize = 4096;
    bool lsbFirst = false;
    juce::HeapBlock<uint8_t> group;   // channels * blockSize
    juce::int64 loadedGroup = -1;
};

class DffDecoder final : public AudioDecoder
{
public:
    static std::unique_ptr<AudioDecoder> open (std::unique_ptr<juce::InputStream>& s, juce::String& error);

    StreamFormat getFormat() const override { return format; }
    juce::int64 getLength() const override   { return lengthBytes; }
    juce::int64 getPosition() const override { return position; }
    bool seek (juce::int64 bytePos) override;
    int readDsd (uint8_t* dest, int maxBytes) override;

    juce::int64 id3Offset = 0, id3Size = 0;  // optional "ID3 " chunk

private:
    std::unique_ptr<juce::InputStream> stream;
    StreamFormat format;
    juce::int64 dataStart = 0, lengthBytes = 0, position = 0;
};

/** Reverses the bit order of a byte (DSF stores LSB-first when bitsPerSample == 1). */
inline uint8_t reverseBits (uint8_t b) noexcept
{
    b = (uint8_t) ((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t) ((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t) ((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

} // namespace nx
