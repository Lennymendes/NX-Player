// NX Player — MIT License
// Streamer / Gaming mode: one global flag read by the interface timers. The audio thread never looks at it.
#pragma once

#include <juce_events/juce_events.h>
#include <atomic>

namespace nx::perf
{

inline std::atomic<bool>& streamerFlag() { static std::atomic<bool> f { false }; return f; }
inline bool streamer() { return streamerFlag().load(); }
inline void setStreamer (bool on) { streamerFlag() = on; }

/** Timer rate for the interface: `normal`, or `low` (a few Hz) in Streamer mode. */
inline int hz (int normal, int low) { return streamer() ? low : normal; }

/** Called at the top of a timerCallback(): switches the timer to the rate of the current mode. */
inline void retune (juce::Timer& t, int normal, int low)
{
    const int interval = juce::jmax (1, 1000 / juce::jmax (1, hz (normal, low)));
    if (t.getTimerInterval() != interval) t.startTimer (interval);
}

} // namespace nx::perf
