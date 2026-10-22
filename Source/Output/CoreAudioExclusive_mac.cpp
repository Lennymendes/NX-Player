// NX Player — MIT License
// CoreAudio extras for bit-perfect output on macOS:
//  - Hog Mode (kAudioDevicePropertyHogMode): no other process can play to the DAC, no mixing
//  - integer physical format (kAudioStreamPropertyPhysicalFormat): the HAL turns our floats back into
//    the exact integers (k / 2^31 is exact for 24 significant bits) instead of mixing in float
//  - nominal sample rates of the device, for automatic rate switching
#include "JuceDeviceBackend.h"

#if JUCE_MAC
#include <CoreAudio/CoreAudio.h>
#include <unistd.h>
#include <vector>

namespace nx::coreaudio
{

namespace
{
    constexpr AudioObjectPropertyElement kMain = 0;   // kAudioObjectPropertyElementMain (formerly ...Master)

    AudioObjectID findDevice (const juce::String& name)
    {
        AudioObjectPropertyAddress addr { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr || size == 0)
            return kAudioObjectUnknown;
        std::vector<AudioObjectID> ids (size / sizeof (AudioObjectID));
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data()) != noErr)
            return kAudioObjectUnknown;

        // JUCE appends " (2)" to duplicate names: compare on the base name too.
        const auto baseName = name.upToLastOccurrenceOf (" (", false, false);
        AudioObjectID fallback = kAudioObjectUnknown;
        for (auto id : ids)
        {
            AudioObjectPropertyAddress na { kAudioDevicePropertyDeviceNameCFString, kAudioObjectPropertyScopeGlobal, kMain };
            CFStringRef cf = nullptr;
            UInt32 s = sizeof (cf);
            if (AudioObjectGetPropertyData (id, &na, 0, nullptr, &s, &cf) == noErr && cf != nullptr)
            {
                const auto n = juce::String::fromCFString (cf);
                CFRelease (cf);
                if (n == name) return id;
                if (n == baseName && fallback == kAudioObjectUnknown) fallback = id;
            }
        }
        return fallback;
    }

    std::vector<AudioStreamID> outputStreams (AudioObjectID dev)
    {
        AudioObjectPropertyAddress addr { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput, kMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (dev, &addr, 0, nullptr, &size) != noErr || size == 0) return {};
        std::vector<AudioStreamID> streams (size / sizeof (AudioStreamID));
        if (AudioObjectGetPropertyData (dev, &addr, 0, nullptr, &size, streams.data()) != noErr) return {};
        return streams;
    }

    std::vector<AudioStreamRangedDescription> physicalFormats (AudioStreamID stream)
    {
        AudioObjectPropertyAddress addr { kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal, kMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (stream, &addr, 0, nullptr, &size) != noErr || size == 0) return {};
        std::vector<AudioStreamRangedDescription> formats (size / sizeof (AudioStreamRangedDescription));
        if (AudioObjectGetPropertyData (stream, &addr, 0, nullptr, &size, formats.data()) != noErr) return {};
        return formats;
    }

    bool isInteger (const AudioStreamBasicDescription& d)
    {
        return d.mFormatID == kAudioFormatLinearPCM
            && (d.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0
            && (d.mFormatFlags & kAudioFormatFlagIsFloat) == 0;
    }
}

bool setHogMode (const juce::String& deviceName, bool take, juce::String& error)
{
    const auto dev = findDevice (deviceName);
    if (dev == kAudioObjectUnknown) { error = "périphérique CoreAudio introuvable"_t; return false; }

    AudioObjectPropertyAddress addr { kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal, kMain };
    pid_t owner = -1;
    UInt32 size = sizeof (owner);
    AudioObjectGetPropertyData (dev, &addr, 0, nullptr, &size, &owner);

    const pid_t me = getpid();
    if (take)
    {
        if (owner == me) return true;
        if (owner != -1) { error = "le DAC est déjà réservé par un autre logiciel (Hog Mode)"_t; return false; }
        pid_t wanted = me;
        if (AudioObjectSetPropertyData (dev, &addr, 0, nullptr, sizeof (wanted), &wanted) != noErr)
        {
            error = "Hog Mode refusé par le périphérique"_t;
            return false;
        }
        size = sizeof (owner);
        AudioObjectGetPropertyData (dev, &addr, 0, nullptr, &size, &owner);
        return owner == me;
    }

    if (owner != me) return true;
    pid_t release = -1;
    return AudioObjectSetPropertyData (dev, &addr, 0, nullptr, sizeof (release), &release) == noErr;
}

int setIntegerPhysicalFormat (const juce::String& deviceName, double sampleRate, int preferredBits, juce::String& error)
{
    const auto dev = findDevice (deviceName);
    if (dev == kAudioObjectUnknown) { error = "périphérique CoreAudio introuvable"_t; return 0; }

    int chosenBits = 0;
    for (auto stream : outputStreams (dev))
    {
        const AudioStreamBasicDescription* best = nullptr;
        const auto formats = physicalFormats (stream);
        for (auto& f : formats)
        {
            if (! isInteger (f.mFormat)) continue;
            const bool rateOk = f.mSampleRateRange.mMinimum <= sampleRate + 0.5 && f.mSampleRateRange.mMaximum >= sampleRate - 0.5;
            if (! rateOk || f.mFormat.mBitsPerChannel > 32) continue;
            if (best == nullptr || f.mFormat.mBitsPerChannel > best->mBitsPerChannel
                || (f.mFormat.mBitsPerChannel == best->mBitsPerChannel && f.mFormat.mChannelsPerFrame > best->mChannelsPerFrame))
                best = &f.mFormat;
        }
        if (best == nullptr) continue;

        AudioStreamBasicDescription desc = *best;
        desc.mSampleRate = sampleRate;
        AudioObjectPropertyAddress addr { kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, kMain };
        if (AudioObjectSetPropertyData (stream, &addr, 0, nullptr, sizeof (desc), &desc) == noErr)
            chosenBits = juce::jmax (chosenBits, (int) desc.mBitsPerChannel);
    }

    // With the device hogged, disable mixing so the HAL hands our stream to the driver untouched.
    AudioObjectPropertyAddress mix { kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, kMain };
    Boolean settable = false;
    if (AudioObjectIsPropertySettable (dev, &mix, &settable) == noErr && settable)
    {
        UInt32 off = 0;
        AudioObjectSetPropertyData (dev, &mix, 0, nullptr, sizeof (off), &off);
    }

    juce::ignoreUnused (preferredBits);
    return chosenBits;
}

juce::Array<double> getNominalSampleRates (const juce::String& deviceName)
{
    juce::Array<double> rates;
    const auto dev = findDevice (deviceName);
    if (dev == kAudioObjectUnknown) return rates;
    AudioObjectPropertyAddress addr { kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeGlobal, kMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (dev, &addr, 0, nullptr, &size) != noErr || size == 0) return rates;
    std::vector<AudioValueRange> ranges (size / sizeof (AudioValueRange));
    if (AudioObjectGetPropertyData (dev, &addr, 0, nullptr, &size, ranges.data()) != noErr) return rates;
    for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 352800.0, 384000.0, 705600.0, 768000.0 })
        for (auto& range : ranges)
            if (range.mMinimum <= r + 0.5 && range.mMaximum >= r - 0.5) { rates.addIfNotAlreadyThere (r); break; }
    return rates;
}

int getMaxPhysicalBits (const juce::String& deviceName)
{
    const auto dev = findDevice (deviceName);
    if (dev == kAudioObjectUnknown) return 0;
    int bits = 0;
    for (auto stream : outputStreams (dev))
        for (auto& f : physicalFormats (stream))
            if (isInteger (f.mFormat) && f.mFormat.mBitsPerChannel <= 32)
                bits = juce::jmax (bits, (int) f.mFormat.mBitsPerChannel);
    return bits;
}

} // namespace nx::coreaudio
#endif
