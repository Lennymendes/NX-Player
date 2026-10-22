// NX Player — MIT License
// Platform backend list, best first.
#include "OutputBackend.h"
#include "JuceDeviceBackend.h"

#if JUCE_WINDOWS
 #include "WindowsBackends.h"
#endif

namespace nx
{

std::vector<std::unique_ptr<OutputBackend>> createPlatformBackends()
{
    std::vector<std::unique_ptr<OutputBackend>> list;
   #if JUCE_WINDOWS
    for (auto& b : createWindowsBackends()) list.push_back (std::move (b));
   #endif
    for (auto& b : JuceDeviceBackend::createAll()) list.push_back (std::move (b));
    return list;
}

} // namespace nx
