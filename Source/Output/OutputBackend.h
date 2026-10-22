// NX Player — MIT License
// Output backends: the only code that talks to the audio hardware.
#pragma once

#include "../Core/Format.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

namespace nx
{

/** Called on the device's real-time thread. Must not lock, allocate or block. */
struct RenderCallback
{
    virtual ~RenderCallback() = default;

    /** Integer PCM or DoP: one int32 per sample, left-justified (the valid bits are the MSBs). */
    virtual void renderInt (int32_t* const* channels, int numChannels, int numFrames) noexcept = 0;

    /** Native DSD: numBytes bytes per channel, MSB = first bit in time. */
    virtual void renderDsd (uint8_t* const* channels, int numChannels, int numBytes) noexcept = 0;
};

struct OutputConfig
{
    juce::String deviceName;
    WireFormat wire = WireFormat::Pcm;
    double sampleRate = 44100.0;      // PCM / DoP word rate, or the DSD bit rate in native DSD
    int numChannels = 2;
    int sourceBits = 24;              // significant bits the engine will deliver (used to pick a device depth)
    int bufferFrames = 0;             // 0 = backend default
    bool exclusive = true;            // hog mode / exclusive mode when the backend supports it
};

/** What the backend really obtained, shown in the Signal Path. */
struct OutputStatus
{
    bool open = false;
    juce::String backendName;         // "WASAPI exclusif", "ASIO", "CoreAudio", "ALSA"...
    juce::String deviceName;
    double sampleRate = 0.0;
    int deviceBits = 0;               // integer word handed to the driver (16 / 24 / 32), 0 = float
    int exactBits = 0;                // bits guaranteed identical between the engine and the driver
    bool exclusive = false;           // nothing else can mix into this device
    bool osMixerBypassed = false;     // no OS mixer / resampler between us and the driver
    WireFormat wire = WireFormat::Pcm;
    int bufferFrames = 0;
    double outputLatencySeconds = 0.0;
    juce::String detail;              // "PCM 24 bits dans 32", "Hog Mode actif"...
};

struct DeviceCapabilities
{
    juce::Array<double> sampleRates;  // PCM rates the device accepts
    int maxBits = 24;
    juce::Array<double> nativeDsdRates; // empty if the backend has no native DSD
    bool exclusiveAvailable = false;
};

class OutputBackend
{
public:
    virtual ~OutputBackend() = default;

    virtual juce::String getTypeName() const = 0;
    virtual juce::StringArray getDeviceNames() = 0;
    virtual DeviceCapabilities probe (const juce::String& deviceName) = 0;

    /** Opens (or reopens with a new format) the device. Called on the message / control thread. */
    virtual bool open (const OutputConfig& config, juce::String& error) = 0;
    virtual bool start (RenderCallback* callback) = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
    virtual OutputStatus getStatus() const = 0;

    /** Set by the backend when the device asked for a reset (rate changed elsewhere, unplugged...). Message thread. */
    std::function<void (const juce::String& reason)> onDeviceError;

    /** Expires with the backend: lets asynchronous notifications check it is still alive. */
    std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
};

/** All backends compiled for this platform, best first (ASIO, WASAPI exclusive, CoreAudio, ALSA, JUCE fallbacks). */
std::vector<std::unique_ptr<OutputBackend>> createPlatformBackends();

} // namespace nx
