// NX Player — MIT License
#include "DeviceArt.h"

namespace nx::deviceart
{

juce::String familyName (DeviceFamily f)
{
    switch (f)
    {
        case DeviceFamily::Merging: return "Merging Anubis";
        case DeviceFamily::Rme:     return "RME ADI-2 Pro";
        case DeviceFamily::None:    break;
    }
    return "DAC";
}

namespace
{
    juce::Rectangle<float> fit (juce::Rectangle<float> r, float aspect)
    {
        const float w = juce::jmin (r.getWidth(), r.getHeight() * aspect);
        return r.withSizeKeepingCentre (w, w / aspect);
    }

    void knob (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour ring, float level, bool lit)
    {
        juce::ColourGradient body (juce::Colour (0xff3a3f45), c.x - radius, c.y - radius, juce::Colour (0xff121417), c.x + radius, c.y + radius, false);
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c).reduced (radius * 0.08f), radius * 0.05f);
        // knurled cap
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b6168), c.x, c.y - radius * 0.7f, juce::Colour (0xff23272b), c.x, c.y + radius * 0.7f, false));
        g.fillEllipse (juce::Rectangle<float> (radius * 1.45f, radius * 1.45f).withCentre (c));
        // LED ring (volume)
        if (lit)
        {
            juce::Path arc;
            const float a0 = -2.4f, a1 = a0 + 4.8f * juce::jlimit (0.15f, 1.0f, 0.55f + level * 0.45f);
            arc.addCentredArc (c.x, c.y, radius * 1.18f, radius * 1.18f, 0.0f, a0, a1, true);
            g.setColour (ring.withAlpha (0.35f));
            g.strokePath (arc, juce::PathStrokeType (radius * 0.22f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (ring);
            g.strokePath (arc, juce::PathStrokeType (radius * 0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // pointer
        const float a = -2.4f + 4.8f * 0.62f;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawLine (c.x + std::sin (a) * radius * 0.25f, c.y - std::cos (a) * radius * 0.25f, c.x + std::sin (a) * radius * 0.6f, c.y - std::cos (a) * radius * 0.6f, juce::jmax (1.0f, radius * 0.07f));
    }

    void jack (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setColour (juce::Colour (0xff8c939b));
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
        g.setColour (juce::Colour (0xff0b0c0e));
        g.fillEllipse (juce::Rectangle<float> (r * 1.15f, r * 1.15f).withCentre (c));
    }

    void meters (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour a, juce::Colour b, float level, int bars)
    {
        const float w = r.getWidth() / (float) bars;
        for (int i = 0; i < bars; ++i)
        {
            const float v = juce::jlimit (0.08f, 1.0f, level * (0.55f + 0.45f * std::sin ((float) i * 1.7f + level * 9.0f)));
            auto bar = juce::Rectangle<float> (r.getX() + w * (float) i + w * 0.18f, r.getBottom() - r.getHeight() * v, w * 0.64f, r.getHeight() * v);
            g.setGradientFill (juce::ColourGradient (b, bar.getX(), r.getBottom(), a, bar.getX(), r.getY(), false));
            g.fillRect (bar);
        }
    }

    void drawAnubis (juce::Graphics& g, juce::Rectangle<float> area, const Theme& t, float level, bool lit)
    {
        // desktop unit seen from the front, slanted top, touch screen on the left, big encoder on the right
        auto r = fit (area, 2.15f);
        juce::Path body;
        const float slant = r.getHeight() * 0.10f;
        body.startNewSubPath (r.getX() + slant, r.getY());
        body.lineTo (r.getRight() - slant, r.getY());
        body.lineTo (r.getRight(), r.getBottom() - r.getHeight() * 0.06f);
        body.lineTo (r.getX(), r.getBottom() - r.getHeight() * 0.06f);
        body.closeSubPath();
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (r.getHeight() * 0.18f), { 0, (int) (r.getHeight() * 0.06f) }).drawForPath (g, body);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c3035), r.getX(), r.getY(), juce::Colour (0xff0e1013), r.getX(), r.getBottom(), false));
        g.fillPath (body);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.strokePath (body, juce::PathStrokeType (juce::jmax (1.0f, r.getHeight() * 0.012f)));
        // feet
        g.setColour (juce::Colour (0xff050607));
        g.fillRoundedRectangle (r.getX() + r.getWidth() * 0.08f, r.getBottom() - r.getHeight() * 0.07f, r.getWidth() * 0.12f, r.getHeight() * 0.07f, 2.0f);
        g.fillRoundedRectangle (r.getRight() - r.getWidth() * 0.20f, r.getBottom() - r.getHeight() * 0.07f, r.getWidth() * 0.12f, r.getHeight() * 0.07f, 2.0f);

        // touch screen
        auto screen = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.07f, r.getY() + r.getHeight() * 0.16f, r.getWidth() * 0.55f, r.getHeight() * 0.62f);
        g.setColour (juce::Colour (0xff020304));
        g.fillRoundedRectangle (screen.expanded (r.getHeight() * 0.02f), r.getHeight() * 0.04f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1a22), screen.getX(), screen.getY(), juce::Colour (0xff04080b), screen.getRight(), screen.getBottom(), false));
        g.fillRoundedRectangle (screen, r.getHeight() * 0.03f);
        if (lit)
        {
            auto s = screen.reduced (screen.getHeight() * 0.10f);
            // status line + level meters + big rate readout
            g.setColour (t.accent().withAlpha (0.9f));
            g.fillRoundedRectangle (s.getX(), s.getY(), s.getWidth() * 0.32f, s.getHeight() * 0.10f, 1.5f);
            g.setColour (t.textDim().withAlpha (0.6f));
            g.fillRoundedRectangle (s.getRight() - s.getWidth() * 0.22f, s.getY(), s.getWidth() * 0.22f, s.getHeight() * 0.10f, 1.5f);
            meters (g, juce::Rectangle<float> (s.getX(), s.getY() + s.getHeight() * 0.22f, s.getWidth() * 0.40f, s.getHeight() * 0.74f), t.accent(), t.accent2().withAlpha (0.5f), level, 8);
            g.setColour (t.textCol().withAlpha (0.92f));
            g.setFont (style::font (s.getHeight() * 0.30f, true));
            g.drawText ("DXD", juce::Rectangle<float> (s.getX() + s.getWidth() * 0.46f, s.getY() + s.getHeight() * 0.22f, s.getWidth() * 0.54f, s.getHeight() * 0.38f), juce::Justification::centredLeft);
            g.setColour (t.accent2());
            g.setFont (style::font (s.getHeight() * 0.16f, true));
            g.drawText ("352.8 kHz", juce::Rectangle<float> (s.getX() + s.getWidth() * 0.46f, s.getY() + s.getHeight() * 0.62f, s.getWidth() * 0.54f, s.getHeight() * 0.24f), juce::Justification::centredLeft);
        }
        // encoder
        const float kr = r.getHeight() * 0.25f;
        knob (g, { r.getRight() - r.getWidth() * 0.19f, r.getCentreY() - r.getHeight() * 0.02f }, kr, t.accent(), level, lit);
        // small front buttons
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (juce::Colour (0xff3c4248));
            g.fillRoundedRectangle (screen.getRight() + r.getWidth() * 0.035f, screen.getY() + (float) i * screen.getHeight() * 0.36f, r.getWidth() * 0.035f, screen.getHeight() * 0.22f, 2.0f);
        }
    }

    void drawRme (juce::Graphics& g, juce::Rectangle<float> area, const Theme& t, float level, bool lit)
    {
        // half-rack unit: two headphone jacks, colour display, two small encoders, big volume knob
        auto r = fit (area, 3.2f);
        juce::Path body;
        body.addRoundedRectangle (r, r.getHeight() * 0.06f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (r.getHeight() * 0.2f), { 0, (int) (r.getHeight() * 0.07f) }).drawForPath (g, body);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3f44), r.getX(), r.getY(), juce::Colour (0xff17191c), r.getX(), r.getBottom(), false));
        g.fillPath (body);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.strokePath (body, juce::PathStrokeType (juce::jmax (1.0f, r.getHeight() * 0.015f)));
        // rack ears hint
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (r.getX() + r.getWidth() * 0.035f, r.getY() + r.getHeight() * 0.08f, 1.5f, r.getHeight() * 0.84f);
        g.fillRect (r.getRight() - r.getWidth() * 0.035f, r.getY() + r.getHeight() * 0.08f, 1.5f, r.getHeight() * 0.84f);

        // headphone jacks
        const float jr = r.getHeight() * 0.11f;
        jack (g, { r.getX() + r.getWidth() * 0.10f, r.getY() + r.getHeight() * 0.36f }, jr);
        jack (g, { r.getX() + r.getWidth() * 0.10f, r.getY() + r.getHeight() * 0.68f }, jr * 0.8f);
        // display
        auto screen = juce::Rectangle<float> (r.getX() + r.getWidth() * 0.20f, r.getY() + r.getHeight() * 0.18f, r.getWidth() * 0.32f, r.getHeight() * 0.62f);
        g.setColour (juce::Colour (0xff030405));
        g.fillRoundedRectangle (screen.expanded (2.0f), 3.0f);
        g.setColour (juce::Colour (0xff070c12));
        g.fillRoundedRectangle (screen, 2.0f);
        if (lit)
        {
            auto s = screen.reduced (screen.getHeight() * 0.10f);
            meters (g, s.removeFromLeft (s.getWidth() * 0.34f), juce::Colour (0xff3cff7a), juce::Colour (0xffffd34d), level, 2);
            s.removeFromLeft (s.getWidth() * 0.08f);
            g.setColour (juce::Colour (0xffeaf2ff));
            g.setFont (style::font (s.getHeight() * 0.30f, true));
            g.drawText ("-12.0", s.removeFromTop (s.getHeight() * 0.5f), juce::Justification::centredLeft);
            g.setColour (t.accent2());
            g.setFont (style::font (s.getHeight() * 0.42f, true));
            g.drawText ("DSD", s, juce::Justification::centredLeft);
        }
        // small encoders + buttons
        for (int i = 0; i < 2; ++i)
            knob (g, { r.getX() + r.getWidth() * (0.585f + 0.085f * (float) i), r.getY() + r.getHeight() * 0.36f }, r.getHeight() * 0.10f, t.accent(), 0.0f, false);
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (juce::Colour (0xff50565d));
            g.fillRoundedRectangle (r.getX() + r.getWidth() * (0.565f + 0.045f * (float) i), r.getY() + r.getHeight() * 0.62f, r.getWidth() * 0.03f, r.getHeight() * 0.14f, 1.5f);
        }
        // volume
        knob (g, { r.getRight() - r.getWidth() * 0.14f, r.getCentreY() }, r.getHeight() * 0.30f, t.accent(), level, lit);
    }

    void drawGeneric (juce::Graphics& g, juce::Rectangle<float> area, const Theme& t, float level, bool lit)
    {
        auto r = fit (area, 2.4f);
        juce::Path body;
        body.addRoundedRectangle (r, r.getHeight() * 0.12f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), (int) (r.getHeight() * 0.2f), { 0, (int) (r.getHeight() * 0.07f) }).drawForPath (g, body);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff30353a), r.getX(), r.getY(), juce::Colour (0xff121417), r.getX(), r.getBottom(), false));
        g.fillPath (body);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.strokePath (body, juce::PathStrokeType (1.2f));
        // status LEDs
        for (int i = 0; i < 4; ++i)
        {
            const bool on = lit && i <= juce::roundToInt (level * 3.0f + 0.5f);
            auto led = juce::Rectangle<float> (r.getHeight() * 0.09f, r.getHeight() * 0.09f).withCentre ({ r.getX() + r.getWidth() * (0.12f + 0.08f * (float) i), r.getCentreY() });
            if (on) { g.setColour (t.accent().withAlpha (0.3f)); g.fillEllipse (led.expanded (led.getWidth() * 0.8f)); }
            g.setColour (on ? t.accent() : juce::Colour (0xff3a3f44));
            g.fillEllipse (led);
        }
        jack (g, { r.getX() + r.getWidth() * 0.55f, r.getCentreY() }, r.getHeight() * 0.1f);
        knob (g, { r.getRight() - r.getWidth() * 0.18f, r.getCentreY() }, r.getHeight() * 0.28f, t.accent(), level, lit);
    }
}

void draw (juce::Graphics& g, DeviceFamily family, juce::Rectangle<float> r, const Theme& t, float level, bool lit)
{
    switch (family)
    {
        case DeviceFamily::Merging: drawAnubis (g, r, t, level, lit); return;
        case DeviceFamily::Rme:     drawRme (g, r, t, level, lit); return;
        case DeviceFamily::None:    drawGeneric (g, r, t, level, lit); return;
    }
}

void drawGlyph (juce::Graphics& g, DeviceFamily family, juce::Rectangle<float> r, juce::Colour c)
{
    const float s = juce::jmax (1.2f, r.getHeight() * 0.07f);
    g.setColour (c);
    switch (family)
    {
        case DeviceFamily::Merging:
        {
            auto b = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.55f);
            juce::Path p;
            p.startNewSubPath (b.getX() + b.getHeight() * 0.2f, b.getY());
            p.lineTo (b.getRight() - b.getHeight() * 0.2f, b.getY());
            p.lineTo (b.getRight(), b.getBottom());
            p.lineTo (b.getX(), b.getBottom());
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (s));
            g.fillRoundedRectangle (b.getX() + b.getWidth() * 0.14f, b.getY() + b.getHeight() * 0.25f, b.getWidth() * 0.42f, b.getHeight() * 0.5f, 1.0f);
            g.drawEllipse (juce::Rectangle<float> (b.getHeight() * 0.62f, b.getHeight() * 0.62f).withCentre ({ b.getRight() - b.getWidth() * 0.2f, b.getCentreY() }), s);
            break;
        }
        case DeviceFamily::Rme:
        {
            auto b = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.42f);
            g.drawRoundedRectangle (b, 2.0f, s);
            g.fillEllipse (juce::Rectangle<float> (b.getHeight() * 0.3f, b.getHeight() * 0.3f).withCentre ({ b.getX() + b.getWidth() * 0.1f, b.getCentreY() }));
            g.fillRect (b.getX() + b.getWidth() * 0.22f, b.getY() + b.getHeight() * 0.25f, b.getWidth() * 0.3f, b.getHeight() * 0.5f);
            g.drawEllipse (juce::Rectangle<float> (b.getHeight() * 0.62f, b.getHeight() * 0.62f).withCentre ({ b.getRight() - b.getWidth() * 0.15f, b.getCentreY() }), s);
            break;
        }
        case DeviceFamily::None:
        {
            auto b = r.withSizeKeepingCentre (r.getWidth() * 0.9f, r.getHeight() * 0.48f);
            g.drawRoundedRectangle (b, b.getHeight() * 0.2f, s);
            g.fillEllipse (juce::Rectangle<float> (b.getHeight() * 0.22f, b.getHeight() * 0.22f).withCentre ({ b.getX() + b.getWidth() * 0.18f, b.getCentreY() }));
            g.drawEllipse (juce::Rectangle<float> (b.getHeight() * 0.56f, b.getHeight() * 0.56f).withCentre ({ b.getRight() - b.getWidth() * 0.2f, b.getCentreY() }), s);
            break;
        }
    }
}

} // namespace nx::deviceart
