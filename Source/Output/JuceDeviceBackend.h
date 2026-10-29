// NX Player — MIT License
// Output through a JUCE AudioIODeviceType: CoreAudio (macOS, with Hog Mode and an integer physical
// format), ALSA (Linux, hw: devices are exclusive) and WASAPI shared mode (Windows fallback, goes
// through the Windows mixer and is labelled as such).
//
// JUCE exchanges float buffers. Every integer sample is turned into the one float that the driver /
// HAL / JUCE maps back onto that very integer (ExactSampleEncoder), so the DAC still receives the
// source's bits.
#pragma once

#include "OutputBackend.h"
#include "../Core/ExactSampleEncoder.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>

namespace nx
{

class JuceDeviceBackend final : public OutputBackend,
                                private juce::AudioIODeviceCallback
{
public:
    /** typeName: "CoreAudio", "ALSA", "Windows Audio" (WASAPI shared), "DirectSound"... */
    explicit JuceDeviceBackend (std::unique_ptr<juce::AudioIODeviceType> type);
    ~JuceDeviceBackend() override;

    juce::String getTypeName() const override;
    juce::StringArray getDeviceNames() override;
    DeviceCapabilities probe (const juce::String& deviceName) override;
    bool open (const OutputConfig& config, juce::String& error) override;
    bool start (RenderCallback* callback) override;
    void stop() override;
    void close() override;
    OutputStatus getStatus() const override;

    static std::vector<std::unique_ptr<OutputBackend>> createAll();

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceError (const juce::String& message) override;

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String typeName;
    OutputConfig config;
    OutputStatus status;
    ExactSampleEncoder encoder;
    bool exactFloatPath = true;      // CoreAudio: k / 2^31 is exact for 24 significant bits
    bool hogged = false;
    juce::uint32 lastCloseMs = 0;    // when close() last released a device (CoreAudio re-open guard)
    std::atomic<RenderCallback*> callback { nullptr };
    juce::HeapBlock<int32_t> intBuffer;
    juce::HeapBlock<int32_t*> intPtrs;
    int allocatedFrames = 0, allocatedChannels = 0;
};

#if JUCE_MAC
/** CoreAudio extras that JUCE does not expose (implemented in CoreAudioExclusive_mac.cpp). */
namespace coreaudio
{
    /** On a refusal because another process owns the DAC, ownerPid / ownerName tell who. */
    bool setHogMode (const juce::String& deviceName, bool take, juce::String& error, int* ownerPid = nullptr, juce::String* ownerName = nullptr);
    /** Writes kAudioDevicePropertyNominalSampleRate and polls every 20 ms until the driver confirms it (or timeoutMs).
        No wait if the device already runs at that rate. Returns true when the rate is confirmed. */
    bool setNominalRateAndWait (const juce::String& deviceName, double sampleRate, int timeoutMs, bool forceTransition = false);
    /** forceTransition: when the device already runs at that rate (warm restart: the driver / Anubis kept the previous
        session's state and would not renegotiate), go through another rate first, confirmed, then back to the target. */
    /** Picks the deepest integer physical format at that rate and disables mixing. Returns its bit depth (0 = unchanged). */
    int setIntegerPhysicalFormat (const juce::String& deviceName, double sampleRate, int preferredBits, juce::String& error);
    juce::Array<double> getNominalSampleRates (const juce::String& deviceName);
    int getMaxPhysicalBits (const juce::String& deviceName);
}
#endif

} // namespace nx
