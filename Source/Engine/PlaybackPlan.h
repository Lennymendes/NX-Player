// NX Player — MIT License
// How a track reaches the DAC: decided once per track from its format, the DAC's capabilities and the
// user's settings. Two consecutive tracks with the same "wire" (format, rate, channels) are played
// gaplessly; a different wire reopens the device at the track boundary.
#pragma once

#include "../Core/Format.h"
#include "../Output/OutputBackend.h"

namespace nx
{

enum class DsdMode { Auto, Native, DoP, ConvertToPcm };

struct EngineSettings
{
    DsdMode dsdMode = DsdMode::Auto;
    bool dopAllowed = true;              // DoP needs a DAC that recognises the markers: on by default
    bool upsampleToDxd = false;          // "sinc vers DXD": integer-ratio upsampling to 352,8 / 384 kHz
    double dsdToPcmGainDb = 0.0;         // 0 dB: SACD 0 dB -> −6 dBFS, never clips; +6 dB: louder
    double maxDsdToPcmRate = 352800.0;
    bool exclusive = true;               // hog mode / WASAPI exclusive
    int bufferMilliseconds = 0;          // 0 = backend default
};

struct PlaybackPlan
{
    StreamFormat source;
    WireFormat wire = WireFormat::Pcm;
    double deviceRate = 44100.0;         // PCM / DoP word rate, DSD bit rate in native DSD
    int channels = 2;                    // channels sent to the device (mono is doubled)
    int ringBitsHint = 24;               // significant bits stored in the ring (requested from the device)

    enum class Conversion { None, FloatToInt, Resample, DsdToPcm } conversion = Conversion::None;
    double conversionRate = 0.0;         // intermediate rate (DSD -> PCM output before an optional resample)
    bool needsRequantize = false;        // the ring holds more precision than the device word: TPDF dither

    juce::String reason;                 // why (shown in the Signal Path)

    /** Same device configuration: the boundary between two tracks can be gapless. */
    bool sameWire (const PlaybackPlan& o) const noexcept
    {
        return wire == o.wire && deviceRate == o.deviceRate && channels == o.channels
            && (wire != WireFormat::Pcm || ringBitsHint == o.ringBitsHint);
    }

    int ringFrameBytes() const noexcept { return wire == WireFormat::Pcm ? channels * 4 : channels; }

    /** Frames of device output per ring frame unit (DoP: one PCM frame per 2 DSD bytes). */
    double ringUnitsPerSecond() const noexcept
    {
        switch (wire)
        {
            case WireFormat::Pcm:       return deviceRate;
            case WireFormat::DoP:       return deviceRate * 2.0;  // bytes per channel per second
            case WireFormat::NativeDsd: return deviceRate / 8.0;
        }
        return deviceRate;
    }
};

PlaybackPlan makePlan (const StreamFormat& source, const DeviceCapabilities& caps, const EngineSettings& settings);

} // namespace nx
