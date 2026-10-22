// NX Player — MIT License
// Real-time processing inserted between the decoder and the DAC (DSP plug-ins). Any active
// processor takes the stream out of bit-perfect mode, and the Signal Path says so.
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace nx
{

class DspProcessor
{
public:
    virtual ~DspProcessor() = default;
    virtual juce::String getName() const = 0;
    virtual juce::String getDescription() const { return {}; }
    /** Called with the device stopped, never on the audio thread. */
    virtual bool prepare (double sampleRate, int numChannels, int maxBlock) = 0;
    /** Audio thread: planar double buffers, in place. No allocation, no lock. */
    virtual void process (double* const* channels, int numChannels, int numFrames) noexcept = 0;
    virtual void reset() noexcept {}
    virtual bool isBypassed() const noexcept { return false; }
};

class DspChain
{
public:
    std::vector<std::unique_ptr<DspProcessor>> processors;

    bool prepare (double rate, int channels, int maxBlock)
    {
        bool ok = true;
        for (auto& p : processors) ok = p->prepare (rate, channels, maxBlock) && ok;
        preparedRate = rate;
        preparedChannels = channels;
        return ok;
    }

    bool isActive() const noexcept
    {
        for (auto& p : processors)
            if (! p->isBypassed()) return true;
        return false;
    }

    void process (double* const* ch, int numCh, int n) noexcept
    {
        for (auto& p : processors)
            if (! p->isBypassed()) p->process (ch, numCh, n);
    }

    juce::StringArray names() const
    {
        juce::StringArray s;
        for (auto& p : processors) if (! p->isBypassed()) s.add (p->getName());
        return s;
    }

    double preparedRate = 0.0;
    int preparedChannels = 0;
};

} // namespace nx
