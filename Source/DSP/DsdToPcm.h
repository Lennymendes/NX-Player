// NX Player — MIT License
// DSD -> PCM (DXD 352,8 kHz by default) for DACs that accept PCM only.
// One linear-phase FIR runs at the DSD rate and is evaluated only at the output instants. Since the
// input is 1 bit, the FIR is computed with one 256-entry table per byte of history (no multiplications).
// The filter is flat to 40 kHz and rejects the shaped DSD noise by 150 dB from 150 kHz on.
#pragma once

#include <cstdint>
#include <vector>

namespace nx::dsp
{

class DsdToPcm
{
public:
    /** dsdRate: 2 822 400 × 2^k; outputRate: dsdRate / (8 × 2^m), 352 800 for DXD. gain: linear (1 = SACD 0 dB -> −6 dBFS). */
    void prepare (double dsdRate, double outputRate, int numChannels, double gain = 1.0);

    double getOutputRate() const noexcept { return outRate; }
    int getBytesPerOutputSample() const noexcept { return bytesPerOut; }
    int getNumTaps() const noexcept { return numTables * 8; }
    /** Group delay in output samples (the filter is linear phase). */
    double getLatencyOutputSamples() const noexcept { return (getNumTaps() - 1) * 0.5 / (8.0 * bytesPerOut); }

    /** Consumes numBytes bytes per channel (interleaved by byte) and appends interleaved doubles to out.
        Returns the number of output frames produced. */
    int process (const uint8_t* interleaved, int numBytes, double* out, int maxOutFrames) noexcept;

    /** Output frames process() can produce for numBytes of input at most. */
    int maxOutputFor (int numBytes) const noexcept { return numBytes / bytesPerOut + 1; }

    void reset() noexcept;

private:
    int channels = 2, bytesPerOut = 1, numTables = 0;
    double outRate = 352800.0;
    std::vector<double> tables;                 // numTables × 256
    std::vector<uint8_t> history;               // per channel: 2 × numTables (mirrored ring)
    std::vector<int> writePos;
    int phase = 0;                              // bytes since the last output
};

} // namespace nx::dsp
