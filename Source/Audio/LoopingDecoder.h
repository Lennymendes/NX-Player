// NX Player — MIT License
// A-B loop at the decoder level: the engine keeps writing a continuous stream into the ring, so the device
// is never stopped, reopened or re-clocked when the loop wraps.
//  - PCM: the last `crossfade` frames before B are mixed with the first ones after A (raised-cosine,
//    a few milliseconds): no click, no gap.
//  - DSD (DoP / native / DSD -> PCM): the jump is a cut aligned on 2-byte (DoP word) boundaries — DSD
//    cannot be mixed without leaving the 1-bit domain.
#pragma once

#include "AudioDecoder.h"
#include <cmath>
#include <vector>

namespace nx
{

class LoopingDecoder final : public AudioDecoder
{
public:
    /** a, b: positions in the decoder's units (frames, or bytes per channel for DSD). */
    LoopingDecoder (std::unique_ptr<AudioDecoder> source, juce::int64 a, juce::int64 b, int crossfadeFrames)
        : inner (std::move (source)), format (inner->getFormat())
    {
        const bool dsd = format.isDsd();
        loopA = dsd ? (a / 2) * 2 : a;
        loopB = dsd ? (b / 2) * 2 : b;
        xf = dsd ? 0 : juce::jlimit (0, (int) juce::jmax<juce::int64> (0, (loopB - loopA) / 4), crossfadeFrames);
    }

    StreamFormat getFormat() const override  { return format; }
    juce::int64 getLength() const override    { return inner->getLength(); }
    juce::int64 getPosition() const override  { return inner->getPosition() - (juce::int64) pendingFrames(); }
    bool seek (juce::int64 p) override
    {
        pending.clear(); pendingD.clear(); pendingPos = 0;
        return inner->seek (p >= loopB || p < 0 ? loopA : p);
    }

    /** Number of wraps so far, and where (in output frames since the last wrap) the stream is now. */
    int getWrapCount() const noexcept { return wraps; }
    /** The next read starts the wrap (reads stop exactly at the wrap point, so the engine can mark it). */
    bool isAtWrap() const noexcept
    {
        return pendingFrames() == 0 && inner->getPosition() >= (format.isDsd() ? loopB : loopB - xf);
    }
    juce::int64 getLoopA() const noexcept { return loopA; }

    int readInt (int32_t* dest, int maxFrames) override
    {
        const int ch = format.numChannels;
        int done = 0;
        while (done < maxFrames)
        {
            if (pendingPos < (int) (pending.size() / (size_t) ch))
            {
                const int n = juce::jmin (maxFrames - done, (int) (pending.size() / (size_t) ch) - pendingPos);
                std::copy (pending.begin() + (std::ptrdiff_t) pendingPos * ch, pending.begin() + (std::ptrdiff_t) (pendingPos + n) * ch, dest + (size_t) done * ch);
                pendingPos += n;
                done += n;
                continue;
            }
            const juce::int64 untilFade = (loopB - xf) - inner->getPosition();
            if (untilFade > 0)
            {
                const int n = inner->readInt (dest + (size_t) done * ch, (int) juce::jmin<juce::int64> (maxFrames - done, untilFade));
                if (n <= 0) { if (! wrapPlain()) break; continue; }
                done += n;
                continue;
            }
            if (done > 0) break;   // stop exactly at the wrap point
            // At B − xf: crossfade the tail into the head of the loop.
            std::vector<int32_t> tail ((size_t) xf * ch), head ((size_t) xf * ch);
            const int t = xf > 0 ? inner->readInt (tail.data(), xf) : 0;
            inner->seek (loopA);
            const int h = xf > 0 ? inner->readInt (head.data(), xf) : 0;
            pending.assign ((size_t) juce::jmin (t, h) * ch, 0);
            for (int i = 0; i < juce::jmin (t, h); ++i)
            {
                const double g = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (i + 0.5) / xf);   // 0 -> 1
                for (int c = 0; c < ch; ++c)
                {
                    const double v = (1.0 - g) * tail[(size_t) i * ch + c] + g * head[(size_t) i * ch + c];
                    pending[(size_t) i * ch + c] = (int32_t) juce::jlimit (-2147483648.0, 2147483647.0, std::round (v));
                }
            }
            pendingPos = 0;
            ++wraps;
        }
        return done;
    }

    int readDouble (double* dest, int maxFrames) override
    {
        const int ch = format.numChannels;
        int done = 0;
        while (done < maxFrames)
        {
            if (pendingPos < (int) (pendingD.size() / (size_t) ch))
            {
                const int n = juce::jmin (maxFrames - done, (int) (pendingD.size() / (size_t) ch) - pendingPos);
                std::copy (pendingD.begin() + (std::ptrdiff_t) pendingPos * ch, pendingD.begin() + (std::ptrdiff_t) (pendingPos + n) * ch, dest + (size_t) done * ch);
                pendingPos += n;
                done += n;
                continue;
            }
            const juce::int64 untilFade = (loopB - xf) - inner->getPosition();
            if (untilFade > 0)
            {
                const int n = inner->readDouble (dest + (size_t) done * ch, (int) juce::jmin<juce::int64> (maxFrames - done, untilFade));
                if (n <= 0) { if (! wrapPlain()) break; continue; }
                done += n;
                continue;
            }
            if (done > 0) break;
            std::vector<double> tail ((size_t) xf * ch), head ((size_t) xf * ch);
            const int t = xf > 0 ? inner->readDouble (tail.data(), xf) : 0;
            inner->seek (loopA);
            const int h = xf > 0 ? inner->readDouble (head.data(), xf) : 0;
            pendingD.assign ((size_t) juce::jmin (t, h) * ch, 0.0);
            for (int i = 0; i < juce::jmin (t, h); ++i)
            {
                const double g = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (i + 0.5) / xf);
                for (int c = 0; c < ch; ++c)
                    pendingD[(size_t) i * ch + c] = (1.0 - g) * tail[(size_t) i * ch + c] + g * head[(size_t) i * ch + c];
            }
            pendingPos = 0;
            ++wraps;
        }
        return done;
    }

    int readDsd (uint8_t* dest, int maxBytes) override
    {
        const int ch = format.numChannels;
        int done = 0;
        while (done < maxBytes)
        {
            const juce::int64 untilB = loopB - inner->getPosition();
            if (untilB <= 0)
            {
                if (done > 0) break;
                inner->seek (loopA);
                ++wraps;
                continue;
            }
            const int n = inner->readDsd (dest + (size_t) done * ch, (int) juce::jmin<juce::int64> (maxBytes - done, untilB));
            if (n <= 0) { if (! wrapPlain()) break; continue; }
            done += n;
        }
        return done;
    }

private:
    int pendingFrames() const noexcept
    {
        const int ch = juce::jmax (1, format.numChannels);
        const int total = (int) juce::jmax (pending.size(), pendingD.size()) / ch;
        return juce::jmax (0, total - pendingPos);
    }

    /** B beyond the end of the file: wrap without crossfade. */
    bool wrapPlain()
    {
        if (inner->getPosition() <= loopA) return false;   // nothing left to play at all
        inner->seek (loopA);
        ++wraps;
        return true;
    }

    std::unique_ptr<AudioDecoder> inner;
    StreamFormat format;
    juce::int64 loopA = 0, loopB = 0;
    int xf = 0, wraps = 0, pendingPos = 0;
    std::vector<int32_t> pending;
    std::vector<double> pendingD;
};

} // namespace nx
