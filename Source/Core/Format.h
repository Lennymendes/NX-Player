// NX Player — MIT License
// Stream formats shared by decoders, the engine, the output backends and the UI.
#pragma once

#include <juce_core/juce_core.h>
#include "Text.h"
#include <cmath>
#include <cstdint>

namespace nx
{

/** What a decoder delivers. */
enum class SampleKind
{
    PcmInt,     // integer PCM: delivered as int32 left-justified (bit-exact)
    PcmFloat,   // floating-point PCM (32f / 64f): delivered as double
    Dsd         // 1-bit DSD: delivered as bytes, MSB = first sample in time
};

struct StreamFormat
{
    SampleKind kind = SampleKind::PcmInt;
    double sampleRate = 44100.0;  // PCM rate, or the DSD bit rate (2 822 400 for DSD64)
    int bitsPerSample = 16;       // 8..32 for integer PCM, 32 / 64 for float, 1 for DSD
    int numChannels = 2;
    bool lossy = false;           // MP3 / AAC / Vorbis / Opus and every stream that went through one
    juce::String codec;           // "FLAC", "WAV", "RF64", "AIFF", "ALAC", "DSF", "DFF", "MP3"...

    bool isDsd() const noexcept    { return kind == SampleKind::Dsd; }
    int dsdMultiple() const noexcept { return isDsd() ? (int) std::lround (sampleRate / 44100.0) : 0; } // 64, 128, 256, 512
    bool operator== (const StreamFormat& o) const noexcept
    {
        return kind == o.kind && sampleRate == o.sampleRate && bitsPerSample == o.bitsPerSample
            && numChannels == o.numChannels;
    }
    bool operator!= (const StreamFormat& o) const noexcept { return ! operator== (o); }

    juce::String describe() const;   // "FLAC 24 bits / 192 kHz", "DSD128 (5,6448 MHz)"
};

/** What the engine hands to the output backend. */
enum class WireFormat
{
    Pcm,        // integer PCM, int32 left-justified; the backend writes it to the device without arithmetic
    DoP,        // DSD over PCM: 24-bit words (marker + 16 DSD bits) left-justified in int32
    NativeDsd   // raw DSD bytes per channel (ASIO DSD), MSB first
};

inline const char* toString (WireFormat w) noexcept
{
    switch (w)
    {
        case WireFormat::Pcm:       return "PCM";
        case WireFormat::DoP:       return "DoP";
        case WireFormat::NativeDsd: return "DSD natif";
    }
    return "?";
}

inline bool isFamily441 (double rate) noexcept { return std::fmod (rate, 11025.0) == 0.0; }

inline juce::String formatRate (double hz)
{
    if (hz >= 1.0e6)
        return juce::String (hz / 1.0e6, 4).replaceCharacter ('.', ',') + " MHz";
    const auto khz = hz / 1000.0;
    return (std::fmod (khz, 1.0) == 0.0 ? juce::String ((int) khz) : juce::String (khz, 1).replaceCharacter ('.', ','))
           + " kHz";
}

inline juce::String StreamFormat::describe() const
{
    if (isDsd())
        return "DSD" + juce::String (dsdMultiple()) + " (" + formatRate (sampleRate) + ")";
    const auto bits = kind == SampleKind::PcmFloat ? juce::String (bitsPerSample) + " bits flottant"
                                                   : juce::String (bitsPerSample) + " bits";
    return (codec.isNotEmpty() ? codec + " " : juce::String()) + bits + " / " + formatRate (sampleRate);
}

} // namespace nx
