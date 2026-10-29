// NX Player — MIT License
// A virtual DAC: records exactly what the engine sends. Used by PlayerSelfTest to prove the
// bit-perfect path, gapless joins and DoP framing, and usable as a silent output for benchmarking.
#pragma once

#include "OutputBackend.h"
#include <vector>

namespace nx
{

class CaptureBackend final : public OutputBackend
{
public:
    DeviceCapabilities capabilities;
    juce::String name = "Capture (DAC virtuel)";
    int bitsAccepted = 24;          // what this virtual DAC stores per word
    bool record = true;
    bool* renderFlag = nullptr;     // set to true while the engine renders (allocation tests)

    CaptureBackend()
    {
        capabilities.sampleRates = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0 };
        capabilities.maxBits = 24;
        capabilities.exclusiveAvailable = true;
    }

    juce::String getTypeName() const override { return "Capture"; }
    juce::StringArray getDeviceNames() override { return { name }; }
    DeviceCapabilities probe (const juce::String&) override { return capabilities; }

    bool open (const OutputConfig& c, juce::String& error) override
    {
        if (c.wire == WireFormat::NativeDsd ? ! capabilities.nativeDsdRates.contains (c.sampleRate)
                                            : ! capabilities.sampleRates.contains (c.sampleRate))
        {
            error = "fréquence refusée : "_t + juce::String (c.sampleRate);
            return false;
        }
        config = c;
        status = {};
        status.open = true;
        status.backendName = "Capture";
        status.deviceName = name;
        status.sampleRate = c.sampleRate;
        status.deviceBits = c.wire == WireFormat::NativeDsd ? 1 : bitsAccepted;
        status.exactBits = status.deviceBits;
        status.exclusive = status.osMixerBypassed = true;
        status.wire = c.wire;
        status.detail = juce::String (toString (c.wire)) + " · "_t + juce::String (status.deviceBits) + " bits";
        ++opens;
        return true;
    }

    bool start (RenderCallback* cb) override { callback = cb; return true; }
    bool isRealtime() const noexcept override { return false; }
    void stop() override { callback = nullptr; }
    void close() override { callback = nullptr; status.open = false; }
    OutputStatus getStatus() const override { return status; }

    /** Pulls numFrames (PCM / DoP frames, or DSD bytes per channel) from the engine. */
    void pump (int numFrames)
    {
        if (callback == nullptr) return;
        const int ch = config.numChannels;
        if (config.wire == WireFormat::NativeDsd)
        {
            dsdBuf.resize ((size_t) ch * (size_t) numFrames);
            std::vector<uint8_t*> ptrs;
            for (int c = 0; c < ch; ++c) ptrs.push_back (dsdBuf.data() + (size_t) c * numFrames);
            if (renderFlag) *renderFlag = true;
            callback->renderDsd (ptrs.data(), ch, numFrames);
            if (renderFlag) *renderFlag = false;
            if (record)
                for (int i = 0; i < numFrames; ++i)
                    for (int c = 0; c < ch; ++c) capturedDsd.push_back (ptrs[(size_t) c][i]);
            return;
        }
        intBuf.resize ((size_t) ch * (size_t) numFrames);
        std::vector<int32_t*> ptrs;
        for (int c = 0; c < ch; ++c) ptrs.push_back (intBuf.data() + (size_t) c * numFrames);
        if (renderFlag) *renderFlag = true;
        callback->renderInt (ptrs.data(), ch, numFrames);
        if (renderFlag) *renderFlag = false;
        if (record)
            for (int i = 0; i < numFrames; ++i)
                for (int c = 0; c < ch; ++c) capturedInt.push_back (ptrs[(size_t) c][i]);
    }

    bool isRunning() const noexcept { return callback != nullptr; }

    OutputConfig config;
    OutputStatus status;
    int opens = 0;
    std::vector<int32_t> capturedInt;   // interleaved
    std::vector<uint8_t> capturedDsd;   // interleaved by byte

private:
    RenderCallback* callback = nullptr;
    std::vector<int32_t> intBuf;
    std::vector<uint8_t> dsdBuf;
};

} // namespace nx
