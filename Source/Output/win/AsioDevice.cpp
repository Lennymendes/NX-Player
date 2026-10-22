// NX Player — MIT License (this file only; it links the Steinberg ASIO SDK)
#include "AsioDevice.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

extern AsioDrivers* asioDrivers;
bool loadAsioDriver (char* name);

namespace nxwin
{

namespace
{
    AsioDevice* current = nullptr;
    ASIOCallbacks callbacks {};

    void cbBufferSwitch (long index, ASIOBool) { if (current) current->bufferSwitch (index); }
    ASIOTime* cbBufferSwitchTimeInfo (ASIOTime* t, long index, ASIOBool) { if (current) current->bufferSwitch (index); return t; }
    void cbSampleRateChanged (ASIOSampleRate) { if (current) current->asioMessage (kAsioResetRequest, 0); }
    long cbAsioMessage (long selector, long value, void*, double*) { return current ? current->asioMessage (selector, value) : 0; }

    uint8_t reverseBits (uint8_t b) noexcept
    {
        b = (uint8_t) ((b & 0xF0) >> 4 | (b & 0x0F) << 4);
        b = (uint8_t) ((b & 0xCC) >> 2 | (b & 0x33) << 2);
        return (uint8_t) ((b & 0xAA) >> 1 | (b & 0x55) << 1);
    }

    bool setIoFormat (ASIOIoFormatType type)
    {
        ASIOIoFormat f {};
        f.FormatType = type;
        return ASIOFuture (kAsioSetIoFormat, &f) == ASE_SUCCESS;
    }

    bool canDoIoFormat (ASIOIoFormatType type)
    {
        ASIOIoFormat f {};
        f.FormatType = type;
        return ASIOFuture (kAsioCanDoIoFormat, &f) == ASE_SUCCESS;
    }
}

std::vector<std::string> AsioDevice::getDriverNames()
{
    std::vector<std::string> names;
    if (asioDrivers == nullptr) asioDrivers = new AsioDrivers();
    char storage[32][64] {};
    char* ptrs[32];
    for (int i = 0; i < 32; ++i) ptrs[i] = storage[i];
    const long n = asioDrivers->getDriverNames (ptrs, 32);
    for (long i = 0; i < n; ++i) names.emplace_back (ptrs[i]);
    return names;
}

AsioDevice::AsioDevice() = default;
AsioDevice::~AsioDevice() { close(); unloadDriver(); }

bool AsioDevice::loadDriver (const std::string& name, std::string& error)
{
    if (driverLoaded && name == driverName) return true;
    unloadDriver();
    if (asioDrivers == nullptr) asioDrivers = new AsioDrivers();
    std::vector<char> buf (name.begin(), name.end());
    buf.push_back (0);
    if (! loadAsioDriver (buf.data())) { error = "pilote ASIO introuvable : " + name; return false; }
    ASIODriverInfo info {};
    info.asioVersion = 2;
    info.sysRef = GetDesktopWindow();
    if (ASIOInit (&info) != ASE_OK)
    {
        error = std::string ("ASIOInit a échoué : ") + info.errorMessage;
        asioDrivers->removeCurrentDriver();
        return false;
    }
    driverName = name;
    driverLoaded = true;
    current = this;
    return true;
}

void AsioDevice::unloadDriver()
{
    if (! driverLoaded) return;
    if (dsdMode) setIoFormat (kASIOPCMFormat);
    dsdMode = false;
    ASIOExit();
    if (asioDrivers != nullptr) asioDrivers->removeCurrentDriver();
    driverLoaded = false;
    if (current == this) current = nullptr;
}

AsioDevice::Capabilities AsioDevice::probe (const std::string& name)
{
    Capabilities caps;
    std::string err;
    const bool wasOpen = isOpen() && name == driverName;
    if (! wasOpen && ! loadDriver (name, err)) return caps;

    long ins = 0, outs = 0;
    ASIOGetChannels (&ins, &outs);
    caps.outputs = (int) outs;

    if (! wasOpen)
    {
        for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0, 705600.0, 768000.0 })
            if (ASIOCanSampleRate (r) == ASE_OK) caps.pcmRates.push_back (r);

        ASIOChannelInfo ci {};
        ci.channel = 0;
        ci.isInput = ASIOFalse;
        if (ASIOGetChannelInfo (&ci) == ASE_OK)
        {
            switch (ci.type)
            {
                case ASIOSTInt16LSB: case ASIOSTInt16MSB: caps.sampleBits = 16; break;
                case ASIOSTFloat32LSB: case ASIOSTFloat32MSB: case ASIOSTFloat64LSB: case ASIOSTFloat64MSB: caps.sampleBits = 0; break;
                case ASIOSTInt32LSB: case ASIOSTInt32MSB: caps.sampleBits = 32; break;
                default: caps.sampleBits = 24; break;
            }
        }

        if (canDoIoFormat (kASIODSDFormat) && setIoFormat (kASIODSDFormat))
        {
            for (double r : { 2822400.0, 5644800.0, 11289600.0, 22579200.0 })
                if (ASIOCanSampleRate (r) == ASE_OK) caps.dsdRates.push_back (r);
            setIoFormat (kASIOPCMFormat);
        }
    }
    return caps;
}

bool AsioDevice::open (const std::string& name, double rate, int numChannels, bool dsd, int frames, std::string& error)
{
    close();
    if (! loadDriver (name, error)) return false;

    if (dsd)
    {
        if (! setIoFormat (kASIODSDFormat)) { error = "le pilote ASIO n'a pas de mode DSD natif"; return false; }
        dsdMode = true;
    }
    else if (dsdMode)
    {
        setIoFormat (kASIOPCMFormat);
        dsdMode = false;
    }

    if (ASIOCanSampleRate (rate) != ASE_OK) { error = "fréquence refusée par le pilote ASIO"; return false; }
    ASIOSampleRate currentAsio = 0;
    ASIOGetSampleRate (&currentAsio);
    if (currentAsio != rate && ASIOSetSampleRate (rate) != ASE_OK) { error = "ASIOSetSampleRate a échoué"; return false; }
    currentRate = rate;

    long ins = 0, outs = 0;
    ASIOGetChannels (&ins, &outs);
    if (outs < numChannels) { error = "pas assez de sorties ASIO"; return false; }

    long minSize = 0, maxSize = 0, preferred = 0, granularity = 0;
    ASIOGetBufferSize (&minSize, &maxSize, &preferred, &granularity);
    long size = frames > 0 ? std::clamp ((long) frames, minSize, maxSize) : preferred;
    if (granularity > 0 && size % granularity != 0) size = preferred;

    std::vector<ASIOBufferInfo> infos ((size_t) numChannels);
    for (int c = 0; c < numChannels; ++c) { infos[(size_t) c].isInput = ASIOFalse; infos[(size_t) c].channelNum = c; }

    callbacks.bufferSwitch = &cbBufferSwitch;
    callbacks.sampleRateDidChange = &cbSampleRateChanged;
    callbacks.asioMessage = &cbAsioMessage;
    callbacks.bufferSwitchTimeInfo = &cbBufferSwitchTimeInfo;
    current = this;

    if (ASIOCreateBuffers (infos.data(), numChannels, size, &callbacks) != ASE_OK) { error = "ASIOCreateBuffers a échoué"; return false; }
    buffersCreated = true;

    ASIOChannelInfo ci {};
    ci.channel = 0;
    ci.isInput = ASIOFalse;
    ASIOGetChannelInfo (&ci);
    sampleType = ci.type;
    floatFormat = false;
    switch (sampleType)
    {
        case ASIOSTInt16LSB:   sampleBits = 16; break;
        case ASIOSTInt24LSB:   sampleBits = 24; break;
        case ASIOSTInt32LSB:   sampleBits = 32; break;
        case ASIOSTInt32LSB16: sampleBits = 16; break;
        case ASIOSTInt32LSB18: sampleBits = 18; break;
        case ASIOSTInt32LSB20: sampleBits = 20; break;
        case ASIOSTInt32LSB24: sampleBits = 24; break;
        case ASIOSTFloat32LSB: sampleBits = 24; floatFormat = true; break;
        case ASIOSTDSDInt8MSB1:
        case ASIOSTDSDInt8LSB1: sampleBits = 1; break;
        default:
            ASIODisposeBuffers();
            buffersCreated = false;
            error = "format d'échantillon ASIO non pris en charge (" + std::to_string (sampleType) + ")";
            return false;
    }
    if (dsd != (sampleBits == 1))
    {
        ASIODisposeBuffers();
        buffersCreated = false;
        error = "le pilote n'a pas basculé dans le mode demandé (PCM / DSD)";
        return false;
    }

    channels = numChannels;
    bufferSize = (int) size;
    bufferPtrs.assign ((size_t) channels * 2, nullptr);
    for (int c = 0; c < channels; ++c)
    {
        bufferPtrs[(size_t) c * 2] = infos[(size_t) c].buffers[0];
        bufferPtrs[(size_t) c * 2 + 1] = infos[(size_t) c].buffers[1];
    }

    const int dsdBytes = bufferSize / 8;   // in DSD mode the buffer size counts 1-bit samples
    planar.assign ((size_t) channels * (size_t) bufferSize, 0);
    planarPtrs.resize ((size_t) channels);
    dsdPlanar.assign ((size_t) channels * (size_t) std::max (1, dsdBytes), 0x69);
    dsdPtrs.resize ((size_t) channels);
    for (int c = 0; c < channels; ++c)
    {
        planarPtrs[(size_t) c] = planar.data() + (size_t) c * (size_t) bufferSize;
        dsdPtrs[(size_t) c] = dsdPlanar.data() + (size_t) c * (size_t) std::max (1, dsdBytes);
    }

    long inLat = 0, outLat = 0;
    ASIOGetLatencies (&inLat, &outLat);
    latencySeconds = (double) outLat / rate;
    postOutput = ASIOOutputReady() == ASE_OK;
    return true;
}

bool AsioDevice::start (RenderIntFn ri, RenderDsdFn rd, EventFn ev)
{
    if (! isOpen()) return false;
    renderInt = std::move (ri);
    renderDsd = std::move (rd);
    onEvent = std::move (ev);
    running = true;
    if (ASIOStart() != ASE_OK) { running = false; return false; }
    return true;
}

void AsioDevice::stop()
{
    if (running.exchange (false))
        ASIOStop();
}

void AsioDevice::close()
{
    stop();
    if (buffersCreated) ASIODisposeBuffers();
    buffersCreated = false;
}

std::string AsioDevice::getFormatDescription() const
{
    if (dsdMode) return sampleType == ASIOSTDSDInt8LSB1 ? "DSD natif (LSB1)" : "DSD natif (MSB1)";
    if (floatFormat) return "PCM flottant 32 bits (conversion par le pilote)";
    char buf[64];
    std::snprintf (buf, sizeof (buf), "PCM entier %d bits", sampleBits);
    return buf;
}

void AsioDevice::bufferSwitch (long index) noexcept
{
    const int half = (int) (index & 1);
    if (! running.load())
    {
        for (int c = 0; c < channels; ++c)
            std::memset (bufferPtrs[(size_t) c * 2 + half], dsdMode ? 0x69 : 0, dsdMode ? (size_t) bufferSize / 8 : (size_t) bufferSize * 4);
        return;
    }

    if (dsdMode)
    {
        const int bytes = bufferSize / 8;
        if (renderDsd) renderDsd (dsdPtrs.data(), channels, bytes);
        for (int c = 0; c < channels; ++c)
        {
            auto* out = static_cast<uint8_t*> (bufferPtrs[(size_t) c * 2 + half]);
            const uint8_t* in = dsdPtrs[(size_t) c];
            if (sampleType == ASIOSTDSDInt8LSB1) for (int i = 0; i < bytes; ++i) out[i] = reverseBits (in[i]);
            else std::memcpy (out, in, (size_t) bytes);
        }
    }
    else
    {
        if (renderInt) renderInt (planarPtrs.data(), channels, bufferSize);
        for (int c = 0; c < channels; ++c)
        {
            const int32_t* in = planarPtrs[(size_t) c];
            void* dst = bufferPtrs[(size_t) c * 2 + half];
            switch (sampleType)
            {
                case ASIOSTInt32LSB:
                    std::memcpy (dst, in, (size_t) bufferSize * 4);
                    break;
                case ASIOSTInt24LSB:
                {
                    auto* o = static_cast<uint8_t*> (dst);
                    for (int i = 0; i < bufferSize; ++i, o += 3)
                    {
                        const auto v = (uint32_t) in[i];
                        o[0] = (uint8_t) (v >> 8); o[1] = (uint8_t) (v >> 16); o[2] = (uint8_t) (v >> 24);
                    }
                    break;
                }
                case ASIOSTInt16LSB:
                {
                    auto* o = static_cast<int16_t*> (dst);
                    for (int i = 0; i < bufferSize; ++i) o[i] = (int16_t) (in[i] >> 16);
                    break;
                }
                case ASIOSTInt32LSB16: case ASIOSTInt32LSB18: case ASIOSTInt32LSB20: case ASIOSTInt32LSB24:
                {
                    auto* o = static_cast<int32_t*> (dst);
                    const int shift = 32 - sampleBits;   // right-aligned in the 32-bit word
                    for (int i = 0; i < bufferSize; ++i) o[i] = in[i] >> shift;
                    break;
                }
                case ASIOSTFloat32LSB:
                {
                    auto* o = static_cast<float*> (dst);
                    for (int i = 0; i < bufferSize; ++i) o[i] = (float) ((double) in[i] * (1.0 / 2147483648.0));
                    break;
                }
                default:
                    std::memset (dst, 0, (size_t) bufferSize * 4);
                    break;
            }
        }
    }
    if (postOutput) ASIOOutputReady();
}

long AsioDevice::asioMessage (long selector, long value) noexcept
{
    switch (selector)
    {
        case kAsioSelectorSupported:
            return (value == kAsioResetRequest || value == kAsioEngineVersion || value == kAsioResyncRequest
                    || value == kAsioLatenciesChanged || value == kAsioSupportsTimeInfo) ? 1 : 0;
        case kAsioEngineVersion:   return 2;
        case kAsioSupportsTimeInfo: return 0;
        case kAsioResetRequest:
            if (onEvent) onEvent ("reset");
            return 1;
        case kAsioResyncRequest:
        case kAsioLatenciesChanged:
            return 1;
        default:
            return 0;
    }
}

} // namespace nxwin
