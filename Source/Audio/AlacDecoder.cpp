// NX Player — MIT License
#include "AlacDecoder.h"
#include "../../ThirdParty/alac/ALACDecoder.h"
#include "../../ThirdParty/alac/ALACBitUtilities.h"
#include <cstring>

namespace nx
{

namespace
{
    struct Box { char type[5] {}; juce::int64 start = 0, body = 0, end = 0; };

    bool readBox (juce::InputStream& in, juce::int64 pos, juce::int64 limit, Box& b)
    {
        uint8_t h[16];
        if (pos + 8 > limit || ! in.setPosition (pos) || in.read (h, 8) != 8) return false;
        juce::uint64 size = bytes::be32 (h);
        std::memcpy (b.type, h + 4, 4);
        b.start = pos;
        b.body = pos + 8;
        if (size == 1)
        {
            if (in.read (h + 8, 8) != 8) return false;
            size = bytes::be64 (h + 8);
            b.body = pos + 16;
        }
        else if (size == 0)
        {
            size = (juce::uint64) (limit - pos);
        }
        b.end = pos + (juce::int64) size;
        return size >= 8 && b.end <= limit;
    }

    bool is (const Box& b, const char* t) { return std::memcmp (b.type, t, 4) == 0; }

    std::vector<uint8_t> readBody (juce::InputStream& in, const Box& b, size_t maxBytes = 64u << 20)
    {
        const auto n = (size_t) std::min<juce::int64> (b.end - b.body, (juce::int64) maxBytes);
        std::vector<uint8_t> v (n);
        in.setPosition (b.body);
        if (n > 0 && in.read (v.data(), (int) n) != (int) n) v.clear();
        return v;
    }

    struct TableData
    {
        std::vector<std::pair<juce::uint32, juce::uint32>> stts;   // count, delta
        struct Stsc { juce::uint32 firstChunk, samplesPerChunk; };
        std::vector<Stsc> stsc;
        std::vector<juce::uint32> sizes;
        juce::uint32 constantSize = 0, sampleCount = 0;
        std::vector<juce::int64> chunkOffsets;
    };

    void parseStbl (juce::InputStream& in, const Box& stbl, Mp4AudioTrack& t, TableData& td)
    {
        Box b;
        for (auto p = stbl.body; readBox (in, p, stbl.end, b); p = b.end)
        {
            if (is (b, "stsd"))
            {
                Box entry;
                if (! readBox (in, b.body + 8, b.end, entry)) continue;
                t.codec = juce::String (entry.type, 4);
                uint8_t e[28];
                in.setPosition (entry.body);
                if (in.read (e, 28) != 28) continue;
                t.channels = bytes::be16 (e + 16);
                t.sampleSize = bytes::be16 (e + 18);
                t.sampleRate = (double) (bytes::be32 (e + 24) >> 16);
                Box child;
                for (auto cp = entry.body + 28; readBox (in, cp, entry.end, child); cp = child.end)
                {
                    if (is (child, "alac") || is (child, "dfLa"))
                    {
                        auto body = readBody (in, child, 1 << 20);
                        if (body.size() > 4) t.codecConfig.assign (body.begin() + 4, body.end()); // skip version / flags
                    }
                }
            }
            else if (is (b, "stts"))
            {
                auto v = readBody (in, b);
                if (v.size() < 8) continue;
                const auto n = bytes::be32 (v.data() + 4);
                for (juce::uint32 i = 0; i < n && 8 + i * 8 + 8 <= v.size(); ++i)
                    td.stts.push_back ({ bytes::be32 (v.data() + 8 + i * 8), bytes::be32 (v.data() + 12 + i * 8) });
            }
            else if (is (b, "stsc"))
            {
                auto v = readBody (in, b);
                if (v.size() < 8) continue;
                const auto n = bytes::be32 (v.data() + 4);
                for (juce::uint32 i = 0; i < n && 8 + i * 12 + 12 <= v.size(); ++i)
                    td.stsc.push_back ({ bytes::be32 (v.data() + 8 + i * 12), bytes::be32 (v.data() + 12 + i * 12) });
            }
            else if (is (b, "stsz"))
            {
                auto v = readBody (in, b);
                if (v.size() < 12) continue;
                td.constantSize = bytes::be32 (v.data() + 4);
                td.sampleCount = bytes::be32 (v.data() + 8);
                if (td.constantSize == 0)
                    for (juce::uint32 i = 0; i < td.sampleCount && 12 + i * 4 + 4 <= v.size(); ++i)
                        td.sizes.push_back (bytes::be32 (v.data() + 12 + i * 4));
            }
            else if (is (b, "stco") || is (b, "co64"))
            {
                auto v = readBody (in, b);
                if (v.size() < 8) continue;
                const auto n = bytes::be32 (v.data() + 4);
                const bool wide = is (b, "co64");
                for (juce::uint32 i = 0; i < n; ++i)
                {
                    const size_t o = 8 + (size_t) i * (wide ? 8 : 4);
                    if (o + (wide ? 8 : 4) > v.size()) break;
                    td.chunkOffsets.push_back (wide ? (juce::int64) bytes::be64 (v.data() + o) : (juce::int64) bytes::be32 (v.data() + o));
                }
            }
        }
    }

    bool findAudioTrack (juce::InputStream& in, const Box& container, Mp4AudioTrack& t, TableData& td, bool& found)
    {
        Box b;
        for (auto p = container.body; ! found && readBox (in, p, container.end, b); p = b.end)
        {
            if (is (b, "trak"))
            {
                Mp4AudioTrack candidate;
                TableData cd;
                bool isSound = false;
                // trak > mdia > (mdhd, hdlr, minf > stbl)
                Box mdia;
                for (auto q = b.body; readBox (in, q, b.end, mdia); q = mdia.end)
                {
                    if (! is (mdia, "mdia")) continue;
                    Box c;
                    for (auto r = mdia.body; readBox (in, r, mdia.end, c); r = c.end)
                    {
                        if (is (c, "mdhd"))
                        {
                            auto v = readBody (in, c, 64);
                            if (v.size() >= 24)
                            {
                                if (v[0] == 1 && v.size() >= 36) { candidate.timescale = bytes::be32 (v.data() + 20); candidate.durationUnits = bytes::be64 (v.data() + 24); }
                                else { candidate.timescale = bytes::be32 (v.data() + 12); candidate.durationUnits = bytes::be32 (v.data() + 16); }
                            }
                        }
                        else if (is (c, "hdlr"))
                        {
                            auto v = readBody (in, c, 64);
                            isSound = v.size() >= 12 && std::memcmp (v.data() + 8, "soun", 4) == 0;
                        }
                        else if (is (c, "minf"))
                        {
                            Box s;
                            for (auto u = c.body; readBox (in, u, c.end, s); u = s.end)
                                if (is (s, "stbl")) parseStbl (in, s, candidate, cd);
                        }
                    }
                }
                if (isSound) { t = std::move (candidate); td = std::move (cd); found = true; }
            }
            else if (is (b, "moov"))
            {
                findAudioTrack (in, b, t, td, found);
            }
        }
        return found;
    }
}

bool parseMp4AudioTrack (juce::InputStream& in, Mp4AudioTrack& t, juce::String& error)
{
    const auto length = in.getTotalLength();
    Box root; root.body = 0; root.end = length;
    Box first;
    if (! readBox (in, 0, length, first) || ! is (first, "ftyp")) return false;

    TableData td;
    bool found = false;
    findAudioTrack (in, root, t, td, found);
    if (! found) { error = "MP4 : aucune piste audio"; return false; }

    // Expand stsc + chunk offsets + sizes into a packet list.
    const juce::uint32 count = td.constantSize != 0 ? td.sampleCount : (juce::uint32) td.sizes.size();
    t.packets.reserve (count);
    juce::uint32 sample = 0;
    for (size_t c = 0; c < td.chunkOffsets.size() && sample < count; ++c)
    {
        juce::uint32 perChunk = 0;
        for (auto& e : td.stsc)
            if (e.firstChunk <= c + 1) perChunk = e.samplesPerChunk;
        auto offset = td.chunkOffsets[c];
        for (juce::uint32 k = 0; k < perChunk && sample < count; ++k, ++sample)
        {
            const auto size = td.constantSize != 0 ? td.constantSize : td.sizes[sample];
            t.packets.push_back ({ offset, size, 0 });
            offset += size;
        }
    }
    size_t p = 0;
    for (auto& [n, delta] : td.stts)
        for (juce::uint32 i = 0; i < n && p < t.packets.size(); ++i)
            t.packets[p++].frames = delta;
    return ! t.packets.empty();
}

//==============================================================================
struct AlacDecoder::Impl { ALACDecoder decoder; };

AlacDecoder::~AlacDecoder() = default;

std::unique_ptr<AudioDecoder> AlacDecoder::open (std::unique_ptr<juce::InputStream>& s, juce::String& error)
{
    Mp4AudioTrack track;
    s->setPosition (0);
    if (! parseMp4AudioTrack (*s, track, error)) return {};
    if (track.codec != "alac")
    {
        error = track.codec == "mp4a" ? "AAC : utilisez le décodeur système"_t : "MP4 : codec " + track.codec + " non pris en charge";
        return {};
    }

    std::unique_ptr<AlacDecoder> d (new AlacDecoder());
    d->impl = std::make_unique<Impl>();
    if (track.codecConfig.size() < 24
        || d->impl->decoder.Init (track.codecConfig.data(), (uint32_t) track.codecConfig.size()) != 0)
    {
        error = "ALAC : configuration invalide";
        return {};
    }

    const auto& cfg = d->impl->decoder.mConfig;
    d->format.kind = SampleKind::PcmInt;
    d->format.codec = "ALAC";
    d->format.numChannels = cfg.numChannels;
    d->format.bitsPerSample = cfg.bitDepth;
    d->format.sampleRate = cfg.sampleRate > 0 ? (double) cfg.sampleRate : track.sampleRate;

    juce::int64 frame = 0;
    for (auto& p : track.packets)
    {
        if (p.frames == 0) p.frames = cfg.frameLength;
        d->packetStartFrame.push_back (frame);
        frame += p.frames;
    }
    d->totalFrames = frame;
    d->track = std::move (track);
    d->pcmBytes.resize ((size_t) cfg.frameLength * cfg.numChannels * 4 + 64);
    d->decoded.resize ((size_t) cfg.frameLength * cfg.numChannels);
    d->stream = std::move (s);
    return d;
}

bool AlacDecoder::decodePacket (size_t index)
{
    if (index >= track.packets.size()) return false;
    const auto& p = track.packets[index];
    packetBytes.resize (p.size + 8);
    if (! stream->setPosition (p.offset) || stream->read (packetBytes.data(), (int) p.size) != (int) p.size)
        return false;

    BitBuffer bits;
    BitBufferInit (&bits, packetBytes.data(), p.size);
    uint32_t out = 0;
    const auto& cfg = impl->decoder.mConfig;
    if (impl->decoder.Decode (&bits, pcmBytes.data(), cfg.frameLength, cfg.numChannels, &out) != 0)
        return false;

    const int ch = cfg.numChannels;
    const size_t n = (size_t) out * ch;
    switch (cfg.bitDepth)
    {
        case 16: { auto* s = reinterpret_cast<const int16_t*> (pcmBytes.data()); for (size_t i = 0; i < n; ++i) decoded[i] = (int32_t) ((uint32_t) (int32_t) s[i] << 16); break; }
        case 20:
        case 24: { const uint8_t* s = pcmBytes.data(); for (size_t i = 0; i < n; ++i, s += 3) decoded[i] = (int32_t) (((uint32_t) s[2] << 24) | ((uint32_t) s[1] << 16) | ((uint32_t) s[0] << 8)); break; }
        case 32: { auto* s = reinterpret_cast<const int32_t*> (pcmBytes.data()); for (size_t i = 0; i < n; ++i) decoded[i] = s[i]; break; }
        default: return false;
    }
    currentPacket = index;
    decodedFrames = (int) out;
    decodedOffset = 0;
    return true;
}

bool AlacDecoder::seek (juce::int64 frame)
{
    frame = juce::jlimit<juce::int64> (0, totalFrames, frame);
    auto it = std::upper_bound (packetStartFrame.begin(), packetStartFrame.end(), frame);
    const size_t index = it == packetStartFrame.begin() ? 0 : (size_t) (it - packetStartFrame.begin() - 1);
    position = frame;
    if (frame >= totalFrames) { currentPacket = track.packets.size(); decodedFrames = decodedOffset = 0; return true; }
    if (! decodePacket (index)) return false;
    decodedOffset = (int) (frame - packetStartFrame[index]);
    return true;
}

int AlacDecoder::readInt (int32_t* dest, int maxFrames)
{
    const int ch = format.numChannels;
    int done = 0;
    while (done < maxFrames && position < totalFrames)
    {
        if (decodedOffset >= decodedFrames)
        {
            const size_t next = currentPacket == (size_t) -1 ? 0 : currentPacket + 1;
            if (! decodePacket (next)) break;
        }
        const int n = juce::jmin (maxFrames - done, decodedFrames - decodedOffset);
        std::memcpy (dest + (size_t) done * ch, decoded.data() + (size_t) decodedOffset * ch, (size_t) n * ch * sizeof (int32_t));
        done += n;
        decodedOffset += n;
        position += n;
    }
    return done;
}

} // namespace nx
