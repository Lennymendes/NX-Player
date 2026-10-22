// NX Player — MIT License
// Roon-style signal path: every stage between the file and the DAC, with its quality.
#pragma once

#include <juce_core/juce_core.h>
#include "../Core/Text.h"
#include <vector>

namespace nx
{

enum class PathQuality
{
    BitPerfect,   // the samples reach the DAC unchanged (violet)
    Enhanced,     // deliberate high-quality processing (upsampling, DSD -> DXD, DSP) (vert)
    HighQuality,  // lossless but not bit-perfect (volume, dither to the DAC's depth) (vert clair)
    LowQuality    // lossy source, OS mixer, shared mode (orange)
};

inline juce::String toString (PathQuality q)
{
    switch (q)
    {
        case PathQuality::BitPerfect:  return "Bit-perfect";
        case PathQuality::Enhanced:    return "Amélioré"_t;
        case PathQuality::HighQuality: return "Haute qualité"_t;
        case PathQuality::LowQuality:  return "Qualité réduite"_t;
    }
    return "?";
}

struct SignalPathNode
{
    juce::String stage;     // "Source", "Décodage", "Conversion", "Volume", "DSP", "Sortie"
    juce::String title;     // "FLAC 24 bits / 192 kHz"
    juce::String detail;    // "Qobuz · 24/192"
    PathQuality quality = PathQuality::BitPerfect;
};

struct SignalPath
{
    std::vector<SignalPathNode> nodes;
    PathQuality overall = PathQuality::BitPerfect;
    juce::String summary;

    void add (const juce::String& stage, const juce::String& title, const juce::String& detail, PathQuality q)
    {
        nodes.push_back ({ stage, title, detail, q });
        if ((int) q > (int) overall) overall = q;
    }

    juce::var toVar() const
    {
        juce::Array<juce::var> arr;
        for (auto& n : nodes)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("stage", n.stage);
            o->setProperty ("title", n.title);
            o->setProperty ("detail", n.detail);
            o->setProperty ("quality", toString (n.quality));
            arr.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("nodes", arr);
        root->setProperty ("overall", toString (overall));
        root->setProperty ("summary", summary);
        return juce::var (root);
    }
};

} // namespace nx
