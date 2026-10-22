// NX Player — MIT License
// double -> integer of B bits (left-justified in int32), TPDF dither of ±1 LSB.
// Real-time safe (no allocation, xorshift noise). Only used when the signal is no longer bit-perfect
// (digital volume, resampling, DSD -> PCM, DSP plug-ins, float sources).
#pragma once

#include <cmath>
#include <cstdint>

namespace nx::dsp
{

class Requantizer
{
public:
    void setBits (int b, bool useDither = true) noexcept
    {
        bits = b < 8 ? 8 : (b > 32 ? 32 : b);
        dither = useDither && bits < 32;
        scale = std::ldexp (1.0, bits - 1);
        maxV = scale - 1.0;
    }

    int getBits() const noexcept { return bits; }

    /** x in [-1, 1) full scale. */
    int32_t process (double x) noexcept
    {
        double v = x * scale;
        if (dither) v += uniform() - uniform();          // TPDF, 2 LSB peak to peak
        v = std::floor (v + 0.5);
        if (v > maxV) { v = maxV; ++clipped; }
        else if (v < -scale) { v = -scale; ++clipped; }
        const auto i = (int64_t) v;
        return (int32_t) (uint32_t) ((uint64_t) i << (32 - bits));
    }

    /** Exactly representable at this depth? (float sources that are really integer PCM). */
    bool isExact (double x) const noexcept
    {
        const double v = x * scale;
        return v == std::floor (v) && v <= maxV && v >= -scale;
    }

    uint64_t clipped = 0;

private:
    double uniform() noexcept
    {
        state ^= state << 13; state ^= state >> 7; state ^= state << 17;
        return (double) (state >> 11) * (1.0 / 9007199254740992.0);   // [0, 1)
    }

    int bits = 24;
    bool dither = true;
    double scale = 8388608.0, maxV = 8388607.0;
    uint64_t state = 0x9E3779B97F4A7C15ull;
};

/** DSD over PCM (DoP 1.1): 16 DSD bits per 24-bit word, markers 0x05 / 0xFA alternating per frame. */
struct DopPacker
{
    bool markerToggle = false;

    /** bytesA / bytesB: two consecutive DSD bytes of one channel (A is earlier, it goes in bits 15..8). */
    static int32_t word (uint8_t marker, uint8_t a, uint8_t b) noexcept
    {
        return (int32_t) (((uint32_t) marker << 24) | ((uint32_t) a << 16) | ((uint32_t) b << 8));
    }

    uint8_t nextMarker() noexcept
    {
        const uint8_t m = markerToggle ? 0xFA : 0x05;
        markerToggle = ! markerToggle;
        return m;
    }
};

} // namespace nx::dsp
