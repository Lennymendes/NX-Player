// NX Player — MIT License
// Last stage of the converted (DSD -> DXD, upsampled) stream, 64-bit, interleaved, in place:
//  - "warmth": a touch of 2nd-order harmonic (−90 dBFS for a full-scale sine, DC removed) and a first-order
//    high shelf of −0.5 dB above ~18 kHz (an analogue-console / tape flavour; 100 % optional)
//  - a peak limiter for gains above 0 dB (DSD -> PCM +3 / +6 dB): instant attack on the frame peak,
//    50 ms release, ceiling −0.2 dBFS — it only acts on the rare peaks that would clip.
// Real-time safe: no allocation after prepare().
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace nx::dsp
{

class OutputShaper
{
public:
    void prepare (double sampleRate, int numChannels, bool warmthOn, bool limiterOn)
    {
        channels = std::max (1, numChannels);
        warmth = warmthOn;
        limiter = limiterOn;
        constexpr double pi = 3.14159265358979323846;
        // 2nd harmonic: x + k (x² − mean). For x = sin, x² = ½ − ½ cos 2ωt -> H2 amplitude k / 2 = −90 dBFS.
        k2 = 2.0 * std::pow (10.0, -90.0 / 20.0);
        dcCoef = 1.0 - std::exp (-2.0 * pi * 5.0 / sampleRate);
        // first-order high shelf, −0.5 dB, corner 18 kHz (bilinear)
        const double g = std::pow (10.0, -0.5 / 20.0);
        const double wc = std::tan (pi * std::min (18000.0, 0.45 * sampleRate) / sampleRate);
        const double a0 = 1.0 + wc;
        // H(s) = (g s + wc) / (s + wc) -> high frequencies × g, DC × 1
        b0 = (g + wc) / a0;
        b1 = (wc - g) / a0;
        a1 = (wc - 1.0) / a0;
        ceiling = std::pow (10.0, -0.2 / 20.0);
        release = std::exp (-1.0 / (0.050 * sampleRate));
        reset();
    }

    void reset()
    {
        dc.assign ((size_t) channels, 0.0);
        x1.assign ((size_t) channels, 0.0);
        y1.assign ((size_t) channels, 0.0);
        gain = 1.0;
        limitedFrames = 0;
    }

    bool isActive() const noexcept { return warmth || limiter; }

    void process (double* interleaved, int frames) noexcept
    {
        if (! isActive()) return;
        for (int i = 0; i < frames; ++i)
        {
            double* f = interleaved + (size_t) i * (size_t) channels;
            if (warmth)
            {
                for (int c = 0; c < channels; ++c)
                {
                    double x = f[c];
                    const double sq = x * x;
                    dc[(size_t) c] += dcCoef * (sq - dc[(size_t) c]);
                    x += k2 * (sq - dc[(size_t) c]);
                    const double y = b0 * x + b1 * x1[(size_t) c] - a1 * y1[(size_t) c];
                    x1[(size_t) c] = x;
                    y1[(size_t) c] = y;
                    f[c] = y;
                }
            }
            if (limiter)
            {
                double peak = 0.0;
                for (int c = 0; c < channels; ++c) peak = std::max (peak, std::abs (f[c]));
                const double needed = peak * gain > ceiling ? ceiling / peak : 1.0;
                if (needed < gain) gain = needed;                        // instant attack
                else gain = 1.0 - (1.0 - gain) * release;                // smooth release towards unity
                if (peak * gain > ceiling) gain = ceiling / peak;
                if (gain < 0.9999) ++limitedFrames;
                for (int c = 0; c < channels; ++c) f[c] *= gain;
            }
        }
    }

    long long getLimitedFrames() const noexcept { return limitedFrames; }

private:
    int channels = 2;
    bool warmth = false, limiter = false;
    double k2 = 0.0, dcCoef = 0.0, b0 = 1.0, b1 = 0.0, a1 = 0.0;
    double ceiling = 1.0, release = 0.0, gain = 1.0;
    std::vector<double> dc, x1, y1;
    long long limitedFrames = 0;
};

} // namespace nx::dsp
