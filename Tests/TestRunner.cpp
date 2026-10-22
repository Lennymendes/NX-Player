// NX Player — MIT License
#include "TestFramework.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>

// Allocation counter: the audio-thread tests set nxtest_countAllocations on the rendering thread.
thread_local bool nxtest_countAllocations = false;
std::atomic<long> nxtest_allocationCount { 0 };

void* operator new (std::size_t n)
{
    if (nxtest_countAllocations) nxtest_allocationCount.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (nxtest_countAllocations) nxtest_allocationCount.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace nxtest
{

int runAll (const juce::String& filter)
{
    auto& r = registry();
    std::sort (r.begin(), r.end(), [] (const Section& a, const Section& b) { return a.order < b.order; });

    Context ctx;
    ctx.tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NXPlayer_selftest");
    ctx.tempDir.deleteRecursively();
    ctx.tempDir.createDirectory();

    std::cout << "NX Player " << NX_VERSION << " — banc d'essai" << std::endl;
    int sections = 0;
    for (auto& s : r)
    {
        const juce::String name (juce::CharPointer_UTF8 (s.name));
        if (filter.isNotEmpty() && ! name.containsIgnoreCase (filter)) continue;
        std::cout << "\n== " << s.order << ". " << name.toStdString() << std::endl;
        ctx.section = name;
        const auto before = ctx.failed;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        try { s.fn (ctx); }
        catch (const std::exception& e) { ctx.check (false, juce::String ("exception : ") + e.what()); }
        std::cout << "   (" << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2).toStdString()
                  << " s" << (ctx.failed > before ? ", ÉCHECS" : "") << ")" << std::endl;
        ++sections;
    }
    std::cout << "\n" << sections << " sections · " << ctx.passed << " vérifications réussies · " << ctx.failed << " échec(s)" << std::endl;
    return ctx.failed;
}

} // namespace nxtest
