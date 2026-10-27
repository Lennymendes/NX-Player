// NX Player — MIT License
#include "AuraBridge.h"
#include "../Core/Text.h"

namespace nx::aura
{

juce::StringArray candidatePaths()
{
    juce::StringArray p;
   #if JUCE_MAC
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    for (auto* name : { "AuraEngine X.app", "AuraEngine DSD.app", "AuraEngine.app" })
    {
        p.add ("/Applications/" + juce::String (name));
        p.add (home + "/Applications/" + juce::String (name));
    }
   #elif JUCE_WINDOWS
    for (auto base : { juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory),
                       juce::File::getSpecialLocation (juce::File::globalApplicationsDirectoryX86) })
        for (auto* rel : { "ZEYN\\AuraEngine X\\AuraEngine X.exe", "ZEYN\\AuraEngine DSD\\AuraEngine DSD.exe",
                           "AuraEngine X\\AuraEngine X.exe", "AuraEngine DSD\\AuraEngine DSD.exe" })
            p.add (base.getChildFile (rel).getFullPathName());
   #else
    p.add ("/usr/local/bin/AuraEngine DSD");
    p.add ("/opt/AuraEngine DSD/AuraEngine DSD");
   #endif
    return p;
}

juce::File findApplication (const juce::String& configured)
{
    if (configured.isNotEmpty() && juce::File::isAbsolutePath (configured) && juce::File (configured).exists()) return juce::File (configured);
    for (auto& c : candidatePaths())
        if (juce::File (c).exists()) return juce::File (c);
    return {};
}

juce::StringArray buildCommand (const juce::File& app, const juce::Array<juce::File>& files)
{
    juce::StringArray cmd;
   #if JUCE_MAC
    if (app.getFileExtension() == ".app") { cmd.add ("/usr/bin/open"); cmd.add ("-a"); cmd.add (app.getFullPathName()); }
    else cmd.add (app.getFullPathName());
   #else
    cmd.add (app.getFullPathName());
   #endif
    for (auto& f : files) cmd.add (f.getFullPathName());
    return cmd;
}

bool openFiles (const juce::Array<juce::File>& files, const juce::String& configured, juce::String& error)
{
    juce::Array<juce::File> local;
    for (auto& f : files) if (f.existsAsFile()) local.add (f);
    if (local.isEmpty()) { error = "AuraEngine X : seuls les fichiers locaux peuvent être envoyés (téléchargez d'abord le morceau du NAS)"_t; return false; }
    const auto app = findApplication (configured);
    if (! app.exists()) { error = "AuraEngine X introuvable : indiquez son emplacement dans Réglages › Outils"_t; return false; }
    juce::ChildProcess process;
    if (! process.start (buildCommand (app, local), 0)) { error = "AuraEngine X : lancement impossible"_t; return false; }
    return true;
}

} // namespace nx::aura
