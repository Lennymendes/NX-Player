// NX Player — MIT License
// Track analysis (one background scan per track, shared by the player bar and the immersive view):
//  - a high-density envelope (min / max peak + RMS per bin) for an Audirvana-style waveform
//  - Peak and RMS in dBFS, and the official Dynamic Range (TT DR meter algorithm)
// PCM is read as stored; DSD is decimated to 88,2 / 96 kHz with the player's own DSD -> PCM filter.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace nx
{

struct TrackAnalysis
{
    std::vector<float> minV, maxV, rms;   // per bin, linear (full scale = 1): signed peak min / max, RMS
    double peakDb = -200.0, rmsDb = -200.0;
    int dr = 0;                            // DR value (0 = too short / silent)
    int channels = 0;
    juce::uint64 id = 0;                   // unique per scan (render cache key)
    /** "Peak -0,4 dBFS · RMS -15,2 dBFS · DR12" */
    juce::String summary() const;
};

/** "2ch", "6ch" (empty for 0). */
juce::String channelBadge (int channels);

class TrackAnalyzer : private juce::Thread, public juce::ChangeBroadcaster
{
public:
    static constexpr int numBins = 1600;
    TrackAnalyzer();
    ~TrackAnalyzer() override;

    /** Message thread. Starts the scan of a local file; a scan already running for another file is cancelled. */
    void request (const juce::String& filePath);
    /** The finished analysis (null while scanning, for streams or unreadable files). */
    std::shared_ptr<const TrackAnalysis> get (const juce::String& filePath) const;

    /** Blocking scan; returns null when cancelled() turns true or the file cannot be decoded. */
    static std::shared_ptr<TrackAnalysis> analyse (const juce::File& file, int bins, const std::function<bool()>& cancelled);

private:
    void run() override;
    mutable std::mutex lock;
    juce::String pending;
    std::atomic<int> generation { 0 };
    std::vector<std::pair<juce::String, std::shared_ptr<const TrackAnalysis>>> cache;   // most recent last
};

/** Fine symmetric waveform (peak envelope + RMS core), rendered once into two cached images (played / rest). */
class WaveformPainter
{
public:
    void paint (juce::Graphics& g, juce::Rectangle<float> area, const TrackAnalysis& a, float progress,
                juce::Colour played, juce::Colour rest);
private:
    juce::Image images[2];
    juce::uint64 sourceId = 0;
    float gain = 1.0f;                     // display normalisation: the track Peak fills the height
    juce::Rectangle<int> pixels;
    juce::Colour colours[2];
};

} // namespace nx
