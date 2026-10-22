// NX Player — MIT License
#include <algorithm>
#include "DsdDecoders.h"
#include <cstring>

namespace nx
{

//==============================================================================
std::unique_ptr<AudioDecoder> DsfDecoder::open (std::unique_ptr<juce::InputStream>& s, juce::String& error)
{
    uint8_t h[28];
    s->setPosition (0);
    if (! bytes::readExactly (*s, h, 28) || std::memcmp (h, "DSD ", 4) != 0)
        return {};

    std::unique_ptr<DsfDecoder> d (new DsfDecoder());
    d->metadataOffset = (juce::int64) bytes::le64 (h + 20);

    uint8_t f[52];
    s->setPosition ((juce::int64) bytes::le64 (h + 4));
    if (! bytes::readExactly (*s, f, 52) || std::memcmp (f, "fmt ", 4) != 0)
    {
        error = "DSF : chunk fmt manquant";
        return {};
    }
    const auto fmtSize = bytes::le64 (f + 4);
    const auto formatId = bytes::le32 (f + 16);
    const int channels = (int) bytes::le32 (f + 24);
    const auto rate = bytes::le32 (f + 28);
    const auto bits = bytes::le32 (f + 32);
    const auto sampleCount = bytes::le64 (f + 36);
    const auto blockSize = bytes::le32 (f + 44);

    if (formatId != 0 || channels < 1 || channels > 6 || (bits != 1 && bits != 8)
        || rate % 44100 != 0 || rate < 2822400 || blockSize == 0 || blockSize > (1u << 20))
    {
        error = "DSF : format non pris en charge";
        return {};
    }

    uint8_t dh[12];
    const auto dataChunkPos = (juce::int64) bytes::le64 (h + 4) + (juce::int64) fmtSize;
    s->setPosition (dataChunkPos);
    if (! bytes::readExactly (*s, dh, 12) || std::memcmp (dh, "data", 4) != 0)
    {
        error = "DSF : chunk data manquant";
        return {};
    }

    d->format.kind = SampleKind::Dsd;
    d->format.sampleRate = (double) rate;
    d->format.bitsPerSample = 1;
    d->format.numChannels = channels;
    d->format.codec = "DSF";
    d->dataStart = dataChunkPos + 12;
    d->blockSize = (int) blockSize;
    d->lsbFirst = (bits == 1);
    d->lengthBytes = (juce::int64) (sampleCount / 8);

    // Never trust sampleCount beyond what the data chunk holds.
    const auto dataBytes = (juce::int64) bytes::le64 (dh + 4) - 12;
    d->lengthBytes = juce::jmin (d->lengthBytes, (dataBytes / (blockSize * (juce::int64) channels)) * blockSize);

    d->group.allocate ((size_t) (channels * (int) blockSize), true);
    d->stream = std::move (s);
    return d;
}

bool DsfDecoder::loadBlockGroup (juce::int64 groupIndex)
{
    if (groupIndex == loadedGroup) return true;
    const auto groupBytes = (juce::int64) blockSize * format.numChannels;
    if (! stream->setPosition (dataStart + groupIndex * groupBytes)) return false;
    if (stream->read (group.get(), (int) groupBytes) != (int) groupBytes) return false;
    if (lsbFirst)
        for (juce::int64 i = 0; i < groupBytes; ++i)
            group[i] = reverseBits (group[i]);
    loadedGroup = groupIndex;
    return true;
}

bool DsfDecoder::seek (juce::int64 bytePos)
{
    position = juce::jlimit<juce::int64> (0, lengthBytes, bytePos);
    return true;
}

int DsfDecoder::readDsd (uint8_t* dest, int maxBytes)
{
    int done = 0;
    const int ch = format.numChannels;
    while (done < maxBytes && position < lengthBytes)
    {
        const auto groupIndex = position / blockSize;
        if (! loadBlockGroup (groupIndex)) break;
        const int offset = (int) (position % blockSize);
        const int n = (int) std::min<juce::int64> ({ (juce::int64) (maxBytes - done), (juce::int64) (blockSize - offset), lengthBytes - position });
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < ch; ++c)
                dest[(size_t) (done + i) * ch + c] = group[c * blockSize + offset + i];
        done += n;
        position += n;
    }
    return done;
}

//==============================================================================
std::unique_ptr<AudioDecoder> DffDecoder::open (std::unique_ptr<juce::InputStream>& s, juce::String& error)
{
    uint8_t h[16];
    s->setPosition (0);
    if (! bytes::readExactly (*s, h, 16) || std::memcmp (h, "FRM8", 4) != 0 || std::memcmp (h + 12, "DSD ", 4) != 0)
        return {};

    std::unique_ptr<DffDecoder> d (new DffDecoder());
    d->format.kind = SampleKind::Dsd;
    d->format.bitsPerSample = 1;
    d->format.codec = "DFF";
    const auto formEnd = 12 + (juce::int64) bytes::be64 (h + 4);
    bool haveRate = false, haveData = false;

    juce::int64 pos = 16;
    while (pos + 12 <= formEnd)
    {
        uint8_t ch[12];
        s->setPosition (pos);
        if (! bytes::readExactly (*s, ch, 12)) break;
        const auto size = (juce::int64) bytes::be64 (ch + 4);
        const auto body = pos + 12;

        if (std::memcmp (ch, "PROP", 4) == 0)
        {
            uint8_t type[4];
            if (! bytes::readExactly (*s, type, 4) || std::memcmp (type, "SND ", 4) != 0) { error = "DFF : PROP invalide"; return {}; }
            juce::int64 p = body + 4;
            while (p + 12 <= body + size)
            {
                uint8_t sc[12];
                s->setPosition (p);
                if (! bytes::readExactly (*s, sc, 12)) break;
                const auto subSize = (juce::int64) bytes::be64 (sc + 4);
                uint8_t v[6] {};
                if (std::memcmp (sc, "FS  ", 4) == 0 && bytes::readExactly (*s, v, 4))
                {
                    d->format.sampleRate = (double) bytes::be32 (v);
                    haveRate = true;
                }
                else if (std::memcmp (sc, "CHNL", 4) == 0 && bytes::readExactly (*s, v, 2))
                {
                    d->format.numChannels = bytes::be16 (v);
                }
                else if (std::memcmp (sc, "CMPR", 4) == 0 && bytes::readExactly (*s, v, 4))
                {
                    if (std::memcmp (v, "DSD ", 4) != 0)
                    {
                        error = "DFF compressé en DST : non pris en charge (décompressez-le en DSD brut)"_t;
                        return {};
                    }
                }
                p += 12 + subSize + (subSize & 1);
            }
        }
        else if (std::memcmp (ch, "DSD ", 4) == 0)
        {
            d->dataStart = body;
            d->lengthBytes = size;
            haveData = true;
        }
        else if (std::memcmp (ch, "DST ", 4) == 0)
        {
            error = "DFF compressé en DST : non pris en charge"_t;
            return {};
        }
        else if (std::memcmp (ch, "ID3 ", 4) == 0)
        {
            d->id3Offset = body;
            d->id3Size = size;
        }
        pos = body + size + (size & 1);
    }

    const int channels = d->format.numChannels;
    if (! haveRate || ! haveData || channels < 1 || channels > 6
        || std::fmod (d->format.sampleRate, 44100.0) != 0.0 || d->format.sampleRate < 2822400.0)
    {
        error = "DFF : en-tête incomplet ou format non pris en charge"_t;
        return {};
    }
    d->lengthBytes /= channels;
    d->stream = std::move (s);
    d->stream->setPosition (d->dataStart);
    return d;
}

bool DffDecoder::seek (juce::int64 bytePos)
{
    position = juce::jlimit<juce::int64> (0, lengthBytes, bytePos);
    return stream->setPosition (dataStart + position * format.numChannels);
}

int DffDecoder::readDsd (uint8_t* dest, int maxBytes)
{
    const auto n = (int) std::min<juce::int64> (maxBytes, lengthBytes - position);
    if (n <= 0) return 0;
    const int got = stream->read (dest, n * format.numChannels) / format.numChannels;
    position += juce::jmax (0, got);
    return juce::jmax (0, got);
}

} // namespace nx
