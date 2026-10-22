// NX Player — MIT License (this file only; compiling it links the Steinberg ASIO SDK, see README)
// ASIO output with integer PCM written straight into the driver's buffers and native DSD
// (ASIOFuture kAsioSetIoFormat / kASIODSDFormat, 1-bit samples packed 8 per byte).
// Plain Win32 / C++ (no JUCE). ASIO allows one driver per process: this class is a singleton in use.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nxwin
{

class AsioDevice
{
public:
    using RenderIntFn = std::function<void (int32_t* const* channels, int numChannels, int numFrames)>;
    using RenderDsdFn = std::function<void (uint8_t* const* channels, int numChannels, int numBytes)>;
    using EventFn = std::function<void (const std::string&)>;

    struct Capabilities
    {
        std::vector<double> pcmRates;
        std::vector<double> dsdRates;     // empty when the driver has no DSD mode
        int outputs = 0;
        int sampleBits = 0;               // 16 / 24 / 32, 0 = float
    };

    static std::vector<std::string> getDriverNames();

    AsioDevice();
    ~AsioDevice();

    /** Loads the driver to read its capabilities (closes it again unless it is the open one). */
    Capabilities probe (const std::string& driverName);

    bool open (const std::string& driverName, double rate, int numChannels, bool dsd, int bufferFrames, std::string& error);
    bool start (RenderIntFn renderInt, RenderDsdFn renderDsd, EventFn onEvent);
    void stop();
    void close();

    bool isOpen() const noexcept { return driverLoaded && buffersCreated; }
    bool isDsd() const noexcept { return dsdMode; }
    int getBufferFrames() const noexcept { return bufferSize; }
    double getSampleRate() const noexcept { return currentRate; }
    int getSampleBits() const noexcept { return sampleBits; }
    bool isFloatFormat() const noexcept { return floatFormat; }
    double getLatencySeconds() const noexcept { return latencySeconds; }
    std::string getFormatDescription() const;

    // ASIO callbacks (static trampolines)
    void bufferSwitch (long index) noexcept;
    long asioMessage (long selector, long value) noexcept;

private:
    bool loadDriver (const std::string& name, std::string& error);
    void unloadDriver();

    std::string driverName;
    bool driverLoaded = false, buffersCreated = false, dsdMode = false, floatFormat = false;
    int channels = 0, bufferSize = 0, sampleBits = 24;
    long sampleType = 0;
    double currentRate = 0.0, latencySeconds = 0.0;
    std::vector<void*> bufferPtrs;        // [channel * 2 + half]
    std::vector<int32_t> planar;
    std::vector<int32_t*> planarPtrs;
    std::vector<uint8_t> dsdPlanar;
    std::vector<uint8_t*> dsdPtrs;
    RenderIntFn renderInt;
    RenderDsdFn renderDsd;
    EventFn onEvent;
    std::atomic<bool> running { false };
    bool postOutput = false;
};

} // namespace nxwin
