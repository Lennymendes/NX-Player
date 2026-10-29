// NX Player — MIT License
// Native CoreAudio output for macOS (Merging Anubis / RAVENNA VAD, RME, any DAC).
//
//  - Hog Mode taken before anything else (5 tries, 50 ms apart); DoP never falls back to shared mode.
//  - the stream is switched to an INTEGER format (physical AND virtual), mixing is disabled: our own IOProc
//    writes the engine's integers straight into the driver's buffers. No float conversion, no JUCE in between,
//    so the 24 bits of a DoP word (marker + 16 DSD bits) reach the device untouched.
//  - the device state found at the first open (nominal rate, stream formats, mixing flag) is saved and put back
//    at close(), so a quick restart never meets what the previous session left behind.
//  - DoP: the carrier keeps running 100 ms before the IOProc is stopped (clean end of the stream).
//  - when no usable integer format exists the JUCE path is used instead (same behaviour as before).
#include "JuceDeviceBackend.h"

#if JUCE_MAC
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace nx
{

namespace
{
    constexpr AudioObjectPropertyElement kMain = 0;   // kAudioObjectPropertyElementMain (formerly ...Master)
    constexpr int kScratchFrames = 8192;              // <= the engine's block size
    constexpr int kMaxChannels = 32;

    template <typename T>
    bool getProp (AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope, T& out)
    {
        AudioObjectPropertyAddress a { sel, scope, kMain };
        UInt32 size = sizeof (T);
        return AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, &out) == noErr;
    }

    template <typename T>
    bool setProp (AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope, const T& value)
    {
        AudioObjectPropertyAddress a { sel, scope, kMain };
        return AudioObjectSetPropertyData (obj, &a, 0, nullptr, sizeof (T), &value) == noErr;
    }

    template <typename T>
    std::vector<T> getArray (AudioObjectID obj, AudioObjectPropertySelector sel, AudioObjectPropertyScope scope)
    {
        AudioObjectPropertyAddress a { sel, scope, kMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (obj, &a, 0, nullptr, &size) != noErr || size < sizeof (T)) return {};
        std::vector<T> v (size / sizeof (T));
        if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, v.data()) != noErr) return {};
        v.resize (size / sizeof (T));
        return v;
    }

    bool isUsableInteger (const AudioStreamBasicDescription& d)
    {
        return d.mFormatID == kAudioFormatLinearPCM
            && (d.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0
            && (d.mFormatFlags & kAudioFormatFlagIsFloat) == 0
            && (d.mFormatFlags & kAudioFormatFlagIsBigEndian) == 0
            && d.mBitsPerChannel >= 16 && d.mBitsPerChannel <= 32;
    }

    int bytesPerChannelOf (const AudioStreamBasicDescription& d)
    {
        if ((d.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0) return (int) d.mBytesPerFrame;
        return d.mChannelsPerFrame > 0 ? (int) (d.mBytesPerFrame / d.mChannelsPerFrame) : 0;
    }
}

class CoreAudioBackend final : public OutputBackend
{
public:
    explicit CoreAudioBackend (std::unique_ptr<juce::AudioIODeviceType> type)
        : juceBackend (std::make_unique<JuceDeviceBackend> (std::move (type)))
    {
        scratch.allocate ((size_t) kScratchFrames * (size_t) kMaxChannels, true);
    }

    ~CoreAudioBackend() override { close(); }

    juce::String getTypeName() const override { return juceBackend->getTypeName(); }
    juce::StringArray getDeviceNames() override { return juceBackend->getDeviceNames(); }
    DeviceCapabilities probe (const juce::String& name) override { return juceBackend->probe (name); }

    //==========================================================================
    bool open (const OutputConfig& c, juce::String& error) override
    {
        if (c.wire == WireFormat::NativeDsd)
        {
            error = "DSD natif indisponible avec CoreAudio (utilisez DoP)";
            return false;
        }
        if (useJuce) { juceBackend->close(); useJuce = false; }
        juceBackend->onDeviceError = onDeviceError;

        juce::String nativeError;
        switch (openNative (c, nativeError))
        {
            case Result::Ok:        return true;
            case Result::HardFail:  error = nativeError; return false;
            case Result::SoftFail:  break;
        }
        // No usable integer format / shared mode: the JUCE path (float, exact for 24 bits) as before.
        useJuce = true;
        if (juceBackend->open (c, error)) return true;
        useJuce = false;
        return false;
    }

    bool start (RenderCallback* cb) override
    {
        if (useJuce) return juceBackend->start (cb);
        if (dev == kAudioObjectUnknown || procId == nullptr) return false;
        callback.store (cb);
        running = AudioDeviceStart (dev, procId) == noErr;
        if (! running) callback.store (nullptr);
        return running;
    }

    void stop() override
    {
        if (useJuce) { juceBackend->stop(); return; }
        if (! running) { callback.store (nullptr); return; }
        // DoP: let the carrier run a moment longer so the DAC sees a clean end of stream, not a cut.
        if (config.wire == WireFormat::DoP) juce::Thread::sleep (100);
        AudioDeviceStop (dev, procId);
        running = false;
        callback.store (nullptr);
    }

    void close() override
    {
        if (useJuce) { juceBackend->close(); useJuce = false; }
        closeNative();
        status.open = false;
    }

    OutputStatus getStatus() const override { return useJuce ? juceBackend->getStatus() : status; }

private:
    enum class Result { Ok, SoftFail, HardFail };

    struct SavedStream { AudioStreamID id = 0; AudioStreamBasicDescription physical {}, virt {}; };

    //==========================================================================
    Result openNative (const OutputConfig& c, juce::String& error)
    {
        const auto newDev = (AudioObjectID) coreaudio::findDeviceId (c.deviceName);
        if (newDev == kAudioObjectUnknown) return Result::SoftFail;
        if (! c.exclusive) return Result::SoftFail;      // integer formats are only set on a device we own

        if (dev != newDev) closeNative();                // another device: put the old one back first
        else releaseIoProc();                            // same device: new format, keep Hog Mode and saved state
        dev = newDev;
        config = c;
        status = {};

        // 1. Hog Mode: 5 tries 50 ms apart (the kernel may still be releasing a dead owner's lock).
        juce::String hogError, ownerName;
        int ownerPid = -1;
        for (int attempt = 0; attempt < 5 && ! hogged; ++attempt)
        {
            if (attempt > 0) juce::Thread::sleep (50);
            hogged = coreaudio::setHogMode (c.deviceName, true, hogError, &ownerPid, &ownerName);
        }
        if (! hogged)
        {
            const bool dop = c.wire == WireFormat::DoP;
            closeNative();
            if (! dop) return Result::SoftFail;          // PCM: shared mode through the JUCE path stays possible
            // DoP through the system mixer would be played as PCM: full-scale noise. Never fall back to shared.
            error = "DoP impossible : le Hog Mode est refusé ("_t + hogError + ")"
                    + (ownerPid > 0 ? " — processus "_t + juce::String (ownerPid) + (ownerName.isNotEmpty() ? " (" + ownerName + ")" : juce::String()) : juce::String());
            return Result::HardFail;
        }

        // 2. what the device looks like now (put back at close)
        if (! saved) saveState();

        // 3. sample rate: written, then confirmed by the driver (1500 ms at most) before any stream exists
        coreaudio::setNominalRateAndWait (c.deviceName, c.sampleRate, 1500);
        Float64 rate = 0;
        getProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate);
        if (std::abs ((double) rate - c.sampleRate) > 0.5)
        {
            error = "le périphérique a refusé "_t + formatRate (c.sampleRate) + " (" + formatRate ((double) rate) + ")";
            closeNative();
            return Result::HardFail;
        }

        // 4. integer stream format, physical and virtual, read back
        AudioStreamBasicDescription fmt {};
        if (! applyIntegerFormat (c, fmt)) { closeNative(); return Result::SoftFail; }
        bytesPerChannel = bytesPerChannelOf (fmt);
        validBits = (int) fmt.mBitsPerChannel;
        alignedHigh = (fmt.mFormatFlags & kAudioFormatFlagIsAlignedHigh) != 0;
        if (bytesPerChannel < 2 || bytesPerChannel > 4) { closeNative(); return Result::SoftFail; }

        // 5. no mixing between us and the driver (settable while hogged)
        {
            AudioObjectPropertyAddress mix { kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, kMain };
            Boolean settable = false;
            if (AudioObjectIsPropertySettable (dev, &mix, &settable) == noErr && settable)
                setProp (dev, kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, (UInt32) 0);
        }

        // 6. buffer size (clamped to what the device accepts)
        UInt32 frames = 0;
        getProp (dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, frames);
        if (c.bufferFrames > 0)
        {
            AudioValueRange range { 0, 0 };
            UInt32 want = (UInt32) c.bufferFrames;
            if (getProp (dev, kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal, range) && range.mMaximum > 0)
                want = juce::jlimit ((UInt32) range.mMinimum, (UInt32) range.mMaximum, want);
            if (setProp (dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, want))
                frames = want;
            getProp (dev, kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, frames);
        }

        // 7. our own IOProc
        if (AudioDeviceCreateIOProcID (dev, &CoreAudioBackend::ioProc, this, &procId) != noErr || procId == nullptr)
        {
            procId = nullptr;
            closeNative();
            return Result::SoftFail;
        }

        // status shown in the Signal Path
        UInt32 latency = 0, safety = 0;
        getProp (dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, latency);
        getProp (dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, safety);
        status.open = true;
        status.backendName = getTypeName();
        status.deviceName = c.deviceName;
        status.sampleRate = (double) rate;
        status.deviceBits = validBits;
        status.exactBits = juce::jmin (validBits, 32);
        status.exclusive = true;
        status.osMixerBypassed = true;
        status.wire = c.wire;
        status.bufferFrames = (int) frames;
        status.outputLatencySeconds = (double) (latency + safety + frames) / juce::jmax (1.0, (double) rate);
        status.detail = "Hog Mode · "_t + juce::String (validBits) + " bits entiers (IOProc)";
        if (c.wire == WireFormat::DoP) status.detail = "DoP · "_t + status.detail;
        return Result::Ok;
    }

    /** Picks the deepest integer format at that rate, sets it as physical AND virtual format, reads it back. */
    bool applyIntegerFormat (const OutputConfig& c, AudioStreamBasicDescription& out)
    {
        juce::ignoreUnused (c);
        bool first = true;
        for (auto& s : saved_streams)
        {
            const auto formats = getArray<AudioStreamRangedDescription> (s.id, kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal);
            const AudioStreamBasicDescription* best = nullptr;
            for (auto& f : formats)
            {
                if (! isUsableInteger (f.mFormat)) continue;
                const bool rateOk = f.mSampleRateRange.mMinimum <= config.sampleRate + 0.5 && f.mSampleRateRange.mMaximum >= config.sampleRate - 0.5;
                if (! rateOk) continue;
                if (best == nullptr || f.mFormat.mBitsPerChannel > best->mBitsPerChannel
                    || (f.mFormat.mBitsPerChannel == best->mBitsPerChannel && f.mFormat.mChannelsPerFrame > best->mChannelsPerFrame))
                    best = &f.mFormat;
            }
            if (best == nullptr) return false;

            AudioStreamBasicDescription want = *best;
            want.mSampleRate = config.sampleRate;
            if (! setProp (s.id, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, want)) return false;
            if (! setProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, want)) return false;

            AudioStreamBasicDescription got {};
            if (! getProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, got)) return false;
            if (! isUsableInteger (got) || std::abs (got.mSampleRate - config.sampleRate) > 0.5) return false;

            if (first) { out = got; first = false; }
            else if (bytesPerChannelOf (got) != bytesPerChannelOf (out) || got.mBitsPerChannel != out.mBitsPerChannel
                     || (got.mFormatFlags & kAudioFormatFlagIsAlignedHigh) != (out.mFormatFlags & kAudioFormatFlagIsAlignedHigh))
                return false;   // streams with different layouts: leave it to the JUCE path
        }
        return ! first;
    }

    void saveState()
    {
        saved_streams.clear();
        for (auto id : getArray<AudioStreamID> (dev, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput))
        {
            SavedStream s;
            s.id = id;
            getProp (id, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, s.physical);
            getProp (id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, s.virt);
            saved_streams.push_back (s);
        }
        Float64 r = 0;
        savedRate = getProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, r) ? (double) r : 0.0;
        UInt32 mixing = 1;
        savedMixingValid = getProp (dev, kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, mixing);
        savedMixing = mixing;
        saved = true;
    }

    void restoreState()
    {
        if (! saved || dev == kAudioObjectUnknown) return;
        // formats first (they depend on the rate range), then mixing, then the rate itself
        for (auto& s : saved_streams)
        {
            if (s.physical.mFormatID != 0) setProp (s.id, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, s.physical);
            if (s.virt.mFormatID != 0) setProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, s.virt);
        }
        if (savedMixingValid)
        {
            AudioObjectPropertyAddress mix { kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, kMain };
            Boolean settable = false;
            if (AudioObjectIsPropertySettable (dev, &mix, &settable) == noErr && settable)
                setProp (dev, kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, savedMixing);
        }
        if (savedRate > 0.0)
        {
            Float64 now = 0;
            getProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, now);
            if (std::abs ((double) now - savedRate) > 0.5)
            {
                setProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, (Float64) savedRate);
                for (int waited = 0; waited < 500; waited += 20)   // let the driver finish before we let go
                {
                    juce::Thread::sleep (20);
                    getProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, now);
                    if (std::abs ((double) now - savedRate) < 0.5) break;
                }
            }
        }
        saved = false;
        saved_streams.clear();
    }

    void releaseIoProc()
    {
        stop();
        if (procId != nullptr && dev != kAudioObjectUnknown) AudioDeviceDestroyIOProcID (dev, procId);
        procId = nullptr;
    }

    /** Stops, then puts the device back as found and releases Hog Mode. */
    void closeNative()
    {
        if (dev == kAudioObjectUnknown) return;
        releaseIoProc();
        restoreState();
        if (hogged)
        {
            juce::String e;
            // Hog Mode is addressed by device name in the shared helper; the name is the one we opened.
            coreaudio::setHogMode (config.deviceName, false, e);
        }
        hogged = false;
        dev = kAudioObjectUnknown;
    }

    //==========================================================================
    static OSStatus ioProc (AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*,
                            AudioBufferList* out, const AudioTimeStamp*, void* self)
    {
        static_cast<CoreAudioBackend*> (self)->render (out);
        return noErr;
    }

    void render (AudioBufferList* out) noexcept
    {
        if (out == nullptr || out->mNumberBuffers == 0) return;
        auto* cb = callback.load();
        if (cb == nullptr)
        {
            for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
                if (out->mBuffers[b].mData != nullptr) std::memset (out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);
            return;
        }

        int totalChannels = 0;
        for (UInt32 b = 0; b < out->mNumberBuffers; ++b) totalChannels += (int) out->mBuffers[b].mNumberChannels;
        const auto& b0 = out->mBuffers[0];
        if (b0.mNumberChannels == 0 || b0.mData == nullptr) return;
        const int frames = (int) (b0.mDataByteSize / (b0.mNumberChannels * (UInt32) bytesPerChannel));
        const int ch = juce::jmin (config.numChannels, totalChannels, kMaxChannels);

        int32_t* ptrs[kMaxChannels];
        for (int done = 0; done < frames;)
        {
            const int n = juce::jmin (frames - done, kScratchFrames);
            for (int c = 0; c < ch; ++c) ptrs[c] = scratch.get() + (size_t) c * (size_t) kScratchFrames;
            cb->renderInt (ptrs, ch, n);

            int globalChannel = 0;
            for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
            {
                auto& buf = out->mBuffers[b];
                const int nch = (int) buf.mNumberChannels;
                auto* base = static_cast<uint8_t*> (buf.mData);
                if (base == nullptr || nch <= 0) { globalChannel += nch; continue; }
                const size_t stride = (size_t) nch * (size_t) bytesPerChannel;
                for (int k = 0; k < nch; ++k)
                {
                    const int c = globalChannel + k;
                    store (base + (size_t) done * stride + (size_t) k * (size_t) bytesPerChannel, stride, c < ch ? ptrs[c] : nullptr, n);
                }
                globalChannel += nch;
            }
            done += n;
        }
    }

    /** Writes n engine samples (left-justified int32) into the driver's integer layout; null source = silence. */
    void store (uint8_t* dst, size_t stride, const int32_t* src, int n) const noexcept
    {
        for (int i = 0; i < n; ++i, dst += stride)
        {
            const int32_t v = src != nullptr ? src[i] : 0;
            switch (bytesPerChannel)
            {
                case 4:
                {
                    // 32 valid bits, or 24 (or fewer) aligned high: the left-justified word as is; aligned low: shifted down
                    const int32_t w = (alignedHigh || validBits >= 32) ? v : (v >> (32 - validBits));
                    std::memcpy (dst, &w, 4);
                    break;
                }
                case 3:
                {
                    const int32_t w = v >> 8;
                    dst[0] = (uint8_t) (w & 0xFF);
                    dst[1] = (uint8_t) ((w >> 8) & 0xFF);
                    dst[2] = (uint8_t) ((w >> 16) & 0xFF);
                    break;
                }
                default:
                {
                    const int16_t w = (int16_t) (v >> 16);
                    std::memcpy (dst, &w, 2);
                    break;
                }
            }
        }
    }

    //==========================================================================
    std::unique_ptr<JuceDeviceBackend> juceBackend;   // device list, capabilities and the fallback path
    bool useJuce = false;

    OutputConfig config;
    OutputStatus status;
    AudioObjectID dev = kAudioObjectUnknown;
    AudioDeviceIOProcID procId = nullptr;
    bool hogged = false, running = false;
    std::atomic<RenderCallback*> callback { nullptr };
    juce::HeapBlock<int32_t> scratch;
    int bytesPerChannel = 4, validBits = 32;
    bool alignedHigh = true;

    // state found at the first open, put back at close
    bool saved = false, savedMixingValid = false;
    double savedRate = 0.0;
    UInt32 savedMixing = 1;
    std::vector<SavedStream> saved_streams;
};

std::unique_ptr<OutputBackend> createNativeCoreAudioBackend (std::unique_ptr<juce::AudioIODeviceType> coreAudioType)
{
    return std::make_unique<CoreAudioBackend> (std::move (coreAudioType));
}

} // namespace nx

#endif // JUCE_MAC
