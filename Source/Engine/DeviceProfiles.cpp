// NX Player — MIT License
#include "DeviceProfiles.h"
#include "../Core/Text.h"

namespace nx
{

bool isMergingDevice (const juce::String& name)
{
    for (auto k : { "merging", "anubis", "ravenna", "aes67", "hapi", "horus" })
        if (name.containsIgnoreCase (k)) return true;
    // "VAD" as a word (Merging's Virtual Audio Device), not inside another word
    return juce::StringArray::fromTokens (name.toUpperCase(), " -_/()[].:", {}).contains ("VAD");
}

bool isRmeDevice (const juce::String& name)
{
    if (name.containsIgnoreCase ("ADI-2") || name.containsIgnoreCase ("ADI 2")) return true;
    return juce::StringArray::fromTokens (name.toUpperCase(), " -_/()[].:", {}).contains ("RME");
}

bool isFiioDevice (const juce::String& name)
{
    return name.containsIgnoreCase ("fiio") || name.containsIgnoreCase ("ka17");
}

DeviceFamily activeFamily (const EngineSettings& s, const juce::String& deviceName)
{
    switch (s.hardwareProfile)
    {
        case HardwareProfile::Generic:        return DeviceFamily::None;
        case HardwareProfile::MergingRavenna: return DeviceFamily::Merging;
        case HardwareProfile::RmeAdi2:        return DeviceFamily::Rme;
        case HardwareProfile::FiioKa17:       return DeviceFamily::Fiio;
        case HardwareProfile::Auto:           break;
    }
    if (isMergingDevice (deviceName)) return DeviceFamily::Merging;
    if (isRmeDevice (deviceName)) return DeviceFamily::Rme;
    if (isFiioDevice (deviceName)) return DeviceFamily::Fiio;
    return DeviceFamily::None;
}

int familyProfileCount (DeviceFamily f) { return f == DeviceFamily::Merging ? 3 : (f == DeviceFamily::Rme || f == DeviceFamily::Fiio) ? 2 : 0; }

int familyCurrentProfile (const EngineSettings& s, DeviceFamily f)
{
    return f == DeviceFamily::Merging ? (int) s.anubisProfile : f == DeviceFamily::Rme ? (int) s.rmeProfile : f == DeviceFamily::Fiio ? s.fiioProfile : 0;
}

void familySetProfile (EngineSettings& s, DeviceFamily f, int i)
{
    if (f == DeviceFamily::Merging) s.anubisProfile = (AnubisProfile) juce::jlimit (0, 2, i);
    if (f == DeviceFamily::Rme)     s.rmeProfile = (RmeProfile) juce::jlimit (0, 1, i);
    if (f == DeviceFamily::Fiio)    s.fiioProfile = juce::jlimit (0, 1, i);
}

juce::String familyTitle (DeviceFamily f)
{
    return f == DeviceFamily::Merging ? juce::String ("Merging Anubis · RAVENNA / AES67"_t)
         : f == DeviceFamily::Rme ? juce::String ("RME ADI-2 · USB")
         : f == DeviceFamily::Fiio ? juce::String ("FiiO KA17 · USB")
         : juce::String();
}

juce::String familyTip (DeviceFamily f)
{
    if (f == DeviceFamily::Merging)
        return "Astuce MT VAD : réglez Inputs / Outputs sur 2 canaux et Latency sur 4 ms (192) dans MT VAD Preferences pour éviter "_t
               "l'erreur ANEMAN en haute fréquence."_t;
    if (f == DeviceFamily::Rme)
        return "Astuce RME : activez « DSD Direct » sur votre ADI-2 Pro uniquement si vous utilisez un contrôle de volume analogique externe "_t
               "(DSD Direct désactive le volume matériel des sorties ligne)."_t;
    if (f == DeviceFamily::Fiio)
        return "Astuce FiiO : sous Windows, installez le pilote ASIO FiiO et choisissez-le comme pilote ; sous macOS et Linux le DAC "_t
               "fonctionne en USB Audio Class 2 sans pilote."_t;
    return {};
}

juce::String profileName (DeviceFamily f, int i)
{
    if (f == DeviceFamily::Merging) return anubisProfileName ((AnubisProfile) i);
    if (f == DeviceFamily::Fiio) return i == 0 ? "FiiO — DoP Bit-Perfect"_t : "FiiO — DSD vers DXD"_t;
    if (i == 0) return "RME — SteadyClock DoP Native"_t;
    return "RME — AKM Velvet DXD"_t;
}

juce::String profileTag (DeviceFamily f, int i)
{
    if (f == DeviceFamily::Merging) return anubisProfileTag ((AnubisProfile) i);
    if (f == DeviceFamily::Fiio) return i == 0 ? juce::String ("FiiO DoP") : juce::String ("FiiO DXD");
    return i == 0 ? juce::String ("RME Bit-Perfect") : juce::String ("RME Velvet");
}

juce::String profileDescription (DeviceFamily f, int i)
{
    if (f == DeviceFamily::Merging) return anubisProfileDescription ((AnubisProfile) i);
    if (f == DeviceFamily::Fiio)
        return i == 0 ? "DSD en DoP 1.0 (jusqu'au DSD256 si le DAC l'annonce), PCM bit-perfect jusqu'à 384 kHz, synchronisation courte de 150 ms."_t
                      : "DSD converti en DXD 352,8 kHz (+6 dB, phase minimale) : pour une chaîne USB qui ne reconnaît pas le DoP."_t;
    if (i == 0)
        return "Bit-perfect USB : DSD en DoP 1.0 jusqu'au DSD256 (selon les fréquences que l'ADI-2 annonce), synchronisation courte "_t
               "de 150 ms, PCM bit-perfect en mode entier."_t;
    return "DSD converti en DXD 352,8 kHz (+6 dB, filtre à phase minimale) : l'égaliseur, le loudness et le crossfeed de l'ADI-2 "_t
           "restent disponibles (ils ne fonctionnent pas en DSD)."_t;
}

juce::String anubisProfileName (AnubisProfile p)
{
    switch (p)
    {
        case AnubisProfile::StudioDxdReference: return "Anubis — Studio DXD Reference"_t;
        case AnubisProfile::AnalogWarmth:       return "Anubis — Analog Warmth"_t;
        case AnubisProfile::PureDoP:            return "Anubis — Pure DoP 1-Bit"_t;
    }
    return {};
}

juce::String anubisProfileTag (AnubisProfile p)
{
    switch (p)
    {
        case AnubisProfile::StudioDxdReference: return "Anubis Ref";
        case AnubisProfile::AnalogWarmth:       return "Warm";
        case AnubisProfile::PureDoP:            return "Bit-Perfect";
    }
    return {};
}

juce::String anubisProfileDescription (AnubisProfile p)
{
    switch (p)
    {
        case AnubisProfile::StudioDxdReference:
            return "Bit-perfect et stabilité : le DSD (DSD64 à DSD512) est converti en DXD 352,8 kHz (FIR phase linéaire 64 bits, "_t
                   "gain réglable avec limiteur), le PCM 44,1 à 192 kHz reste bit-perfect. Aucun marqueur DoP sur le réseau."_t;
        case AnubisProfile::AnalogWarmth:
            return "Signature chaleureuse : DSD et PCM portés en DXD 352,8 / 384 kHz par un filtre à phase minimale (sans pré-écho), "_t
                   "étage optionnel d'harmoniques paires (−90 dB) et légère détente au-dessus de 18 kHz."_t;
        case AnubisProfile::PureDoP:
            return "DSD en DoP 1.0, sécurité RAVENNA : tampon de 2048 échantillons, 2 canaux, 1,5 s de silence DoP à chaque "_t
                   "changement de fréquence pour laisser l'horloge PTP se verrouiller."_t;
    }
    return {};
}

EngineSettings resolveProfile (const EngineSettings& in, const juce::String& deviceName)
{
    EngineSettings s = in;
    s.filterShape = FilterShape::LinearPhase;
    s.limiter = s.dsdToPcmGainDb > 0.0;
    s.warmth = false;
    s.bufferFrames = 0;
    s.maxChannels = 8;
    s.dopPrerollMs = 0;
    s.profileTag = {};
    s.lockVolume = false;

    const auto family = activeFamily (in, deviceName);
    if (family == DeviceFamily::Rme)
    {
        s.profileTag = profileTag (family, (int) in.rmeProfile);
        if (in.rmeProfile == RmeProfile::SteadyClockDoP)
        {
            s.dsdMode = DsdMode::DoP;
            s.dopAllowed = true;
            s.upsampleToDxd = false;
            s.dopPrerollMs = 150;
            s.lockVolume = in.rmeLockVolume;
        }
        else
        {
            s.dsdMode = DsdMode::ConvertToPcm;
            s.maxDsdToPcmRate = 352800.0;
            s.upsampleToDxd = false;
            s.dsdToPcmGainDb = 6.0;
            s.limiter = true;
            s.filterShape = FilterShape::MinimumPhase;
        }
        return s;
    }
    if (family == DeviceFamily::Fiio)
    {
        s.profileTag = profileTag (family, in.fiioProfile);
        if (in.fiioProfile == 0)
        {
            s.dsdMode = DsdMode::DoP;
            s.dopAllowed = true;
            s.upsampleToDxd = false;
            s.dopPrerollMs = 150;
        }
        else
        {
            s.dsdMode = DsdMode::ConvertToPcm;
            s.maxDsdToPcmRate = 352800.0;
            s.upsampleToDxd = false;
            s.dsdToPcmGainDb = 6.0;
            s.limiter = true;
            s.filterShape = FilterShape::MinimumPhase;
        }
        return s;
    }
    if (family != DeviceFamily::Merging) return s;

    const bool noDsd = modelHasNoDsd (in);
    const auto anubis = noDsd && in.anubisProfile == AnubisProfile::PureDoP ? AnubisProfile::StudioDxdReference : in.anubisProfile;
    s.profileTag = anubisProfileTag (anubis);
    switch (anubis)
    {
        case AnubisProfile::StudioDxdReference:
            s.dsdMode = DsdMode::ConvertToPcm;
            s.maxDsdToPcmRate = 352800.0;
            s.upsampleToDxd = false;               // PCM stays bit-perfect
            s.dsdToPcmGainDb = in.anubisDsdGainDb;
            s.limiter = true;
            break;

        case AnubisProfile::AnalogWarmth:
            s.dsdMode = DsdMode::ConvertToPcm;
            s.maxDsdToPcmRate = 352800.0;
            s.upsampleToDxd = true;
            s.dsdToPcmGainDb = in.anubisDsdGainDb;
            s.limiter = true;
            s.filterShape = FilterShape::MinimumPhase;
            s.warmth = in.anubisHarmonics;
            break;

        case AnubisProfile::PureDoP:
            s.dsdMode = DsdMode::DoP;
            s.dopAllowed = true;
            s.upsampleToDxd = false;
            s.bufferFrames = 2048;
            s.maxChannels = 2;
            s.dopPrerollMs = 1500;
            break;
    }
    if (noDsd) { s.dsdMode = DsdMode::ConvertToPcm; s.dopAllowed = false; }
    return s;
}

juce::String pipelineLabel (const PlaybackPlan& plan, const juce::String& qualityName)
{
    auto khz = [] (double r)
    {
        const auto k = r / 1000.0;
        return std::fmod (k, 1.0) == 0.0 ? juce::String ((int) k) : juce::String (k, 1).replaceCharacter ('.', ',');
    };
    auto target = [&] (double r)
    {
        return r >= 352000.0 ? "DXD " + khz (r) + " kHz" : khz (r) + " kHz";
    };
    const auto tag = plan.profileTag.isNotEmpty() ? " [" + plan.profileTag + "]" : juce::String();
    const auto& src = plan.source;
    const auto arrow = juce::String::fromUTF8 ("\xe2\x86\x92");   // →

    if (src.isDsd())
    {
        const auto name = "DSD" + juce::String (src.dsdMultiple());
        if (plan.wire == WireFormat::DoP)
            return name + " DoP [" + (plan.profileTag.startsWith ("RME") ? juce::String ("RME Bit-Perfect") : juce::String ("Bit-Perfect")) + "]";
        if (plan.wire == WireFormat::NativeDsd) return name + " natif [Bit-Perfect]";
        return name + " " + arrow + " " + target (plan.deviceRate) + tag;
    }
    if (plan.conversion == PlaybackPlan::Conversion::Resample)
        return "PCM " + khz (src.sampleRate) + " " + arrow + " " + (plan.deviceRate >= 352000.0 ? juce::String ("DXD") : target (plan.deviceRate)) + tag;

    juce::String text = src.lossy ? src.codec
                                  : juce::String (src.bitsPerSample) + "/" + khz (src.sampleRate);
    text << "  " << qualityName << tag;
    return text;
}

ConnectionKind connectionKind (const juce::String& deviceName, const juce::String& backendName)
{
    const auto n = deviceName.toLowerCase();
    if (isMergingDevice (deviceName) || n.contains ("dante") || n.contains ("network") || n.contains ("airplay") || n.contains ("roon")
        || n.contains ("ravenna") || n.contains ("aes67"))
        return ConnectionKind::Network;
    if (n.contains ("virtuel") || n.contains ("virtual") || n.contains ("silencieuse") || backendName.containsIgnoreCase ("virtuel"))
        return ConnectionKind::Virtual;
    for (auto* builtIn : { "built-in", "builtin", "intégré", "internal", "macbook", "imac", "speakers", "haut-parleur", "headphones",
                           "casque", "hdmi", "displayport", "realtek", "hda intel", "default", "pulse", "pipewire", "mac mini", "studio display" })
        if (n.contains (juce::String::fromUTF8 (builtIn))) return ConnectionKind::BuiltIn;
    return ConnectionKind::Usb;
}

juce::String connectionLabel (const juce::String& deviceName, const juce::String& backendName)
{
    switch (connectionKind (deviceName, backendName))
    {
        case ConnectionKind::Network: return isMergingDevice (deviceName) ? "Réseau RAVENNA / AES67"_t : "Réseau"_t;
        case ConnectionKind::BuiltIn: return "Intégré"_t;
        case ConnectionKind::Virtual: return "Virtuel";
        case ConnectionKind::Usb:     break;
    }
    return "USB";
}

DeviceFamily familyFromName (const juce::String& deviceName)
{
    if (isMergingDevice (deviceName)) return DeviceFamily::Merging;
    if (isRmeDevice (deviceName)) return DeviceFamily::Rme;
    if (isFiioDevice (deviceName)) return DeviceFamily::Fiio;
    return DeviceFamily::None;
}

const std::vector<DeviceModel>& deviceModels()
{
    static const std::vector<DeviceModel> models
    {
        { "rme-adi2-dac",   "RME",     "ADI-2 DAC",             "ADI-2",   HardwareProfile::RmeAdi2,        0, true,  true,  true },
        { "rme-adi2-pro",   "RME",     "ADI-2 Pro",             "ADI-2",   HardwareProfile::RmeAdi2,        0, true,  true,  true },
        { "rme-adi2-pro-ae","RME",     "ADI-2 Pro AE",          "ADI-2",   HardwareProfile::RmeAdi2,        0, true,  true,  true },
        { "merging-anubis-premium", "Merging", "Anubis Premium", "Anubis", HardwareProfile::MergingRavenna,  0, true,  true,  true },
        { "merging-anubis-pro",     "Merging", "Anubis Pro / SPS", "Anubis", HardwareProfile::MergingRavenna, 0, false, false, true },
        { "fiio-ka17",      "FiiO",    "KA17",                  "KA",      HardwareProfile::FiioKa17,       0, true,  true,  true },
        { "generic",        "",        "Autre DAC (générique)", "",        HardwareProfile::Generic,        0, true,  true,  false }
    };
    return models;
}

const DeviceModel* findDeviceModel (const juce::String& id)
{
    for (auto& m : deviceModels()) if (id == m.id) return &m;
    return nullptr;
}

std::vector<const DeviceModel*> searchDeviceModels (const juce::String& query)
{
    std::vector<const DeviceModel*> out;
    const auto words = juce::StringArray::fromTokens (query.trim(), " -", {});
    for (auto& m : deviceModels())
    {
        const juce::String hay = juce::String (m.brand) + " " + juce::String::fromUTF8 (m.name) + " " + m.series + " " + juce::String::fromUTF8 (m.name).replace ("-", " ")
                               + (juce::String (m.id) == "generic" ? " generique générique autre dac" : "");
        bool all = true;
        for (auto& w : words) if (w.isNotEmpty() && ! hay.containsIgnoreCase (w)) { all = false; break; }
        if (all) out.push_back (&m);
    }
    return out;
}

void applyDeviceModel (EngineSettings& s, const DeviceModel& m)
{
    s.deviceModel = m.id;
    s.hardwareProfile = m.hardware;
    if (m.hardware == HardwareProfile::MergingRavenna) s.anubisProfile = (AnubisProfile) m.profile;
    if (m.hardware == HardwareProfile::RmeAdi2)        s.rmeProfile = (RmeProfile) m.profile;
    if (m.hardware == HardwareProfile::FiioKa17)       s.fiioProfile = m.profile;
}

bool modelHasNoDsd (const EngineSettings& s)
{
    return s.deviceModel == "merging-anubis-pro";
}

} // namespace nx
