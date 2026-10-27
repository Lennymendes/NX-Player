// NX Player — MIT License
#include "SystemMonitor.h"

#if JUCE_MAC
 #include <mach/mach.h>
 #include <mach/mach_host.h>
 #include <sys/sysctl.h>
 #include <IOKit/pwr_mgt/IOPMLib.h>
#elif JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
#elif JUCE_LINUX
 #include <unistd.h>
#endif

namespace nx
{

struct SystemMonitor::Impl
{
    juce::uint64 lastIdle = 0, lastTotal = 0;
    bool haveLast = false;

    double cpuFromTicks (juce::uint64 idle, juce::uint64 total)
    {
        double load = -1.0;
        if (haveLast && total > lastTotal)
            load = 1.0 - (double) (idle - lastIdle) / (double) (total - lastTotal);
        lastIdle = idle;
        lastTotal = total;
        haveLast = true;
        return load < 0.0 ? -1.0 : juce::jlimit (0.0, 1.0, load);
    }
};

SystemMonitor::SystemMonitor() : impl (std::make_unique<Impl>()) { sample(); }
SystemMonitor::~SystemMonitor() = default;

SystemSnapshot SystemMonitor::sample()
{
    SystemSnapshot s;
    s.osName = juce::SystemStats::getOperatingSystemName();
    s.cpuName = juce::SystemStats::getCpuModel().trim();
    if (s.cpuName.isEmpty()) s.cpuName = juce::SystemStats::getCpuVendor();
    s.cpuCores = juce::SystemStats::getNumCpus();
    s.machine = juce::SystemStats::getDeviceDescription();
    s.memTotal = (juce::int64) juce::SystemStats::getMemorySizeInMegabytes() * 1024 * 1024;

   #if JUCE_MAC
    {
        host_cpu_load_info_data_t info;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics (mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t) &info, &count) == KERN_SUCCESS)
        {
            juce::uint64 total = 0;
            for (int i = 0; i < CPU_STATE_MAX; ++i) total += info.cpu_ticks[i];
            s.cpuLoad = impl->cpuFromTicks (info.cpu_ticks[CPU_STATE_IDLE], total);
        }
        vm_statistics64_data_t vm;
        mach_msg_type_number_t vmCount = HOST_VM_INFO64_COUNT;
        vm_size_t pageSize = 4096;
        host_page_size (mach_host_self(), &pageSize);
        if (host_statistics64 (mach_host_self(), HOST_VM_INFO64, (host_info64_t) &vm, &vmCount) == KERN_SUCCESS)
            s.memUsed = (juce::int64) ((juce::uint64) (vm.active_count + vm.wire_count + vm.compressor_page_count) * (juce::uint64) pageSize);
        mach_task_basic_info_data_t task;
        mach_msg_type_number_t taskCount = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info (mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &task, &taskCount) == KERN_SUCCESS)
            s.appMemory = (juce::int64) task.resident_size;
    }
   #elif JUCE_WINDOWS
    {
        FILETIME idle, kernel, user;
        if (GetSystemTimes (&idle, &kernel, &user))
        {
            auto v = [] (const FILETIME& f) { return ((juce::uint64) f.dwHighDateTime << 32) | f.dwLowDateTime; };
            s.cpuLoad = impl->cpuFromTicks (v (idle), v (kernel) + v (user));   // kernel time includes idle time
        }
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof (ms);
        if (GlobalMemoryStatusEx (&ms))
        {
            s.memTotal = (juce::int64) ms.ullTotalPhys;
            s.memUsed = (juce::int64) (ms.ullTotalPhys - ms.ullAvailPhys);
        }
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc))) s.appMemory = (juce::int64) pmc.WorkingSetSize;
    }
   #elif JUCE_LINUX
    {
        juce::StringArray stat;
        stat.addLines (juce::File ("/proc/stat").loadFileAsString());
        if (stat.size() > 0 && stat[0].startsWith ("cpu "))
        {
            auto f = juce::StringArray::fromTokens (stat[0].substring (4), " ", {});
            f.removeEmptyStrings();
            juce::uint64 total = 0;
            for (auto& x : f) total += (juce::uint64) x.getLargeIntValue();
            const juce::uint64 idle = (juce::uint64) f[3].getLargeIntValue() + (juce::uint64) f[4].getLargeIntValue();   // idle + iowait
            s.cpuLoad = impl->cpuFromTicks (idle, total);
        }
        juce::int64 memTotalKb = 0, memAvailKb = -1;
        juce::StringArray mem;
        mem.addLines (juce::File ("/proc/meminfo").loadFileAsString());
        for (auto& line : mem)
        {
            if (line.startsWith ("MemTotal:")) memTotalKb = line.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue();
            if (line.startsWith ("MemAvailable:")) memAvailKb = line.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue();
        }
        if (memTotalKb > 0) s.memTotal = memTotalKb * 1024;
        if (memTotalKb > 0 && memAvailKb >= 0) s.memUsed = (memTotalKb - memAvailKb) * 1024;
        juce::StringArray status;
        status.addLines (juce::File ("/proc/self/status").loadFileAsString());
        for (auto& line : status)
            if (line.startsWith ("VmRSS:")) s.appMemory = line.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue() * 1024;
    }
   #endif
    return s;
}

//==============================================================================
SleepBlocker::SleepBlocker() = default;
SleepBlocker::~SleepBlocker() { setActive (false); }

bool SleepBlocker::isSupported()
{
   #if JUCE_MAC || JUCE_WINDOWS
    return true;
   #else
    return false;
   #endif
}

void SleepBlocker::setActive (bool shouldBlock)
{
    if (shouldBlock == active) return;
   #if JUCE_MAC
    if (shouldBlock)
    {
        IOPMAssertionID id = 0;
        if (IOPMAssertionCreateWithName (kIOPMAssertionTypePreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
                                         CFSTR ("NX Player : lecture audio en cours"), &id) == kIOReturnSuccess)
            macAssertion = id;
        else return;
    }
    else if (macAssertion != 0)
    {
        IOPMAssertionRelease ((IOPMAssertionID) macAssertion);
        macAssertion = 0;
    }
   #elif JUCE_WINDOWS
    SetThreadExecutionState (shouldBlock ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED) : ES_CONTINUOUS);
   #else
    if (shouldBlock) return;   // not supported: stays inactive
   #endif
    active = shouldBlock;
}

} // namespace nx
