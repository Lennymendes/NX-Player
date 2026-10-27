// NX Player — MIT License
// Studio tools of the player bar: listening checks used in mastering, applied in 64 bits after the decoder.
//   Mono (L+R)      both channels carry (L + R) / 2
//   Solo Mid / Side M = (L + R) / 2 or S = (L − R) / 2 on both channels
//   Phase (Ø)       polarity inversion of both channels (absolute phase)
//   Smart crossfeed Bauer stereophonic-to-binaural crossfeed (bs2b, 700 Hz / 4.5 dB), off by itself in mono
// Any active tool takes the stream out of bit-perfect (the Signal Path says so); all off = bypassed.
// The switches are atomics read by the audio thread: toggling never rebuilds the chain.
#pragma once

#include "../Engine/DspChain.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace nx
{

struct StudioToolsState
{
    enum class Monitor { Stereo, Mono, MidSolo, SideSolo };
    std::atomic<Monitor> monitor { Monitor::Stereo };
    std::atomic<bool> invertPhase { false };
    std::atomic<bool> crossfeed { false };

    bool anyActive() const noexcept { return monitor.load() != Monitor::Stereo || invertPhase.load() || crossfeed.load(); }
    juce::String describe() const;
};

class StudioToolsProcessor : public DspProcessor
{
public:
    explicit StudioToolsProcessor (std::shared_ptr<StudioToolsState> s) : state (std::move (s)) {}

    juce::String getName() const override { return "Outils Studio : " + state->describe(); }
    juce::String getDescription() const override { return "Mono, solo Mid / Side, phase, crossfeed (64 bits)"; }
    bool isBypassed() const noexcept override { return ! state->anyActive(); }

    bool prepare (double sampleRate, int, int) override
    {
        // bs2b default level: 700 Hz, 4.5 dB of feed
        constexpr double pi = 3.14159265358979323846;
        const double fcLo = 700.0, feed = 4.5;
        const double gbLo = feed * -5.0 / 6.0 - 3.0, gbHi = feed / 6.0 - 3.0;
        const double gLo = std::pow (10.0, gbLo / 20.0);
        const double gHi = 1.0 - std::pow (10.0, gbHi / 20.0);
        const double fcHi = fcLo * std::pow (2.0, (gbLo - 20.0 * std::log10 (gHi)) / 12.0);
        double x = std::exp (-2.0 * pi * fcLo / sampleRate);
        b1Lo = x; a0Lo = gLo * (1.0 - x);
        x = std::exp (-2.0 * pi * fcHi / sampleRate);
        b1Hi = x; a0Hi = 1.0 - gHi * (1.0 - x); a1Hi = -x;
        gain = 1.0 / (1.0 - gHi + gLo);
        reset();
        return true;
    }

    void reset() noexcept override { lo = {}; hi = {}; asis = {}; }

    void process (double* const* ch, int numChannels, int numFrames) noexcept override
    {
        if (numChannels < 2) { if (state->invertPhase.load()) for (int i = 0; i < numFrames; ++i) ch[0][i] = -ch[0][i]; return; }
        const auto monitor = state->monitor.load();
        const bool invert = state->invertPhase.load();
        const bool xfeed = state->crossfeed.load() && monitor == StudioToolsState::Monitor::Stereo;
        double* l = ch[0];
        double* r = ch[1];
        for (int i = 0; i < numFrames; ++i)
        {
            double L = l[i], R = r[i];
            switch (monitor)
            {
                case StudioToolsState::Monitor::Mono:
                case StudioToolsState::Monitor::MidSolo:  L = R = 0.5 * (L + R); break;
                case StudioToolsState::Monitor::SideSolo: L = R = 0.5 * (L - R); break;
                case StudioToolsState::Monitor::Stereo:   break;
            }
            if (xfeed)
            {
                lo[0] = a0Lo * L + b1Lo * lo[0];
                lo[1] = a0Lo * R + b1Lo * lo[1];
                hi[0] = a0Hi * L + a1Hi * asis[0] + b1Hi * hi[0];
                hi[1] = a0Hi * R + a1Hi * asis[1] + b1Hi * hi[1];
                asis[0] = L; asis[1] = R;
                L = (hi[0] + lo[1]) * gain;
                R = (hi[1] + lo[0]) * gain;
            }
            if (invert) { L = -L; R = -R; }
            l[i] = L;
            r[i] = R;
        }
        // extra channels (multichannel): polarity only
        if (invert)
            for (int c = 2; c < numChannels; ++c)
                for (int i = 0; i < numFrames; ++i) ch[c][i] = -ch[c][i];
    }

private:
    std::shared_ptr<StudioToolsState> state;
    double a0Lo = 0, b1Lo = 0, a0Hi = 0, a1Hi = 0, b1Hi = 0, gain = 1;
    std::array<double, 2> lo {}, hi {}, asis {};
};

inline juce::String StudioToolsState::describe() const
{
    juce::StringArray parts;
    switch (monitor.load())
    {
        case Monitor::Mono:     parts.add ("Mono (L+R)"); break;
        case Monitor::MidSolo:  parts.add ("Solo Mid"); break;
        case Monitor::SideSolo: parts.add ("Solo Side"); break;
        case Monitor::Stereo:   break;
    }
    if (invertPhase.load()) parts.add (juce::String::fromUTF8 ("Phase \xc3\x98"));
    if (crossfeed.load()) parts.add (monitor.load() == Monitor::Stereo ? "Crossfeed" : "Crossfeed (inactif en mono)");
    return parts.isEmpty() ? juce::String ("aucun") : parts.joinIntoString (" + ");
}

} // namespace nx
