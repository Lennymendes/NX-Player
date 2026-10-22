// NX Player — MIT License
// Kaiser-windowed sinc low-pass design (double precision), the same family of filters as AuraEngine.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace nx::dsp
{

constexpr double kPi = 3.14159265358979323846;

inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 200; ++k)
    {
        term *= q / ((double) k * k);
        sum += term;
        if (term < sum * 1e-21) break;
    }
    return sum;
}

inline double kaiserBeta (double attenuationDb) noexcept
{
    if (attenuationDb > 50.0) return 0.1102 * (attenuationDb - 8.7);
    if (attenuationDb >= 21.0) return 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
    return 0.0;
}

/** Number of taps for a Kaiser design (transition width in normalised frequency, cycles / sample). */
inline int kaiserLength (double attenuationDb, double transitionWidth) noexcept
{
    const double n = (attenuationDb - 7.95) / (14.36 * transitionWidth) + 1.0;
    return (int) std::ceil (n);
}

/** Linear-phase low-pass: cutoff (−6 dB) in cycles / sample, unity DC gain multiplied by gain. */
inline std::vector<double> designLowpass (int numTaps, double cutoff, double attenuationDb, double gain = 1.0)
{
    std::vector<double> h ((size_t) numTaps);
    const double beta = kaiserBeta (attenuationDb);
    const double i0b = besselI0 (beta);
    const double centre = 0.5 * (numTaps - 1);
    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n)
    {
        const double x = n - centre;
        const double sinc = x == 0.0 ? 2.0 * cutoff : std::sin (2.0 * kPi * cutoff * x) / (kPi * x);
        const double r = centre > 0 ? x / centre : 0.0;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        h[(size_t) n] = sinc * w;
        sum += h[(size_t) n];
    }
    for (auto& c : h) c *= gain / sum;
    return h;
}

} // namespace nx::dsp
