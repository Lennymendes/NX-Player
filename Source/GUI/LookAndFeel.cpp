// NX Player — MIT License
#include "LookAndFeel.h"

namespace nx
{

NXLookAndFeel::NXLookAndFeel() { applyTheme (theme); }

void NXLookAndFeel::applyTheme (const Theme& t)
{
    theme = t;
    const auto bg = t.bg(), text = t.textCol(), dim = t.textDim(), accent = t.accent();
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, juce::Colours::white.withAlpha (0.07f));
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, t.colour ("--nx-on-accent", juce::Colours::black));
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white.withAlpha (0.06f));
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, t.border());
    setColour (juce::TextEditor::focusedOutlineColourId, accent.withAlpha (0.7f));
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha (0.06f));
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, t.border());
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, t.surface());
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha (0.18f));
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, juce::Colours::white.withAlpha (0.12f));
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, t.surface());
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::AlertWindow::backgroundColourId, t.surface());
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, t.border());
    setColour (juce::HyperlinkButton::textColourId, accent);
}

void NXLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : base;
    if (hi) c = c.brighter (0.15f).withMultipliedAlpha (1.3f);
    if (down) c = c.darker (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (theme.border());
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
}

juce::Font NXLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return style::font (juce::jmin (15.0f, (float) h * 0.48f), true); }

void NXLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    // iOS-style switch on the right, label on the left
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromRight (42.0f).withSizeKeepingCentre (38.0f, 22.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? theme.accent() : juce::Colours::white.withAlpha (hi ? 0.22f : 0.15f));
    g.fillRoundedRectangle (sw, 11.0f);
    const float knob = 18.0f;
    auto k = juce::Rectangle<float> (knob, knob).withCentre ({ on ? sw.getRight() - 11.0f : sw.getX() + 11.0f, sw.getCentreY() });
    g.setColour (on ? theme.colour ("--nx-on-accent", juce::Colours::black) : theme.textCol());
    g.fillEllipse (k);
    g.setColour (b.isEnabled() ? theme.textCol() : theme.textDim());
    g.setFont (style::font (14.0f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedRight (6), juce::Justification::centredLeft, 2);
}

void NXLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                      juce::Slider::SliderStyle, juce::Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float th = s.isMouseOverOrDragging() ? 5.0f : 4.0f;
    auto track = juce::Rectangle<float> ((float) x, cy - th / 2, (float) w, th);
    g.setColour (findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, th / 2);
    g.setGradientFill (juce::ColourGradient (theme.accent(), (float) x, cy, theme.accent2(), (float) (x + w), cy, false));
    g.fillRoundedRectangle (track.withRight (pos), th / 2);
    if (s.isMouseOverOrDragging())
    {
        g.setColour (theme.textCol());
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, cy }));
    }
}

void NXLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                                   int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.35f : over ? 0.25f : 0.12f));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
}

void NXLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, juce::jmin (10.0f, (float) h * 0.5f));
}

void NXLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? e.findColour (juce::TextEditor::focusedOutlineColourId) : e.findColour (juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f, juce::jmin (10.0f, (float) h * 0.5f), 1.0f);
}

void NXLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 9.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 18.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4, ay - 2); arrow.lineTo (ax, ay + 2); arrow.lineTo (ax + 4, ay - 2);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void NXLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (theme.surface());
    g.setColour (theme.border());
    g.drawRect (0, 0, w, h);
}

void NXLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (theme.surface());
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 6.0f);
    g.setColour (theme.border());
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 6.0f, 1.0f);
    g.setColour (theme.textCol());
    g.setFont (style::font (13.0f));
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
}

} // namespace nx
