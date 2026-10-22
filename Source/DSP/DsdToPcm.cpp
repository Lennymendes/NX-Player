// NX Player — MIT License
#include "DsdToPcm.h"
#include "FilterDesign.h"
#include <algorithm>
#include <cmath>

namespace nx::dsp
{

void DsdToPcm::prepare (double dsdRate, double outputRate, int numChannels, double gain)
{
    channels = numChannels;
    bytesPerOut = std::max (1, (int) std::lround (dsdRate / (8.0 * outputRate)));
    outRate = dsdRate / (8.0 * bytesPerOut);

    const double passHz = std::min (40000.0, 0.42 * outRate);
    const double stopHz = std::min (150000.0, 0.5 * outRate);
    const double attenuation = 150.0;
    int taps = kaiserLength (attenuation, (stopHz - passHz) / dsdRate);
    taps = (taps + 7) / 8 * 8;
    numTables = taps / 8;

    const auto h = designLowpass (taps, 0.5 * (passHz + stopHz) / dsdRate, attenuation, gain);

    tables.assign ((size_t) numTables * 256, 0.0);
    for (int j = 0; j < numTables; ++j)
        for (int v = 0; v < 256; ++v)
        {
            double s = 0.0;
            for (int t = 0; t < 8; ++t)                       // t = 0 : MSB, earliest sample of the byte
            {
                const bool one = (v >> (7 - t)) & 1;
                s += h[(size_t) (8 * j + 7 - t)] * (one ? 1.0 : -1.0);
            }
            tables[(size_t) j * 256 + (size_t) v] = s;
        }
    reset();
}

void DsdToPcm::reset() noexcept
{
    history.assign ((size_t) channels * 2 * (size_t) numTables, 0x69); // 0x69 = 01101001: DSD idle pattern (silence)
    writePos.assign ((size_t) channels, 0);
    phase = 0;
}

int DsdToPcm::process (const uint8_t* in, int numBytes, double* out, int maxOutFrames) noexcept
{
    int produced = 0;
    const int nt = numTables;
    for (int i = 0; i < numBytes; ++i)
    {
        for (int c = 0; c < channels; ++c)
        {
            uint8_t* h = history.data() + (size_t) c * 2 * nt;
            int& wp = writePos[(size_t) c];
            const uint8_t b = in[(size_t) i * channels + c];
            h[wp] = b;
            h[wp + nt] = b;
            wp = wp + 1 == nt ? 0 : wp + 1;
        }

        if (++phase < bytesPerOut) continue;
        phase = 0;
        if (produced >= maxOutFrames) continue;

        for (int c = 0; c < channels; ++c)
        {
            const uint8_t* h = history.data() + (size_t) c * 2 * nt + writePos[(size_t) c];
            const double* t = tables.data();
            double acc = 0.0;
            // newest byte (j = 0) is h[nt - 1]
            for (int j = 0; j < nt; ++j, t += 256)
                acc += t[h[nt - 1 - j]];
            out[(size_t) produced * channels + c] = acc;
        }
        ++produced;
    }
    return produced;
}

} // namespace nx::dsp
