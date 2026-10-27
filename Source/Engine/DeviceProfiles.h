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

enum class DeviceFamily { None, Merging, Rme };
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
