// NX Player — MIT License
// Writers for the self-test: every container is produced byte by byte (independent of the readers),
// plus a 5th-order sigma-delta modulator to make real DSD.
#pragma once

#include <juce_core/juce_core.h>
#include "../ThirdParty/alac/ALACEncoder.h"
#include "../ThirdParty/alac/ALACAudioTypes.h"
#include <cmath>
#include <random>
#include <vector>

namespace nxtest
{

struct ByteWriter
{
    juce::MemoryBlock data;
    void u8 (int v)              { uint8_t b = (uint8_t) v; data.append (&b, 1); }
    void le16 (uint32_t v)       { u8 ((int) (v & 0xff)); u8 ((int) ((v >> 8) & 0xff)); }
    void le24 (uint32_t v)       { le16 (v & 0xffff); u8 ((int) ((v >> 16) & 0xff)); }
    void le32 (uint32_t v)       { le16 (v & 0xffff); le16 (v >> 16); }
    void le64 (uint64_t v)       { le32 ((uint32_t) v); le32 ((uint32_t) (v >> 32)); }
    void be16 (uint32_t v)       { u8 ((int) ((v >> 8) & 0xff)); u8 ((int) (v & 0xff)); }
    void be24 (uint32_t v)       { u8 ((int) ((v >> 16) & 0xff)); be16 (v & 0xffff); }
    void be32 (uint32_t v)       { be16 (v >> 16); be16 (v & 0xffff); }
    void be64 (uint64_t v)       { be32 ((uint32_t) (v >> 32)); be32 ((uint32_t) v); }
    void tag (const char* t)     { data.append (t, 4); }
    void raw (const void* p, size_t n) { data.append (p, n); }
    size_t size() const          { return data.getSize(); }
    void patchLe32 (size_t at, uint32_t v) { auto* p = static_cast<uint8_t*> (data.getData()) + at; p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24); }
    void patchBe32 (size_t at, uint32_t v) { auto* p = static_cast<uint8_t*> (data.getData()) + at; p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
    void patchBe64 (size_t at, uint64_t v) { patchBe32 (at, (uint32_t) (v >> 32)); patchBe32 (at + 4, (uint32_t) v); }
    bool save (const juce::File& f) const { return f.replaceWithData (data.getData(), data.getSize()); }
};

/** Random integer PCM of `bits` significant bits, left-justified in int32, interleaved. */
inline std::vector<int32_t> randomPcm (int frames, int channels, int bits, uint32_t seed)
{
    std::mt19937 rng (seed);
    std::vector<int32_t> v ((size_t) frames * (size_t) channels);
    for (auto& s : v)
    {
        const uint32_t r = rng();
        s = (int32_t) (r & ~((bits >= 32) ? 0u : ((1u << (32 - bits)) - 1u)));
    }
    return v;
}

inline std::vector<int32_t> sinePcm (int frames, int channels, int bits, double rate, double freq, double amp)
{
    std::vector<int32_t> v ((size_t) frames * (size_t) channels);
    const double scale = std::ldexp (1.0, bits - 1);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
        {
            const double x = amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / rate + c * 0.5);
            const auto k = (int64_t) std::llround (x * scale);
            v[(size_t) i * channels + c] = (int32_t) ((uint32_t) k << (32 - bits));
        }
    return v;
}

/** WAV / RF64 writer: integer (8..32 bits, optional 24-in-32 extensible) or float (32 / 64). */
inline bool writeWav (const juce::File& f, const std::vector<int32_t>* ints, const std::vector<double>* floats,
                      int channels, double rate, int containerBits, int validBits, bool isFloat, bool rf64 = false, bool extensible = false)
{
    const size_t frames = (ints ? ints->size() : floats->size()) / (size_t) channels;
    const int bps = containerBits / 8;
    const uint64_t dataBytes = frames * (size_t) channels * (size_t) bps;
    ByteWriter w;
    w.tag (rf64 ? "RF64" : "RIFF");
    w.le32 (rf64 ? 0xFFFFFFFFu : 0);
    w.tag ("WAVE");
    if (rf64)
    {
        w.tag ("ds64"); w.le32 (28);
        w.le64 (0); w.le64 (dataBytes); w.le64 (frames); w.le32 (0);
    }
    w.tag ("fmt ");
    w.le32 (extensible ? 40 : 16);
    w.le16 (extensible ? 0xFFFE : (isFloat ? 3 : 1));
    w.le16 ((uint32_t) channels);
    w.le32 ((uint32_t) rate);
    w.le32 ((uint32_t) (rate * channels * bps));
    w.le16 ((uint32_t) (channels * bps));
    w.le16 ((uint32_t) containerBits);
    if (extensible)
    {
        w.le16 (22); w.le16 ((uint32_t) validBits); w.le32 (3);
        w.le16 (isFloat ? 3 : 1);
        const uint8_t guidTail[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
        w.raw (guidTail, 14);
    }
    w.tag ("LIST"); w.le32 (4); w.tag ("INFO");   // a chunk to skip
    w.tag ("data");
    w.le32 (rf64 ? 0xFFFFFFFFu : (uint32_t) dataBytes);
    for (size_t i = 0; i < frames * (size_t) channels; ++i)
    {
        if (isFloat)
        {
            const double x = (*floats)[i];
            if (bps == 4) { float fl = (float) x; uint32_t b; std::memcpy (&b, &fl, 4); w.le32 (b); }
            else          { uint64_t b; std::memcpy (&b, &x, 8); w.le64 (b); }
            continue;
        }
        const uint32_t v = (uint32_t) (*ints)[i];
        switch (bps)
        {
            case 1: w.u8 ((int) ((v >> 24) ^ 0x80)); break;
            case 2: w.le16 (v >> 16); break;
            case 3: w.le24 (v >> 8); break;
            case 4: w.le32 (v); break;
        }
    }
    if (dataBytes & 1) w.u8 (0);
    if (! rf64) w.patchLe32 (4, (uint32_t) (w.size() - 8));
    return w.save (f);
}

inline bool writeWavInts (const juce::File& f, std::vector<int32_t> pcm, int channels, double rate, int containerBits, int validBits, bool isFloat)
{
    return writeWav (f, &pcm, nullptr, channels, rate, containerBits, validBits, isFloat);
}

inline void writeExtended80 (ByteWriter& w, double v)
{
    int e = 0;
    const double m = std::frexp (v, &e);           // v = m * 2^e, 0.5 <= m < 1
    const uint64_t mant = (uint64_t) std::ldexp (m, 64);
    w.be16 ((uint32_t) (e - 1 + 16383));
    w.be64 (mant);
}

/** AIFF ("NONE"), or AIFF-C with "sowt", "fl32", "fl64". */
inline bool writeAiff (const juce::File& f, const std::vector<int32_t>* ints, const std::vector<double>* floats,
                       int channels, double rate, int bits, const char* compression)
{
    const juce::String comp (compression);
    const bool aifc = comp != "NONE";
    const bool isFloat = comp == "fl32" || comp == "fl64";
    const size_t frames = (ints ? ints->size() : floats->size()) / (size_t) channels;
    const int bps = isFloat ? (comp == "fl32" ? 4 : 8) : (bits + 7) / 8;
    ByteWriter w;
    w.tag ("FORM"); w.be32 (0); w.tag (aifc ? "AIFC" : "AIFF");
    if (aifc) { w.tag ("FVER"); w.be32 (4); w.be32 (0xA2805140); }
    w.tag ("COMM"); w.be32 (aifc ? 24 : 18);
    w.be16 ((uint32_t) channels); w.be32 ((uint32_t) frames); w.be16 ((uint32_t) (isFloat ? bps * 8 : bits));
    writeExtended80 (w, rate);
    if (aifc) { w.tag (compression); w.u8 (0); w.u8 (0); }
    w.tag ("SSND"); w.be32 ((uint32_t) (8 + frames * (size_t) channels * (size_t) bps)); w.be32 (0); w.be32 (0);
    for (size_t i = 0; i < frames * (size_t) channels; ++i)
    {
        if (isFloat)
        {
            const double x = (*floats)[i];
            if (bps == 4) { float fl = (float) x; uint32_t b; std::memcpy (&b, &fl, 4); w.be32 (b); }
            else          { uint64_t b; std::memcpy (&b, &x, 8); w.be64 (b); }
            continue;
        }
        const uint32_t v = (uint32_t) (*ints)[i];
        const bool le = comp == "sowt";
        switch (bps)
        {
            case 1: w.u8 ((int) (v >> 24)); break;
            case 2: le ? w.le16 (v >> 16) : w.be16 (v >> 16); break;
            case 3: le ? w.le24 (v >> 8) : w.be24 (v >> 8); break;
            case 4: le ? w.le32 (v) : w.be32 (v); break;
        }
    }
    if (w.size() & 1) w.u8 (0);
    w.patchBe32 (4, (uint32_t) (w.size() - 8));
    return w.save (f);
}

/** 2nd-order sigma-delta modulator (Boser-Wooley): real DSD for the tests, stable for |u| < 0.7.
    Bytes MSB-first, interleaved by byte. */
inline std::vector<uint8_t> sigmaDelta (const std::vector<std::vector<double>>& input /*per channel, at the DSD rate*/)
{
    const size_t channels = input.size();
    const size_t n = input[0].size() / 8 * 8;
    std::vector<uint8_t> out (n / 8 * channels, 0);
    for (size_t c = 0; c < channels; ++c)
    {
        double i1 = 0.0, i2 = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            const double y = i2 >= 0.0 ? 1.0 : -1.0;
            i1 += input[c][i] - y;
            i2 += i1 - y;
            if (y > 0) out[(i / 8) * channels + c] |= (uint8_t) (0x80 >> (i % 8));
        }
    }
    return out;
}

inline std::vector<uint8_t> dsdSine (double dsdRate, double seconds, int channels, double freq, double amp)
{
    const auto n = (size_t) (dsdRate * seconds) / 8 * 8;
    std::vector<std::vector<double>> in ((size_t) channels, std::vector<double> (n));
    for (int c = 0; c < channels; ++c)
        for (size_t i = 0; i < n; ++i)
            in[(size_t) c][i] = amp * std::sin (2.0 * juce::MathConstants<double>::pi * freq * (double) i / dsdRate + c * 0.3);
    return sigmaDelta (in);
}

/** DSF writer: bytes MSB-first interleaved by byte; lsbFirst stores them bit-reversed (bitsPerSample = 1). */
inline bool writeDsf (const juce::File& f, const std::vector<uint8_t>& bytes, int channels, double rate, bool lsbFirst)
{
    const size_t perChannel = bytes.size() / (size_t) channels;
    const size_t blocks = (perChannel + 4095) / 4096;
    const uint64_t dataSize = 12 + blocks * 4096 * (size_t) channels;
    ByteWriter w;
    w.tag ("DSD "); w.le64 (28); w.le64 (0); w.le64 (0);
    w.tag ("fmt "); w.le64 (52); w.le32 (1); w.le32 (0); w.le32 (channels == 1 ? 1 : 2); w.le32 ((uint32_t) channels);
    w.le32 ((uint32_t) rate); w.le32 (lsbFirst ? 1 : 8); w.le64 (perChannel * 8); w.le32 (4096); w.le32 (0);
    w.tag ("data"); w.le64 (dataSize);
    auto rev = [] (uint8_t b) { uint8_t r = 0; for (int i = 0; i < 8; ++i) if (b & (1 << i)) r |= (uint8_t) (0x80 >> i); return r; };
    for (size_t b = 0; b < blocks; ++b)
        for (int c = 0; c < channels; ++c)
            for (size_t i = 0; i < 4096; ++i)
            {
                const size_t idx = b * 4096 + i;
                uint8_t v = idx < perChannel ? bytes[idx * (size_t) channels + (size_t) c] : 0;
                w.u8 (lsbFirst ? rev (v) : v);
            }
    const auto total = w.size();
    auto* p = static_cast<uint8_t*> (w.data.getData());
    for (int i = 0; i < 8; ++i) p[12 + i] = (uint8_t) ((uint64_t) total >> (8 * i));
    return w.save (f);
}

inline bool writeDff (const juce::File& f, const std::vector<uint8_t>& bytes, int channels, double rate)
{
    ByteWriter w;
    w.tag ("FRM8"); w.be64 (0); w.tag ("DSD ");
    w.tag ("FVER"); w.be64 (4); w.be32 (0x01050000);
    const size_t propStart = w.size();
    w.tag ("PROP"); w.be64 (0); w.tag ("SND ");
    w.tag ("FS  "); w.be64 (4); w.be32 ((uint32_t) rate);
    w.tag ("CHNL"); w.be64 (2 + 4 * (uint64_t) channels); w.be16 ((uint32_t) channels);
    for (int c = 0; c < channels; ++c) w.tag (c == 0 ? "SLFT" : (c == 1 ? "SRGT" : "C   "));
    w.tag ("CMPR"); w.be64 (20); w.tag ("DSD "); w.u8 (14); w.raw ("not compressed", 14); w.u8 (0);
    w.patchBe64 (propStart + 4, w.size() - propStart - 12);
    w.tag ("DSD "); w.be64 (bytes.size()); w.raw (bytes.data(), bytes.size());
    if (bytes.size() & 1) w.u8 (0);
    w.patchBe64 (4, w.size() - 12);
    return w.save (f);
}

/** ALAC in a minimal M4A: encoded with Apple's reference encoder, our own MP4 boxes. */
inline bool writeAlacM4a (const juce::File& f, const std::vector<int32_t>& pcm, int channels, double rate, int bits)
{
    AudioFormatDescription in {}, out {};
    in.mSampleRate = rate; in.mFormatID = kALACFormatLinearPCM;
    in.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
    in.mBitsPerChannel = (uint32_t) bits; in.mChannelsPerFrame = (uint32_t) channels;
    in.mBytesPerFrame = in.mBytesPerPacket = (uint32_t) (channels * ((bits + 7) / 8)); in.mFramesPerPacket = 1;
    out.mSampleRate = rate; out.mFormatID = kALACFormatAppleLossless;
    out.mFormatFlags = bits == 16 ? 1u : (bits == 20 ? 2u : (bits == 24 ? 3u : 4u));
    out.mFramesPerPacket = 4096; out.mChannelsPerFrame = (uint32_t) channels;

    ALACEncoder enc;
    enc.SetFrameSize (4096);
    enc.InitializeEncoder (out);
    uint32_t cookieSize = enc.GetMagicCookieSize ((uint32_t) channels);
    std::vector<uint8_t> cookie (cookieSize);
    enc.GetMagicCookie (cookie.data(), &cookieSize);

    const size_t frames = pcm.size() / (size_t) channels;
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint32_t> packetFrames;
    std::vector<uint8_t> inBuf, outBuf;
    for (size_t start = 0; start < frames; start += 4096)
    {
        const size_t n = std::min<size_t> (4096, frames - start);
        inBuf.clear();
        for (size_t i = 0; i < n * (size_t) channels; ++i)
        {
            const uint32_t v = (uint32_t) pcm[start * (size_t) channels + i];
            if (bits == 16) { const uint16_t s = (uint16_t) (v >> 16); inBuf.push_back ((uint8_t) s); inBuf.push_back ((uint8_t) (s >> 8)); }
            else if (bits == 24) { const uint32_t s = v >> 8; inBuf.push_back ((uint8_t) s); inBuf.push_back ((uint8_t) (s >> 8)); inBuf.push_back ((uint8_t) (s >> 16)); }
            else { for (int k = 0; k < 4; ++k) inBuf.push_back ((uint8_t) (v >> (8 * k))); }
        }
        outBuf.assign (inBuf.size() + 1024, 0);
        int32_t numBytes = (int32_t) inBuf.size();
        enc.Encode (in, out, inBuf.data(), outBuf.data(), &numBytes);
        packets.emplace_back (outBuf.begin(), outBuf.begin() + numBytes);
        packetFrames.push_back ((uint32_t) n);
    }

    ByteWriter w;
    auto box = [&w] (const char* type) { const size_t at = w.size(); w.be32 (0); w.tag (type); return at; };
    auto close = [&w] (size_t at) { w.patchBe32 (at, (uint32_t) (w.size() - at)); };

    auto ftyp = box ("ftyp"); w.tag ("M4A "); w.be32 (0); w.tag ("M4A "); w.tag ("isom"); close (ftyp);
    auto mdat = box ("mdat");
    const auto firstPacket = (uint32_t) w.size();
    for (auto& p : packets) w.raw (p.data(), p.size());
    close (mdat);

    auto moov = box ("moov");
    auto trak = box ("trak");
    auto mdia = box ("mdia");
    auto mdhd = box ("mdhd"); w.be32 (0); w.be32 (0); w.be32 (0); w.be32 ((uint32_t) rate); w.be32 ((uint32_t) frames); w.be32 (0); close (mdhd);
    auto hdlr = box ("hdlr"); w.be32 (0); w.be32 (0); w.tag ("soun"); w.be32 (0); w.be32 (0); w.be32 (0); w.u8 (0); close (hdlr);
    auto minf = box ("minf");
    auto stbl = box ("stbl");
    auto stsd = box ("stsd"); w.be32 (0); w.be32 (1);
    auto entry = box ("alac");
    for (int i = 0; i < 6; ++i) w.u8 (0);
    w.be16 (1); w.be16 (0); w.be16 (0); w.be32 (0);
    w.be16 ((uint32_t) channels); w.be16 ((uint32_t) bits); w.be16 (0); w.be16 (0); w.be32 ((uint32_t) rate << 16);
    auto alac = box ("alac"); w.be32 (0); w.raw (cookie.data(), 24); close (alac);
    close (entry);
    close (stsd);
    auto stts = box ("stts"); w.be32 (0);
    {
        // group identical frame counts
        std::vector<std::pair<uint32_t, uint32_t>> runs;
        for (auto n : packetFrames)
            if (! runs.empty() && runs.back().second == n) ++runs.back().first; else runs.push_back ({ 1, n });
        w.be32 ((uint32_t) runs.size());
        for (auto& r : runs) { w.be32 (r.first); w.be32 (r.second); }
    }
    close (stts);
    auto stsc = box ("stsc"); w.be32 (0); w.be32 (1); w.be32 (1); w.be32 ((uint32_t) packets.size()); w.be32 (1); close (stsc);
    auto stsz = box ("stsz"); w.be32 (0); w.be32 (0); w.be32 ((uint32_t) packets.size());
    for (auto& p : packets) w.be32 ((uint32_t) p.size());
    close (stsz);
    auto stco = box ("stco"); w.be32 (0); w.be32 (1); w.be32 (firstPacket); close (stco);
    close (stbl); close (minf); close (mdia); close (trak); close (moov);
    return w.save (f);
}

} // namespace nxtest
