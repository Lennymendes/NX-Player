// NX Player — MIT License
// Kaiser-windowed sinc low-pass design (double precision), the same family of filters as AuraEngine.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
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

/** In-place radix-2 FFT (size must be a power of two). inverse: unscaled inverse transform. */
inline void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPi / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Minimum-phase version of a FIR with the same magnitude response (homomorphic / real-cepstrum method).
    All the energy moves to the start of the impulse: no pre-ringing ("pré-écho"), at the price of a
    frequency-dependent group delay. The FFT is 32× longer than the filter to keep cepstral aliasing
    below the stop-band; the result keeps the DC gain of the original. */
inline std::vector<double> toMinimumPhase (const std::vector<double>& h)
{
    const size_t len = h.size();
    if (len < 4) return h;
    size_t n = 1;
    while (n < len * 32) n <<= 1;

    std::vector<std::complex<double>> spec (n);
    for (size_t i = 0; i < len; ++i) spec[i] = h[i];
    fft (spec, false);

    double peak = 0.0;
    for (auto& c : spec) peak = std::max (peak, std::abs (c));
    const double floorMag = peak * 1.0e-12;   // −240 dB: below any stop-band we design
    for (auto& c : spec) c = std::log (std::max (std::abs (c), floorMag));

    fft (spec, true);                          // real cepstrum (unscaled)
    for (auto& c : spec) c /= (double) n;
    // fold: keep c[0] and c[n/2], double the causal part, drop the anti-causal part
    for (size_t i = 1; i < n / 2; ++i) spec[i] *= 2.0;
    for (size_t i = n / 2 + 1; i < n; ++i) spec[i] = 0.0;

    fft (spec, false);
    for (auto& c : spec) c = std::exp (c);
    fft (spec, true);

    std::vector<double> out (len);
    double sumIn = 0.0, sumOut = 0.0;
    for (size_t i = 0; i < len; ++i)
    {
        out[i] = spec[i].real() / (double) n;
        sumIn += h[i];
        sumOut += out[i];
    }
    if (std::abs (sumOut) > 1.0e-30)
        for (auto& v : out) v *= sumIn / sumOut;
    return out;
}

} // namespace nx::dsp
