// NX Player — MIT License
// Vector drawings of the audio interfaces (dashboard, onboarding, player bar chip): a desktop network
// interface with a touch screen and a big encoder (Merging Anubis style), a half-rack USB DAC with
// display, two encoders and headphone jacks (RME ADI-2 style), and a generic DAC. Stylised, no logos.
#pragma once

#include "Style.h"
#include "../Engine/DeviceProfiles.h"

namespace nx
{

namespace deviceart
{
    /** Draws the interface into r (keeps its proportions, centred). level: 0..1 animates the meters / ring. */
    void draw (juce::Graphics& g, DeviceFamily family, juce::Rectangle<float> r, const Theme& t, float level = 0.0f, bool lit = true);
    /** Small monochrome glyph for the player bar (16–28 px). */
    void drawGlyph (juce::Graphics& g, DeviceFamily family, juce::Rectangle<float> r, juce::Colour c);
    juce::String familyName (DeviceFamily family);
}

} // namespace nx
