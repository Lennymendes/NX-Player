// NX Player — MIT License
#include <algorithm>
#include "RawPcmDecoder.h"
#include <cstring>

namespace nx
{

RawPcmDecoder::RawPcmDecoder (std::unique_ptr<juce::InputStream> s, const RawPcmLayout& l)
    : stream (std::move (s)), layout (l)
{
    frameBytes = layout.bytesPerSample * layout.numChannels;
    totalFrames = frameBytes > 0 ? layout.dataBytes / frameBytes : 0;
    bufferFrames = juce::jmax (1, 65536 / juce::jmax (1, frameBytes));
    buffer.allocate ((size_t) (bufferFrames * frameBytes), true);
    stream->setPosition (layout.dataStart);
}

StreamFormat RawPcmDecoder::getFormat() const
{
    StreamFormat f;
    f.kind = layout.isFloat ? SampleKind::PcmFloat : SampleKind::PcmInt;
    f.sampleRate = layout.sampleRate;
    f.bitsPerSample = layout.isFloat ? layout.bytesPerSample * 8 : layout.validBits;
    f.numChannels = layout.numChannels;
    f.codec = layout.codec;
    return f;
}

bool RawPcmDecoder::seek (juce::int64 frame)
{
    frame = juce::jlimit<juce::int64> (0, totalFrames, frame);
    if (! stream->setPosition (layout.dataStart + frame * frameBytes))
        return false;
    position = frame;
    return true;
}

int RawPcmDecoder::readRaw (int maxFrames)
{
    const auto n = (int) std::min<juce::int64> ({ (juce::int64) maxFrames, (juce::int64) bufferFrames, totalFrames - position });
    if (n <= 0) return 0;
    const int got = stream->read (buffer.get(), n * frameBytes);
    const int frames = juce::jmax (0, got) / frameBytes;
    position += frames;
    return frames;
}

int RawPcmDecoder::readInt (int32_t* dest, int maxFrames)
{
    if (layout.isFloat) return 0;
    int done = 0;
    while (done < maxFrames)
    {
        const int frames = readRaw (maxFrames - done);
        if (frames == 0) break;
        const int samples = frames * layout.numChannels;
        const uint8_t* p = buffer.get();
        int32_t* d = dest + (size_t) done * layout.numChannels;
        const int bps = layout.bytesPerSample;

        for (int i = 0; i < samples; ++i, p += bps)
        {
            uint32_t v = 0;
            switch (bps)
            {
                case 1: v = layout.unsigned8 ? (uint32_t) (uint8_t) (p[0] ^ 0x80) << 24 : (uint32_t) p[0] << 24; break;
                case 2: v = (uint32_t) (layout.bigEndian ? bytes::be16 (p) : bytes::le16 (p)) << 16; break;
                case 3: v = layout.bigEndian ? ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8)
                                             : ((uint32_t) p[2] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[0] << 8); break;
                case 4: v = layout.bigEndian ? bytes::be32 (p) : bytes::le32 (p); break;
                default: break;
            }
            // 24 valid bits in a 32-bit container are left-justified already (WAVE_FORMAT_EXTENSIBLE);
            // anything below the valid bits is padding and must be zero for the stream to be bit-exact.
            if (layout.validBits < bps * 8)
                v &= ~((1u << (32 - layout.validBits)) - 1u);
            d[i] = (int32_t) v;
        }
        done += frames;
    }
    return done;
}

int RawPcmDecoder::readDouble (double* dest, int maxFrames)
{
    if (! layout.isFloat) return 0;
    int done = 0;
    while (done < maxFrames)
    {
        const int frames = readRaw (maxFrames - done);
        if (frames == 0) break;
        const int samples = frames * layout.numChannels;
        const uint8_t* p = buffer.get();
        double* d = dest + (size_t) done * layout.numChannels;

        if (layout.bytesPerSample == 4)
        {
            for (int i = 0; i < samples; ++i, p += 4)
            {
                const uint32_t bits = layout.bigEndian ? bytes::be32 (p) : bytes::le32 (p);
                float f; std::memcpy (&f, &bits, 4);
                d[i] = (double) f;
            }
        }
        else
        {
            for (int i = 0; i < samples; ++i, p += 8)
            {
                const uint64_t bits = layout.bigEndian ? bytes::be64 (p) : bytes::le64 (p);
                double x; std::memcpy (&x, &bits, 8);
                d[i] = x;
            }
        }
        done += frames;
    }
    return done;
}

//==============================================================================
std::unique_ptr<AudioDecoder> RawPcmDecoder::openWav (std::unique_ptr<juce::InputStream>& s, juce::String& error)
{
    uint8_t h[12];
    s->setPosition (0);
    if (! bytes::readExactly (*s, h, 12)) { error = "fichier trop court"; return {}; }

    const bool rf64 = std::memcmp (h, "RF64", 4) == 0 || std::memcmp (h, "BW64", 4) == 0;
    if (! (std::memcmp (h, "RIFF", 4) == 0 || rf64) || std::memcmp (h + 8, "WAVE", 4) != 0)
        return {};

    RawPcmLayout l;
    l.codec = rf64 ? "RF64" : "WAV";
    juce::uint64 ds64DataSize = 0;
    bool haveFmt = false;
    int formatTag = 1;
    const auto streamLength = s->getTotalLength();

    juce::int64 pos = 12;
    while (true)
    {
        uint8_t ch[8];
        s->setPosition (pos);
        if (! bytes::readExactly (*s, ch, 8)) break;
        juce::uint64 size = bytes::le32 (ch + 4);
        const juce::int64 dataStart = pos + 8;

        if (std::memcmp (ch, "ds64", 4) == 0)
        {
            uint8_t d[24];
            if (! bytes::readExactly (*s, d, 24)) { error = "chunk ds64 tronqué"_t; return {}; }
            ds64DataSize = bytes::le64 (d + 8);
        }
        else if (std::memcmp (ch, "fmt ", 4) == 0)
        {
            uint8_t f[40] {};
            const int toRead = (int) std::min<juce::uint64> (size, 40);
            if (! bytes::readExactly (*s, f, toRead) || toRead < 16) { error = "chunk fmt invalide"; return {}; }
            formatTag = bytes::le16 (f);
            l.numChannels = bytes::le16 (f + 2);
            l.sampleRate = (double) bytes::le32 (f + 4);
            const int blockAlign = bytes::le16 (f + 12);
            const int bits = bytes::le16 (f + 14);
            l.bytesPerSample = l.numChannels > 0 ? blockAlign / l.numChannels : 0;
            l.validBits = bits;
            if (formatTag == 0xFFFE && toRead >= 40)
            {
                const int valid = bytes::le16 (f + 18);
                if (valid > 0 && valid <= bits) l.validBits = valid;
                formatTag = bytes::le16 (f + 24); // first two bytes of the SubFormat GUID
            }
            haveFmt = true;
        }
        else if (std::memcmp (ch, "data", 4) == 0)
        {
            if (! haveFmt) { error = "chunk data avant fmt"; return {}; }
            if (rf64 && size == 0xFFFFFFFFu) size = ds64DataSize;
            if (streamLength > 0 && dataStart + (juce::int64) size > streamLength)
                size = (juce::uint64) (streamLength - dataStart);   // file still being written or truncated

            if (formatTag == 3)              l.isFloat = true;
            else if (formatTag != 1)         { error = "WAV compressé non pris en charge (format "_t + juce::String (formatTag) + ")"; return {}; }

            if (l.numChannels <= 0 || l.sampleRate <= 0 || l.bytesPerSample <= 0 || l.bytesPerSample > 8
                || (l.isFloat && l.bytesPerSample != 4 && l.bytesPerSample != 8)
                || (! l.isFloat && l.bytesPerSample > 4))
            {
                error = "format WAV non pris en charge";
                return {};
            }
            l.validBits = juce::jlimit (1, l.bytesPerSample * 8, l.validBits);
            l.unsigned8 = (l.bytesPerSample == 1);
            l.dataStart = dataStart;
            l.dataBytes = (juce::int64) size;
            return std::make_unique<RawPcmDecoder> (std::move (s), l);
        }

        pos = dataStart + (juce::int64) size + (size & 1);
        if (streamLength > 0 && pos >= streamLength) break;
    }

    error = "chunk data introuvable";
    return {};
}

std::unique_ptr<AudioDecoder> RawPcmDecoder::openAiff (std::unique_ptr<juce::InputStream>& s, juce::String& error)
{
    uint8_t h[12];
    s->setPosition (0);
    if (! bytes::readExactly (*s, h, 12)) { error = "fichier trop court"; return {}; }
    if (std::memcmp (h, "FORM", 4) != 0) return {};
    const bool aifc = std::memcmp (h + 8, "AIFC", 4) == 0;
    if (! aifc && std::memcmp (h + 8, "AIFF", 4) != 0) return {};

    RawPcmLayout l;
    l.bigEndian = true;
    l.codec = aifc ? "AIFF-C" : "AIFF";
    bool haveComm = false;
    juce::int64 numFrames = 0;
    const auto streamLength = s->getTotalLength();

    juce::int64 pos = 12;
    while (true)
    {
        uint8_t ch[8];
        s->setPosition (pos);
        if (! bytes::readExactly (*s, ch, 8)) break;
        const juce::uint64 size = bytes::be32 (ch + 4);
        const juce::int64 dataStart = pos + 8;

        if (std::memcmp (ch, "COMM", 4) == 0)
        {
            uint8_t c[22] {};
            const int toRead = (int) std::min<juce::uint64> (size, 22);
            if (! bytes::readExactly (*s, c, toRead) || toRead < 18) { error = "chunk COMM invalide"; return {}; }
            l.numChannels = bytes::be16 (c);
            numFrames = bytes::be32 (c + 2);
            l.validBits = bytes::be16 (c + 6);
            l.sampleRate = bytes::extended80 (c + 8);
            l.bytesPerSample = (l.validBits + 7) / 8;

            if (aifc && toRead >= 22)
            {
                const juce::String type ((const char*) c + 18, 4);
                if (type == "sowt")                          l.bigEndian = false;
                else if (type == "fl32" || type == "FL32")   { l.isFloat = true; l.bytesPerSample = 4; l.validBits = 32; }
                else if (type == "fl64" || type == "FL64")   { l.isFloat = true; l.bytesPerSample = 8; l.validBits = 64; }
                else if (type == "in24")                     { l.bytesPerSample = 3; l.validBits = 24; }
                else if (type == "in32")                     { l.bytesPerSample = 4; l.validBits = 32; }
                else if (type == "42ni")                     { l.bytesPerSample = 4; l.validBits = 32; l.bigEndian = false; }
                else if (type != "NONE" && type != "twos" && type != "raw ")
                {
                    error = "AIFF-C compressé ("_t + type + ") non pris en charge";
                    return {};
                }
                if (! l.isFloat && l.bigEndian == false && type == "sowt") l.codec = "AIFF-C";
            }
            haveComm = true;
        }
        else if (std::memcmp (ch, "SSND", 4) == 0)
        {
            if (! haveComm) { error = "chunk SSND avant COMM"; return {}; }
            uint8_t o[8];
            if (! bytes::readExactly (*s, o, 8)) { error = "chunk SSND tronqué"_t; return {}; }
            const auto offset = bytes::be32 (o);
            l.dataStart = dataStart + 8 + offset;
            const juce::int64 frameBytes = (juce::int64) l.bytesPerSample * l.numChannels;
            juce::int64 available = (juce::int64) size - 8 - offset;
            if (streamLength > 0) available = juce::jmin (available, streamLength - l.dataStart);
            l.dataBytes = juce::jmin (available, numFrames * frameBytes);
            if (l.numChannels <= 0 || l.sampleRate <= 0 || l.bytesPerSample <= 0 || l.bytesPerSample > 8)
            {
                error = "format AIFF non pris en charge";
                return {};
            }
            return std::make_unique<RawPcmDecoder> (std::move (s), l);
        }

        pos = dataStart + (juce::int64) size + (size & 1);
        if (streamLength > 0 && pos >= streamLength) break;
    }

    error = "chunk SSND introuvable";
    return {};
}

} // namespace nx
