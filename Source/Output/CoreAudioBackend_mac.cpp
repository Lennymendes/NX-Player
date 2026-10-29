// NX Player — MIT License
// Native CoreAudio output for macOS (Merging Anubis / RAVENNA VAD, RME, any DAC).
//
//  - Hog Mode taken before anything else (5 tries, 50 ms apart); DoP never falls back to shared mode.
//  - our own IOProc writes the engine's samples straight into the driver's buffers, no JUCE in between:
//    an integer stream format when the device offers one (physical AND virtual, read back), otherwise the
//    driver's own Float32 format (MT VAD exposes only Float32) with k / 2^31 values, exact for the 24 bits
//    of a DoP word. Mixing is disabled.
//  - DoP: every channel of the stream carries DoP (unused channels get DoP silence with the same marker), the
//    carrier runs 100 ms before a reconfiguration, and a session ends with 300 ms of PCM silence so the VAD
//    ("Follow DoP detection") goes back to PCM before the device is released.
//  - the nominal rate is never restored; only the stream formats and the mixing flag we changed are.
#include "JuceDeviceBackend.h"
#include "../Core/AudioLog.h"

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
        if (pcmLead) pcmTail.store (true);              // PCM silence first (see openNative)
        const auto ioBefore = ioCount.load();
        // The driver may still be settling after a rate / format change: 3 tries, 100 ms apart.
        OSStatus err = noErr;
        for (int attempt = 0; attempt < 3 && ! running; ++attempt)
        {
            if (attempt > 0) juce::Thread::sleep (100);
            err = AudioDeviceStart (dev, procId);
            running = err == noErr;
            audioLog ("CoreAudio : AudioDeviceStart essai " + juce::String (attempt + 1) + " -> " + juce::String ((int) err));
        }
        if (running && pcmLead)
        {
            // Wait for 30 buffers of the new clock (1.5 s at most), then hand over to the engine (DoP pre-roll).
            int waited = 0;
            while (waited < 1500 && ioCount.load() - ioBefore < 30) { juce::Thread::sleep (10); waited += 10; }
            audioLog ("CoreAudio : amorce PCM, " + juce::String ((juce::int64) (ioCount.load() - ioBefore)) + " buffers en " + juce::String (waited) + " ms, passage au DoP");
        }
        pcmLead = false;
        pcmTail.store (false);
        if (! running) callback.store (nullptr);
        return running;
    }

    void stop() override
    {
        if (useJuce) { juceBackend->stop(); return; }
        if (! running) { callback.store (nullptr); return; }
        // DoP: let the carrier run a moment longer so the DAC sees a clean end of stream, not a cut.
        if (config.wire == WireFormat::DoP && ! pcmTail.load()) juce::Thread::sleep (100);
        AudioDeviceStop (dev, procId);
        running = false;
        callback.store (nullptr);
    }

    void close() override
    {
        if (useJuce) { juceBackend->close(); useJuce = false; }
        if (procId != nullptr && dev != kAudioObjectUnknown && config.wire == WireFormat::DoP)
        {
            // End of session: plain PCM silence on every channel for 300 ms, so the VAD ("Follow DoP detection")
            // sees the DoP stream end and goes back to PCM, as after a cold start, before we let go of the device.
            // The engine usually stops the stream before closing: restart it just for this tail.
            audioLog ("CoreAudio : fin de session DoP, 300 ms de silence PCM pour que le VAD repasse en PCM");
            pcmTail.store (true);
            if (! running) running = AudioDeviceStart (dev, procId) == noErr;
            juce::Thread::sleep (300);
        }
        closeNative();
        pcmTail.store (false);
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

        audioLog ("CoreAudio : ouverture " + c.deviceName + " | " + formatRate (c.sampleRate) + " | " + juce::String (c.numChannels)
                  + " voies | tampon demande " + juce::String (c.bufferFrames) + (c.wire == WireFormat::DoP ? " | DoP" : " | PCM"));
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
        audioLog (juce::String ("CoreAudio : Hog Mode obtenu") + (ownerPid > 0 ? " (ancien proprietaire " + juce::String (ownerPid) + ")" : juce::String()));
        if (! saved) saveState();
        audioLog ("CoreAudio : etat d'origine sauvegarde, frequence " + formatRate (savedRate) + ", " + juce::String ((int) saved_streams.size()) + " flux de sortie");

        // 3. sample rate: written, then confirmed by the driver (1500 ms at most) before any stream exists
        Float64 rateBefore = 0;
        getProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rateBefore);
        coreaudio::setNominalRateAndWait (c.deviceName, c.sampleRate, 1500);
        Float64 rate = 0;
        getProp (dev, kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate);
        if (std::abs ((double) rate - c.sampleRate) > 0.5)
        {
            error = "le périphérique a refusé "_t + formatRate (c.sampleRate) + " (" + formatRate ((double) rate) + ")";
            closeNative();
            return Result::HardFail;
        }

        audioLog ("CoreAudio : frequence nominale confirmee " + formatRate ((double) rate));
        // 4. integer stream format, physical and virtual, read back
        AudioStreamBasicDescription fmt {};
        floatSamples = false;
        if (! applyIntegerFormat (c, fmt))
        {
            // MT VAD only exposes Float32 (8 channels, interleaved): write floats ourselves, exact for 24 bits.
            if (! useFloatFormat (fmt))
            {
                audioLog ("CoreAudio : ni entier ni Float32 exploitable, repli sur JUCE (rien n'est restaure, le Hog Mode est conserve)");
                abandonForFallback();
                return Result::SoftFail;
            }
            floatSamples = true;
        }
        bytesPerChannel = bytesPerChannelOf (fmt);
        validBits = floatSamples ? 24 : (int) fmt.mBitsPerChannel;
        alignedHigh = (fmt.mFormatFlags & kAudioFormatFlagIsAlignedHigh) != 0;
        if (bytesPerChannel < 2 || bytesPerChannel > 4) { audioLog ("CoreAudio : taille de mot inattendue " + juce::String (bytesPerChannel)); abandonForFallback(); return Result::SoftFail; }
        audioLog ("CoreAudio : ecriture " + (floatSamples ? juce::String ("Float32 (24 bits exacts)") : "entier " + juce::String (validBits) + " bits")
                  + ", " + juce::String (bytesPerChannel) + " octets par voie, " + juce::String ((int) fmt.mChannelsPerFrame) + " voies");

        // 5. no mixing between us and the driver (settable while hogged)
        {
            AudioObjectPropertyAddress mix { kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, kMain };
            Boolean settable = false;
            if (AudioObjectIsPropertySettable (dev, &mix, &settable) == noErr && settable)
            { setProp (dev, kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, (UInt32) 0); mixingTouched = true; }
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
            audioLog ("CoreAudio : AudioDeviceCreateIOProcID a echoue, repli sur JUCE");
            abandonForFallback();
            return Result::SoftFail;
        }

        audioLog ("CoreAudio : IOProc cree, tampon " + juce::String ((int) frames) + " trames");
        // status shown in the Signal Path
        UInt32 latency = 0, safety = 0;
        getProp (dev, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, latency);
        getProp (dev, kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, safety);
        status.open = true;
        status.backendName = getTypeName();
        status.deviceName = c.deviceName;
        status.sampleRate = (double) rate;
        status.deviceBits = floatSamples ? 0 : validBits;
        status.exactBits = floatSamples ? 24 : juce::jmin (validBits, 32);
        status.exclusive = true;
        status.osMixerBypassed = true;
        status.wire = c.wire;
        status.bufferFrames = (int) frames;
        status.outputLatencySeconds = (double) (latency + safety + frames) / juce::jmax (1.0, (double) rate);
        status.detail = "Hog Mode · "_t + (floatSamples ? juce::String ("Float32, 24 bits exacts (IOProc)")
                                                       : juce::String (validBits) + " bits entiers (IOProc)");
        if (c.wire == WireFormat::DoP) status.detail = "DoP · "_t + status.detail;
        // DoP right after a rate change, or after a PCM stream on this device: the VAD must finish its rate switch
        // before it can detect DoP and reconfigure itself in DSD; doing both at once stalls it (journal 13:16:35).
        // start() then sends plain PCM silence until the clock runs steadily, before the engine's DoP pre-roll.
        const bool rateChanged = std::abs ((double) rateBefore - c.sampleRate) > 0.5;
        pcmLead = c.wire == WireFormat::DoP && (rateChanged || hadPcmStream);
        if (pcmLead) audioLog (juce::String ("CoreAudio : amorce PCM prevue avant le DoP (") + (rateChanged ? "changement de frequence" : "flux PCM precedent") + ")");
        hadPcmStream = c.wire != WireFormat::DoP;
        return Result::Ok;
    }

    static juce::String describe (const AudioStreamBasicDescription& d)
    {
        return formatRate (d.mSampleRate) + " " + juce::String ((int) d.mBitsPerChannel) + " bits, " + juce::String ((int) d.mBytesPerFrame) + " octets/trame, "
               + juce::String ((int) d.mChannelsPerFrame) + " voies, "
               + ((d.mFormatFlags & kAudioFormatFlagIsFloat) != 0 ? "float" : (d.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0 ? "entier" : "autre")
               + ((d.mFormatFlags & kAudioFormatFlagIsBigEndian) != 0 ? " big-endian" : "")
               + ((d.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0 ? " non-entrelace" : " entrelace")
               + ((d.mFormatFlags & kAudioFormatFlagIsAlignedHigh) != 0 ? " aligne-haut" : "");
    }

    /** Picks the deepest integer format at that rate, sets it as physical AND virtual format, reads it back.
        Everything it finds and every reason for a refusal goes to the log (what MT VAD really offers). */
    bool applyIntegerFormat (const OutputConfig& c, AudioStreamBasicDescription& out)
    {
        juce::ignoreUnused (c);
        bool first = true;
        int streamIndex = 0;
        for (auto& s : saved_streams)
        {
            const auto label = "CoreAudio : flux " + juce::String (streamIndex++) + " ";
            audioLog (label + "format virtuel actuel : " + describe (s.virt));
            audioLog (label + "format physique actuel : " + describe (s.physical));
            const auto formats = getArray<AudioStreamRangedDescription> (s.id, kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal);
            int shown = 0;
            for (auto& f : formats)
                if (shown++ < 40)
                    audioLog (label + "physique disponible : " + describe (f.mFormat) + " (" + formatRate (f.mSampleRateRange.mMinimum) + " a " + formatRate (f.mSampleRateRange.mMaximum) + ")");
            if (formats.empty()) audioLog (label + "aucun format physique disponible (liste vide)");

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
            if (best == nullptr) { audioLog (label + "refus : aucun format entier a cette frequence"); return false; }

            AudioStreamBasicDescription want = *best;
            want.mSampleRate = config.sampleRate;
            formatsTouched = true;
            if (! setProp (s.id, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, want)) { audioLog (label + "refus : ecriture du format physique (" + describe (want) + ")"); return false; }
            if (! setProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, want)) { audioLog (label + "refus : ecriture du format virtuel entier (" + describe (want) + ")"); return false; }

            AudioStreamBasicDescription got {};
            if (! getProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, got)) { audioLog (label + "refus : relecture du format virtuel"); return false; }
            audioLog (label + "format virtuel relu : " + describe (got));
            if (! isUsableInteger (got) || std::abs (got.mSampleRate - config.sampleRate) > 0.5) { audioLog (label + "refus : le format relu n'est pas l'entier demande"); return false; }

            if (first) { out = got; first = false; }
            else if (bytesPerChannelOf (got) != bytesPerChannelOf (out) || got.mBitsPerChannel != out.mBitsPerChannel
                     || (got.mFormatFlags & kAudioFormatFlagIsAlignedHigh) != (out.mFormatFlags & kAudioFormatFlagIsAlignedHigh))
            { audioLog (label + "refus : flux de dispositions differentes"); return false; }
        }
        if (first) audioLog ("CoreAudio : aucun flux de sortie");
        return ! first;
    }

    static bool isFloat32 (const AudioStreamBasicDescription& d)
    {
        return d.mFormatID == kAudioFormatLinearPCM && (d.mFormatFlags & kAudioFormatFlagIsFloat) != 0
            && (d.mFormatFlags & kAudioFormatFlagIsBigEndian) == 0 && d.mBitsPerChannel == 32;
    }

    /** Keeps the driver's own Float32 virtual format (only its rate is aligned if needed): no conversion in the HAL. */
    bool useFloatFormat (AudioStreamBasicDescription& out)
    {
        bool first = true;
        int streamIndex = 0;
        for (auto& s : saved_streams)
        {
            const auto label = "CoreAudio : flux " + juce::String (streamIndex++) + " ";
            AudioStreamBasicDescription got {};
            if (! getProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, got)) { audioLog (label + "refus : lecture du format virtuel"); return false; }
            if (std::abs (got.mSampleRate - config.sampleRate) > 0.5 && isFloat32 (got))
            {
                AudioStreamBasicDescription want = got;
                want.mSampleRate = config.sampleRate;
                formatsTouched = true;
                setProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, want);
                getProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, got);
            }
            audioLog (label + "format virtuel retenu : " + describe (got));
            if (! isFloat32 (got) || std::abs (got.mSampleRate - config.sampleRate) > 0.5) { audioLog (label + "refus : pas en Float32 a la bonne frequence"); return false; }
            if (first) { out = got; first = false; }
            else if (bytesPerChannelOf (got) != bytesPerChannelOf (out)) { audioLog (label + "refus : flux de dispositions differentes"); return false; }
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

    /** Puts back what WE changed: stream formats and the mixing flag. The nominal rate is never touched: a rate
        change makes the VAD / Anubis renegotiate the network stream, it must only happen when the user plays at another rate. */
    void restoreState()
    {
        if (! saved || dev == kAudioObjectUnknown) return;
        if (formatsTouched)
            for (auto& s : saved_streams)
            {
                if (s.physical.mFormatID != 0) setProp (s.id, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, s.physical);
                if (s.virt.mFormatID != 0) setProp (s.id, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal, s.virt);
            }
        if (savedMixingValid && mixingTouched)
        {
            AudioObjectPropertyAddress mix { kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, kMain };
            Boolean settable = false;
            if (AudioObjectIsPropertySettable (dev, &mix, &settable) == noErr && settable)
                setProp (dev, kAudioDevicePropertySupportsMixing, kAudioObjectPropertyScopeGlobal, savedMixing);
        }
        formatsTouched = mixingTouched = false;
        saved = false;
        saved_streams.clear();
    }

    /** The JUCE path takes over right after: put the formats back, keep the Hog Mode and the rate as they are
        (releasing and re-taking them within milliseconds is what unsettles the driver). */
    void abandonForFallback()
    {
        releaseIoProc();
        restoreState();
        dev = kAudioObjectUnknown;
        hogged = false;
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
        audioLog ("CoreAudio : fermeture (formats remis, frequence laissee telle quelle)");
        restoreState();
        if (hogged)
        {
            juce::String e;
            // Hog Mode is addressed by device name in the shared helper; the name is the one we opened.
            coreaudio::setHogMode (config.deviceName, false, e);
        }
        hogged = false;
        hadPcmStream = false;
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
        ioCount.fetch_add (1, std::memory_order_relaxed);
        if (out == nullptr || out->mNumberBuffers == 0) return;
        auto* cb = callback.load();
        if (cb == nullptr)
        {
            for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
                if (out->mBuffers[b].mData != nullptr) std::memset (out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);
            return;
        }

        if (pcmTail.load())
        {
            for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
                if (out->mBuffers[b].mData != nullptr) std::memset (out->mBuffers[b].mData, 0, out->mBuffers[b].mDataByteSize);
            return;
        }

        int totalChannels = 0;
        for (UInt32 b = 0; b < out->mNumberBuffers; ++b) totalChannels += (int) out->mBuffers[b].mNumberChannels;
        const bool dop = config.wire == WireFormat::DoP;
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
                    // Channels the engine does not feed: DoP silence with the same marker in DoP (so the VAD sees one
                    // consistent DoP stream on all its channels), plain zero in PCM.
                    auto* dst = base + (size_t) done * stride + (size_t) k * (size_t) bytesPerChannel;
                    if (c < ch) store (dst, stride, ptrs[c], n, false);
                    else store (dst, stride, dop && ch > 0 ? ptrs[0] : nullptr, n, dop && ch > 0);
                }
                globalChannel += nch;
            }
            done += n;
        }
    }

    /** Writes n engine samples (left-justified int32) into the driver's layout (integer or Float32); null source = silence. */
    void store (uint8_t* dst, size_t stride, const int32_t* src, int n, bool dopIdleFromMarker) const noexcept
    {
        for (int i = 0; i < n; ++i, dst += stride)
        {
            int32_t v = src != nullptr ? src[i] : 0;
            if (dopIdleFromMarker) v = (int32_t) (((uint32_t) v & 0xFF000000u) | 0x00696900u);   // marker + 0x69 0x69
            if (floatSamples)
            {
                // k / 2^31 is exact in a float for 24 significant bits (a DoP word): the driver gets the same integer back
                const float f = (float) ((double) v * (1.0 / 2147483648.0));
                std::memcpy (dst, &f, 4);
                continue;
            }
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
    bool floatSamples = false;               // MT VAD: Float32 virtual format, written by us (exact for 24 bits)
    std::atomic<bool> pcmTail { false };     // PCM zeros on every channel: end of a DoP session, or lead-in before DoP
    std::atomic<juce::uint64> ioCount { 0 }; // IOProc calls (real-time thread), to see the clock run
    bool pcmLead = false, hadPcmStream = false;

    // state found at the first open, put back at close
    bool saved = false, savedMixingValid = false, formatsTouched = false, mixingTouched = false;
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
