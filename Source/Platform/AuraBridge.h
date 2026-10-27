// NX Player — MIT License
// Bridge to AuraEngine X (AuraEngine DSD, same suite): "Analyser / Convertir dans AuraEngine X" sends the
// selected files to the application, which opens them in its list (deep analysis first, then conversion).
#pragma once

#include <juce_core/juce_core.h>

namespace nx::aura
{
/** The application: the configured path if valid, else the usual install places. Invalid File if absent. */
juce::File findApplication (const juce::String& configuredPath = {});
/** Usual install places (shown in the settings when the application is not found). */
juce::StringArray candidatePaths();
/** Opens the files in AuraEngine X. Only local files can be sent (a NAS track must be downloaded first). */
bool openFiles (const juce::Array<juce::File>& files, const juce::String& configuredPath, juce::String& error);
/** The command line that openFiles runs (tests). */
juce::StringArray buildCommand (const juce::File& app, const juce::Array<juce::File>& files);
}
