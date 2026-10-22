// NX Player — MIT License
// Apple Lossless (ALAC) in MP4 / M4A, decoded with Apple's open-source reference decoder
// (ThirdParty/alac, Apache 2.0) on every platform, so the result is bit-exact everywhere.
#pragma once

#include "AudioDecoder.h"
#include <vector>

namespace nx
{

/** Minimal ISO-BMFF sample table (one audio track). Also used by the tag reader. */
struct Mp4AudioTrack
{
    juce::String codec;              // "alac", "mp4a", "fLaC"...
    std::vector<uint8_t> codecConfig;// ALAC magic cookie (ALACSpecificConfig) or dfLa metadata blocks
    int channels = 0, sampleSize = 0;
    double sampleRate = 0;
    juce::uint32 timescale = 0;
    juce::uint64 durationUnits = 0;
    struct Packet { juce::int64 offset; juce::uint32 size; juce::uint32 frames; };
    std::vector<Packet> packets;
};

bool parseMp4AudioTrack (juce::InputStream& in, Mp4AudioTrack& out, juce::String& error);

class AlacDecoder final : public AudioDecoder
{
public:
    static std::unique_ptr<AudioDecoder> open (std::unique_ptr<juce::InputStream>& s, juce::String& error);
    ~AlacDecoder() override;

    StreamFormat getFormat() const override { return format; }
    juce::int64 getLength() const override   { return totalFrames; }
    juce::int64 getPosition() const override { return position; }
    bool seek (juce::int64 frame) override;
    int readInt (int32_t* dest, int maxFrames) override;

private:
    AlacDecoder() = default;
    bool decodePacket (size_t index);

    std::unique_ptr<juce::InputStream> stream;
    Mp4AudioTrack track;
    StreamFormat format;
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::vector<juce::int64> packetStartFrame;
    juce::int64 totalFrames = 0, position = 0;

    std::vector<uint8_t> packetBytes, pcmBytes;
    std::vector<int32_t> decoded;    // interleaved int32 left-justified for the current packet
    size_t currentPacket = (size_t) -1;
    int decodedFrames = 0, decodedOffset = 0;
};

} // namespace nx
