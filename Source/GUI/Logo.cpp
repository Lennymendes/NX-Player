// NX Player — MIT License
#include "Logo.h"
#include "NXPlayerData.h"

namespace nx::logo
{
namespace
{
    // the artwork inside the SVG is the 848 x 848 square at (86, 86); the rest of the 1024 canvas is margin
    constexpr float artX = 86.0f, artSize = 848.0f;

    juce::Drawable* get()
    {
        static std::unique_ptr<juce::Drawable> d = []() -> std::unique_ptr<juce::Drawable>
        {
            if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (NXData::NXPlayerLogo_svg, NXData::NXPlayerLogo_svgSize)))
                return juce::Drawable::createFromSVG (*xml);
            return {};
        }();
        return d.get();
    }
}

void draw (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto* d = get();
    if (d == nullptr || area.isEmpty()) return;
    const float side = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (side, side);
    const float k = side / artSize;
    d->draw (g, 1.0f, juce::AffineTransform::scale (k).translated (box.getX() - artX * k, box.getY() - artX * k));
}
}
