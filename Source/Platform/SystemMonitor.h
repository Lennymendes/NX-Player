// NX Player — MIT License
// Computer card of the hardware dashboard: OS, CPU load, memory in use, and the "no idle sleep while
// playing" assertion (macOS IOKit power assertion, Windows SetThreadExecutionState).
#pragma once

#include <juce_core/juce_core.h>
#include <memory>

namespace nx
{

struct SystemSnapshot
{
    juce::String osName, cpuName, machine;
    int cpuCores = 0;
    double cpuLoad = -1.0;          // 0..1 for the whole machine, −1 = unknown
    juce::int64 memTotal = 0;       // bytes
    juce::int64 memUsed = -1;       // bytes, −1 = unknown
    juce::int64 appMemory = -1;     // resident size of NX Player, bytes (−1 = unknown)
};

class SystemMonitor
{
public:
    SystemMonitor();
    ~SystemMonitor();
    /** Samples the counters (CPU load is measured between two calls: call it about once a second). */
    SystemSnapshot sample();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** Keeps the computer awake while audio plays (the display may still sleep). */
class SleepBlocker
{
public:
    SleepBlocker();
    ~SleepBlocker();
    void setActive (bool shouldBlock);
    bool isActive() const noexcept { return active; }
    /** False on systems where NX Player cannot hold such an assertion (Linux without a session API). */
    static bool isSupported();
private:
    bool active = false;
    juce::uint32 macAssertion = 0;
};

} // namespace nx
