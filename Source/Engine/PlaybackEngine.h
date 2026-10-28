// NX Player — MIT License
// The player: a decoder thread fills a lock-free ring, the device's real-time thread empties it.
//
//   message thread : commands (play, seek, queue...), device (re)configuration, notifications
//   decoder thread : opens tracks, decodes, converts when needed, writes the ring, gapless transitions
//   audio thread   : reads the ring, packs DoP, applies volume / DSP only when asked, feeds the backend
//
// Track boundaries, seeks and format changes travel through a second lock-free queue of events that
// carry the absolute ring position where they take effect, so the audio thread switches tracks on the
// exact sample and drops stale data after a seek without any lock.
#pragma once

#include "PlaybackPlan.h"
#include "SignalPath.h"
#include "DspChain.h"
#include "../Core/SpscRing.h"
#include "../Audio/AudioDecoder.h"
#include "../DSP/Requantizer.h"
#include "../DSP/OutputShaper.h"
#include "DeviceProfiles.h"
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>

namespace nx
{

namespace dsp { class DsdToPcm; class Resampler; }

struct PlayableItem
{
    juce::String id;                     // library id (or provider:id)
    juce::String uri;                    // absolute file path, or an http(s) URL
    juce::String title, artist, album;
    juce::String sourceName;             // "Fichiers locaux", "Qobuz", "NAS (UPnP)"...
    double durationHint = 0.0;
    /** Optional: custom stream opener (authenticated streams, caches...). Called on the decoder thread. */
    std::function<std::unique_ptr<juce::InputStream>()> openStream;
};

/** Lock-free tap of what is being played (for visualisers). Written by the audio thread. */
class AnalyzerTap
{
public:
    AnalyzerTap() : ring (1 << 17) {}
    void push (const float* stereo, int frames) noexcept { ring.write (stereo, (size_t) frames * 2); }
    /** Message thread: newest frames, at getSampleRate(). */
    int pull (float* stereo, int maxFrames) noexcept { return (int) ring.read (stereo, (size_t) maxFrames * 2) / 2; }
    double getSampleRate() const noexcept { return rate.load(); }
    std::atomic<double> rate { 44100.0 };
    std::atomic<bool> isDsdDensity { false };   // samples are 1-bit densities, not PCM
private:
    SpscRing<float> ring;
};

class PlaybackEngine : private RenderCallback,
                       private juce::Thread,
                       private juce::Timer
{
public:
    enum class State { Stopped, Playing, Paused, Buffering };

    explicit PlaybackEngine (std::unique_ptr<OutputBackend> backend);
    ~PlaybackEngine() override;

    //== Device ===============================================================
    OutputBackend& getBackend() noexcept { return *backend; }
    void setBackend (std::unique_ptr<OutputBackend> newBackend);
    void setDevice (const juce::String& deviceName);
    juce::String getDeviceName() const { return deviceName; }
    DeviceCapabilities getCapabilities() const;
    /** replanCurrent: the playing track is re-opened with the new settings (from the play position). */
    void setSettings (const EngineSettings& s, bool replanCurrent = true);
    EngineSettings getSettings() const;
    /** The settings in force for the current device (device profile applied, e.g. Merging Anubis). */
    EngineSettings getEffectiveSettings() const;

    //== Queue & transport (message thread) ===================================
    void setQueue (std::vector<PlayableItem> items, int startIndex = 0, bool startPlaying = true);
    void appendToQueue (std::vector<PlayableItem> items);
    const std::vector<PlayableItem>& getQueue() const noexcept { return queue; }
    void playIndex (int index, double startSeconds = 0.0);
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void next();
    void previous();
    void seek (double seconds);

    //== A-B loop (gapless: the device keeps running, the decoder wraps) =======
    /** Loops [a, b) of the current track for ever (b − a ≥ 50 ms). Re-reads from the play position once. */
    void setLoop (double aSeconds, double bSeconds);
    void clearLoop (bool rebuffer = true);
    bool isLoopActive() const noexcept { return loopIndex.load() >= 0; }
    double getLoopA() const noexcept { return loopA.load(); }
    double getLoopB() const noexcept { return loopB.load(); }
    int getLoopIndex() const noexcept { return loopIndex.load(); }
    /** Crossfade at the wrap point (PCM), in milliseconds. */
    static constexpr double loopCrossfadeMs = 4.0;

    State getState() const noexcept { return state.load(); }
    int getCurrentIndex() const noexcept { return currentIndex.load(); }
    double getPositionSeconds() const noexcept;
    double getDurationSeconds() const;

    //== Volume & DSP =========================================================
    /** Linear gain. 1.0 with fixedVolume keeps the stream bit-perfect. */
    void setVolume (double gain);
    double getVolume() const noexcept { return volume.load(); }
    void setFixedVolume (bool fixed);
    /** Volume held at 100 %: by the user (bit-perfect) or by the device profile (RME DSD Direct safety). */
    bool isFixedVolume() const noexcept { return fixedVolume.load() || profileLocksVolume.load(); }
    bool isVolumeLockedByProfile() const noexcept { return profileLocksVolume.load(); }
    void setDspChain (std::unique_ptr<DspChain> chain);

    //== Status ===============================================================
    PlaybackPlan getCurrentPlan() const;
    OutputStatus getOutputStatus() const { return backend->getStatus(); }
    SignalPath getSignalPath() const;
    AnalyzerTap& getAnalyzer() noexcept { return analyzer; }

    struct Stats
    {
        std::atomic<juce::uint64> underruns { 0 }, callbacks { 0 }, framesOut { 0 }, reconfigurations { 0 }, clipped { 0 };
    };
    const Stats& getStats() const noexcept { return stats; }
    juce::String getLastError() const;
    size_t getBufferedBytes() const noexcept { return ring.readable(); }
    bool isDecoderBusy() const noexcept { return decoderBusy.load(); }

    /** Message thread notifications. */
    std::function<void()> onTrackChanged, onStateChanged, onQueueFinished, onSignalPathChanged;
    std::function<void (const juce::String&)> onError;

    /** Opens network / provider streams (Brique 2). Called on the decoder thread. Files are opened directly. */
    std::function<std::unique_ptr<juce::InputStream> (const PlayableItem&)> streamResolver;

    /** For tests: run the message-thread housekeeping now instead of waiting for the timer. */
    void serviceMessageThread() { timerCallback(); }

private:
    //== RenderCallback (audio thread) ========================================
    void renderInt (int32_t* const* channels, int numChannels, int numFrames) noexcept override;
    void renderDsd (uint8_t* const* channels, int numChannels, int numBytes) noexcept override;

    //== Decoder thread ========================================================
    void run() override;
    bool openItem (int index, double startSeconds, bool flush);
    void preparePlanConverters (const PlaybackPlan& plan, bool keepState);
    int produce (int writableBytes);
    void drainConverters();
    void writeAll (const void* data, size_t bytes);

    //== Message thread ========================================================
    void timerCallback() override;
    void requestFlushStart (int index, double seconds);
    bool configureDevice (int planId);

    struct PlanWire
    {
        WireFormat wire = WireFormat::Pcm;
        double rate = 0.0;
        int channels = 2;
        int ringBits = 24;
        bool needsRequantize = false;
        double unitsPerSecond = 44100.0;
        bool valid = false;
        bool same (const PlanWire& o) const noexcept
        {
            return valid && o.valid && wire == o.wire && rate == o.rate && channels == o.channels
                && (wire != WireFormat::Pcm || ringBits == o.ringBits);
        }
    };

    struct RingEvent
    {
        enum Kind : int { Track, End, Position } kind = Track;   // Position: the loop wrapped, the clock jumps back
        juce::int64 startByte = 0;
        juce::uint32 gen = 0;
        int itemIndex = 0;
        int planId = 0;
        double startSeconds = 0.0;
    };

    bool consumeEvents (const PlanWire& active) noexcept;   // audio thread
    bool checkPreroll (size_t unitBytes, double unitsPerSecond, int requestedUnits) noexcept;
    void outputSilenceInt (int32_t* const* ch, int numCh, int n) noexcept;

    std::unique_ptr<OutputBackend> backend;
    juce::String deviceName;
    DeviceCapabilities caps;
    EngineSettings settings;
    mutable std::mutex controlLock;          // decoder thread <-> message thread only, never the audio thread

    std::vector<PlayableItem> queue;         // message thread copy
    std::vector<PlayableItem> decoderQueue;  // decoder thread copy (under controlLock)
    bool queueDirty = false;

    // Ring + events
    SpscRing<uint8_t> ring;
    SpscRing<RingEvent> events { 256 };
    std::array<PlanWire, 64> planWires {};
    std::map<int, PlaybackPlan> plans;       // planId -> full plan (controlLock)
    std::atomic<int> nextPlanId { 1 };

    // Commands to the decoder thread
    struct FlushCommand { bool pending = false; int index = 0; double seconds = 0.0; juce::uint32 gen = 0; };
    FlushCommand flushCommand;               // controlLock
    std::atomic<juce::uint32> flushGen { 0 };
    std::atomic<bool> stopDecoding { false };

    // Decoder thread state
    std::unique_ptr<AudioDecoder> decoder;
    int decIndex = -1;
    PlaybackPlan decPlan;
    int decPlanId = 0;
    bool decHavePlan = false;
    juce::int64 bytesWritten = 0;
    std::unique_ptr<dsp::DsdToPcm> dsdConverter;
    std::unique_ptr<dsp::Resampler> resampler;
    dsp::OutputShaper shaper;                 // warmth / limiter after a conversion (device profiles)
    std::vector<int32_t> tmpInt;
    std::vector<double> tmpDouble, tmpDouble2;
    std::vector<uint8_t> tmpBytes;
    juce::uint32 decoderGen = 0;

    // Audio thread state
    juce::uint32 consumerGen = 0;
    juce::int64 consumedBytes = 0;
    RingEvent currentEvent;
    bool haveCurrent = false;
    juce::int64 unitsIntoTrack = 0;
    bool prerolling = false;
    juce::int64 prerollFrames = 0;
    juce::int64 prerollMinUnits = 0;         // forced silence after a (re)configuration (DoP "Sync Safe")
    // DoP lock: the timeline only starts once the hardware callback has delivered this many consecutive
    // identical, underrun-free buffers (the driver / network clock is then stable).
    int stableCallbacks = 0;
    int requiredStableCallbacks = 0;
    int lastCallbackFrames = 0;
    juce::uint64 lastUnderruns = 0;
    bool keepsDopCarrier() const noexcept;   // device kept open, sending the DoP silence carrier
    dsp::DopPacker dop;
    std::array<dsp::Requantizer, 8> requantizers;
    double smoothedGain = 1.0;
    std::vector<double> planar;              // 8 × maxBlock
    std::array<double*, 8> planarPtrs {};
    std::vector<uint8_t> rtBytes;
    std::vector<float> rtTap;
    static constexpr int maxBlock = 16384;

    // Shared atomics
    std::atomic<State> state { State::Stopped };
    std::atomic<int> currentIndex { -1 };
    std::atomic<double> loopA { -1.0 }, loopB { -1.0 };
    std::atomic<int> loopIndex { -1 };
    class LoopingDecoder* loopDecoder = nullptr;   // decoder thread: the wrapper of the current track, if any
    int loopWrapsSeen = 0;
    std::atomic<int> activePlanId { 0 };     // plan the device is configured for
    std::atomic<int> reconfigurePlanId { 0 };// set when the device must be reopened
    std::atomic<int> playingPlanId { 0 };
    std::atomic<double> positionBase { 0.0 };
    std::atomic<juce::int64> positionUnits { 0 };
    std::atomic<double> positionUnitsPerSecond { 44100.0 };
    std::atomic<bool> trackChangedFlag { false }, endedFlag { false }, pathChangedFlag { false };
    std::atomic<double> volume { 1.0 };
    std::atomic<bool> fixedVolume { true };
    std::atomic<bool> profileLocksVolume { false };
    std::atomic<DspChain*> pendingChain { nullptr };
    SpscRing<DspChain*> retiredChains { 16 };
    DspChain* activeChain = nullptr;         // audio thread (or message thread while the device is stopped)
    std::vector<std::unique_ptr<DspChain>> chainStore;   // message thread owns every chain
    std::atomic<int> deviceExactBits { 24 };
    std::atomic<bool> errorFlag { false };
    std::atomic<bool> decoderBusy { false };
    bool deviceRunning = false;

    AnalyzerTap analyzer;
    Stats stats;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackEngine)
};

} // namespace nx
