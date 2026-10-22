// NX Player — MIT License
// WASAPI exclusive mode, event driven, integer formats only: the samples are written to the
// endpoint buffer as integers, nothing converts them (no float, no Windows mixer, no resampler).
// Plain Win32 / C++ (no JUCE) so it can be compiled and checked on its own.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace nxwin
{

struct EndpointInfo
{
    std::wstring id;
    std::string name;     // UTF-8 friendly name
};

std::vector<EndpointInfo> enumerateRenderEndpoints();

struct ExclusiveCapabilities
{
    std::vector<double> sampleRates;   // rates accepted in exclusive mode with an integer format
    int maxValidBits = 0;
};

ExclusiveCapabilities probeExclusive (const std::wstring& endpointId, int numChannels);

class WasapiExclusiveDevice
{
public:
    /** Called on the render thread (MMCSS "Pro Audio"): fill numChannels planar int32 left-justified buffers. */
    using RenderFn = std::function<void (int32_t* const* channels, int numChannels, int numFrames)>;
    using ErrorFn = std::function<void (const std::string&)>;

    WasapiExclusiveDevice();
    ~WasapiExclusiveDevice();

    bool open (const std::wstring& endpointId, double sampleRate, int numChannels, int bufferFrames, std::string& error);
    bool start (RenderFn render, ErrorFn onError);
    void stop();
    void close();

    bool isOpen() const noexcept       { return client != nullptr; }
    int getContainerBits() const noexcept { return containerBits; }
    int getValidBits() const noexcept  { return validBits; }
    int getBufferFrames() const noexcept { return bufferFrames; }
    double getSampleRate() const noexcept { return rate; }
    double getLatencySeconds() const noexcept { return latencySeconds; }

private:
    void renderThread();
    bool fill (unsigned frames);

    void* client = nullptr;          // IAudioClient*
    void* renderClient = nullptr;    // IAudioRenderClient*
    void* event = nullptr;           // HANDLE
    int containerBits = 32, validBits = 24, channels = 2, bufferFrames = 0;
    double rate = 44100.0, latencySeconds = 0.0;
    std::vector<int32_t> planar;
    std::vector<int32_t*> planarPtrs;
    RenderFn renderFn;
    ErrorFn errorFn;
    std::thread thread;
    std::atomic<bool> running { false };
};

} // namespace nxwin
