// NX Player — MIT License
// Device profiles (in the spirit of Roon's device setup): a recognised interface gets dedicated settings.
// Merging Anubis / RAVENNA / AES67 (MT VAD driver): DoP switches the network stream between PCM and DSD
// framing, which can upset ANEMAN / PTP with many channels and 1 ms latency. The profiles avoid it (DXD) or
// make it safe (large buffer, 2 channels, DoP silence before the music).
#pragma once

#include "PlaybackPlan.h"

namespace nx
{

/** "Merging", "Anubis", "RAVENNA", "AES67" or "VAD" in the device name. */
bool isMergingDevice (const juce::String& deviceName);
/** "RME" or "ADI-2" in the device name. */
bool isRmeDevice (const juce::String& deviceName);
/** "FiiO" or "KA17" in the device name. */
bool isFiioDevice (const juce::String& deviceName);

enum class DeviceFamily { None, Merging, Rme, Fiio };
/** The family whose profiles apply: the forced hardware profile, else the one recognised from the name. */
DeviceFamily activeFamily (const EngineSettings& settings, const juce::String& deviceName);

int familyProfileCount (DeviceFamily f);
int familyCurrentProfile (const EngineSettings& s, DeviceFamily f);
void familySetProfile (EngineSettings& s, DeviceFamily f, int index);
juce::String familyTitle (DeviceFamily f);
juce::String familyTip (DeviceFamily f);
juce::String profileName (DeviceFamily f, int index);
juce::String profileTag (DeviceFamily f, int index);
juce::String profileDescription (DeviceFamily f, int index);

/** Equipment catalogue (Roon-style device setup): pick your model when the driver name is not recognised. */
struct DeviceModel
{
    const char* id;          // saved in the settings
    const char* brand;
    const char* name;
    const char* series;
    HardwareProfile hardware;
    int profile;             // profile of the family applied with the model
    bool dsd, dop, dxd;      // capabilities shown as tags (dsd = DSD files reach the DAC, as DoP or converted)
};
const std::vector<DeviceModel>& deviceModels();
const DeviceModel* findDeviceModel (const juce::String& id);
/** Models whose brand / name / series contain every word of the query (all of them when empty). */
std::vector<const DeviceModel*> searchDeviceModels (const juce::String& query);
/** Forces the family and profile of the model (and remembers it). */
void applyDeviceModel (EngineSettings& s, const DeviceModel& m);
/** Anubis Pro / SPS (PCM / DXD only): DSD is always converted, never sent as DoP. */
bool modelHasNoDsd (const EngineSettings& s);

/** The settings actually used for this device: the profile's choices override the generic ones. */
EngineSettings resolveProfile (const EngineSettings& settings, const juce::String& deviceName);

juce::String anubisProfileName (AnubisProfile p);
juce::String anubisProfileTag (AnubisProfile p);
juce::String anubisProfileDescription (AnubisProfile p);

/** Badge text for the player bar: "DSD64 → DXD 352,8 kHz [Anubis Ref]", "PCM 44,1 → DXD [Warm]", "DSD64 DoP [Bit-Perfect]". */
juce::String pipelineLabel (const PlaybackPlan& plan, const juce::String& qualityName);

/** How the DAC is connected, guessed from its name and driver (dashboard, Signal Path). */
enum class ConnectionKind { BuiltIn, Network, Usb, Virtual };
ConnectionKind connectionKind (const juce::String& deviceName, const juce::String& backendName = {});
juce::String connectionLabel (const juce::String& deviceName, const juce::String& backendName = {});
/** The family recognised from the name only (drawings), whatever the profile setting. */
DeviceFamily familyFromName (const juce::String& deviceName);

} // namespace nx
