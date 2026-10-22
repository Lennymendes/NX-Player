// NX Player — MIT License
#include "NXPlayerApp.h"
#include "AppController.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <cstdio>
#endif

namespace nx
{

static NXPlayerApplication* instance = nullptr;

NXPlayerApplication::NXPlayerApplication() = default;
NXPlayerApplication::~NXPlayerApplication() = default;

void NXPlayerApplication::initialise (const juce::String& commandLine)
{
    instance = this;
    auto args = juce::StringArray::fromTokens (commandLine, true);
    args.removeEmptyStrings();

    const int st = args.indexOf ("--selftest");
    if (st >= 0)
    {
       #if JUCE_WINDOWS
        // GUI-subsystem executable: print into the console that started us (CI, cmd, PowerShell).
        if (AttachConsole (ATTACH_PARENT_PROCESS) || AllocConsole())
        {
            FILE* f = nullptr;
            freopen_s (&f, "CONOUT$", "w", stdout);
            freopen_s (&f, "CONOUT$", "w", stderr);
            SetConsoleOutputCP (CP_UTF8);
        }
       #endif
        const auto filter = args[st + 1].unquoted();
        const int failures = nxtest::runAll (filter);
        setApplicationReturnValue (failures == 0 ? 0 : 1);
        quit();
        return;
    }

    controller = std::make_unique<AppController>();
    juce::StringArray files;
    for (auto& a : args) if (! a.startsWith ("-")) files.add (a.unquoted());
    if (! files.isEmpty()) controller->openFiles (files);
}

void NXPlayerApplication::shutdown()
{
    controller.reset();
    instance = nullptr;
}

void NXPlayerApplication::anotherInstanceStarted (const juce::String& commandLine)
{
    if (controller == nullptr) return;
    auto args = juce::StringArray::fromTokens (commandLine, true);
    juce::StringArray files;
    for (auto& a : args) if (a.isNotEmpty() && ! a.startsWith ("-")) files.add (a.unquoted());
    if (! files.isEmpty()) controller->openFiles (files);
}

AppController* NXPlayerApplication::getController()
{
    return instance != nullptr ? instance->controller.get() : nullptr;
}

} // namespace nx
