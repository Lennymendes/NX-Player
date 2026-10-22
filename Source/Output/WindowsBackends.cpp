// NX Player — MIT License
// JUCE-side adapters for the native Windows outputs (Source/Output/win).
#include "WindowsBackends.h"

#if JUCE_WINDOWS
#include "win/WasapiExclusiveDevice.h"
#if NX_HAS_ASIO
 #include "win/AsioDevice.h"
#endif
#include <juce_events/juce_events.h>
#include <map>

namespace nx
{

namespace
{
    void postError (OutputBackend* b, const std::string& message)
    {
        auto msg = juce::String::fromUTF8 (message.c_str());
        std::weak_ptr<int> alive = b->lifeToken;
        juce::MessageManager::callAsync ([b, msg, alive]
        {
            if (! alive.expired() && b->onDeviceError) b->onDeviceError (msg);
        });
    }

//==============================================================================
class WasapiExclusiveBackend final : public OutputBackend
{
public:
    juce::String getTypeName() const override { return "WASAPI exclusif"; }

    juce::StringArray getDeviceNames() override
    {
        ids.clear();
        juce::StringArray names;
        for (auto& e : nxwin::enumerateRenderEndpoints())
        {
            auto n = juce::String::fromUTF8 (e.name.c_str());
            if (names.contains (n)) n << " (" << (names.size() + 1) << ")";
            names.add (n);
            ids[n] = e.id;
        }
        return names;
    }

    DeviceCapabilities probe (const juce::String& name) override
    {
        if (ids.empty()) getDeviceNames();
        DeviceCapabilities caps;
        const auto c = nxwin::probeExclusive (idFor (name), 2);
        for (auto r : c.sampleRates) caps.sampleRates.add (r);
        caps.maxBits = c.maxValidBits > 0 ? c.maxValidBits : 24;
        caps.exclusiveAvailable = ! c.sampleRates.empty();
        return caps;
    }

    bool open (const OutputConfig& c, juce::String& error) override
    {
        if (c.wire == WireFormat::NativeDsd) { error = "DSD natif : utilisez ASIO (ou DoP)"; return false; }
        if (ids.empty()) getDeviceNames();
        std::string err;
        if (! device.open (idFor (c.deviceName), c.sampleRate, c.numChannels, c.bufferFrames, err))
        {
            error = juce::String::fromUTF8 (err.c_str());
            return false;
        }
        status = {};
        status.open = true;
        status.backendName = getTypeName();
        status.deviceName = c.deviceName;
        status.sampleRate = device.getSampleRate();
        status.deviceBits = device.getValidBits();
        status.exactBits = device.getValidBits();
        status.exclusive = status.osMixerBypassed = true;
        status.wire = c.wire;
        status.bufferFrames = device.getBufferFrames();
        status.outputLatencySeconds = device.getLatencySeconds();
        status.detail = (c.wire == WireFormat::DoP ? juce::String ("DoP · "_t) : juce::String())
                        + "PCM entier " + juce::String (device.getValidBits()) + " bits"
                        + (device.getContainerBits() != device.getValidBits() ? " dans " + juce::String (device.getContainerBits()) : juce::String())
                        + " · exclusif"_t;
        return true;
    }

    bool start (RenderCallback* cb) override
    {
        return device.start ([cb] (int32_t* const* ch, int n, int f) { cb->renderInt (ch, n, f); },
                             [this] (const std::string& m) { postError (this, m); });
    }

    void stop() override  { device.stop(); }
    void close() override { device.close(); status.open = false; }
    OutputStatus getStatus() const override { return status; }

private:
    std::wstring idFor (const juce::String& name) const
    {
        auto it = ids.find (name);
        return it != ids.end() ? it->second : std::wstring();
    }

    nxwin::WasapiExclusiveDevice device;
    std::map<juce::String, std::wstring> ids;
    OutputStatus status;
};

#if NX_HAS_ASIO
//==============================================================================
class AsioBackend final : public OutputBackend
{
public:
    juce::String getTypeName() const override { return "ASIO"; }

    juce::StringArray getDeviceNames() override
    {
        juce::StringArray names;
        for (auto& n : nxwin::AsioDevice::getDriverNames()) names.add (juce::String (n.c_str()));
        return names;
    }

    DeviceCapabilities probe (const juce::String& name) override
    {
        DeviceCapabilities caps;
        const auto c = device.probe (name.toStdString());
        for (auto r : c.pcmRates) caps.sampleRates.add (r);
        for (auto r : c.dsdRates) caps.nativeDsdRates.add (r);
        caps.maxBits = c.sampleBits == 0 ? 24 : juce::jmax (16, c.sampleBits);
        caps.exclusiveAvailable = true;
        return caps;
    }

    bool open (const OutputConfig& c, juce::String& error) override
    {
        std::string err;
        const bool dsd = c.wire == WireFormat::NativeDsd;
        if (! device.open (c.deviceName.toStdString(), c.sampleRate, c.numChannels, dsd, c.bufferFrames, err))
        {
            error = juce::String::fromUTF8 (err.c_str());
            return false;
        }
        status = {};
        status.open = true;
        status.backendName = getTypeName();
        status.deviceName = c.deviceName;
        status.sampleRate = device.getSampleRate();
        status.deviceBits = device.getSampleBits();
        status.exactBits = device.isFloatFormat() ? 24 : device.getSampleBits();
        status.exclusive = true;
        status.osMixerBypassed = ! device.isFloatFormat();
        status.wire = c.wire;
        status.bufferFrames = device.getBufferFrames();
        status.outputLatencySeconds = device.getLatencySeconds();
        status.detail = (c.wire == WireFormat::DoP ? juce::String ("DoP · "_t) : juce::String())
                        + juce::String::fromUTF8 (device.getFormatDescription().c_str());
        return true;
    }

    bool start (RenderCallback* cb) override
    {
        return device.start ([cb] (int32_t* const* ch, int n, int f) { cb->renderInt (ch, n, f); },
                             [cb] (uint8_t* const* ch, int n, int b) { cb->renderDsd (ch, n, b); },
                             [this] (const std::string& m) { postError (this, m == "reset" ? std::string ("le pilote ASIO demande une réinitialisation") : m); });
    }

    void stop() override  { device.stop(); }
    void close() override { device.close(); status.open = false; }
    OutputStatus getStatus() const override { return status; }

private:
    nxwin::AsioDevice device;
    OutputStatus status;
};
#endif
} // namespace

std::vector<std::unique_ptr<OutputBackend>> createWindowsBackends()
{
    std::vector<std::unique_ptr<OutputBackend>> list;
   #if NX_HAS_ASIO
    list.push_back (std::make_unique<AsioBackend>());
   #endif
    list.push_back (std::make_unique<WasapiExclusiveBackend>());
    return list;
}

} // namespace nx
#endif
