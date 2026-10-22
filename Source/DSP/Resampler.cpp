// NX Player — MIT License
#include "Resampler.h"
#include "FilterDesign.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace nx::dsp
{

void Resampler::prepare (double inputRate, double outputRate, int numChannels, double attenuationDb, double passFraction)
{
    channels = numChannels;
    const auto in = (int64_t) std::llround (inputRate);
    const auto out = (int64_t) std::llround (outputRate);
    const auto g = std::gcd (in, out);
    L = (int) (out / g);
    M = (int) (in / g);

    if (L == M)
    {
        taps = 1;
        phases.assign (1, 1.0);
    }
    else
    {
        const double nyqMin = 0.5 * (double) std::min (in, out);
        const double pass = passFraction * nyqMin;
        const double stop = nyqMin;
        const double fsUp = (double) L * (double) in;
        const int total = kaiserLength (attenuationDb, (stop - pass) / fsUp);
        taps = std::max (2, (total + L - 1) / L);
        const int n = taps * L;
        const auto h = designLowpass (n, 0.5 * (pass + stop) / fsUp, attenuationDb, (double) L);

        phases.assign ((size_t) n, 0.0);
        for (int p = 0; p < L; ++p)
            for (int i = 0; i < taps; ++i)
                phases[(size_t) p * taps + i] = h[(size_t) (p + (taps - 1 - i) * L)];
    }
    reset();
}

void Resampler::reset() noexcept
{
    buffers.assign ((size_t) channels, std::vector<double> ((size_t) (taps - 1), 0.0));
    for (auto& b : buffers) b.reserve ((size_t) taps + 65536);
    bufferStart = -(int64_t) (taps - 1);
    inputCount = 0;
    nextTime = 0;
}

int Resampler::process (const double* in, int nIn, double* out, int maxOut) noexcept
{
    if (L == M)
    {
        const int n = std::min (nIn, maxOut);
        std::memcpy (out, in, sizeof (double) * (size_t) n * (size_t) channels);
        return n;
    }

    for (int c = 0; c < channels; ++c)
    {
        auto& b = buffers[(size_t) c];
        const size_t old = b.size();
        b.resize (old + (size_t) nIn);   // capacity reserved in reset(); grows only for very large blocks
        for (int i = 0; i < nIn; ++i)
            b[old + (size_t) i] = in[(size_t) i * channels + c];
    }
    inputCount += nIn;

    int produced = 0;
    while (produced < maxOut)
    {
        const int64_t base = nextTime / L;
        if (base >= inputCount) break;
        const int p = (int) (nextTime % L);
        const double* coef = phases.data() + (size_t) p * taps;
        const auto offset = (size_t) (base - (taps - 1) - bufferStart);
        for (int c = 0; c < channels; ++c)
        {
            const double* x = buffers[(size_t) c].data() + offset;
            double acc = 0.0;
            for (int i = 0; i < taps; ++i)
                acc += coef[i] * x[i];
            out[(size_t) produced * channels + c] = acc;
        }
        ++produced;
        nextTime += M;
    }

    // Drop what no future output needs.
    const int64_t keepFrom = nextTime / L - (taps - 1);
    const int64_t drop = keepFrom - bufferStart;
    if (drop > 0)
    {
        for (auto& b : buffers)
            b.erase (b.begin(), b.begin() + (std::ptrdiff_t) std::min<int64_t> (drop, (int64_t) b.size()));
        bufferStart += drop;
    }
    return produced;
}

} // namespace nx::dsp
