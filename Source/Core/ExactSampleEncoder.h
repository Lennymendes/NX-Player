// NX Player — MIT License
// Bit-exact transport of integer samples through a float API.
//
// JUCE (and CoreAudio's IOProc) only exchange float buffers. The driver, the OS (CoreAudio HAL) or
// JUCE itself then turns each float back into the integer the DAC receives, with a rule that depends
// on the backend. For every integer we want the DAC to receive, this encoder finds a float that the
// backend's rule maps exactly onto that integer. PlayerSelfTest checks every one of the 2^24 values.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace nx
{

enum class FloatToIntRule
{
    Scale2N,        // int = round (f * 2^(B-1))          CoreAudio HAL, JUCE AudioData Int16 / Int24 (WASAPI, ALSA)
    RoundMaxMinus1, // int = round (f * (2^(B-1) - 1))    JUCE ASIO converters
    TruncMaxMinus1  // int = (int) (f * (2^(B-1) - 1))    JUCE AudioData Int32 (WASAPI / ALSA 32-bit integer)
};

class ExactSampleEncoder
{
public:
    ExactSampleEncoder (FloatToIntRule r = FloatToIntRule::Scale2N, int deviceBits = 24) noexcept
        : rule (r), bits (deviceBits)
    {
        const double full = std::ldexp (1.0, bits - 1);
        scale = rule == FloatToIntRule::Scale2N ? full : full - 1.0;
        maxInt = (int64_t) full - 1;
    }

    FloatToIntRule getRule() const noexcept { return rule; }
    int getDeviceBits() const noexcept { return bits; }

    /** What the backend will produce from f (emulation of its conversion rule). */
    int64_t apply (float f) const noexcept
    {
        double x = (double) f * scale;
        if (rule == FloatToIntRule::TruncMaxMinus1)
        {
            x = x < -scale ? -scale : (x > scale ? scale : x);
            return (int64_t) x;
        }
        int64_t r = (int64_t) std::llround (x);
        return r < -maxInt ? -maxInt : (r > maxInt ? maxInt : r);
    }

    /** Float that makes the backend output (v >> (32 - B)), v being an int32 left-justified sample.
        Exact for every source of 24 bits or fewer; for B = 32 the 24 most significant bits are exact. */
    float encode (int32_t v) const noexcept
    {
        const int64_t target = (int64_t) (v >> (32 - bits));
        const int64_t clampedTarget = target < -maxInt ? -maxInt : target; // -2^(B-1) is not reachable with ±max rules
        float f = (float) ((double) clampedTarget / scale);

        if (apply (f) == clampedTarget)
            return f;

        // Walk a few ulps either way: the float grid is denser than the integer grid below 2^24.
        float up = f, down = f;
        for (int i = 0; i < 8; ++i)
        {
            up   = std::nextafter (up,    std::numeric_limits<float>::infinity());
            down = std::nextafter (down, -std::numeric_limits<float>::infinity());
            if (apply (up) == clampedTarget)   return up;
            if (apply (down) == clampedTarget) return down;
        }

        // No float lands exactly (B = 32 and more than 24 significant bits): keep the 24 MSBs right.
        const int shift = bits > 24 ? bits - 24 : 0;
        up = down = f;
        for (int i = 0; i < 8; ++i)
        {
            if ((apply (up) >> shift) == (clampedTarget >> shift))     return up;
            if ((apply (down) >> shift) == (clampedTarget >> shift))   return down;
            up   = std::nextafter (up,    std::numeric_limits<float>::infinity());
            down = std::nextafter (down, -std::numeric_limits<float>::infinity());
        }
        return f;
    }

private:
    FloatToIntRule rule;
    int bits;
    double scale = 1.0;
    int64_t maxInt = 0;
};

} // namespace nx
