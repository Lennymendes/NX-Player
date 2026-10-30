// NX Player — MIT License
#include "SacdIso.h"
#include "../Core/Text.h"
#include <array>
#include <cstring>

namespace nx::sacd
{

namespace
{
    using Sector = std::array<uint8_t, sectorSize>;

    bool readSector (juce::InputStream& in, juce::int64 lsn, uint8_t* dest)
    {
        if (lsn < 0 || ! in.setPosition (lsn * sectorSize)) return false;
        return in.read (dest, sectorSize) == sectorSize;
    }

    /** NUL-terminated text at pos (UTF-8 when valid, else Latin-1: SACD text is ISO 646 / 8859-1 in practice). */
    juce::String textAt (const uint8_t* base, size_t size, size_t pos)
    {
        if (pos == 0 || pos >= size) return {};
        size_t end = pos;
        while (end < size && base[end] != 0) ++end;
        const auto* p = reinterpret_cast<const char*> (base + pos);
        const auto n = (int) (end - pos);
        if (juce::CharPointer_UTF8::isValidString (p, n)) return juce::String::fromUTF8 (p, n).trim();
        juce::String s;
        for (size_t i = pos; i < end; ++i) s += (juce::juce_wchar) base[i];   // Latin-1 = the first 256 code points
        return s.trim();
    }

    juce::int64 timeToFrames (const uint8_t* p) { return (juce::int64) p[0] * 60 * 75 + (juce::int64) p[1] * 75 + p[2]; }

    bool readArea (juce::InputStream& in, juce::uint32 tocStart, int index, Area& area)
    {
        Sector s;
        if (tocStart == 0 || ! readSector (in, tocStart, s.data())) return false;
        const char* expected = index == 0 ? "TWOCHTOC" : "MULCHTOC";
        if (std::memcmp (s.data(), expected, 8) != 0) return false;

        area.index = index;
        const int tocSectors = juce::jlimit (1, 64, (int) bytes::be16 (s.data() + 0x0A));
        area.fsCode = s[0x14];
        area.frameFormat = s[0x15] & 0x0F;
        area.channels = juce::jlimit (1, 6, (int) s[0x20]);
        const int trackCount = juce::jlimit (0, 255, (int) s[0x45]);
        const int textSector = bytes::be16 (s.data() + 0x80);
        area.description = textAt (s.data(), s.size(), bytes::be16 (s.data() + 0x90));
        area.tracks.resize ((size_t) trackCount);
        for (int i = 0; i < trackCount; ++i) area.tracks[(size_t) i].number = i + 1;

        // track lists (sector addresses and times) and track texts, inside the Area TOC
        for (int k = 1; k < tocSectors; ++k)
        {
            Sector t;
            if (! readSector (in, (juce::int64) tocStart + k, t.data())) break;
            if (std::memcmp (t.data(), "SACDTRL1", 8) == 0)
                for (int i = 0; i < trackCount; ++i)
                {
                    area.tracks[(size_t) i].startSector = bytes::be32 (t.data() + 8 + 4 * i);
                    area.tracks[(size_t) i].sectorCount = bytes::be32 (t.data() + 0x404 + 4 * i);
                }
            else if (std::memcmp (t.data(), "SACDTRL2", 8) == 0)
                for (int i = 0; i < trackCount; ++i)
                {
                    area.tracks[(size_t) i].startFrame = timeToFrames (t.data() + 8 + 4 * i);
                    area.tracks[(size_t) i].frameCount = timeToFrames (t.data() + 0x404 + 4 * i);
                }
        }
        if (textSector > 0 && textSector < tocSectors)
        {
            // SACDTTxt: per-track u16 pointers (relative to the block), then items {type, 0x20, text\0}
            const int blockSectors = tocSectors - textSector;
            juce::HeapBlock<uint8_t> block ((size_t) blockSectors * sectorSize, true);
            int got = 0;
            while (got < blockSectors && readSector (in, (juce::int64) tocStart + textSector + got, block + (size_t) got * sectorSize)) ++got;
            const size_t size = (size_t) got * sectorSize;
            if (got > 0 && std::memcmp (block, "SACDTTxt", 8) == 0)
                for (int i = 0; i < trackCount; ++i)
                {
                    const size_t ptr = bytes::be16 (block + 8 + 2 * i);
                    if (ptr == 0 || ptr + 4 >= size) continue;
                    int items = block[ptr];
                    size_t pos = ptr + 4;
                    while (items-- > 0 && pos + 2 < size)
                    {
                        const int type = block[pos];
                        const auto text = textAt (block, size, pos + 2);
                        if (type == 0x01) area.tracks[(size_t) i].title = text;
                        if (type == 0x02) area.tracks[(size_t) i].performer = text;
                        pos += 2;
                        while (pos < size && block[pos] != 0) ++pos;          // the text
                        while (pos < size && block[pos] == 0) ++pos;          // its terminator + padding
                    }
                }
        }
        return true;
    }

    //==========================================================================
    /** One track of the image, as raw DSD64 (packet headers removed, data types other than audio dropped). */
    class TrackDecoder final : public AudioDecoder
    {
    public:
        TrackDecoder (std::unique_ptr<juce::InputStream> s, const Area& a, const Track& t)
            : in (std::move (s)), channels (a.channels), track (t)
        {
            format.kind = SampleKind::Dsd;
            format.sampleRate = 2822400.0;
            format.bitsPerSample = 1;
            format.numChannels = channels;
            format.codec = "SACD";
            firstSector = t.startSector;
            endSector = t.startSector + juce::jmax<juce::uint32> (1, t.sectorCount);
            lengthBytes = t.frameCount > 0 ? t.frameCount * bytesPerFrame
                                           : (juce::int64) t.sectorCount * (sectorSize - 32) / channels;   // no time list: estimate
            totalFrames = juce::jmax<juce::int64> (1, lengthBytes / bytesPerFrame);
        }

        StreamFormat getFormat() const override  { return format; }
        juce::int64 getLength() const override    { return lengthBytes; }
        juce::int64 getPosition() const override  { return position; }

        bool seek (juce::int64 p) override
        {
            p = juce::jlimit<juce::int64> (0, lengthBytes, p);
            const juce::int64 target = p / bytesPerFrame;
            const juce::int64 span = (juce::int64) endSector - firstSector;
            const juce::int64 estimate = firstSector + target * span / totalFrames;
            // start a little before the estimate; if the first frame found is already past the target, go further back
            for (juce::int64 back = 24; ; back *= 4)
            {
                const auto from = (juce::uint32) juce::jmax<juce::int64> (firstSector, estimate - back);
                restart (from, target);
                int guard = 0;
                while (! started && ! overshoot && guard++ < 2048 && pump()) {}
                if (started)
                {
                    skip = (size_t) ((p - startedFrame * bytesPerFrame) * channels);
                    position = p;
                    return true;
                }
                if (from == firstSector) break;
            }
            restart (firstSector, 0);                 // no frame start at all: from the beginning
            position = 0;
            return p == 0;
        }

        int readDsd (uint8_t* dest, int maxBytes) override
        {
            if (! primed) { primed = true; seek (0); }
            const juce::int64 left = lengthBytes - position;
            const size_t want = (size_t) juce::jmin<juce::int64> (maxBytes, left) * (size_t) channels;
            size_t done = 0;
            while (done < want)
            {
                if (skip > 0)
                {
                    const auto drop = juce::jmin (skip, pending.size() - readPos);
                    readPos += drop;
                    skip -= drop;
                }
                const size_t avail = pending.size() - readPos;
                if (avail > 0 && skip == 0)
                {
                    const auto n = juce::jmin (avail, want - done);
                    std::memcpy (dest + done, pending.data() + readPos, n);
                    readPos += n;
                    done += n;
                    continue;
                }
                if (readPos > 0) { pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) readPos); readPos = 0; }
                if (! pump()) break;
            }
            const int frames = (int) (done / (size_t) channels);
            position += frames;
            return frames;
        }

    private:
        void restart (juce::uint32 sector, juce::int64 targetFrame)
        {
            next = sector;
            target = targetFrame;
            started = overshoot = false;
            startedFrame = 0;
            pending.clear();
            readPos = skip = 0;
            primed = true;
        }

        /** Demultiplexes one sector into `pending`. False at the end of the track (or past a read error). */
        bool pump()
        {
            if (next >= endSector + 64) return false;     // a frame may end a few sectors after the listed area
            uint8_t s[sectorSize];
            if (! readSector (*in, next++, s)) return false;
            const bool dst = (s[0] & 1) != 0;
            if (dst) { lastError = "SACD : secteur DST"; return false; }
            const int packets = (s[0] >> 5) & 7, frames = (s[0] >> 2) & 7;
            const int frameInfoSize = 3;
            size_t pos = 1 + (size_t) packets * 2 + (size_t) frames * (size_t) frameInfoSize;
            int frameIndex = 0;
            for (int i = 0; i < packets; ++i)
            {
                const uint8_t* pi = s + 1 + 2 * i;
                const bool frameStart = (pi[0] & 0x80) != 0;
                const int type = (pi[0] >> 3) & 7;
                const size_t length = (size_t) (((pi[0] & 7) << 8) | pi[1]);
                if (pos + length > (size_t) sectorSize) return false;   // corrupt sector
                if (type == 2)
                {
                    if (frameStart)
                    {
                        const uint8_t* fi = s + 1 + packets * 2 + frameIndex * frameInfoSize;
                        const bool timed = frameIndex < frames && track.startFrame >= 0;
                        const juce::int64 rel = timed ? timeToFrames (fi) - track.startFrame : 0;   // < 0: end of the previous track
                        ++frameIndex;
                        if (! started)
                        {
                            if (! timed)                 { started = true; startedFrame = target; }   // no time code: trust the estimate
                            else if (rel == target)      { started = true; startedFrame = rel; }
                            else if (rel > target)       { overshoot = true; return true; }
                        }
                    }
                    if (started) pending.insert (pending.end(), s + pos, s + pos + length);
                }
                pos += length;
            }
            return true;
        }

        std::unique_ptr<juce::InputStream> in;
        StreamFormat format;
        int channels;
        Track track;
        juce::uint32 firstSector = 0, endSector = 0, next = 0;
        juce::int64 lengthBytes = 0, position = 0, totalFrames = 1, target = 0, startedFrame = 0;
        bool started = false, overshoot = false, primed = false;
        std::vector<uint8_t> pending;
        size_t readPos = 0, skip = 0;
    };
}

//==============================================================================
bool readDisc (juce::InputStream& in, Disc& disc, juce::String& error)
{
    Sector m;
    if (! readSector (in, masterTocSector, m.data()) || std::memcmp (m.data(), "SACDMTOC", 8) != 0)
    {
        error = "pas une image SACD (en-tête SACDMTOC absent au secteur 510)"_t;
        return false;
    }
    disc = {};
    disc.catalog = textAt (m.data(), 0x28, 0x18);
    disc.year = bytes::be16 (m.data() + 0x78);
    if (disc.year < 1990 || disc.year > 2200) disc.year = 0;

    Sector t;
    if (readSector (in, masterTextSector, t.data()) && std::memcmp (t.data(), "SACDText", 8) == 0)
    {
        auto at = [&t] (int off) { return textAt (t.data(), t.size(), bytes::be16 (t.data() + off)); };
        disc.albumTitle = at (0x10);
        disc.albumArtist = at (0x12);
        disc.publisher = at (0x14);
        disc.copyright = at (0x16);
        disc.discTitle = at (0x20);
        disc.discArtist = at (0x22);
    }

    for (int index = 0; index < 2; ++index)
    {
        Area a;
        if (readArea (in, bytes::be32 (m.data() + (index == 0 ? 0x40 : 0x48)), index, a)) disc.areas.push_back (std::move (a));
    }
    if (disc.areas.empty()) { error = "image SACD sans zone audio lisible"_t; return false; }
    return true;
}

bool readDisc (const juce::File& iso, Disc& disc, juce::String& error)
{
    juce::FileInputStream in (iso);
    if (! in.openedOk()) { error = "impossible d'ouvrir "_t + iso.getFullPathName(); return false; }
    return readDisc (in, disc, error);
}

juce::String virtualUri (const juce::File& iso, int area, int track)
{
    return iso.getFullPathName() + "#area=" + juce::String (area) + "&track=" + juce::String (track);
}

bool isVirtualUri (const juce::String& uri)
{
    return uri.containsIgnoreCase (".iso#area=");
}

bool parseVirtualUri (const juce::String& uri, juce::File& iso, int& area, int& track)
{
    if (! isVirtualUri (uri)) return false;
    const auto path = uri.upToLastOccurrenceOf ("#area=", false, true);
    const auto query = uri.fromLastOccurrenceOf ("#", false, false);
    juce::StringPairArray p;
    for (auto& kv : juce::StringArray::fromTokens (query, "&", {}))
        p.set (kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false));
    if (! juce::File::isAbsolutePath (path) || p["area"].isEmpty() || p["track"].isEmpty()) return false;
    iso = juce::File (path);
    area = p["area"].getIntValue();
    track = p["track"].getIntValue();
    return area >= 0 && area <= 1 && track >= 1;
}

juce::File realFile (const juce::String& pathOrUri)
{
    juce::File f;
    int a = 0, t = 0;
    if (parseVirtualUri (pathOrUri, f, a, t)) return f;
    return juce::File::isAbsolutePath (pathOrUri) ? juce::File (pathOrUri) : juce::File();
}

std::unique_ptr<AudioDecoder> openTrack (std::unique_ptr<juce::InputStream> in, int areaIndex, int trackNumber, juce::String& error)
{
    if (in == nullptr) { error = "flux introuvable"; return {}; }
    Disc disc;
    if (! readDisc (*in, disc, error)) return {};
    for (auto& a : disc.areas)
    {
        if (a.index != areaIndex) continue;
        if (trackNumber < 1 || trackNumber > (int) a.tracks.size()) break;
        if (a.isDst())
        {
            error = "SACD : zone compressée en DST, décompression non prise en charge (convertissez l'ISO en DSF / DFF non compressé)"_t;
            return {};
        }
        if (a.fsCode != 4)
        {
            error = "SACD : fréquence d'échantillonnage inconnue"_t;
            return {};
        }
        const auto& t = a.tracks[(size_t) trackNumber - 1];
        if (t.startSector == 0) { error = "SACD : liste des pistes absente"_t; return {}; }
        return std::make_unique<TrackDecoder> (std::move (in), a, t);
    }
    error = "SACD : piste introuvable dans l'image"_t;
    return {};
}

std::unique_ptr<AudioDecoder> openTrack (const juce::String& uri, juce::String& error)
{
    juce::File iso;
    int area = 0, track = 0;
    if (! parseVirtualUri (uri, iso, area, track)) { error = "SACD : chemin de piste invalide"_t; return {}; }
    auto f = std::make_unique<juce::FileInputStream> (iso);
    if (! f->openedOk()) { error = "impossible d'ouvrir "_t + iso.getFullPathName(); return {}; }
    return openTrack (std::make_unique<juce::BufferedInputStream> (f.release(), 1 << 17, true), area, track, error);
}

} // namespace nx::sacd
