// NX Player — MIT License
#include "WasapiExclusiveDevice.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <ksmedia.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nxwin
{

namespace
{
    template <typename T> void safeRelease (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }

    struct ComScope
    {
        HRESULT hr;
        ComScope() : hr (CoInitializeEx (nullptr, COINIT_MULTITHREADED)) {}
        ~ComScope() { if (SUCCEEDED (hr)) CoUninitialize(); }
    };

    std::string utf8 (const wchar_t* w)
    {
        if (w == nullptr) return {};
        const int n = WideCharToMultiByte (CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s ((size_t) std::max (0, n - 1), '\0');
        if (n > 1) WideCharToMultiByte (CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    std::string hrText (const char* what, HRESULT hr)
    {
        char buf[160];
        std::snprintf (buf, sizeof (buf), "%s (HRESULT 0x%08lX)", what, (unsigned long) hr);
        return buf;
    }

    IMMDevice* openEndpoint (const std::wstring& id)
    {
        IMMDeviceEnumerator* en = nullptr;
        if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)))
            return nullptr;
        IMMDevice* dev = nullptr;
        if (id.empty()) en->GetDefaultAudioEndpoint (eRender, eConsole, &dev);
        else en->GetDevice (id.c_str(), &dev);
        en->Release();
        return dev;
    }

    WAVEFORMATEXTENSIBLE makeFormat (double rate, int channels, int container, int valid)
    {
        WAVEFORMATEXTENSIBLE f {};
        f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        f.Format.nChannels = (WORD) channels;
        f.Format.nSamplesPerSec = (DWORD) rate;
        f.Format.wBitsPerSample = (WORD) container;
        f.Format.nBlockAlign = (WORD) (channels * container / 8);
        f.Format.nAvgBytesPerSec = f.Format.nSamplesPerSec * f.Format.nBlockAlign;
        f.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
        f.Samples.wValidBitsPerSample = (WORD) valid;
        f.dwChannelMask = channels == 1 ? SPEAKER_FRONT_CENTER
                        : channels == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
                        : (DWORD) ((1u << channels) - 1u);
        f.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;   // integers only: no float path, ever
        return f;
    }

    struct Depth { int container, valid; };
    constexpr Depth kDepths[] = { { 32, 32 }, { 32, 24 }, { 24, 24 }, { 16, 16 } };
}

//==============================================================================
std::vector<EndpointInfo> enumerateRenderEndpoints()
{
    ComScope com;
    std::vector<EndpointInfo> list;
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)))
        return list;
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &col)))
    {
        UINT count = 0;
        col->GetCount (&count);
        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* dev = nullptr;
            if (FAILED (col->Item (i, &dev))) continue;
            LPWSTR id = nullptr;
            EndpointInfo info;
            if (SUCCEEDED (dev->GetId (&id))) { info.id = id; CoTaskMemFree (id); }
            IPropertyStore* props = nullptr;
            if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &props)))
            {
                PROPVARIANT v;
                PropVariantInit (&v);
                if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                    info.name = utf8 (v.pwszVal);
                PropVariantClear (&v);
                props->Release();
            }
            dev->Release();
            if (! info.id.empty()) list.push_back (std::move (info));
        }
        col->Release();
    }
    en->Release();
    return list;
}

ExclusiveCapabilities probeExclusive (const std::wstring& endpointId, int numChannels)
{
    ComScope com;
    ExclusiveCapabilities caps;
    IMMDevice* dev = openEndpoint (endpointId);
    if (dev == nullptr) return caps;
    IAudioClient* c = nullptr;
    if (SUCCEEDED (dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &c)))
    {
        for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0, 705600.0, 768000.0 })
        {
            for (auto d : kDepths)
            {
                auto f = makeFormat (r, numChannels, d.container, d.valid);
                if (c->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*) &f, nullptr) == S_OK)
                {
                    caps.sampleRates.push_back (r);
                    caps.maxValidBits = std::max (caps.maxValidBits, d.valid);
                    break;
                }
            }
        }
        c->Release();
    }
    dev->Release();
    return caps;
}

//==============================================================================
WasapiExclusiveDevice::WasapiExclusiveDevice() = default;
WasapiExclusiveDevice::~WasapiExclusiveDevice() { close(); }

bool WasapiExclusiveDevice::open (const std::wstring& endpointId, double sampleRate, int numChannels, int frames, std::string& error)
{
    close();
    ComScope com;
    IMMDevice* dev = openEndpoint (endpointId);
    if (dev == nullptr) { error = "périphérique introuvable"; return false; }

    IAudioClient* c = nullptr;
    HRESULT hr = dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &c);
    if (FAILED (hr)) { dev->Release(); error = hrText ("IAudioClient indisponible", hr); return false; }

    WAVEFORMATEXTENSIBLE fmt {};
    bool found = false;
    for (auto d : kDepths)
    {
        fmt = makeFormat (sampleRate, numChannels, d.container, d.valid);
        if (c->IsFormatSupported (AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*) &fmt, nullptr) == S_OK)
        {
            containerBits = d.container;
            validBits = d.valid;
            found = true;
            break;
        }
    }
    if (! found)
    {
        c->Release(); dev->Release();
        char buf[128];
        std::snprintf (buf, sizeof (buf), "le DAC refuse %.0f Hz en mode exclusif (formats entiers 16 à 32 bits)", sampleRate);
        error = buf;
        return false;
    }

    REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
    c->GetDevicePeriod (&defaultPeriod, &minPeriod);
    REFERENCE_TIME period = frames > 0 ? (REFERENCE_TIME) (1.0e7 * frames / sampleRate + 0.5) : defaultPeriod;
    period = std::max (period, minPeriod);

    hr = c->Initialize (AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, (WAVEFORMATEX*) &fmt, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
    {
        UINT32 aligned = 0;
        c->GetBufferSize (&aligned);
        c->Release();
        c = nullptr;
        period = (REFERENCE_TIME) (1.0e7 * aligned / sampleRate + 0.5);
        hr = dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &c);
        if (SUCCEEDED (hr))
            hr = c->Initialize (AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, (WAVEFORMATEX*) &fmt, nullptr);
    }
    dev->Release();
    if (FAILED (hr))
    {
        if (c != nullptr) c->Release();
        error = hr == AUDCLNT_E_DEVICE_IN_USE ? "le DAC est déjà utilisé en mode exclusif par une autre application"
                                              : hrText ("IAudioClient::Initialize (exclusif) a échoué", hr);
        return false;
    }

    HANDLE ev = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    c->SetEventHandle (ev);
    UINT32 bufFrames = 0;
    c->GetBufferSize (&bufFrames);
    IAudioRenderClient* rc = nullptr;
    hr = c->GetService (__uuidof (IAudioRenderClient), (void**) &rc);
    if (FAILED (hr))
    {
        c->Release();
        CloseHandle (ev);
        error = hrText ("IAudioRenderClient indisponible", hr);
        return false;
    }
    REFERENCE_TIME latency = 0;
    c->GetStreamLatency (&latency);

    client = c;
    renderClient = rc;
    event = ev;
    channels = numChannels;
    rate = sampleRate;
    bufferFrames = (int) bufFrames;
    latencySeconds = (double) latency / 1.0e7 + bufFrames / sampleRate;
    planar.assign ((size_t) channels * (size_t) bufferFrames, 0);
    planarPtrs.resize ((size_t) channels);
    for (int ch = 0; ch < channels; ++ch) planarPtrs[(size_t) ch] = planar.data() + (size_t) ch * (size_t) bufferFrames;
    return true;
}

bool WasapiExclusiveDevice::fill (unsigned frames)
{
    auto* rc = static_cast<IAudioRenderClient*> (renderClient);
    BYTE* data = nullptr;
    const HRESULT hr = rc->GetBuffer (frames, &data);
    if (FAILED (hr))
    {
        if (errorFn) errorFn (hrText ("GetBuffer", hr));
        return false;
    }

    if (renderFn) renderFn (planarPtrs.data(), channels, (int) frames);
    else std::fill (planar.begin(), planar.end(), 0);

    const int bytes = containerBits / 8;
    for (unsigned i = 0; i < frames; ++i)
        for (int ch = 0; ch < channels; ++ch)
        {
            const uint32_t v = (uint32_t) planarPtrs[(size_t) ch][i];
            BYTE* p = data + ((size_t) i * (size_t) channels + (size_t) ch) * (size_t) bytes;
            switch (bytes)
            {
                case 4: std::memcpy (p, &v, 4); break;                                  // MSB-aligned valid bits
                case 3: p[0] = (BYTE) (v >> 8); p[1] = (BYTE) (v >> 16); p[2] = (BYTE) (v >> 24); break;
                case 2: p[0] = (BYTE) (v >> 16); p[1] = (BYTE) (v >> 24); break;
                default: break;
            }
        }
    rc->ReleaseBuffer (frames, 0);
    return true;
}

bool WasapiExclusiveDevice::start (RenderFn render, ErrorFn onError)
{
    if (client == nullptr) return false;
    stop();
    renderFn = std::move (render);
    errorFn = std::move (onError);
    fill ((unsigned) bufferFrames);   // the first buffer must be full before Start()
    const HRESULT hr = static_cast<IAudioClient*> (client)->Start();
    if (FAILED (hr)) { if (errorFn) errorFn (hrText ("IAudioClient::Start", hr)); return false; }
    running = true;
    thread = std::thread ([this] { renderThread(); });
    return true;
}

void WasapiExclusiveDevice::renderThread()
{
    ComScope com;
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
    if (mmcss != nullptr) AvSetMmThreadPriority (mmcss, AVRT_PRIORITY_CRITICAL);

    while (running.load())
    {
        const DWORD r = WaitForSingleObject ((HANDLE) event, 2000);
        if (! running.load()) break;
        if (r != WAIT_OBJECT_0)
        {
            if (errorFn) errorFn ("le pilote WASAPI ne répond plus");
            break;
        }
        if (! fill ((unsigned) bufferFrames)) break;
    }

    if (mmcss != nullptr) AvRevertMmThreadCharacteristics (mmcss);
}

void WasapiExclusiveDevice::stop()
{
    if (running.exchange (false))
    {
        if (event != nullptr) SetEvent ((HANDLE) event);
        if (thread.joinable()) thread.join();
        if (client != nullptr) static_cast<IAudioClient*> (client)->Stop();
    }
    else if (thread.joinable())
    {
        thread.join();
    }
}

void WasapiExclusiveDevice::close()
{
    stop();
    auto* rc = static_cast<IAudioRenderClient*> (renderClient);
    auto* c = static_cast<IAudioClient*> (client);
    safeRelease (rc);
    if (c != nullptr) { c->Reset(); }
    safeRelease (c);
    renderClient = client = nullptr;
    if (event != nullptr) { CloseHandle ((HANDLE) event); event = nullptr; }
}

} // namespace nxwin
