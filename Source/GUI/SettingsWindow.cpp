// NX Player — MIT License
#include "SettingsWindow.h"

namespace nx
{

SettingsWindow::SettingsWindow (AppContext& c) : ctx (c), page (c), closeButton (style::Icon::Close, "Fermer (Échap)"_t, c)
{
    page.embedded = true;
    addAndMakeVisible (page);
    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { close(); };
    setWantsKeyboardFocus (false);
}

void SettingsWindow::open (const juce::String& tab)
{
    page.setTab (SettingsPage::tabIds().contains (tab) ? tab : (page.getTab().isNotEmpty() ? page.getTab() : juce::String ("audio")));
    setVisible (true);
    toFront (false);
    resized();
    repaint();
}

void SettingsWindow::close()
{
    if (! isVisible()) return;
    setVisible (false);
    if (onClose) onClose();
}

juce::Rectangle<int> SettingsWindow::panelBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (1220, getWidth() - 60), juce::jmin (860, getHeight() - 48));
}

juce::Rectangle<int> SettingsWindow::tabBounds (int i) const
{
    auto p = panelBounds();
    return { p.getX() + 14, p.getY() + 78 + i * 44, 216, 38 };
}

void SettingsWindow::resized()
{
    auto p = panelBounds();
    closeButton.setBounds (p.getRight() - 46, p.getY() + 12, 32, 32);
    page.setBounds (p.withTrimmedLeft (244).withTrimmedTop (56).withTrimmedRight (6).withTrimmedBottom (6));
}

void SettingsWindow::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < SettingsPage::tabIds().size(); ++i) if (tabBounds (i).contains (e.getPosition())) h = i;
    if (h != hoverTab) { hoverTab = h; repaint(); }
}

void SettingsWindow::mouseUp (const juce::MouseEvent& e)
{
    if (! panelBounds().contains (e.getPosition())) { close(); return; }
    const auto ids = SettingsPage::tabIds();
    for (int i = 0; i < ids.size(); ++i)
        if (tabBounds (i).contains (e.getPosition())) { page.setTab (ids[i]); repaint(); return; }
}

void SettingsWindow::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    auto p = panelBounds().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (p, t.radius() + 4.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 40, { 0, 16 }).drawForPath (g, shape);
    style::paintGlass (g, *this, ctx, p, t.radius() + 4.0f, true);
    g.setColour (t.bg().withAlpha (0.55f));
    g.fillRoundedRectangle (p, t.radius() + 4.0f);

    // navigation
    auto nav = p.withWidth (244.0f);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRoundedRectangle (nav.reduced (6.0f), t.radius());
    g.setColour (t.border());
    g.fillRect (nav.getRight(), p.getY() + 16.0f, 1.0f, p.getHeight() - 32.0f);
    auto title = juce::Rectangle<float> (p.getX() + 24.0f, p.getY() + 20.0f, 200.0f, 40.0f);
    style::drawIcon (g, style::Icon::Settings, title.removeFromLeft (26.0f).withSizeKeepingCentre (22, 22), t.accent());
    title.removeFromLeft (10.0f);
    g.setColour (t.textCol());
    g.setFont (style::font (22.0f, true));
    g.drawText ("Réglages"_t, title, juce::Justification::centredLeft);

    const auto ids = SettingsPage::tabIds();
    const auto labels = SettingsPage::tabLabels();
    const style::Icon icons[] = { style::Icon::Signal, style::Icon::Library, style::Icon::Theme, style::Icon::Phone, style::Icon::Studio, style::Icon::Plugin };
    for (int i = 0; i < ids.size(); ++i)
    {
        auto r = tabBounds (i).toFloat();
        const bool on = ids[i] == page.getTab();
        if (on || i == hoverTab)
        {
            g.setColour (on ? t.accent().withAlpha (0.16f) : juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 9.0f);
        }
        if (on)
        {
            g.setColour (t.accent());
            g.fillRoundedRectangle (r.getX(), r.getY() + 9.0f, 3.0f, r.getHeight() - 18.0f, 1.5f);
        }
        style::drawIcon (g, icons[juce::jmin (i, 5)], r.removeFromLeft (40.0f).withSizeKeepingCentre (18, 18), on ? t.accent() : t.textCol().withAlpha (0.75f));
        g.setColour (on ? t.textCol() : t.textCol().withAlpha (0.82f));
        g.setFont (style::font (14.0f, on));
        g.drawText (labels[i], r, juce::Justification::centredLeft, true);
    }
    g.setColour (t.textDim());
    g.setFont (style::font (11.5f));
    g.drawText ("NX Player " NX_VERSION, juce::Rectangle<float> (nav.getX() + 24.0f, p.getBottom() - 36.0f, 200.0f, 20.0f), juce::Justification::centredLeft);

    // current tab title
    g.setColour (t.textCol());
    g.setFont (style::font (20.0f, true));
    const int idx = ids.indexOf (page.getTab());
    g.drawText (idx >= 0 ? labels[idx] : juce::String(), juce::Rectangle<float> (p.getX() + 276.0f, p.getY() + 16.0f, p.getWidth() - 340.0f, 34.0f),
                juce::Justification::centredLeft);
}

} // namespace nx
