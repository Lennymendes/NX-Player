// NX Player — MIT License
// The official NX Player logo (Resources/NXPlayerLogo.svg, embedded), drawn as a vector at any size.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace nx::logo
{
/** Paints the logo (the rounded square with the waveform) into `area`, keeping its proportions. */
void draw (juce::Graphics& g, juce::Rectangle<float> area);

/** A plain component that shows the logo. */
struct Mark : juce::Component
{
    Mark() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override { draw (g, getLocalBounds().toFloat()); }
};
}
