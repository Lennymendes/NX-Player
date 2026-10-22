// NX Player — MIT License
// Uncompressed PCM decoders: WAV / BWF / RF64 / BW64 and AIFF / AIFF-C.
#pragma once

#include "AudioDecoder.h"

namespace nx
{

struct RawPcmLayout
{
    juce::int64 dataStart = 0;
    juce::int64 dataBytes = 0;
    int numChannels = 2;
    int bytesPerSample = 2;     // container size
    int validBits = 16;         // significant bits (24 in a 32-bit container, etc.)
    bool isFloat = false;
    bool bigEndian = false;
    bool unsigned8 = false;     // 8-bit WAV is unsigned
    double sampleRate = 44100.0;
    juce::String codec = "WAV";
};

class RawPcmDecoder final : public AudioDecoder
{
public:
    RawPcmDecoder (std::unique_ptr<juce::InputStream> s, const RawPcmLayout& l);

    StreamFormat getFormat() const override;
    juce::int64 getLength() const override       { return totalFrames; }
    juce::int64 getPosition() const override     { return position; }
    bool seek (juce::int64 frame) override;
    int readInt (int32_t* dest, int maxFrames) override;
    int readDouble (double* dest, int maxFrames) override;

    /** Parsers: return nullptr (and an error) when the stream is not of that type. */
    static std::unique_ptr<AudioDecoder> openWav  (std::unique_ptr<juce::InputStream>& s, juce::String& error);
    static std::unique_ptr<AudioDecoder> openAiff (std::unique_ptr<juce::InputStream>& s, juce::String& error);

private:
    int readRaw (int maxFrames);

    std::unique_ptr<juce::InputStream> stream;
    RawPcmLayout layout;
    juce::int64 totalFrames = 0, position = 0;
    int frameBytes = 0;
    juce::HeapBlock<uint8_t> buffer;
    int bufferFrames = 0;
};

} // namespace nx
