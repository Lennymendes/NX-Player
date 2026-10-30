// Synthetic SACD image (Scarlet Book layout, 2048-byte sectors) for the tests: Master TOC (510), master text
// (511), a 2-channel area and a multichannel area with their track lists / track texts, and audio sectors
// built the way a disc stores them (packet + frame headers, supplementary packets, frames across sectors).
#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace sacdfixture
{
constexpr int kSector = 2048, kFrameBytes = 4704;

/** The DSD byte of a track: frame f (from the track start), byte i of the channel, channel c. */
inline uint8_t sample (int area, int track, int f, int i, int c) { return (uint8_t) (f * 131 + i * 7 + c * 29 + track * 53 + area * 97); }

struct TrackSpec { int frames; const char* title; const char* performer; };
struct AreaSpec { int channels; std::vector<TrackSpec> tracks; int frameFormat = 2; };

struct Image
{
    std::vector<uint8_t> data;
    uint8_t* sector (int lsn) { if ((size_t) (lsn + 1) * kSector > data.size()) data.resize ((size_t) (lsn + 1) * kSector, 0); return data.data() + (size_t) lsn * kSector; }
    static void be16 (uint8_t* p, int v) { p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; }
    static void be32 (uint8_t* p, uint32_t v) { p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
    static void time (uint8_t* p, int frames) { p[0] = (uint8_t) (frames / 4500); p[1] = (uint8_t) (frames / 75 % 60); p[2] = (uint8_t) (frames % 75); }
};

/** areaTocLsn: where each area's TOC goes; audio follows. Returns the image bytes. */
inline std::vector<uint8_t> build (const std::vector<AreaSpec>& areas, const char* album, const char* artistLatin1)
{
    Image img;
    img.data.reserve (32u << 20);   // no reallocation: the sector pointers below stay valid
    auto* m = img.sector (510);
    std::memcpy (m, "SACDMTOC", 8);
    m[8] = 1; m[9] = 20;
    std::memcpy (m + 0x18, "NX-TEST-0001    ", 16);
    Image::be16 (m + 0x78, 2004);
    auto* tx = img.sector (511);
    std::memcpy (tx, "SACDText", 8);
    Image::be16 (tx + 0x10, 0x100);
    std::strcpy ((char*) tx + 0x100, album);
    Image::be16 (tx + 0x12, 0x200);
    std::strcpy ((char*) tx + 0x200, artistLatin1);

    int next = 540;
    for (size_t ai = 0; ai < areas.size(); ++ai)
    {
        const auto& a = areas[ai];
        const int toc = next;
        Image::be32 (m + (ai == 0 ? 0x40 : 0x48), (uint32_t) toc);
        auto* h = img.sector (toc);
        std::memcpy (h, a.channels == 2 ? "TWOCHTOC" : "MULCHTOC", 8);
        Image::be16 (h + 0x0A, 4);
        h[0x14] = 4;
        h[0x15] = (uint8_t) a.frameFormat;
        h[0x20] = (uint8_t) a.channels;
        h[0x45] = (uint8_t) a.tracks.size();
        Image::be16 (h + 0x80, 3);
        std::memcpy (h + 0x100, "Zone de test", 13);
        Image::be16 (h + 0x90, 0x100);
        std::memcpy (img.sector (toc + 1), "SACDTRL1", 8);
        std::memcpy (img.sector (toc + 2), "SACDTRL2", 8);
        auto* tt = img.sector (toc + 3);
        std::memcpy (tt, "SACDTTxt", 8);
        int textPos = 0x100;
        for (size_t ti = 0; ti < a.tracks.size(); ++ti)
        {
            Image::be16 (tt + 8 + 2 * ti, textPos);
            tt = img.sector (toc + 3);
            tt[textPos] = 2;
            int p = textPos + 4;
            for (int item = 0; item < 2; ++item)
            {
                const char* s = item == 0 ? a.tracks[ti].title : a.tracks[ti].performer;
                tt[p] = (uint8_t) (item + 1); tt[p + 1] = 0x20;
                std::strcpy ((char*) tt + p + 2, s);
                p += 2 + (int) std::strlen (s) + 1;
                while (p % 4) tt[p++] = 0;
            }
            textPos = p + 4;
        }

        // audio: the frames of all tracks, packed into sectors (packet info 2 bytes, frame info 3 bytes)
        int lsn = toc + 8;
        int absFrame = 150;                                          // the area starts at 00:02:00
        struct Pending { std::vector<uint8_t> bytes; size_t used = 0; int tc = 0; };
        std::vector<Pending> frames;
        std::vector<int> trackFirstFrame;
        for (size_t ti = 0; ti < a.tracks.size(); ++ti)
        {
            trackFirstFrame.push_back ((int) frames.size());
            for (int f = 0; f < a.tracks[ti].frames; ++f)
            {
                Pending pf;
                pf.tc = absFrame++;
                pf.bytes.resize ((size_t) kFrameBytes * (size_t) a.channels);
                for (int i = 0; i < kFrameBytes; ++i)
                    for (int c = 0; c < a.channels; ++c)
                        pf.bytes[(size_t) i * (size_t) a.channels + (size_t) c] = sample ((int) ai, (int) ti + 1, f, i, c);
                frames.push_back (std::move (pf));
            }
        }
        std::vector<int> frameSector (frames.size(), 0), frameEndSector (frames.size(), 0);
        size_t cur = 0;
        int sectorNo = 0;
        while (cur < frames.size())
        {
            struct Packet { bool start; int type; size_t len; const uint8_t* src; };
            std::vector<Packet> packets;
            std::vector<int> frameTcs;
            int budget = kSector - 1;
            static const uint8_t supplementary[24] = { 0xEE };
            if (sectorNo % 5 == 0) { packets.push_back ({ false, 3, sizeof (supplementary), supplementary }); budget -= 2 + (int) sizeof (supplementary); }
            while (cur < frames.size() && packets.size() < 7)
            {
                auto& f = frames[cur];
                const bool start = f.used == 0;
                const int overhead = 2 + (start ? 3 : 0);
                if (start && frameTcs.size() >= 7) break;
                if (budget <= overhead + 16) break;
                const size_t len = std::min ({ f.bytes.size() - f.used, (size_t) (budget - overhead), (size_t) 2047 });
                packets.push_back ({ start, 2, len, f.bytes.data() + f.used });
                if (start) { frameTcs.push_back (f.tc); frameSector[cur] = lsn; }
                f.used += len;
                budget -= overhead + (int) len;
                if (f.used == f.bytes.size()) { frameEndSector[cur] = lsn; ++cur; }
            }
            auto* s = img.sector (lsn);
            s[0] = (uint8_t) ((packets.size() << 5) | (frameTcs.size() << 2));
            size_t pos = 1;
            for (auto& p : packets)
            {
                s[pos] = (uint8_t) ((p.start ? 0x80 : 0) | (p.type << 3) | ((p.len >> 8) & 7));
                s[pos + 1] = (uint8_t) p.len;
                pos += 2;
            }
            for (int tc : frameTcs) { Image::time (s + pos, tc); pos += 3; }
            for (auto& p : packets) { std::memcpy (s + pos, p.src, p.len); pos += p.len; }
            ++lsn;
            ++sectorNo;
        }
        for (size_t ti = 0; ti < a.tracks.size(); ++ti)
        {
            const int first = trackFirstFrame[ti], last = first + a.tracks[ti].frames - 1;
            auto* l1 = img.sector (toc + 1);
            Image::be32 (l1 + 8 + 4 * ti, (uint32_t) frameSector[(size_t) first]);
            Image::be32 (l1 + 0x404 + 4 * ti, (uint32_t) (frameEndSector[(size_t) last] - frameSector[(size_t) first] + 1));
            auto* l2 = img.sector (toc + 2);
            Image::time (l2 + 8 + 4 * ti, frames[(size_t) first].tc);
            Image::time (l2 + 0x404 + 4 * ti, a.tracks[ti].frames);
        }
        next = lsn + 16;
    }
    img.sector (next + 4);   // a little slack at the end of the image
    return img.data;
}

/** The expected interleaved DSD of a track (from frame `from`, all channels). */
inline std::vector<uint8_t> expected (int area, int track, int channels, int frames)
{
    std::vector<uint8_t> out;
    out.reserve ((size_t) frames * kFrameBytes * (size_t) channels);
    for (int f = 0; f < frames; ++f)
        for (int i = 0; i < kFrameBytes; ++i)
            for (int c = 0; c < channels; ++c)
                out.push_back (sample (area, track, f, i, c));
    return out;
}
}
