// NX Player — MIT License
#include "TagReader.h"
#include "../Audio/AudioDecoder.h"
#include <cstring>

namespace nx
{

namespace
{
    juce::String latin1 (const uint8_t* p, size_t n)
    {
        juce::String s;
        s.preallocateBytes (n * 2);
        for (size_t i = 0; i < n && p[i] != 0; ++i) s << juce::String::charToString ((juce::juce_wchar) p[i]);
        return s;
    }

    juce::String utf16 (const uint8_t* p, size_t n, bool bigEndian)
    {
        if (n >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF)))
        {
            bigEndian = p[0] == 0xFE;
            p += 2; n -= 2;
        }
        std::vector<juce::CharPointer_UTF16::CharType> u;
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            const auto c = (juce::CharPointer_UTF16::CharType) (bigEndian ? (p[i] << 8) | p[i + 1] : p[i] | (p[i + 1] << 8));
            if (c == 0) break;
            u.push_back (c);
        }
        u.push_back (0);
        return juce::String (juce::CharPointer_UTF16 (u.data()));
    }

    /** Decodes an ID3 text payload; `consumed` returns the bytes used including the terminator. */
    juce::String id3Text (int encoding, const uint8_t* p, size_t n, size_t* consumed = nullptr)
    {
        size_t end = 0;
        if (encoding == 1 || encoding == 2)
        {
            while (end + 1 < n && ! (p[end] == 0 && p[end + 1] == 0)) end += 2;
            if (consumed) *consumed = juce::jmin (n, end + 2);
            return utf16 (p, end, encoding == 2);
        }
        while (end < n && p[end] != 0) ++end;
        if (consumed) *consumed = juce::jmin (n, end + 1);
        return encoding == 3 ? juce::String::fromUTF8 ((const char*) p, (int) end) : latin1 (p, end);
    }

    uint32_t syncsafe (const uint8_t* p) { return ((uint32_t) (p[0] & 0x7f) << 21) | ((uint32_t) (p[1] & 0x7f) << 14) | ((uint32_t) (p[2] & 0x7f) << 7) | (p[3] & 0x7f); }

    void mapId3 (const juce::String& id, const juce::String& value, TagData& t)
    {
        static const std::pair<const char*, const char*> map[] = {
            { "TIT2", "title" }, { "TPE1", "artist" }, { "TPE2", "albumartist" }, { "TALB", "album" }, { "TCON", "genre" },
            { "TCOM", "composer" }, { "TDRC", "year" }, { "TYER", "year" }, { "TRCK", "tracknumber" }, { "TPOS", "discnumber" },
            { "TSRC", "isrc" }, { "TPUB", "label" }, { "TPE3", "conductor" }, { "TEXT", "lyricist" }, { "TCOP", "copyright" },
            { "TT2", "title" }, { "TP1", "artist" }, { "TP2", "albumartist" }, { "TAL", "album" }, { "TCO", "genre" },
            { "TYE", "year" }, { "TRK", "tracknumber" }, { "TPA", "discnumber" }, { "TCM", "composer" },
        };
        for (auto& [k, v] : map)
            if (id == k)
            {
                auto val = value;
                if (id == "TCON" || id == "TCO")   // "(17)Rock" / "(17)" numeric genres: keep the text part
                    val = val.startsWithChar ('(') && val.containsChar (')') ? val.fromFirstOccurrenceOf (")", false, false) : val;
                if (juce::String (v) == "year") val = val.substring (0, 4);
                t.set (v, val);
                return;
            }
    }
}

void parseId3v2 (const uint8_t* data, size_t size, TagData& t)
{
    if (size < 10 || std::memcmp (data, "ID3", 3) != 0) return;
    const int version = data[3];
    const uint8_t flags = data[5];
    size_t tagSize = std::min<size_t> (syncsafe (data + 6) + 10, size);

    std::vector<uint8_t> buf (data, data + tagSize);
    if ((flags & 0x80) && version < 4)       // whole-tag unsynchronisation (v2.3): FF 00 -> FF
    {
        std::vector<uint8_t> clean;
        clean.reserve (buf.size());
        for (size_t i = 0; i < buf.size(); ++i)
        {
            clean.push_back (buf[i]);
            if (buf[i] == 0xFF && i + 1 < buf.size() && buf[i + 1] == 0x00) ++i;
        }
        buf.swap (clean);
        tagSize = buf.size();
    }

    size_t pos = 10;
    if ((flags & 0x40) && version >= 3 && pos + 4 <= tagSize)   // extended header
        pos += version == 4 ? syncsafe (&buf[pos]) : bytes::be32 (&buf[pos]) + 4;

    const size_t idLen = version == 2 ? 3 : 4, headerLen = version == 2 ? 6 : 10;
    while (pos + headerLen <= tagSize)
    {
        const uint8_t* h = &buf[pos];
        if (h[0] == 0) break;
        const juce::String id ((const char*) h, idLen);
        size_t frameSize = version == 2 ? (size_t) ((h[3] << 16) | (h[4] << 8) | h[5])
                         : version == 4 ? syncsafe (h + 4) : bytes::be32 (h + 4);
        const size_t body = pos + headerLen;
        if (frameSize == 0 || body + frameSize > tagSize) break;
        const uint8_t* p = &buf[body];
        const int enc = p[0];

        if (id == "TXXX" || id == "TXX")
        {
            size_t used = 0;
            const auto desc = id3Text (enc, p + 1, frameSize - 1, &used).toLowerCase();
            const auto value = id3Text (enc, p + 1 + used, frameSize - 1 - used);
            if (desc == "albumartist" || desc == "album artist") t.set ("albumartist", value);
            else if (desc.isNotEmpty()) t.set (desc, value);
        }
        else if (id == "TIPL" || id == "TMCL" || id == "IPLS")
        {
            // pairs role \0 name \0 ...
            size_t off = 1;
            while (off < frameSize)
            {
                size_t a = 0, b = 0;
                const auto role = id3Text (enc, p + off, frameSize - off, &a);
                if (a == 0) break;
                const auto name = id3Text (enc, p + off + a, frameSize - off - a, &b);
                if (role.isNotEmpty() && name.isNotEmpty()) t.credits.add (role + " : " + name);
                off += a + b;
                if (b == 0) break;
            }
        }
        else if (id.startsWithChar ('T'))
        {
            mapId3 (id, id3Text (enc, p + 1, frameSize - 1), t);
        }
        else if ((id == "USLT" || id == "ULT") && frameSize > 4)
        {
            size_t used = 0;
            id3Text (enc, p + 4, frameSize - 4, &used);
            t.set ("lyrics", id3Text (enc, p + 4 + used, frameSize - 4 - used));
        }
        else if ((id == "APIC" || id == "PIC") && t.picture.getSize() == 0 && frameSize > 4)
        {
            size_t off = 1;
            juce::String mime;
            if (id == "PIC") { mime = juce::String ((const char*) p + 1, 3).toLowerCase() == "png" ? "image/png" : "image/jpeg"; off += 3; }
            else { size_t used = 0; mime = id3Text (0, p + off, frameSize - off, &used); off += used; }
            off += 1;                                  // picture type
            size_t used = 0;
            id3Text (enc, p + off, frameSize - off, &used);
            off += used;
            if (off < frameSize) { t.picture.replaceAll (p + off, frameSize - off); t.pictureMime = mime; }
        }
        pos = body + frameSize;
    }
}

void parseVorbisComment (const uint8_t* p, size_t n, TagData& t)
{
    if (n < 8) return;
    size_t pos = 4 + bytes::le32 (p);           // vendor string
    if (pos + 4 > n) return;
    const uint32_t count = bytes::le32 (p + pos);
    pos += 4;
    for (uint32_t i = 0; i < count && pos + 4 <= n; ++i)
    {
        const uint32_t len = bytes::le32 (p + pos);
        pos += 4;
        if (pos + len > n) break;
        const auto entry = juce::String::fromUTF8 ((const char*) p + pos, (int) len);
        pos += len;
        const auto key = entry.upToFirstOccurrenceOf ("=", false, false).toLowerCase();
        const auto value = entry.fromFirstOccurrenceOf ("=", false, false);
        if (key == "date")                                   t.set ("year", value.substring (0, 4));
        else if (key == "album artist" || key == "albumartist") t.set ("albumartist", value);
        else if (key == "unsyncedlyrics" || key == "lyrics")  t.set ("lyrics", value);
        else if (key == "totaltracks")                       t.set ("tracktotal", value);
        else if (key == "performer" || key == "conductor" || key == "producer" || key == "engineer" || key == "mixer"
                 || key == "arranger" || key == "lyricist")
        {
            t.credits.add (key.substring (0, 1).toUpperCase() + key.substring (1) + " : " + value);
            t.set (key, value);
        }
        else t.set (key, value);
    }
}

namespace
{
    bool readAt (juce::InputStream& in, juce::int64 pos, void* dest, int n)
    {
        return in.setPosition (pos) && in.read (dest, n) == n;
    }

    juce::MemoryBlock readBlock (juce::InputStream& in, juce::int64 pos, juce::int64 n, juce::int64 limit = 32 << 20)
    {
        juce::MemoryBlock mb;
        if (n <= 0 || n > limit) return mb;
        mb.setSize ((size_t) n);
        if (! readAt (in, pos, mb.getData(), (int) n)) mb.reset();
        return mb;
    }

    void readFlac (juce::InputStream& in, juce::int64 start, TagData& t)
    {
        juce::int64 pos = start + 4;
        for (int guard = 0; guard < 128; ++guard)
        {
            uint8_t h[4];
            if (! readAt (in, pos, h, 4)) return;
            const bool last = (h[0] & 0x80) != 0;
            const int type = h[0] & 0x7f;
            const juce::int64 len = (h[1] << 16) | (h[2] << 8) | h[3];
            if (type == 4)
            {
                auto mb = readBlock (in, pos + 4, len);
                parseVorbisComment ((const uint8_t*) mb.getData(), mb.getSize(), t);
            }
            else if (type == 6 && t.picture.getSize() == 0)
            {
                auto mb = readBlock (in, pos + 4, len);
                const auto* p = (const uint8_t*) mb.getData();
                const size_t n = mb.getSize();
                if (n >= 32)
                {
                    size_t o = 4;
                    const auto mimeLen = bytes::be32 (p + o); o += 4;
                    const auto mime = juce::String ((const char*) p + o, (size_t) std::min<uint32_t> (mimeLen, (uint32_t) (n - o))); o += mimeLen;
                    if (o + 4 <= n) { const auto dl = bytes::be32 (p + o); o += 4 + dl; }
                    o += 16;
                    if (o + 4 <= n)
                    {
                        const auto dataLen = bytes::be32 (p + o); o += 4;
                        if (o + dataLen <= n) { t.picture.replaceAll (p + o, dataLen); t.pictureMime = mime; }
                    }
                }
            }
            pos += 4 + len;
            if (last) return;
        }
    }

    void readRiffChunks (juce::InputStream& in, TagData& t)
    {
        juce::int64 pos = 12;
        const auto total = in.getTotalLength();
        while (pos + 8 <= total)
        {
            uint8_t h[8];
            if (! readAt (in, pos, h, 8)) return;
            const juce::int64 size = bytes::le32 (h + 4);
            if (std::memcmp (h, "LIST", 4) == 0 && size >= 4)
            {
                auto mb = readBlock (in, pos + 8, size);
                const auto* p = (const uint8_t*) mb.getData();
                if (mb.getSize() >= 4 && std::memcmp (p, "INFO", 4) == 0)
                {
                    size_t o = 4;
                    while (o + 8 <= mb.getSize())
                    {
                        const juce::String id ((const char*) p + o, 4);
                        const auto len = bytes::le32 (p + o + 4);
                        if (o + 8 + len > mb.getSize()) break;
                        const auto v = juce::String::fromUTF8 ((const char*) p + o + 8, (int) len).trimCharactersAtEnd (juce::String::charToString (0));
                        if (id == "INAM") t.set ("title", v);
                        else if (id == "IART") t.set ("artist", v);
                        else if (id == "IPRD") t.set ("album", v);
                        else if (id == "ICRD") t.set ("year", v.substring (0, 4));
                        else if (id == "IGNR") t.set ("genre", v);
                        else if (id == "ITRK" || id == "IPRT") t.set ("tracknumber", v);
                        else if (id == "ICMT") t.set ("comment", v);
                        o += 8 + len + (len & 1);
                    }
                }
            }
            else if (std::memcmp (h, "id3 ", 4) == 0 || std::memcmp (h, "ID3 ", 4) == 0)
            {
                auto mb = readBlock (in, pos + 8, size);
                parseId3v2 ((const uint8_t*) mb.getData(), mb.getSize(), t);
            }
            if (std::memcmp (h, "data", 4) == 0 && size == 0xFFFFFFFF) return;   // RF64: data runs to the end
            pos += 8 + size + (size & 1);
        }
    }

    void readAiffChunks (juce::InputStream& in, TagData& t)
    {
        juce::int64 pos = 12;
        const auto total = in.getTotalLength();
        while (pos + 8 <= total)
        {
            uint8_t h[8];
            if (! readAt (in, pos, h, 8)) return;
            const juce::int64 size = bytes::be32 (h + 4);
            if (std::memcmp (h, "ID3 ", 4) == 0)
            {
                auto mb = readBlock (in, pos + 8, size);
                parseId3v2 ((const uint8_t*) mb.getData(), mb.getSize(), t);
            }
            else if (std::memcmp (h, "NAME", 4) == 0)
            {
                auto mb = readBlock (in, pos + 8, size, 4096);
                t.set ("title", juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()));
            }
            pos += 8 + size + (size & 1);
        }
    }

    void readMp4 (juce::InputStream& in, juce::int64 start, juce::int64 end, int depth, TagData& t)
    {
        juce::int64 pos = start;
        while (pos + 8 <= end && depth < 8)
        {
            uint8_t h[8];
            if (! readAt (in, pos, h, 8)) return;
            juce::int64 size = bytes::be32 (h);
            const juce::String type ((const char*) h + 4, 4);
            juce::int64 body = pos + 8;
            if (size == 1) { uint8_t l[8]; if (! readAt (in, pos + 8, l, 8)) return; size = (juce::int64) bytes::be64 (l); body += 8; }
            if (size < 8 || pos + size > end) return;

            if (type == "moov" || type == "udta" || type == "ilst")
                readMp4 (in, body, pos + size, depth + 1, t);
            else if (type == "meta")
                readMp4 (in, body + 4, pos + size, depth + 1, t);
            else if (depth >= 2)
            {
                // ilst item: children 'data' (and 'mean' / 'name' for freeform '----')
                juce::String freeName;
                juce::int64 c = body;
                while (c + 16 <= pos + size)
                {
                    uint8_t ch[8];
                    if (! readAt (in, c, ch, 8)) break;
                    const juce::int64 cs = bytes::be32 (ch);
                    if (cs < 8) break;
                    const juce::String ct ((const char*) ch + 4, 4);
                    if (ct == "name") { auto mb = readBlock (in, c + 12, cs - 12, 1024); freeName = juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()).toLowerCase(); }
                    if (ct == "data")
                    {
                        auto mb = readBlock (in, c + 16, cs - 16);
                        const auto* p = (const uint8_t*) mb.getData();
                        const auto n = mb.getSize();
                        const auto text = juce::String::fromUTF8 ((const char*) p, (int) n);
                        const auto key = juce::String::fromUTF8 (type.toRawUTF8());
                        if (type == juce::String::fromUTF8 ("\xC2\xA9nam")) t.set ("title", text);
                        else if (type == juce::String::fromUTF8 ("\xC2\xA9" "ART")) t.set ("artist", text);
                        else if (type == "aART") t.set ("albumartist", text);
                        else if (type == juce::String::fromUTF8 ("\xC2\xA9" "alb")) t.set ("album", text);
                        else if (type == juce::String::fromUTF8 ("\xC2\xA9" "day")) t.set ("year", text.substring (0, 4));
                        else if (type == juce::String::fromUTF8 ("\xC2\xA9" "gen")) t.set ("genre", text);
                        else if (type == juce::String::fromUTF8 ("\xC2\xA9" "wrt")) t.set ("composer", text);
                        else if (type == juce::String::fromUTF8 ("\xC2\xA9" "lyr")) t.set ("lyrics", text);
                        else if ((type == "trkn" || type == "disk") && n >= 6)
                        {
                            t.set (type == "trkn" ? "tracknumber" : "discnumber", juce::String (bytes::be16 (p + 2)));
                            if (type == "trkn" && bytes::be16 (p + 4) > 0) t.set ("tracktotal", juce::String (bytes::be16 (p + 4)));
                        }
                        else if (type == "covr" && t.picture.getSize() == 0 && n > 0)
                        {
                            t.picture = mb;
                            t.pictureMime = (n > 4 && p[0] == 0x89 && p[1] == 'P') ? "image/png" : "image/jpeg";
                        }
                        else if (type == "----" && freeName.isNotEmpty()) t.set (freeName, text);
                        juce::ignoreUnused (key);
                    }
                    c += cs;
                }
            }
            pos += size;
        }
    }
}

bool readTags (juce::InputStream& in, const juce::String&, TagData& t)
{
    uint8_t magic[16] {};
    if (! readAt (in, 0, magic, 16)) return true;

    juce::int64 start = 0;
    if (std::memcmp (magic, "ID3", 3) == 0)
    {
        const auto size = (juce::int64) syncsafe (magic + 6) + 10;
        auto mb = readBlock (in, 0, size);
        parseId3v2 ((const uint8_t*) mb.getData(), mb.getSize(), t);
        start = size;
        readAt (in, start, magic, 16);
    }

    if (std::memcmp (magic, "fLaC", 4) == 0) readFlac (in, start, t);
    else if (std::memcmp (magic, "RIFF", 4) == 0 || std::memcmp (magic, "RF64", 4) == 0 || std::memcmp (magic, "BW64", 4) == 0) readRiffChunks (in, t);
    else if (std::memcmp (magic, "FORM", 4) == 0) readAiffChunks (in, t);
    else if (std::memcmp (magic + 4, "ftyp", 4) == 0) readMp4 (in, 0, in.getTotalLength(), 0, t);
    else if (std::memcmp (magic, "DSD ", 4) == 0)
    {
        const auto meta = (juce::int64) bytes::le64 (magic + 20 - 0) ;
        uint8_t hdr[28];
        if (readAt (in, 0, hdr, 28))
        {
            const auto offset = (juce::int64) bytes::le64 (hdr + 20);
            if (offset > 0 && offset < in.getTotalLength())
            {
                auto mb = readBlock (in, offset, in.getTotalLength() - offset);
                parseId3v2 ((const uint8_t*) mb.getData(), mb.getSize(), t);
            }
        }
        juce::ignoreUnused (meta);
    }
    else if (std::memcmp (magic, "FRM8", 4) == 0)
    {
        juce::int64 pos = 16;
        const auto total = in.getTotalLength();
        while (pos + 12 <= total)
        {
            uint8_t h[12];
            if (! readAt (in, pos, h, 12)) break;
            const auto size = (juce::int64) bytes::be64 (h + 4);
            if (std::memcmp (h, "ID3 ", 4) == 0)
            {
                auto mb = readBlock (in, pos + 12, size);
                parseId3v2 ((const uint8_t*) mb.getData(), mb.getSize(), t);
            }
            else if (std::memcmp (h, "DIIN", 4) == 0)
            {
                juce::int64 p = pos + 12;
                while (p + 12 <= pos + 12 + size)
                {
                    uint8_t s[12];
                    if (! readAt (in, p, s, 12)) break;
                    const auto ss = (juce::int64) bytes::be64 (s + 4);
                    if (std::memcmp (s, "DITI", 4) == 0 || std::memcmp (s, "DIAR", 4) == 0)
                    {
                        auto mb = readBlock (in, p + 12, ss, 4096);
                        if (mb.getSize() > 4)
                        {
                            const auto len = bytes::be32 ((const uint8_t*) mb.getData());
                            const auto v = juce::String::fromUTF8 ((const char*) mb.getData() + 4, (int) std::min<size_t> (len, mb.getSize() - 4));
                            t.set (std::memcmp (s, "DITI", 4) == 0 ? "title" : "artist", v);
                        }
                    }
                    p += 12 + ss + (ss & 1);
                }
            }
            pos += 12 + size + (size & 1);
        }
    }
    return true;
}

bool readTags (const juce::File& file, TagData& out)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return false;
    return readTags (in, file.getFileName(), out);
}

} // namespace nx
