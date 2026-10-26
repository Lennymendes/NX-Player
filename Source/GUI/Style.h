// NX Player — MIT License
// Drawing helpers shared by the interface: glass panels, quality colours, vector icons.
#pragma once

#include "AppContext.h"
#include "../Engine/SignalPath.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace nx::style
{

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

/** Upper case that also handles French accented letters (towupper depends on the C locale). */
inline juce::String upper (const juce::String& s)
{
    auto u = s.toUpperCase();
    static const char* pairs[][2] = { { "\xc3\xa9", "\xc3\x89" }, { "\xc3\xa8", "\xc3\x88" }, { "\xc3\xaa", "\xc3\x8a" }, { "\xc3\xab", "\xc3\x8b" },
                                      { "\xc3\xa0", "\xc3\x80" }, { "\xc3\xa2", "\xc3\x82" }, { "\xc3\xa7", "\xc3\x87" }, { "\xc3\xae", "\xc3\x8e" },
                                      { "\xc3\xaf", "\xc3\x8f" }, { "\xc3\xb4", "\xc3\x94" }, { "\xc3\xb9", "\xc3\x99" }, { "\xc3\xbb", "\xc3\x9b" } };
    for (auto& p : pairs) u = u.replace (juce::String::fromUTF8 (p[0]), juce::String::fromUTF8 (p[1]));
    return u;
}

inline juce::Colour qualityColour (const Theme& t, PathQuality q)
{
    switch (q)
    {
        case PathQuality::BitPerfect:  return t.colour ("--nx-quality-bitperfect", juce::Colour (0xffb07cff));
        case PathQuality::Enhanced:    return t.colour ("--nx-quality-enhanced", t.accent());
        case PathQuality::HighQuality: return t.colour ("--nx-quality-high", juce::Colour (0xff7fe3c0));
        case PathQuality::LowQuality:  return t.colour ("--nx-quality-low", juce::Colour (0xffffb547));
    }
    return t.accent();
}

/** Frosted glass: the (more blurred) backdrop seen through the panel, tint, inner highlight, hairline border. */
inline void paintGlass (juce::Graphics& g, juce::Component& panel, AppContext& ctx, juce::Rectangle<float> area,
                        float radius, bool strong = false)
{
    const auto& t = ctx.theme();
    juce::Path shape;
    shape.addRoundedRectangle (area, radius);

    g.saveState();
    g.reduceClipRegion (shape);
    if (auto* owner = ctx.backdropOwner(); owner != nullptr && ctx.backdrop().isValid())
    {
        const auto inOwner = owner->getLocalArea (&panel, area);
        const auto& img = ctx.backdrop();
        g.setOpacity (1.0f);
        const float sx = (float) img.getWidth() / juce::jmax (1.0f, (float) owner->getWidth());
        const float sy = (float) img.getHeight() / juce::jmax (1.0f, (float) owner->getHeight());
        g.drawImage (img, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                     juce::roundToInt (inOwner.getX() * sx), juce::roundToInt (inOwner.getY() * sy),
                     juce::jmax (1, juce::roundToInt (inOwner.getWidth() * sx)), juce::jmax (1, juce::roundToInt (inOwner.getHeight() * sy)));
    }
    g.setColour (strong ? t.colour ("--nx-glass-strong", juce::Colour (0xcc0a0d10)) : t.glassTint());
    g.fillRect (area);
    g.setGradientFill (juce::ColourGradient (t.colour ("--nx-highlight", juce::Colours::white.withAlpha (0.05f)), area.getX(), area.getY(),
                                             juce::Colours::transparentWhite, area.getX(), area.getY() + juce::jmin (80.0f, area.getHeight()), false));
    g.fillRect (area);
    g.restoreState();

    g.setColour (t.border());
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
}

inline void paintBadge (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c, bool filled = false)
{
    g.setColour (filled ? c : c.withAlpha (0.14f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.35f);
    g.setColour (filled ? juce::Colours::black.withAlpha (0.85f) : c);
    g.setFont (font (r.getHeight() * 0.62f, true));
    g.drawText (text, r, juce::Justification::centred);
}

inline juce::String formatTime (double seconds)
{
    if (seconds < 0 || ! std::isfinite (seconds)) seconds = 0;
    const int s = (int) seconds;
    return s >= 3600 ? juce::String::formatted ("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
                     : juce::String::formatted ("%d:%02d", s / 60, s % 60);
}

//==============================================================================
/** Vector icons (24 × 24 design grid, scaled into r). */
enum class Icon { Play, Pause, Next, Previous, Shuffle, Repeat, Volume, VolumeMute, Lyrics, Queue, Mini, Home, Search, Clock,
                  Album, Artist, Song, Playlist, Folder, Network, Cloud, Settings, Heart, Plus, Close, Expand, Signal, Theme, Plugin,
                  ChevronLeft, ChevronRight, SidebarToggle, Grid, Download, Trash };

inline juce::Path iconPath (Icon icon)
{
    juce::Path p;
    switch (icon)
    {
        case Icon::Play:     p.addTriangle (7, 4.5f, 7, 19.5f, 19.5f, 12); break;
        case Icon::Pause:    p.addRoundedRectangle (6, 4.5f, 4, 15, 1.2f); p.addRoundedRectangle (14, 4.5f, 4, 15, 1.2f); break;
        case Icon::Next:     p.addTriangle (5, 5, 5, 19, 15, 12); p.addRoundedRectangle (16, 5, 2.6f, 14, 1); break;
        case Icon::Previous: p.addTriangle (19, 5, 19, 19, 9, 12); p.addRoundedRectangle (5.4f, 5, 2.6f, 14, 1); break;
        case Icon::Shuffle:
            p.startNewSubPath (3, 7); p.cubicTo (10, 7, 12, 17, 19, 17); p.startNewSubPath (3, 17); p.cubicTo (10, 17, 12, 7, 19, 7);
            p.addTriangle (18, 4, 22, 7, 18, 10); p.addTriangle (18, 14, 22, 17, 18, 20); break;
        case Icon::Repeat:
            p.addRoundedRectangle (4, 7, 16, 11, 4); p.addTriangle (13, 4, 17, 7, 13, 10); break;
        case Icon::Volume:
            p.addTriangle (3, 12, 11, 5, 11, 19); p.addRectangle (3, 9, 4, 6);
            p.startNewSubPath (14, 8.5f); p.quadraticTo (17, 12, 14, 15.5f);
            p.startNewSubPath (16.5f, 6); p.quadraticTo (21.5f, 12, 16.5f, 18); break;
        case Icon::VolumeMute:
            p.addTriangle (3, 12, 11, 5, 11, 19); p.addRectangle (3, 9, 4, 6);
            p.startNewSubPath (15, 9); p.lineTo (21, 15); p.startNewSubPath (21, 9); p.lineTo (15, 15); break;
        case Icon::Lyrics:
            p.addRoundedRectangle (3, 4, 18, 13, 3); p.addTriangle (7, 16, 7, 21, 12, 16);
            p.startNewSubPath (7, 9); p.lineTo (17, 9); p.startNewSubPath (7, 12.5f); p.lineTo (14, 12.5f); break;
        case Icon::Queue:
            for (int i = 0; i < 3; ++i) { p.startNewSubPath (4, 6.0f + i * 5.5f); p.lineTo (15, 6.0f + i * 5.5f); }
            p.addTriangle (17, 13, 17, 21, 22, 17); break;
        case Icon::Mini:     p.addRoundedRectangle (3, 5, 18, 14, 3); p.addRoundedRectangle (12, 12, 7, 5, 1.5f); break;
        case Icon::Home:     p.startNewSubPath (3, 11); p.lineTo (12, 3.5f); p.lineTo (21, 11); p.addRoundedRectangle (6, 10, 12, 10.5f, 2); break;
        case Icon::Search:   p.addEllipse (4, 4, 12, 12); p.startNewSubPath (14.5f, 14.5f); p.lineTo (20.5f, 20.5f); break;
        case Icon::Clock:    p.addEllipse (3.5f, 3.5f, 17, 17); p.startNewSubPath (12, 7); p.lineTo (12, 12); p.lineTo (16, 14); break;
        case Icon::Album:    p.addRoundedRectangle (3, 3, 18, 18, 3); p.addEllipse (8, 8, 8, 8); p.addEllipse (11, 11, 2, 2); break;
        case Icon::Artist:   p.addEllipse (8, 3.5f, 8, 8); p.startNewSubPath (4, 20.5f); p.cubicTo (5, 14, 19, 14, 20, 20.5f); break;
        case Icon::Song:     p.startNewSubPath (10, 17); p.lineTo (10, 4.5f); p.lineTo (19, 3); p.lineTo (19, 15); p.addEllipse (5, 14.5f, 5.5f, 5); p.addEllipse (14, 12.5f, 5.5f, 5); break;
        case Icon::Playlist:
            for (int i = 0; i < 3; ++i) { p.startNewSubPath (3, 6.0f + i * 5.0f); p.lineTo (13, 6.0f + i * 5.0f); }
            p.startNewSubPath (18, 17); p.lineTo (18, 6); p.lineTo (21.5f, 7); p.addEllipse (14, 15, 4.5f, 4); break;
        case Icon::Folder:   p.startNewSubPath (3, 7); p.lineTo (3, 19); p.lineTo (21, 19); p.lineTo (21, 9); p.lineTo (11, 9); p.lineTo (9, 6); p.lineTo (3, 6); p.closeSubPath(); break;
        case Icon::Network:  p.addRoundedRectangle (8, 3, 8, 6, 1.5f); p.addRoundedRectangle (2.5f, 15, 7, 6, 1.5f); p.addRoundedRectangle (14.5f, 15, 7, 6, 1.5f);
                             p.startNewSubPath (12, 9); p.lineTo (12, 12); p.startNewSubPath (6, 15); p.lineTo (6, 12); p.lineTo (18, 12); p.lineTo (18, 15); break;
        case Icon::Cloud:    p.startNewSubPath (7, 18); p.cubicTo (1.5f, 18, 1.5f, 11, 7, 11); p.cubicTo (8, 5, 16, 5, 17, 10.5f); p.cubicTo (22.5f, 10.5f, 22.5f, 18, 17, 18); p.closeSubPath(); break;
        case Icon::Settings: p.addEllipse (8.5f, 8.5f, 7, 7);
                             for (int i = 0; i < 8; ++i) { const float a = (float) i * juce::MathConstants<float>::pi / 4.0f;
                                 p.startNewSubPath (12 + 6.5f * std::sin (a), 12 - 6.5f * std::cos (a)); p.lineTo (12 + 9.5f * std::sin (a), 12 - 9.5f * std::cos (a)); } break;
        case Icon::Heart:    p.startNewSubPath (12, 20); p.cubicTo (2, 13, 3, 4, 9, 5); p.cubicTo (10.5f, 5.3f, 11.5f, 6.5f, 12, 7.5f);
                             p.cubicTo (12.5f, 6.5f, 13.5f, 5.3f, 15, 5); p.cubicTo (21, 4, 22, 13, 12, 20); p.closeSubPath(); break;
        case Icon::Plus:     p.startNewSubPath (12, 5); p.lineTo (12, 19); p.startNewSubPath (5, 12); p.lineTo (19, 12); break;
        case Icon::Close:    p.startNewSubPath (6, 6); p.lineTo (18, 18); p.startNewSubPath (18, 6); p.lineTo (6, 18); break;
        case Icon::Expand:   p.startNewSubPath (4, 10); p.lineTo (4, 4); p.lineTo (10, 4); p.startNewSubPath (20, 14); p.lineTo (20, 20); p.lineTo (14, 20);
                             p.startNewSubPath (4, 4); p.lineTo (10, 10); p.startNewSubPath (20, 20); p.lineTo (14, 14); break;
        case Icon::Signal:   for (int i = 0; i < 4; ++i) p.addRoundedRectangle (3.0f + i * 5.0f, 17.0f - i * 4.0f, 3.2f, 4.0f + i * 4.0f, 1.0f); break;
        case Icon::Theme:    p.addEllipse (3.5f, 3.5f, 17, 17); p.addEllipse (7, 8, 3, 3); p.addEllipse (12, 6.5f, 3, 3); p.addEllipse (15, 11, 3, 3); break;
        case Icon::Plugin:   p.addRoundedRectangle (4, 7, 16, 13, 2.5f); p.addRectangle (8, 3, 2.5f, 4); p.addRectangle (13.5f, 3, 2.5f, 4); break;
        case Icon::ChevronLeft:  p.startNewSubPath (15, 5); p.lineTo (8, 12); p.lineTo (15, 19); break;
        case Icon::ChevronRight: p.startNewSubPath (9, 5); p.lineTo (16, 12); p.lineTo (9, 19); break;
        case Icon::SidebarToggle: p.addRoundedRectangle (3, 4.5f, 18, 15, 3); p.startNewSubPath (9.5f, 4.5f); p.lineTo (9.5f, 19.5f);
                                  p.startNewSubPath (5.2f, 8.5f); p.lineTo (7.4f, 8.5f); p.startNewSubPath (5.2f, 11.5f); p.lineTo (7.4f, 11.5f); break;
        case Icon::Grid:     p.addRoundedRectangle (3.5f, 3.5f, 7.5f, 7.5f, 2); p.addRoundedRectangle (13, 3.5f, 7.5f, 7.5f, 2);
                             p.addRoundedRectangle (3.5f, 13, 7.5f, 7.5f, 2); p.addRoundedRectangle (13, 13, 7.5f, 7.5f, 2); break;
        case Icon::Download: p.startNewSubPath (12, 4); p.lineTo (12, 15); p.startNewSubPath (7.5f, 10.5f); p.lineTo (12, 15); p.lineTo (16.5f, 10.5f);
                             p.startNewSubPath (5, 19.5f); p.lineTo (19, 19.5f); break;
        case Icon::Trash:    p.startNewSubPath (4, 6.5f); p.lineTo (20, 6.5f); p.startNewSubPath (9.5f, 6.5f); p.lineTo (10, 4); p.lineTo (14, 4); p.lineTo (14.5f, 6.5f);
                             p.startNewSubPath (6, 6.5f); p.lineTo (7, 20); p.lineTo (17, 20); p.lineTo (18, 6.5f); break;
    }
    return p;
}

inline bool iconIsFilled (Icon i)
{
    switch (i)
    {
        case Icon::Play: case Icon::Pause: case Icon::Next: case Icon::Previous: case Icon::Heart: case Icon::Signal: return true;
        default: return false;
    }
}

inline void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c, float stroke = 1.8f)
{
    auto p = iconPath (icon);
    p.applyTransform (juce::AffineTransform::scale (r.getWidth() / 24.0f, r.getHeight() / 24.0f).translated (r.getX(), r.getY()));
    g.setColour (c);
    if (iconIsFilled (icon)) g.fillPath (p);
    else g.strokePath (p, juce::PathStrokeType (stroke * r.getWidth() / 24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace nx::style
