// NX Player — MIT License
#include "JuceDeviceBackend.h"

namespace nx
{

JuceDeviceBackend::JuceDeviceBackend (std::unique_ptr<juce::AudioIODeviceType> t)
    : type (std::move (t))
{
    typeName = type->getTypeName();
    type->scanForDevices();
}

JuceDeviceBackend::~JuceDeviceBackend()
{
    close();
}

juce::String JuceDeviceBackend::getTypeName() const
{
    if (typeName == "Windows Audio") return "WASAPI partagé (mixeur Windows)"_t;
    return typeName;
}

juce::StringArray JuceDeviceBackend::getDeviceNames()
{
    type->scanForDevices();
    return type->getDeviceNames (false);
}

DeviceCapabilities JuceDeviceBackend::probe (const juce::String& name)
{
    DeviceCapabilities caps;
    std::unique_ptr<juce::AudioIODevice> d;
    juce::AudioIODevice* dev = device != nullptr && device->getName() == name ? device.get() : nullptr;
    if (dev == nullptr)
    {
        d.reset (type->createDevice (name, {}));
        dev = d.get();
    }
    if (dev == nullptr) return caps;

    caps.sampleRates = dev->getAvailableSampleRates();
    caps.maxBits = juce::jmax (24, dev->getCurrentBitDepth());
   #if JUCE_MAC
    if (typeName == "CoreAudio")
    {
        const auto rates = coreaudio::getNominalSampleRates (name);
        if (! rates.isEmpty()) caps.sampleRates = rates;
        const auto bits = coreaudio::getMaxPhysicalBits (name);
        if (bits > 0) caps.maxBits = bits;
        caps.exclusiveAvailable = true;
    }
   #endif
    if (typeName == "ALSA") caps.exclusiveAvailable = name.containsIgnoreCase ("hw:") || ! name.containsIgnoreCase ("default");
    // Through a float API, 24 bits is what can be guaranteed exact.
    caps.maxBits = juce::jmin (caps.maxBits, 24);
    return caps;
}

bool JuceDeviceBackend::open (const OutputConfig& c, juce::String& error)
{
    if (c.wire == WireFormat::NativeDsd)
    {
        error = "DSD natif indisponible avec " + typeName + " (utilisez DoP)";
        return false;
    }

    if (device != nullptr && device->getName() != c.deviceName)
        close();
    if (device == nullptr)
    {
        device.reset (type->createDevice (c.deviceName, {}));
        if (device == nullptr) { error = "périphérique introuvable : "_t + c.deviceName; return false; }
    }
    if (device->isOpen()) { device->close(); if (typeName == "CoreAudio") juce::Thread::sleep (50); }
    // A device closed a moment ago may still be purging its previous session (MT VAD / PTP): short guard.
    if (typeName == "CoreAudio" && lastCloseMs != 0)
    {
        const auto since = juce::Time::getMillisecondCounter() - lastCloseMs;
        if (since < 50) juce::Thread::sleep ((int) (50 - since));
    }

    config = c;
    status = {};

    const bool hogBefore = hogged;
    // A failed open must not leave the DAC hogged by a closed device (only if we just took it).
    auto releaseIfNew = [&]
    {
       #if JUCE_MAC
        if (hogged && ! hogBefore)
        {
            juce::String e;
            coreaudio::setHogMode (config.deviceName, false, e);
            hogged = false;
        }
       #else
        juce::ignoreUnused (hogBefore);
       #endif
    };

   #if JUCE_MAC
    if (typeName == "CoreAudio" && c.exclusive)
    {
        // Hog Mode before the stream; up to 5 tries 50 ms apart (the kernel may still be releasing a dead owner's lock).
        juce::String hogError, ownerName;
        int ownerPid = -1;
        for (int attempt = 0; attempt < 5 && ! hogged; ++attempt)
        {
            if (attempt > 0) juce::Thread::sleep (50);
            hogged = coreaudio::setHogMode (c.deviceName, true, hogError, &ownerPid, &ownerName);
        }
        if (! hogged && c.wire == WireFormat::DoP)
        {
            // DoP through the system mixer would be played as PCM: full-scale noise. Never fall back to shared.
            error = "DoP impossible : le Hog Mode est refusé ("_t + hogError + ")"
                    + (ownerPid > 0 ? " — processus "_t + juce::String (ownerPid) + (ownerName.isNotEmpty() ? " (" + ownerName + ")" : juce::String()) : juce::String());
            device.reset();
            return false;
        }
        if (hogged) coreaudio::setNominalRateAndWait (c.deviceName, c.sampleRate, 1500);
    }
   #endif

    juce::BigInteger outputs;
    outputs.setRange (0, c.numChannels, true);
    const int buffer = c.bufferFrames > 0 ? c.bufferFrames : device->getDefaultBufferSize();
    // CoreAudio: the driver may refuse while it tears the previous session down: up to 3 tries, 50 ms apart.
    juce::String err;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (attempt > 0) { device->close(); juce::Thread::sleep (50); }
        err = device->open ({}, outputs, c.sampleRate, buffer);
        if (err.isEmpty() || typeName != "CoreAudio") break;
    }
    if (err.isNotEmpty()) { error = err; releaseIfNew(); return false; }

    if (std::abs (device->getCurrentSampleRate() - c.sampleRate) > 0.5)
    {
        error = "le périphérique a refusé "_t + formatRate (c.sampleRate) + " (" + formatRate (device->getCurrentSampleRate()) + ")";
        device->close();
        releaseIfNew();
        return false;
    }
    if (device->getActiveOutputChannels().countNumberOfSetBits() < c.numChannels)
    {
        error = "pas assez de sorties (" + juce::String (c.numChannels) + " demandées)"_t;
        device->close();
        releaseIfNew();
        return false;
    }

    int bits = device->getCurrentBitDepth();
    status.exclusive = false;
    status.osMixerBypassed = false;
    exactFloatPath = false;

    if (typeName == "CoreAudio")
    {
       #if JUCE_MAC
        juce::String fmtError;
        const int physical = coreaudio::setIntegerPhysicalFormat (c.deviceName, c.sampleRate, juce::jmax (24, c.sourceBits), fmtError);
        if (physical > 0) bits = physical;
       #endif
        exactFloatPath = true;                        // HAL: int = f * 2^(B-1); k / 2^31 is exact
        status.exclusive = hogged;
        status.osMixerBypassed = hogged;
        status.exactBits = juce::jmin (bits, 24);
        status.detail = (hogged ? "Hog Mode · "_t : "Partagé · "_t) + juce::String (bits) + " bits entiers";
    }
    else if (typeName == "ALSA")
    {
        encoder = ExactSampleEncoder (bits >= 32 ? FloatToIntRule::TruncMaxMinus1 : FloatToIntRule::Scale2N, bits);
        const bool hw = c.deviceName.containsIgnoreCase ("hw:") || ! c.deviceName.containsIgnoreCase ("default");
        status.exclusive = hw;
        status.osMixerBypassed = hw;
        status.exactBits = juce::jmin (bits, 24);
        status.detail = juce::String (hw ? "ALSA hw (exclusif) · "_t : "ALSA (plug) · "_t) + juce::String (bits) + " bits";
    }
    else
    {
        exactFloatPath = true;
        status.exactBits = 0;                         // a mixer sits in between
        status.detail = "Mixeur du système : ni exclusif ni bit-perfect"_t;
    }

    status.open = true;
    status.backendName = getTypeName();
    status.deviceName = c.deviceName;
    status.sampleRate = device->getCurrentSampleRate();
    status.deviceBits = bits;
    status.wire = c.wire;
    status.bufferFrames = device->getCurrentBufferSizeSamples();
    status.outputLatencySeconds = (double) device->getOutputLatencyInSamples() / juce::jmax (1.0, status.sampleRate);
    if (c.wire == WireFormat::DoP) status.detail = "DoP · "_t + status.detail;
    return true;
}

bool JuceDeviceBackend::start (RenderCallback* cb)
{
    if (device == nullptr || ! device->isOpen()) return false;
    callback.store (cb);
    device->start (this);
    return device->isPlaying();
}

void JuceDeviceBackend::stop()
{
    if (device != nullptr && device->isPlaying()) device->stop();
    callback.store (nullptr);
}

void JuceDeviceBackend::close()
{
    stop();
    if (device != nullptr) device->close();
   #if JUCE_MAC
    if (hogged && device != nullptr)
    {
        juce::String e;
        coreaudio::setHogMode (device->getName(), false, e);
    }
   #endif
    hogged = false;
    device.reset();
    status.open = false;
    lastCloseMs = juce::Time::getMillisecondCounter();
}

OutputStatus JuceDeviceBackend::getStatus() const { return status; }

void JuceDeviceBackend::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    const int frames = juce::jmax (4096, d->getCurrentBufferSizeSamples() * 2);
    const int ch = juce::jmax (config.numChannels, d->getActiveOutputChannels().getHighestBit() + 1);
    if (frames > allocatedFrames || ch > allocatedChannels)
    {
        intBuffer.allocate ((size_t) frames * (size_t) ch, true);
        intPtrs.allocate ((size_t) ch, true);
        allocatedFrames = frames;
        allocatedChannels = ch;
    }
}

void JuceDeviceBackend::audioDeviceError (const juce::String& message)
{
    if (onDeviceError) onDeviceError (message);
}

void JuceDeviceBackend::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                          int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    auto* cb = callback.load();
    const int ch = juce::jmin (numOutputs, allocatedChannels);
    if (cb == nullptr || ch <= 0)
    {
        for (int c = 0; c < numOutputs; ++c) if (outputs[c] != nullptr) juce::FloatVectorOperations::clear (outputs[c], numSamples);
        return;
    }

    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (numSamples - done, allocatedFrames);
        for (int c = 0; c < ch; ++c) intPtrs[c] = intBuffer.get() + (size_t) c * (size_t) allocatedFrames;
        cb->renderInt (intPtrs.get(), ch, n);

        for (int c = 0; c < ch; ++c)
        {
            float* o = outputs[c];
            if (o == nullptr) continue;
            const int32_t* s = intPtrs[c];
            if (exactFloatPath)
                for (int i = 0; i < n; ++i) o[done + i] = (float) ((double) s[i] * (1.0 / 2147483648.0));
            else
                for (int i = 0; i < n; ++i) o[done + i] = encoder.encode (s[i]);
        }
        done += n;
    }
    for (int c = ch; c < numOutputs; ++c)
        if (outputs[c] != nullptr) juce::FloatVectorOperations::clear (outputs[c], numSamples);
}

std::vector<std::unique_ptr<OutputBackend>> JuceDeviceBackend::createAll()
{
    std::vector<std::unique_ptr<OutputBackend>> list;
   #if JUCE_MAC
    if (auto* t = juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio())
        list.push_back (std::make_unique<JuceDeviceBackend> (std::unique_ptr<juce::AudioIODeviceType> (t)));
   #endif
   #if JUCE_LINUX && JUCE_ALSA
    if (auto* t = juce::AudioIODeviceType::createAudioIODeviceType_ALSA())
        list.push_back (std::make_unique<JuceDeviceBackend> (std::unique_ptr<juce::AudioIODeviceType> (t)));
   #endif
   #if JUCE_WINDOWS && JUCE_WASAPI
    if (auto* t = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared))
        list.push_back (std::make_unique<JuceDeviceBackend> (std::unique_ptr<juce::AudioIODeviceType> (t)));
   #endif
    return list;
}

} // namespace nx
