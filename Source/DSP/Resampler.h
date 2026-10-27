// NX Player — MIT License
// Rational polyphase resampler (L / M), linear phase, Kaiser-windowed sinc, 64-bit.
// Used for: PCM rates the DAC cannot play, the optional "sinc upsampling to DXD", and the last
// stage of DSD -> PCM when the DAC tops out below 352,8 kHz.
#pragma once

#include <cstdint>
#include <vector>

namespace nx::dsp
{

class Resampler
{
public:
    /** attenuationDb: stop-band rejection (default 160 dB). passFraction: pass-band edge as a fraction
        of the lower Nyquist (0.907 keeps 20 kHz flat at 44,1 kHz). */
    void prepare (double inputRate, double outputRate, int numChannels,
                  double attenuationDb = 160.0, double passFraction = 0.907, bool minimumPhase = false);
    bool isMinimumPhase() const noexcept { return minPhase; }

    bool isIdentity() const noexcept { return L == M; }
    int getL() const noexcept { return L; }
    int getM() const noexcept { return M; }
    int getTapsPerPhase() const noexcept { return taps; }
    double getRatio() const noexcept { return (double) L / (double) M; }
    /** Group delay expressed in input samples. */
    double getLatencyInputSamples() const noexcept { return (taps * L - 1) * 0.5 / L; }

    /** Upper bound of process() output for nIn input frames. */
    int maxOutputFor (int nIn) const noexcept { return (int) (((int64_t) nIn * L) / M) + 2; }

    /** Interleaved in -> interleaved out. Consumes all nIn frames. Returns frames written. */
    int process (const double* in, int nIn, double* out, int maxOut) noexcept;
    void reset() noexcept;

private:
    int L = 1, M = 1, taps = 1, channels = 2;
    bool minPhase = false;
    std::vector<double> phases;                   // L × taps, phase-major, reversed for direct dot product
    std::vector<std::vector<double>> buffers;     // per channel: history + pending input
    int64_t bufferStart = 0;                      // absolute index of buffers[c][0]
    int64_t inputCount = 0;                       // absolute number of input samples received
    int64_t nextTime = 0;                         // next output time, in 1/L input samples
};

} // namespace nx::dsp
