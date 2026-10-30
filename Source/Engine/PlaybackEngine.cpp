// NX Player — MIT License
#include "PlaybackEngine.h"
#include "../Audio/SacdIso.h"
#include "../Core/AudioLog.h"
#include "../Audio/MemoryPlayStream.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "../DSP/DsdToPcm.h"
#include "../DSP/Resampler.h"
#include "../Audio/LoopingDecoder.h"
#include <cmath>
#include <cstring>

namespace nx
{

namespace
{
    constexpr size_t kRingBytes = 8u << 20;      // 8 Mo: ~2,7 s of stereo DXD 384 kHz, ~3 s of DSD512
    constexpr int kChunkFrames = 8192;
    constexpr int kDopMinPrerollMs = 400;        // true DSD silence (0x05/0xFA markers + 0x69) before the first music
    constexpr int kDopStableCallbacks = 30;      // consecutive stable hardware buffers before the timeline starts
    constexpr uint8_t kDsdIdle = 0x69;           // DSD silence pattern

    inline int32_t doubleToInt32 (double x) noexcept
    {
        double v = std::floor (x * 2147483648.0 + 0.5);
        if (v > 2147483647.0) v = 2147483647.0;
        if (v < -2147483648.0) v = -2147483648.0;
        return (int32_t) (int64_t) v;
    }

    inline int popcount8 (uint8_t b) noexcept
    {
        b = (uint8_t) (b - ((b >> 1) & 0x55));
        b = (uint8_t) ((b & 0x33) + ((b >> 2) & 0x33));
        return (b + (b >> 4)) & 0x0F;
    }
}

PlaybackEngine::PlaybackEngine (std::unique_ptr<OutputBackend> b)
    : juce::Thread ("NX decoder"), backend (std::move (b)), ring (kRingBytes)
{
    planar.assign ((size_t) 8 * maxBlock, 0.0);
    for (int c = 0; c < 8; ++c) planarPtrs[(size_t) c] = planar.data() + (size_t) c * maxBlock;
    rtBytes.assign ((size_t) maxBlock * 2 * 8, 0);
    rtTap.assign ((size_t) maxBlock * 2 * 8, 0.0f);
    tmpInt.resize ((size_t) kChunkFrames * 8 * 2);
    tmpDouble.resize ((size_t) kChunkFrames * 8 * 8);
    tmpDouble2.resize ((size_t) kChunkFrames * 8 * 8);
    tmpBytes.resize ((size_t) kChunkFrames * 8 * 16);
    for (auto& r : requantizers) r.setBits (24);

    if (backend != nullptr && backend->getDeviceNames().size() > 0)
        setDevice (backend->getDeviceNames()[0]);

    startThread (juce::Thread::Priority::high);
    startTimerHz (60);
}

PlaybackEngine::~PlaybackEngine()
{
    stopTimer();
    signalThreadShouldExit();
    stopThread (4000);
    if (deviceRunning) backend->stop();
    backend->close();
    if (auto* p = pendingChain.exchange (nullptr)) juce::ignoreUnused (p);
}

//==============================================================================
void PlaybackEngine::setBackend (std::unique_ptr<OutputBackend> newBackend)
{
    stop();
    if (deviceRunning) backend->stop();
    backend->close();
    deviceRunning = false;
    backend = std::move (newBackend);
    activePlanId = 0;
    if (backend->getDeviceNames().size() > 0)
        setDevice (backend->getDeviceNames()[0]);
}

void PlaybackEngine::setDevice (const juce::String& name)
{
    const bool wasPlaying = state.load() == State::Playing || state.load() == State::Paused;
    const int index = currentIndex.load();
    const double pos = getPositionSeconds();
    if (deviceRunning) { backend->stop(); deviceRunning = false; }
    backend->close();
    activePlanId = 0;

    auto newCaps = backend->probe (name);
    {
        std::lock_guard<std::mutex> l (controlLock);
        deviceName = name;
        caps = newCaps;
    }
    if (wasPlaying && index >= 0)
        requestFlushStart (index, pos);   // replan for the new DAC
}

DeviceCapabilities PlaybackEngine::getCapabilities() const
{
    std::lock_guard<std::mutex> l (controlLock);
    return caps;
}

void PlaybackEngine::setSettings (const EngineSettings& s, bool replanCurrent)
{
    {
        std::lock_guard<std::mutex> l (controlLock);
        settings = s;
    }
    if (replanCurrent && state.load() != State::Stopped && currentIndex.load() >= 0)
        requestFlushStart (currentIndex.load(), getPositionSeconds());
}

EngineSettings PlaybackEngine::getSettings() const
{
    std::lock_guard<std::mutex> l (controlLock);
    return settings;
}

void PlaybackEngine::setLoop (double a, double b)
{
    if (b - a < 0.05 || currentIndex.load() < 0) return;
    loopA = juce::jmax (0.0, a);
    loopB = b;
    loopIndex = currentIndex.load();
    double pos = getPositionSeconds();
    if (pos < a || pos >= b) pos = a;
    // The ring already holds what follows the play position: re-read once, from here, through the loop.
    if (state.load() != State::Stopped) requestFlushStart (currentIndex.load(), pos);
}

void PlaybackEngine::clearLoop (bool rebuffer)
{
    const bool wasActive = loopIndex.load() >= 0;
    loopIndex = -1;
    loopA = -1.0;
    loopB = -1.0;
    if (rebuffer && wasActive && state.load() != State::Stopped && currentIndex.load() >= 0)
        requestFlushStart (currentIndex.load(), getPositionSeconds());
}

EngineSettings PlaybackEngine::getEffectiveSettings() const
{
    std::lock_guard<std::mutex> l (controlLock);
    return resolveProfile (settings, deviceName);
}

juce::String PlaybackEngine::getLastError() const
{
    std::lock_guard<std::mutex> l (controlLock);
    return lastError;
}

//==============================================================================
void PlaybackEngine::setQueue (std::vector<PlayableItem> items, int startIndex, bool startPlaying)
{
    queue = std::move (items);
    {
        std::lock_guard<std::mutex> l (controlLock);
        decoderQueue = queue;
    }
    if (queue.empty()) { stop(); return; }
    startIndex = juce::jlimit (0, (int) queue.size() - 1, startIndex);
    if (startPlaying) playIndex (startIndex);
    else currentIndex = startIndex;
}

void PlaybackEngine::appendToQueue (std::vector<PlayableItem> items)
{
    for (auto& i : items) queue.push_back (std::move (i));
    std::lock_guard<std::mutex> l (controlLock);
    decoderQueue = queue;
}

void PlaybackEngine::playIndex (int index, double startSeconds)
{
    if (index < 0 || index >= (int) queue.size()) return;
    state = State::Playing;
    requestFlushStart (index, startSeconds);
    if (onStateChanged) onStateChanged();
}

void PlaybackEngine::play()
{
    if (state.load() == State::Paused) { state = State::Playing; if (onStateChanged) onStateChanged(); return; }
    if (state.load() == State::Stopped && ! queue.empty())
        playIndex (juce::jmax (0, currentIndex.load()), 0.0);
}

void PlaybackEngine::pause()
{
    if (state.load() == State::Playing) { state = State::Paused; if (onStateChanged) onStateChanged(); }
}

void PlaybackEngine::togglePlayPause()
{
    if (state.load() == State::Playing) pause(); else play();
}

void PlaybackEngine::stop()
{
    state = State::Stopped;
    {
        std::lock_guard<std::mutex> l (controlLock);
        flushCommand = {};
    }
    stopDecoding = true;
    flushGen.fetch_add (1);                      // the audio thread drops everything
    reconfigurePlanId = 0;
    positionUnits = 0;
    if (keepsDopCarrier())
    {
        // DoP carrier keep-alive: the device stays open and keeps sending 0x05/0xFA + 0x69 silence, so the
        // DAC never loses its DSD lock between tracks or after a stop (released on device / backend change).
        if (onStateChanged) onStateChanged();
        return;
    }
    if (deviceRunning) { backend->stop(); deviceRunning = false; }
    backend->close();
    activePlanId = 0;
    if (onStateChanged) onStateChanged();
}

void PlaybackEngine::next()
{
    const int i = currentIndex.load() + 1;
    if (i < (int) queue.size()) playIndex (i);
}

void PlaybackEngine::previous()
{
    const int i = currentIndex.load();
    if (getPositionSeconds() > 3.0 || i <= 0) seek (0.0);
    else playIndex (i - 1);
}

void PlaybackEngine::seek (double seconds)
{
    const int i = currentIndex.load();
    if (i < 0 || i >= (int) queue.size()) return;
    if (state.load() == State::Stopped) state = State::Paused;
    requestFlushStart (i, juce::jmax (0.0, seconds));
}

void PlaybackEngine::requestFlushStart (int index, double seconds)
{
    const auto gen = flushGen.fetch_add (1) + 1;
    {
        std::lock_guard<std::mutex> l (controlLock);
        flushCommand = { true, index, seconds, gen };
    }
    stopDecoding = false;
    currentIndex = index;
    positionBase = seconds;
    positionUnits = 0;
}

double PlaybackEngine::getPositionSeconds() const noexcept
{
    const double ups = positionUnitsPerSecond.load();
    return positionBase.load() + (ups > 0 ? (double) positionUnits.load() / ups : 0.0);
}

double PlaybackEngine::getDurationSeconds() const
{
    const int i = currentIndex.load();
    if (i >= 0 && i < (int) queue.size()) return queue[(size_t) i].durationHint;
    return 0.0;
}

void PlaybackEngine::setVolume (double gain)
{
    volume = juce::jlimit (0.0, 1.0, gain);
    pathChangedFlag = true;
}

void PlaybackEngine::setFixedVolume (bool fixed)
{
    fixedVolume = fixed;
    pathChangedFlag = true;
}

void PlaybackEngine::setDspChain (std::unique_ptr<DspChain> chain)
{
    if (chain == nullptr) chain = std::make_unique<DspChain>();
    const auto status = backend->getStatus();
    if (status.open)
        chain->prepare (status.sampleRate, juce::jmax (1, getCurrentPlan().channels), maxBlock);
    auto* raw = chain.get();
    chainStore.push_back (std::move (chain));
    if (deviceRunning)
        pendingChain.store (raw);
    else
        activeChain = raw;
    pathChangedFlag = true;
}

PlaybackPlan PlaybackEngine::getCurrentPlan() const
{
    std::lock_guard<std::mutex> l (controlLock);
    auto it = plans.find (playingPlanId.load());
    if (it == plans.end()) it = plans.find (activePlanId.load());
    return it != plans.end() ? it->second : PlaybackPlan();
}

//==============================================================================
// Decoder thread
//==============================================================================
void PlaybackEngine::run()
{
    while (! threadShouldExit())
    {
        FlushCommand cmd;
        {
            std::lock_guard<std::mutex> l (controlLock);
            cmd = flushCommand;
            flushCommand.pending = false;
        }

        if (cmd.pending)
        {
            decoderGen = cmd.gen;
            decoder.reset();
            int index = cmd.index;
            bool opened = false;
            while (! opened && ! threadShouldExit())
            {
                size_t n;
                { std::lock_guard<std::mutex> l (controlLock); n = decoderQueue.size(); }
                if (index >= (int) n) break;
                opened = openItem (index, cmd.seconds, true);
                if (! opened) { ++index; cmd.seconds = 0.0; }
            }
            if (! opened)
            {
                const RingEvent end { RingEvent::End, bytesWritten, decoderGen, index, 0, 0.0 };
                events.write (&end, 1);
            }
        }

        if (stopDecoding.load())
            decoder.reset();
        decoderBusy = decoder != nullptr;

        if (decoder == nullptr || flushGen.load() != decoderGen)
        {
            wait (5);
            continue;
        }

        const auto writable = (int) std::min<size_t> (ring.writable(), 1u << 20);
        if (writable < 16384)
        {
            wait (3);
            continue;
        }

        if (loopDecoder != nullptr && loopDecoder->isAtWrap())
        {
            // The next byte written is the start of the loop again: mark it exactly.
            const auto f = loopDecoder->getFormat();
            const double unitRate = f.isDsd() ? f.sampleRate / 8.0 : f.sampleRate;
            const RingEvent pe { RingEvent::Position, bytesWritten, decoderGen, decIndex, decPlanId, (double) loopDecoder->getLoopA() / unitRate };
            events.write (&pe, 1);
            loopWrapsSeen = loopDecoder->getWrapCount() + 1;
        }
        const int produced = produce (writable);
        if (loopDecoder != nullptr && loopDecoder->getWrapCount() > loopWrapsSeen)   // wrapped without notice (B past the end)
        {
            // The loop wrapped inside this chunk: tell the audio thread where the clock restarts.
            loopWrapsSeen = loopDecoder->getWrapCount();
            const auto f = loopDecoder->getFormat();
            const double unitRate = f.isDsd() ? f.sampleRate / 8.0 : f.sampleRate;
            const RingEvent pe { RingEvent::Position, bytesWritten, decoderGen, decIndex, decPlanId, (double) loopDecoder->getPosition() / unitRate };
            events.write (&pe, 1);
        }
        if (produced > 0)
            continue;

        // End of this track: gapless move to the next one.
        bool moved = false;
        int index = decIndex + 1;
        while (! moved && ! threadShouldExit() && flushGen.load() == decoderGen)
        {
            size_t n;
            { std::lock_guard<std::mutex> l (controlLock); n = decoderQueue.size(); }
            if (index >= (int) n) break;
            moved = openItem (index, 0.0, false);
            if (! moved) ++index;
        }

        if (! moved && flushGen.load() == decoderGen)
        {
            drainConverters();
            if (decPlan.wire != WireFormat::Pcm && (bytesWritten / juce::jmax (1, decPlan.ringFrameBytes())) % 2 != 0)
            {
                std::array<uint8_t, 8> idle; idle.fill (kDsdIdle);   // keep DoP words whole
                writeAll (idle.data(), (size_t) decPlan.ringFrameBytes());
            }
            const RingEvent end { RingEvent::End, bytesWritten, decoderGen, index, 0, 0.0 };
            events.write (&end, 1);
            decoder.reset();
            decHavePlan = false;
        }
    }
}

bool PlaybackEngine::openItem (int index, double startSeconds, bool flush)
{
    PlayableItem item;
    DeviceCapabilities c;
    EngineSettings s;
    {
        std::lock_guard<std::mutex> l (controlLock);
        if (index < 0 || index >= (int) decoderQueue.size()) return false;
        item = decoderQueue[(size_t) index];
        c = caps;
        s = resolveProfile (settings, deviceName);
    }

    std::unique_ptr<juce::InputStream> stream;
    if (item.openStream)          stream = item.openStream();
    else if (streamResolver)      stream = streamResolver (item);
    bool inMemory = false;
    if (stream == nullptr && sacd::isVirtualUri (item.uri))
    {
        // a SACD image track: read from the disc image (a whole .iso is far too big for Memory Play)
        auto f = std::make_unique<juce::FileInputStream> (sacd::realFile (item.uri));
        if (f->openedOk()) stream = std::make_unique<juce::BufferedInputStream> (f.release(), 1 << 17, true);
    }
    if (stream == nullptr && juce::File::isAbsolutePath (item.uri) && ! sacd::isVirtualUri (item.uri))
    {
        // Memory Play: the file is copied into RAM (in the background, playback starts at once).
        if (s.memoryPlay)
            if (auto m = MemoryPlayStream::open (juce::File (item.uri), (juce::int64) juce::jmax (1, s.memoryPlayMaxMb) * 1024 * 1024))
            {
                stream = std::move (m);
                inMemory = true;
            }
        if (stream == nullptr)
        {
            auto f = std::make_unique<juce::FileInputStream> (juce::File (item.uri));
            if (f->openedOk()) stream = std::make_unique<juce::BufferedInputStream> (f.release(), 1 << 17, true);
        }
    }

    juce::String err;
    auto dec = stream != nullptr ? openDecoder (std::move (stream), item.uri, err) : nullptr;
    if (dec == nullptr)
    {
        std::lock_guard<std::mutex> l (controlLock);
        lastError = (item.title.isNotEmpty() ? item.title : item.uri) + " : " + (err.isNotEmpty() ? err : juce::String ("illisible"));
        errorFlag = true;
        return false;
    }

    auto plan = makePlan (dec->getFormat(), c, s);
    plan.channels = juce::jmin (plan.channels, juce::jlimit (1, 8, s.maxChannels));
    plan.inMemory = inMemory;
    plan.fileBytes = sacd::isVirtualUri (item.uri) ? 0 : juce::File::isAbsolutePath (item.uri) ? juce::File (item.uri).getSize() : 0;

    const bool sameWire = decHavePlan && plan.sameWire (decPlan);
    if (! flush && ! sameWire) drainConverters();

    const bool keepState = ! flush && sameWire && decPlan.conversion == plan.conversion
                           && decPlan.source.sampleRate == plan.source.sampleRate
                           && decPlan.conversionRate == plan.conversionRate;
    preparePlanConverters (plan, keepState);

    if (startSeconds > 0.0)
    {
        const auto f = dec->getFormat();
        const auto pos = (juce::int64) (startSeconds * (f.isDsd() ? f.sampleRate / 8.0 : f.sampleRate));
        dec->seek (pos);
    }

    const int planId = nextPlanId.fetch_add (1);
    PlanWire w;
    w.wire = plan.wire;
    w.rate = plan.deviceRate;
    w.channels = plan.channels;
    w.ringBits = plan.ringBitsHint;
    w.needsRequantize = plan.needsRequantize;
    w.unitsPerSecond = plan.ringUnitsPerSecond();
    w.valid = true;
    planWires[(size_t) (planId & 63)] = w;
    {
        std::lock_guard<std::mutex> l (controlLock);
        plans[planId] = plan;
        while (plans.size() > 32) plans.erase (plans.begin());
    }

    const RingEvent e { RingEvent::Track, bytesWritten, decoderGen, index, planId, startSeconds };
    events.write (&e, 1);

    // A-B loop on this track: wrap the decoder (positions in its units, the start is moved inside the loop).
    loopDecoder = nullptr;
    loopWrapsSeen = 0;
    if (loopIndex.load() == index && loopB.load() > loopA.load())
    {
        const auto f = dec->getFormat();
        const double unitRate = f.isDsd() ? f.sampleRate / 8.0 : f.sampleRate;
        auto looping = std::make_unique<LoopingDecoder> (std::move (dec), (juce::int64) (loopA.load() * unitRate), (juce::int64) (loopB.load() * unitRate),
                                                         (int) (loopCrossfadeMs * 0.001 * unitRate));
        const auto pos = (juce::int64) (startSeconds * unitRate);
        if (pos < looping->getLoopA() || startSeconds >= loopB.load()) looping->seek (looping->getLoopA());
        loopDecoder = looping.get();
        dec = std::move (looping);
    }
    decoder = std::move (dec);
    decIndex = index;
    decPlan = plan;
    decPlanId = planId;
    decHavePlan = true;

    if (flush && ! planWires[(size_t) (activePlanId.load() & 63)].same (w))
        reconfigurePlanId = planId;
    return true;
}

void PlaybackEngine::preparePlanConverters (const PlaybackPlan& plan, bool keepState)
{
    if (keepState) return;
    dsdConverter.reset();
    resampler.reset();
    const int ch = plan.source.numChannels;
    const auto eff = getEffectiveSettings();
    const bool minPhase = plan.filterShape == FilterShape::MinimumPhase;
    // Minimum phase ("Analog Warmth"): a gentler slope that starts below 20 kHz, fully attenuated at Nyquist.
    const double passFraction = minPhase ? 0.86 : 0.907;

    if (plan.conversion == PlaybackPlan::Conversion::DsdToPcm)
    {
        dsdConverter = std::make_unique<dsp::DsdToPcm>();
        dsdConverter->prepare (plan.source.sampleRate, plan.conversionRate, ch,
                               std::pow (10.0, eff.dsdToPcmGainDb / 20.0), minPhase);
        if (std::abs (dsdConverter->getOutputRate() - plan.deviceRate) > 0.5)
        {
            resampler = std::make_unique<dsp::Resampler>();
            resampler->prepare (dsdConverter->getOutputRate(), plan.deviceRate, ch, 160.0, passFraction, minPhase);
        }
    }
    else if (plan.conversion == PlaybackPlan::Conversion::Resample)
    {
        resampler = std::make_unique<dsp::Resampler>();
        resampler->prepare (plan.source.sampleRate, plan.deviceRate, ch, 160.0, passFraction, minPhase);
    }

    // Only converted streams are shaped: a bit-perfect plan never goes through this stage.
    const bool converted = plan.conversion == PlaybackPlan::Conversion::DsdToPcm || plan.conversion == PlaybackPlan::Conversion::Resample;
    shaper.prepare (plan.deviceRate, ch, converted && eff.warmth,
                    plan.conversion == PlaybackPlan::Conversion::DsdToPcm && eff.limiter && eff.dsdToPcmGainDb > 0.0);
}

void PlaybackEngine::writeAll (const void* data, size_t numBytes)
{
    auto* p = static_cast<const uint8_t*> (data);
    while (numBytes > 0 && ! threadShouldExit() && flushGen.load() == decoderGen)
    {
        const auto n = ring.write (p, numBytes);
        p += n;
        numBytes -= n;
        bytesWritten += (juce::int64) n;
        if (numBytes > 0) wait (2);
    }
}

int PlaybackEngine::produce (int writableBytes)
{
    const auto& plan = decPlan;
    const auto src = decoder->getFormat();
    const int srcCh = src.numChannels;
    const int outCh = plan.channels;
    const int frameBytes = plan.ringFrameBytes();
    const int maxOutFrames = juce::jmin (kChunkFrames, writableBytes / frameBytes - 4);
    if (maxOutFrames <= 0) return 1;

    auto expandAndWriteInt = [&] (const int32_t* in, int frames)
    {
        if (srcCh == outCh) { writeAll (in, (size_t) frames * (size_t) outCh * 4); return; }
        auto* out = reinterpret_cast<int32_t*> (tmpBytes.data());
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < outCh; ++c)
                out[(size_t) i * outCh + c] = in[(size_t) i * srcCh + juce::jmin (c, srcCh - 1)];
        writeAll (out, (size_t) frames * (size_t) outCh * 4);
    };

    auto doublesToRing = [&] (const double* in, int frames)
    {
        auto* out = tmpInt.data();
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < srcCh; ++c)
                out[(size_t) i * srcCh + c] = doubleToInt32 (in[(size_t) i * srcCh + c]);
        expandAndWriteInt (out, frames);
    };

    if (plan.wire != WireFormat::Pcm)
    {
        const int n = decoder->readDsd (tmpBytes.data() + (size_t) kChunkFrames * 8, maxOutFrames);
        if (n <= 0) return 0;
        const uint8_t* in = tmpBytes.data() + (size_t) kChunkFrames * 8;
        if (srcCh == outCh) writeAll (in, (size_t) n * (size_t) outCh);
        else
        {
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < outCh; ++c)
                    tmpBytes[(size_t) i * outCh + c] = in[(size_t) i * srcCh + juce::jmin (c, srcCh - 1)];
            writeAll (tmpBytes.data(), (size_t) n * (size_t) outCh);
        }
        return n;
    }

    switch (plan.conversion)
    {
        case PlaybackPlan::Conversion::None:
        {
            const int n = decoder->readInt (tmpInt.data(), maxOutFrames);
            if (n > 0) expandAndWriteInt (tmpInt.data(), n);
            return n;
        }

        case PlaybackPlan::Conversion::FloatToInt:
        {
            const int n = decoder->readDouble (tmpDouble.data(), maxOutFrames);
            if (n > 0) doublesToRing (tmpDouble.data(), n);
            return n;
        }

        case PlaybackPlan::Conversion::Resample:
        {
            const double ratio = resampler->getRatio();
            const int inFrames = juce::jmax (1, (int) std::floor ((maxOutFrames - 4) / ratio));
            int n = 0;
            if (src.kind == SampleKind::PcmInt)
            {
                n = decoder->readInt (tmpInt.data(), juce::jmin (inFrames, kChunkFrames));
                for (int i = 0; i < n * srcCh; ++i) tmpDouble[(size_t) i] = tmpInt[(size_t) i] * (1.0 / 2147483648.0);
            }
            else
            {
                n = decoder->readDouble (tmpDouble.data(), juce::jmin (inFrames, kChunkFrames));
            }
            if (n <= 0) return 0;
            const int out = resampler->process (tmpDouble.data(), n, tmpDouble2.data(), resampler->maxOutputFor (n));
            if (out > 0) { shaper.process (tmpDouble2.data(), out); doublesToRing (tmpDouble2.data(), out); }
            return n;
        }

        case PlaybackPlan::Conversion::DsdToPcm:
        {
            const double ratio = resampler != nullptr ? resampler->getRatio() : 1.0;
            const int pcmFrames = juce::jmax (1, (int) std::floor ((maxOutFrames - 4) / ratio) - 1);
            const int bytesIn = juce::jmin (pcmFrames * dsdConverter->getBytesPerOutputSample(), kChunkFrames * 8);
            const int n = decoder->readDsd (tmpBytes.data(), bytesIn);
            if (n <= 0) return 0;
            int pcm = dsdConverter->process (tmpBytes.data(), n, tmpDouble.data(), dsdConverter->maxOutputFor (n));
            if (resampler != nullptr)
            {
                const int out = resampler->process (tmpDouble.data(), pcm, tmpDouble2.data(), resampler->maxOutputFor (pcm));
                if (out > 0) { shaper.process (tmpDouble2.data(), out); doublesToRing (tmpDouble2.data(), out); }
            }
            else if (pcm > 0)
            {
                shaper.process (tmpDouble.data(), pcm);
                doublesToRing (tmpDouble.data(), pcm);
            }
            return n;
        }
    }
    return 0;
}

void PlaybackEngine::drainConverters()
{
    if (! decHavePlan || (dsdConverter == nullptr && resampler == nullptr)) return;
    const int srcCh = decPlan.source.numChannels;
    const int outCh = decPlan.channels;

    auto writeDoubles = [&] (const std::vector<double>& pcm, int frames)
    {
        std::vector<int32_t> ints ((size_t) frames * (size_t) outCh);
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < outCh; ++c)
                ints[(size_t) i * outCh + c] = doubleToInt32 (pcm[(size_t) i * srcCh + juce::jmin (c, srcCh - 1)]);
        writeAll (ints.data(), ints.size() * 4);
    };

    // Feed silence through the filters so the end of the music (their group delay) reaches the DAC.
    std::vector<double> pcm;
    int frames = 0;
    if (dsdConverter != nullptr)
    {
        const int bytes = dsdConverter->getNumTaps() / 8 + 8;
        std::vector<uint8_t> idle ((size_t) bytes * (size_t) srcCh, kDsdIdle);
        pcm.resize ((size_t) (dsdConverter->maxOutputFor (bytes) + 8) * (size_t) srcCh);
        frames = dsdConverter->process (idle.data(), bytes, pcm.data(), dsdConverter->maxOutputFor (bytes));
    }
    if (resampler != nullptr)
    {
        const int zeros = (int) std::ceil (resampler->getLatencyInputSamples()) + 2;
        std::vector<double> in ((size_t) (frames + zeros) * (size_t) srcCh, 0.0);
        std::copy (pcm.begin(), pcm.begin() + (std::ptrdiff_t) ((size_t) frames * (size_t) srcCh), in.begin());
        std::vector<double> out ((size_t) (resampler->maxOutputFor (frames + zeros) + 8) * (size_t) srcCh);
        frames = resampler->process (in.data(), frames + zeros, out.data(), resampler->maxOutputFor (frames + zeros));
        pcm.swap (out);
    }
    if (frames > 0) { shaper.process (pcm.data(), frames); writeDoubles (pcm, frames); }
    dsdConverter.reset();
    resampler.reset();
}

//==============================================================================
// Audio thread
//==============================================================================
bool PlaybackEngine::consumeEvents (const PlanWire& active) noexcept
{
    // 1. after a seek / new queue: drop stale events and bytes up to the new generation's first event
    const auto fg = flushGen.load (std::memory_order_acquire);
    if (fg != consumerGen)
    {
        RingEvent e;
        while (events.peek (&e, 1) == 1)
        {
            if (e.gen == fg)
            {
                const auto stale = e.startByte - consumedBytes;
                if (stale > 0) ring.discard ((size_t) stale);
                consumedBytes = e.startByte;
                consumerGen = fg;
                haveCurrent = false;
                prerolling = true;
                prerollFrames = 0;
                break;
            }
            events.discard (1);
        }
        if (consumerGen != fg) return false;
    }

    // 2. events that start here
    RingEvent e;
    while (events.peek (&e, 1) == 1 && e.gen == consumerGen && e.startByte <= consumedBytes)
    {
        if (e.kind == RingEvent::End)
        {
            events.discard (1);
            haveCurrent = false;
            endedFlag = true;
            return false;
        }
        if (e.kind == RingEvent::Position)
        {
            events.discard (1);
            positionBase.store (e.startSeconds);
            positionUnits.store (0);
            unitsIntoTrack = 0;
            continue;
        }
        const auto& w = planWires[(size_t) (e.planId & 63)];
        if (! w.same (active))
        {
            reconfigurePlanId.store (e.planId);
            return false;
        }
        events.discard (1);
        currentEvent = e;
        haveCurrent = true;
        unitsIntoTrack = 0;
        currentIndex.store (e.itemIndex);
        positionBase.store (e.startSeconds);
        positionUnits.store (0);
        positionUnitsPerSecond.store (w.unitsPerSecond);
        playingPlanId.store (e.planId);
        trackChangedFlag = true;
    }
    return haveCurrent;
}

bool PlaybackEngine::keepsDopCarrier() const noexcept
{
    if (! deviceRunning) return false;
    const auto& w = planWires[(size_t) (activePlanId.load() & 63)];
    return w.valid && w.wire == WireFormat::DoP;
}

bool PlaybackEngine::checkPreroll (size_t unitBytes, double unitsPerSecond, int requestedUnits) noexcept
{
    if (! prerolling) return true;
    // Wait for 150 ms of data (or the end of the queue, or 1 s at most) before starting after a seek.
    // Cold DoP start (device just opened): 800 ms of data, never more than half the ring.
    const double wantedSeconds = coldPreroll.load (std::memory_order_relaxed) ? 0.8 : 0.15;
    const auto wanted = std::min ((size_t) (wantedSeconds * unitsPerSecond) * unitBytes, ring.capacity() / 2);
    RingEvent pendingEvents[4];
    const auto numPending = events.peek (pendingEvents, 4);
    bool endQueued = false;
    for (size_t i = 0; i < numPending; ++i) endQueued |= pendingEvents[i].kind == RingEvent::End;
    // A forced minimum (DoP "Sync Safe" after a rate change) keeps sending DoP silence while the network
    // clock (PTP) locks, before the first byte of music.
    // The 30-stable-buffers gate never blocks forever: after 2 s past the minimum the music starts anyway.
    const bool minimumDone = prerollFrames >= prerollMinUnits
                             && (stableCallbacks >= requiredStableCallbacks
                                 || prerollFrames > prerollMinUnits + (juce::int64) (2.0 * unitsPerSecond));
    if (minimumDone && (ring.readable() >= wanted || endQueued || prerollFrames > (juce::int64) (2.0 * unitsPerSecond) + prerollMinUnits))
    {
        prerolling = false;
        prerollMinUnits = 0;
        coldPreroll.store (false, std::memory_order_relaxed);
        return true;
    }
    prerollFrames += requestedUnits;
    return false;
}

void PlaybackEngine::outputSilenceInt (int32_t* const* ch, int numCh, int n) noexcept
{
    const auto& active = planWires[(size_t) (activePlanId.load() & 63)];
    if (active.valid && active.wire == WireFormat::DoP)
    {
        for (int i = 0; i < n; ++i)
        {
            const auto w = dsp::DopPacker::word (dop.nextMarker(), kDsdIdle, kDsdIdle);
            for (int c = 0; c < numCh; ++c) ch[c][i] = w;
        }
        return;
    }
    for (int c = 0; c < numCh; ++c)
        std::memset (ch[c], 0, sizeof (int32_t) * (size_t) n);
}

void PlaybackEngine::renderInt (int32_t* const* out, int numCh, int numFrames) noexcept
{
    stats.callbacks.fetch_add (1, std::memory_order_relaxed);
    {
        const auto u = stats.underruns.load (std::memory_order_relaxed);
        if (numFrames == lastCallbackFrames && u == lastUnderruns) { if (stableCallbacks < 1000000) ++stableCallbacks; }
        else stableCallbacks = 0;
        lastCallbackFrames = numFrames;
        lastUnderruns = u;
    }

    if (auto* p = pendingChain.exchange (nullptr))
    {
        if (activeChain != nullptr) retiredChains.write (&activeChain, 1);
        activeChain = p;
    }

    const auto& active = planWires[(size_t) (activePlanId.load() & 63)];
    int done = 0;

    while (done < numFrames)
    {
        if (state.load() != State::Playing || reconfigurePlanId.load() != 0 || ! consumeEvents (active))
            break;

        const int ringCh = active.channels;
        const bool isDop = active.wire == WireFormat::DoP;
        const int unitBytes = isDop ? ringCh * 2 : ringCh * 4;   // DoP: two DSD bytes per channel per frame

        if (! checkPreroll ((size_t) unitBytes, active.rate, numFrames - done))
            break;

        juce::int64 limit = std::numeric_limits<juce::int64>::max();
        RingEvent next;
        if (events.peek (&next, 1) == 1 && next.gen == consumerGen) limit = next.startByte - consumedBytes;

        int frames = juce::jmin (numFrames - done, maxBlock);
        frames = (int) std::min<juce::int64> (frames, limit / unitBytes);
        frames = (int) std::min<juce::int64> (frames, (juce::int64) (ring.readable() / (size_t) unitBytes));
        if (frames <= 0)
        {
            if (limit > 0 && limit < unitBytes)   // odd tail before a boundary: skip it
            {
                ring.discard ((size_t) limit);
                consumedBytes += limit;
                continue;
            }
            if (limit != 0) stats.underruns.fetch_add (1, std::memory_order_relaxed);
            break;
        }

        const size_t bytes = (size_t) frames * (size_t) unitBytes;
        ring.read (rtBytes.data(), bytes);
        consumedBytes += (juce::int64) bytes;

        if (isDop)
        {
            const uint8_t* b = rtBytes.data();
            for (int i = 0; i < frames; ++i)
            {
                const uint8_t marker = dop.nextMarker();
                for (int c = 0; c < numCh; ++c)
                {
                    const int sc = juce::jmin (c, ringCh - 1);
                    out[c][done + i] = dsp::DopPacker::word (marker, b[(size_t) (2 * i) * ringCh + sc], b[(size_t) (2 * i + 1) * ringCh + sc]);
                }
            }
            // analyser: bit density of the first two channels
            for (int i = 0; i < frames * 2; ++i)
                for (int c = 0; c < 2; ++c)
                    rtTap[(size_t) i * 2 + c] = (float) (popcount8 (b[(size_t) i * ringCh + juce::jmin (c, ringCh - 1)]) - 4) * 0.25f;
            analyzer.isDsdDensity = true;
            analyzer.rate = active.rate * 2.0;
            analyzer.push (rtTap.data(), frames * 2);
            unitsIntoTrack += frames * 2;
        }
        else
        {
            const auto* src = reinterpret_cast<const int32_t*> (rtBytes.data());
            const double targetGain = isFixedVolume() ? 1.0 : volume.load();
            const bool dspOn = activeChain != nullptr && activeChain->isActive();
            const int exactBits = deviceExactBits.load();
            const bool process = active.needsRequantize || dspOn || targetGain != 1.0 || smoothedGain != 1.0
                                 || exactBits < active.ringBits;

            if (! process)
            {
                for (int c = 0; c < numCh; ++c)
                {
                    const int sc = juce::jmin (c, ringCh - 1);
                    for (int i = 0; i < frames; ++i) out[c][done + i] = src[(size_t) i * ringCh + sc];
                }
            }
            else
            {
                const int pc = juce::jmin (ringCh, 8);
                for (int c = 0; c < pc; ++c)
                    for (int i = 0; i < frames; ++i)
                        planarPtrs[(size_t) c][i] = src[(size_t) i * ringCh + c] * (1.0 / 2147483648.0);

                if (targetGain != 1.0 || smoothedGain != 1.0)
                {
                    const double coeff = 1.0 - std::exp (-1.0 / (0.010 * juce::jmax (1.0, active.rate)));
                    for (int i = 0; i < frames; ++i)
                    {
                        smoothedGain += (targetGain - smoothedGain) * coeff;
                        if (std::abs (smoothedGain - targetGain) < 1e-9) smoothedGain = targetGain;
                        for (int c = 0; c < pc; ++c) planarPtrs[(size_t) c][i] *= smoothedGain;
                    }
                }
                if (dspOn) activeChain->process (planarPtrs.data(), pc, frames);

                for (int c = 0; c < numCh; ++c)
                {
                    const int sc = juce::jmin (c, pc - 1);
                    auto& rq = requantizers[(size_t) sc];
                    for (int i = 0; i < frames; ++i) out[c][done + i] = rq.process (planarPtrs[(size_t) sc][i]);
                }
            }

            for (int i = 0; i < frames; ++i)
                for (int c = 0; c < 2; ++c)
                    rtTap[(size_t) i * 2 + c] = (float) (out[juce::jmin (c, numCh - 1)][done + i] * (1.0 / 2147483648.0));
            analyzer.isDsdDensity = false;
            analyzer.rate = active.rate;
            analyzer.push (rtTap.data(), frames);
            unitsIntoTrack += frames;
        }

        positionUnits.store (unitsIntoTrack, std::memory_order_relaxed);
        done += frames;
    }

    if (done < numFrames)
    {
        int32_t* rest[32];
        for (int c = 0; c < juce::jmin (numCh, 32); ++c) rest[c] = out[c] + done;
        outputSilenceInt (rest, juce::jmin (numCh, 32), numFrames - done);
    }
    stats.framesOut.fetch_add ((juce::uint64) numFrames, std::memory_order_relaxed);
}

void PlaybackEngine::renderDsd (uint8_t* const* out, int numCh, int numBytes) noexcept
{
    stats.callbacks.fetch_add (1, std::memory_order_relaxed);
    const auto& active = planWires[(size_t) (activePlanId.load() & 63)];
    int done = 0;

    while (done < numBytes)
    {
        if (state.load() != State::Playing || reconfigurePlanId.load() != 0 || ! consumeEvents (active))
            break;

        const int ringCh = active.channels;
        if (! checkPreroll ((size_t) ringCh, active.rate / 8.0, numBytes - done))
            break;
        juce::int64 limit = std::numeric_limits<juce::int64>::max();
        RingEvent next;
        if (events.peek (&next, 1) == 1 && next.gen == consumerGen) limit = next.startByte - consumedBytes;

        int n = juce::jmin (numBytes - done, maxBlock);
        n = (int) std::min<juce::int64> (n, limit / ringCh);
        n = (int) std::min<juce::int64> (n, (juce::int64) (ring.readable() / (size_t) ringCh));
        if (n <= 0)
        {
            if (limit != 0) stats.underruns.fetch_add (1, std::memory_order_relaxed);
            break;
        }

        ring.read (rtBytes.data(), (size_t) n * (size_t) ringCh);
        consumedBytes += (juce::int64) n * ringCh;
        for (int c = 0; c < numCh; ++c)
        {
            const int sc = juce::jmin (c, ringCh - 1);
            for (int i = 0; i < n; ++i) out[c][done + i] = rtBytes[(size_t) i * ringCh + sc];
        }
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
                rtTap[(size_t) i * 2 + c] = (float) (popcount8 (rtBytes[(size_t) i * ringCh + juce::jmin (c, ringCh - 1)]) - 4) * 0.25f;
        analyzer.isDsdDensity = true;
        analyzer.rate = active.rate / 8.0;
        analyzer.push (rtTap.data(), n);
        unitsIntoTrack += n;
        positionUnits.store (unitsIntoTrack, std::memory_order_relaxed);
        done += n;
    }

    for (int c = 0; c < numCh; ++c)
        std::memset (out[c] + done, kDsdIdle, (size_t) (numBytes - done));
    stats.framesOut.fetch_add ((juce::uint64) numBytes, std::memory_order_relaxed);
}

//==============================================================================
// Message thread
//==============================================================================
bool PlaybackEngine::configureDevice (int planId)
{
    PlaybackPlan plan;
    EngineSettings s;
    juce::String dev;
    {
        std::lock_guard<std::mutex> l (controlLock);
        auto it = plans.find (planId);
        if (it == plans.end()) return false;
        plan = it->second;
        s = resolveProfile (settings, deviceName);
        dev = deviceName;
    }

    if (deviceRunning) { backend->stop(); deviceRunning = false; }

    OutputConfig cfg;
    cfg.deviceName = dev;
    cfg.wire = plan.wire;
    cfg.sampleRate = plan.deviceRate;
    cfg.numChannels = plan.channels;
    cfg.sourceBits = plan.ringBitsHint;
    cfg.exclusive = s.exclusive;
    // A manual buffer (ms) always wins; "Automatique" gets 2048 frames for DoP and for Merging / Anubis / RAVENNA.
    cfg.bufferFrames = s.bufferMilliseconds > 0 ? (int) (s.bufferMilliseconds * plan.deviceRate / 1000.0)
                     : s.bufferFrames > 0 ? s.bufferFrames
                     : (plan.wire == WireFormat::DoP || isMergingDevice (dev)) ? 2048 : 0;

    juce::String err;
    if (! backend->open (cfg, err))
    {
        {
            std::lock_guard<std::mutex> l (controlLock);
            lastError = "Sortie audio : " + err;
        }
        int expected = planId;
        reconfigurePlanId.compare_exchange_strong (expected, 0);
        state = State::Stopped;
        if (onError) onError ("Sortie audio : " + err);
        if (onStateChanged) onStateChanged();
        return false;
    }

    const auto status = backend->getStatus();
    const int exact = plan.wire == WireFormat::Pcm ? juce::jlimit (8, 32, status.exactBits > 0 ? status.exactBits : 24) : 24;
    deviceExactBits = exact;
    for (auto& r : requantizers) r.setBits (exact);
    profileLocksVolume = s.lockVolume;
    smoothedGain = isFixedVolume() ? 1.0 : volume.load();

    // The device is stopped: the DSP chain can be (re)prepared and adopted directly.
    if (auto* p = pendingChain.exchange (nullptr)) activeChain = p;
    if (activeChain != nullptr) activeChain->prepare (status.sampleRate > 0 ? status.sampleRate : plan.deviceRate, plan.channels, maxBlock);

    dop.markerToggle = false;
    coldPreroll.store (false, std::memory_order_relaxed);
    stableCallbacks = 0;
    lastCallbackFrames = 0;
    lastUnderruns = stats.underruns.load();
    requiredStableCallbacks = 0;
    // The device is stopped: the audio-thread state can be set directly. DoP: real DSD silence first
    // (never PCM zeros), at least 400 ms, then wait for 30 stable hardware buffers.
    if (plan.wire == WireFormat::DoP)
    {
        prerolling = true;
        prerollFrames = 0;
        prerollMinUnits = (juce::int64) (juce::jmax (s.dopPrerollMs, kDopMinPrerollMs) * plan.deviceRate / 1000.0);
        requiredStableCallbacks = kDopStableCallbacks;
        coldPreroll.store (true, std::memory_order_relaxed);
    }
    activePlanId = planId;
    int expected = planId;
    reconfigurePlanId.compare_exchange_strong (expected, 0);
    stats.reconfigurations.fetch_add (1);

    deviceRunning = backend->start (this);
    pathChangedFlag = true;
    if (! deviceRunning)
    {
        audioLog ("Moteur : le peripherique n'a pas demarre (plan " + juce::String (planId) + ")");
        abortDeviceStart ("Sortie audio : le périphérique n'a pas démarré (journal : "_t + audioLogFile().getFullPathName() + ")");
        return false;
    }
    startedAtMs = juce::Time::getMillisecondCounter();
    callbacksAtStart = stats.callbacks.load();
    watchStage = 0; watchRetries = 0; watchConfirmed = false;
    audioLog ("Moteur : flux demarre, " + formatRate (plan.deviceRate) + (plan.wire == WireFormat::DoP ? " DoP" : "")
              + ", tampon " + juce::String (backend->getStatus().bufferFrames));
    return true;
}

/** The stream never started or the device never clocks it: say so (the timeline would otherwise sit at 0:00)
    and let go of the DAC. */
void PlaybackEngine::abortDeviceStart (const juce::String& why)
{
    {
        std::lock_guard<std::mutex> l (controlLock);
        lastError = why;
    }
    state = State::Stopped;
    if (deviceRunning) { backend->stop(); deviceRunning = false; }
    backend->close();
    activePlanId = 0;
    startedAtMs = 0;
    errorFlag = true;
    if (onStateChanged) onStateChanged();
}

/** After a start, wait for real callbacks (a hardware clock that really runs), log the first seconds, and restart
    the stream up to twice when nothing arrives: this replaces guessing with a measurement. */
void PlaybackEngine::watchDeviceStart()
{
    if (startedAtMs == 0) return;
    if (! deviceRunning || state.load() == State::Stopped) { startedAtMs = 0; return; }

    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsed = now - startedAtMs;
    const auto cbs = stats.callbacks.load() - callbacksAtStart;
    static constexpr juce::uint32 stages[] = { 500, 1000, 2500, 5000 };   // last one after the 4 s check
    while (watchStage < 4 && elapsed >= stages[watchStage])
    {
        audioLog ("Moteur : +" + juce::String ((int) stages[watchStage]) + " ms, callbacks " + juce::String ((juce::int64) cbs)
                  + ", sous-alimentations " + juce::String ((juce::int64) stats.underruns.load())
                  + ", position " + juce::String (getPositionSeconds(), 2) + " s, ring " + juce::String ((juce::int64) ring.readable()) + " octets");
        ++watchStage;
    }
    if (! backend->isRealtime()) { if (watchStage >= 4) startedAtMs = 0; return; }

    if (! watchConfirmed && cbs >= 8)
    {
        watchConfirmed = true;
        audioLog ("Moteur : flux confirme, " + juce::String ((juce::int64) cbs) + " callbacks en " + juce::String ((int) elapsed) + " ms");
    }
    // 4 s, once: when the VAD sees DoP it reconfigures itself in DSD and may pause the stream meanwhile.
    if (! watchConfirmed && elapsed >= 4000)
    {
        if (watchRetries >= 1)
        {
            audioLog ("Moteur : aucun callback apres un redemarrage du flux, abandon");
            abortDeviceStart ("Sortie audio : le DAC ne cadence pas le flux (aucun buffer reçu). Journal : "_t + audioLogFile().getFullPathName());
            return;
        }
        ++watchRetries;
        audioLog ("Moteur : " + juce::String ((juce::int64) cbs) + " callbacks en 4 s, redemarrage du flux (essai " + juce::String (watchRetries) + ")");
        backend->stop();
        stableCallbacks = 0; lastCallbackFrames = 0;       // the device is stopped: audio-thread state can be set directly
        deviceRunning = backend->start (this);
        if (! deviceRunning) { abortDeviceStart ("Sortie audio : le périphérique n'a pas redémarré (journal : "_t + audioLogFile().getFullPathName() + ")"); return; }
        startedAtMs = juce::Time::getMillisecondCounter();
        callbacksAtStart = stats.callbacks.load();
        watchStage = 0;
        return;
    }
    if (watchStage >= 4 && watchConfirmed) startedAtMs = 0;
}

void PlaybackEngine::timerCallback()
{
    DspChain* retired = nullptr;
    while (retiredChains.read (&retired, 1) == 1)
        for (auto it = chainStore.begin(); it != chainStore.end(); ++it)
            if (it->get() == retired) { chainStore.erase (it); break; }

    const int rp = reconfigurePlanId.load();
    if (rp != 0 && state.load() != State::Stopped)
        configureDevice (rp);

    watchDeviceStart();

    // DoP carrier idling in Stopped: release the DAC (Hog Mode, MT VAD) after 10 s. Paused keeps it, resume stays instant.
    if (state.load() == State::Stopped && keepsDopCarrier())
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (stoppedAtMs == 0) stoppedAtMs = now;
        else if (now - stoppedAtMs >= 10000)
        {
            backend->stop();
            deviceRunning = false;
            backend->close();
            activePlanId = 0;
            stoppedAtMs = 0;
        }
    }
    else stoppedAtMs = 0;

    if (trackChangedFlag.exchange (false))
    {
        pathChangedFlag = true;
        if (onTrackChanged) onTrackChanged();
    }
    if (endedFlag.exchange (false))
    {
        state = State::Stopped;
        if (! keepsDopCarrier())
        {
            if (deviceRunning) { backend->stop(); deviceRunning = false; }
            backend->close();
            activePlanId = 0;
        }
        if (onQueueFinished) onQueueFinished();
        if (onStateChanged) onStateChanged();
    }
    if (errorFlag.exchange (false) && onError)
        onError (getLastError());
    if (pathChangedFlag.exchange (false) && onSignalPathChanged)
        onSignalPathChanged();
    stats.clipped = [this] { juce::uint64 c = 0; for (auto& r : requantizers) c += r.clipped; return c; }();
}

//==============================================================================
SignalPath PlaybackEngine::getSignalPath() const
{
    SignalPath path;
    const auto plan = getCurrentPlan();
    const auto status = backend->getStatus();
    const int idx = currentIndex.load();
    const auto item = idx >= 0 && idx < (int) queue.size() ? queue[(size_t) idx] : PlayableItem();

    if (plan.source.sampleRate <= 0 || ! status.open)
    {
        path.summary = "Aucune lecture";
        return path;
    }

    const auto& src = plan.source;
    path.add ("Source", src.describe(), item.sourceName.isNotEmpty() ? item.sourceName : juce::String ("Fichier local"),
              src.lossy ? PathQuality::LowQuality : PathQuality::BitPerfect);

    if (src.isDsd())
        path.add ("Décodage"_t, src.codec + " -> flux 1 bit", "Lecture directe, aucune conversion", PathQuality::BitPerfect);
    else if (src.kind == SampleKind::PcmFloat)
        path.add ("Décodage"_t, src.codec + " -> 64 bits flottant", src.lossy ? "Décodeur avec pertes"_t : "Échantillons flottants"_t,
                  src.lossy ? PathQuality::LowQuality : PathQuality::HighQuality);
    else
        path.add ("Décodage"_t, src.codec + " -> entiers " + juce::String (src.bitsPerSample) + " bits", "Bit à bit, sans passer par le flottant"_t,
                  PathQuality::BitPerfect);

    switch (plan.conversion)
    {
        case PlaybackPlan::Conversion::DsdToPcm:
        {
            const auto eff = getEffectiveSettings();
            path.add ("Conversion", "DSD -> PCM " + formatRate (plan.deviceRate),
                      (plan.filterShape == FilterShape::MinimumPhase ? juce::String ("FIR phase minimale 64 bits, 150 dB"_t) : juce::String ("FIR phase linéaire 64 bits, 150 dB"_t))
                          + (eff.dsdToPcmGainDb != 0.0 ? ", gain +"_t + juce::String (eff.dsdToPcmGainDb, 0) + " dB" + (eff.limiter ? " + limiteur"_t : juce::String()) : juce::String()),
                      PathQuality::Enhanced);
            break;
        }
        case PlaybackPlan::Conversion::Resample:
            path.add ("Conversion", "Sinc " + formatRate (src.sampleRate) + " -> " + formatRate (plan.deviceRate),
                      plan.filterShape == FilterShape::MinimumPhase ? juce::String ("Polyphase 64 bits, phase minimale (sans pré-écho)"_t)
                                                                    : juce::String ("Polyphase 64 bits, 160 dB"), PathQuality::Enhanced);
            break;
        case PlaybackPlan::Conversion::FloatToInt:
        case PlaybackPlan::Conversion::None:
            break;
    }

    {
        // NX engine: 64-bit pipeline, Memory Play (whole file in RAM) or disk reads through the 8 MB ring
        path.add ("Moteur", "Moteur NX 64 bits"_t + (plan.inMemory ? " · Memory Play"_t : juce::String()),
                  plan.inMemory ? "Fichier entier en RAM ("_t + juce::String ((double) plan.fileBytes / 1048576.0, 1) + " Mo) : le disque reste au repos"_t
                                : juce::String ("Lecture disque / réseau, tampon circulaire sans verrou de 8 Mo"_t),
                  PathQuality::BitPerfect);
        const auto eff = getEffectiveSettings();
        if (eff.warmth && plan.profileTag.isNotEmpty()
            && (plan.conversion == PlaybackPlan::Conversion::DsdToPcm || plan.conversion == PlaybackPlan::Conversion::Resample))
            path.add ("Modelage", "Harmoniques paires −90 dB · plateau −0,5 dB > 18 kHz"_t, "64 bits, débrayable dans les réglages"_t, PathQuality::Enhanced);
    }
    if (plan.wire == WireFormat::DoP)
        path.add ("Transport", "DoP " + formatRate (plan.deviceRate) + " · 24 bits"_t, "Marqueurs 0x05 / 0xFA, données DSD intactes"_t, PathQuality::BitPerfect);
    if (plan.wire == WireFormat::NativeDsd)
        path.add ("Transport", "DSD natif " + formatRate (plan.deviceRate), "ASIO DSD", PathQuality::BitPerfect);

    if (plan.wire == WireFormat::Pcm)
    {
        if (! isFixedVolume() && volume.load() != 1.0)
            path.add ("Volume", juce::String (juce::Decibels::gainToDecibels (volume.load()), 1) + " dB",
                      "Numérique 64 bits + dither TPDF"_t, PathQuality::HighQuality);
        if (activeChain != nullptr && activeChain->isActive())
            path.add ("DSP", activeChain->names().joinIntoString (" · "_t), "Plug-ins DSP 64 bits", PathQuality::Enhanced);
        const bool enhancing = plan.conversion == PlaybackPlan::Conversion::Resample || plan.conversion == PlaybackPlan::Conversion::DsdToPcm
                               || (activeChain != nullptr && activeChain->isActive());
        if (plan.needsRequantize || (! isFixedVolume() && volume.load() != 1.0) || (activeChain != nullptr && activeChain->isActive()))
            path.add ("Quantification", juce::String (deviceExactBits.load()) + " bits", "Dither TPDF",
                      enhancing ? PathQuality::Enhanced : PathQuality::HighQuality);
    }

    path.add ("Sortie", status.backendName + " · "_t + status.deviceName,
              status.detail.isNotEmpty() ? status.detail : (formatRate (status.sampleRate) + " · "_t + juce::String (status.deviceBits) + " bits"),
              status.exclusive && status.osMixerBypassed ? PathQuality::BitPerfect : PathQuality::LowQuality);

    // DAC and the active device profile (Merging Anubis, RME ADI-2...)
    {
        const auto family = activeFamily (getSettings(), status.deviceName.isNotEmpty() ? status.deviceName : deviceName);
        const auto dac = status.deviceName.isNotEmpty() ? status.deviceName : deviceName;
        path.add ("DAC", dac, connectionLabel (dac, status.backendName) + (status.exclusive ? " · accès exclusif"_t : juce::String()),
                  status.exclusive && status.osMixerBypassed ? PathQuality::BitPerfect : PathQuality::LowQuality);
        if (plan.profileTag.isNotEmpty() && family != DeviceFamily::None)
        {
            const auto eff = getEffectiveSettings();
            const int idx = familyCurrentProfile (eff, family);
            path.add ("Profil", profileName (family, idx), profileDescription (family, idx),
                      plan.conversion == PlaybackPlan::Conversion::None || plan.conversion == PlaybackPlan::Conversion::FloatToInt
                          ? PathQuality::BitPerfect : PathQuality::Enhanced);
        }
    }

    switch (path.overall)
    {
        case PathQuality::BitPerfect:  path.summary = "Bit-perfect : le DAC reçoit exactement les données du fichier"_t; break;
        case PathQuality::Enhanced:    path.summary = "Amélioré : traitement 64 bits volontaire"_t; break;
        case PathQuality::HighQuality: path.summary = "Haute qualité : sans perte, mais pas bit-perfect"_t; break;
        case PathQuality::LowQuality:  path.summary = "Qualité réduite : source avec pertes ou mixeur du système"_t; break;
    }
    return path;
}

} // namespace nx
