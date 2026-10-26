// NX Player — MIT License
#include "Marketplace.h"

namespace nx
{

//==============================================================================
void ToggleSwitch::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (42.0f, 24.0f);
    if (on)
        g.setGradientFill (juce::ColourGradient (t.accent(), r.getX(), r.getY(), t.accent2(), r.getRight(), r.getBottom(), false));
    else
        g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.22f : 0.16f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    const float d = r.getHeight() - 6.0f;
    auto knob = juce::Rectangle<float> (d, d).withCentre ({ on ? r.getRight() - 3.0f - d * 0.5f : r.getX() + 3.0f + d * 0.5f, r.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillEllipse (knob.translated (0.0f, 1.0f));
    g.setColour (on ? juce::Colours::white : t.textCol().withAlpha (0.85f));
    g.fillEllipse (knob);
}

void ToggleSwitch::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    on = ! on;
    repaint();
    if (onToggle) onToggle (on);
}

//==============================================================================
/** Sliders of a DSP plug-in (shown in a call-out from its card; the values are applied when released). */
class DspParamsPanel : public juce::Component
{
public:
    DspParamsPanel (AppContext& c, const DspPluginInfo& info) : ctx (c), id (info.id)
    {
        for (int i = 0; i < (int) info.params.size(); ++i)
        {
            const auto& prm = info.params[(size_t) i];
            auto* label = labels.add (new juce::Label ({}, juce::String::fromUTF8 (prm.name)));
            label->setColour (juce::Label::textColourId, ctx.theme().textCol());
            label->setFont (style::font (13.0f));
            addAndMakeVisible (label);
            auto* sl = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
            sl->setRange (prm.min_value, prm.max_value, (prm.max_value - prm.min_value) / 200.0);
            sl->setValue (ctx.plugins().getDspParameter (id, i), juce::dontSendNotification);
            sl->setTextValueSuffix (juce::String (" ") + juce::String::fromUTF8 (prm.unit));
            sl->setDoubleClickReturnValue (true, prm.default_value);
            sl->setColour (juce::Slider::textBoxTextColourId, ctx.theme().textCol());
            sl->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            sl->onDragEnd = [this, sl, i] { ctx.plugins().setDspParameter (id, i, sl->getValue()); };
            sl->onValueChange = [this, sl, i] { if (! sl->isMouseButtonDown()) ctx.plugins().setDspParameter (id, i, sl->getValue()); };
            addAndMakeVisible (sl);
        }
        setSize (380, 20 + (int) info.params.size() * 56);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        for (int i = 0; i < sliders.size(); ++i)
        {
            labels[i]->setBounds (r.removeFromTop (20));
            sliders[i]->setBounds (r.removeFromTop (30));
            r.removeFromTop (6);
        }
    }
private:
    AppContext& ctx;
    juce::String id;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> sliders;
};

//==============================================================================
MarketCard::MarketCard (AppContext& c, Data d)
    : ctx (c), data (std::move (d)), toggle (c), installButton ("Installer"), removeButton (style::Icon::Trash, "Retirer de votre dossier", c),
      settingsButton (style::Icon::Settings, "Réglages du plug-in"_t, c)
{
    toggle.setOn (data.enabled);
    toggle.onToggle = [this] (bool v) { if (onToggle) onToggle (v); };
    installButton.onClick = [this] { if (onInstall) onInstall(); };
    removeButton.onClick = [this] { if (onRemove) onRemove(); };
    settingsButton.onClick = [this] { if (onSettings) onSettings(); };
    addChildComponent (settingsButton);
    settingsButton.setVisible (data.kind == Kind::DspPlugin && data.installed && data.unavailable.isEmpty() && data.paramCount > 0);
    const bool showToggle = data.installed && data.unavailable.isEmpty();
    addChildComponent (toggle);
    addChildComponent (installButton);
    addChildComponent (removeButton);
    toggle.setVisible (showToggle);
    installButton.setVisible (data.catalogue && ! data.installed);
    removeButton.setVisible (data.removable);
    setRepaintsOnMouseActivity (true);
}

void MarketCard::resized()
{
    auto bottom = getLocalBounds().reduced (14, 12).removeFromBottom (30);
    toggle.setBounds (bottom.removeFromRight (48));
    installButton.setBounds (getLocalBounds().reduced (14, 12).removeFromBottom (30).removeFromRight (104));
    if (removeButton.isVisible()) removeButton.setBounds (bottom.removeFromRight (34).withSizeKeepingCentre (30, 30));
    if (settingsButton.isVisible()) settingsButton.setBounds (bottom.removeFromRight (34).withSizeKeepingCentre (30, 30));
}

void MarketCard::mouseUp (const juce::MouseEvent& e)
{
    // A theme card applies the theme when clicked anywhere (like choosing a wallpaper).
    if (data.kind == Kind::Theme && ! data.enabled && e.mouseWasClicked() && onToggle) onToggle (true);
}

void MarketCard::paintThemeMock (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& th = data.theme;
    g.setColour (th.bg());
    g.fillRect (r);
    // artwork-tinted backdrop
    g.setGradientFill (juce::ColourGradient (th.accent().withAlpha (0.55f), r.getX() + r.getWidth() * 0.7f, r.getY(),
                                             th.accent2().withAlpha (0.0f), r.getX() + r.getWidth() * 0.2f, r.getBottom(), true));
    g.fillRect (r);
    auto inner = r.reduced (8.0f);
    auto player = inner.removeFromBottom (20.0f);
    inner.removeFromBottom (5.0f);
    auto side = inner.removeFromLeft (inner.getWidth() * 0.28f);
    inner.removeFromLeft (6.0f);
    const float rad = juce::jlimit (2.0f, 6.0f, th.radius() * 0.35f);
    g.setColour (th.glassTint().withMultipliedAlpha (1.4f));
    g.fillRoundedRectangle (side, rad);
    g.setColour (th.colour ("--nx-glass-strong", juce::Colour (0xcc0a0d10)));
    g.fillRoundedRectangle (player, rad);
    // sidebar rows
    for (int i = 0; i < 5; ++i)
    {
        auto row = juce::Rectangle<float> (side.getX() + 6.0f, side.getY() + 8.0f + (float) i * 11.0f, side.getWidth() - 12.0f, 5.0f);
        g.setColour (i == 1 ? th.accent() : th.textDim().withAlpha (0.6f));
        g.fillRoundedRectangle (row.withWidth (row.getWidth() * (i == 1 ? 0.9f : 0.55f + 0.1f * (float) (i % 3))), 2.5f);
    }
    // title + album tiles
    g.setColour (th.textCol());
    g.fillRoundedRectangle (inner.getX(), inner.getY() + 2.0f, inner.getWidth() * 0.45f, 7.0f, 3.5f);
    const float tile = juce::jmin ((inner.getWidth() - 12.0f) / 3.0f, inner.getHeight() - 18.0f);
    for (int i = 0; i < 3; ++i)
    {
        auto tr = juce::Rectangle<float> (inner.getX() + (float) i * (tile + 6.0f), inner.getY() + 16.0f, tile, tile);
        g.setGradientFill (juce::ColourGradient (i == 1 ? th.accent2() : th.accent(), tr.getX(), tr.getY(),
                                                 th.surface(), tr.getRight(), tr.getBottom(), false));
        g.fillRoundedRectangle (tr, rad);
    }
    // player bar: play button + progress
    g.setColour (th.textCol());
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (player.getCentre()));
    auto prog = juce::Rectangle<float> (player.getX() + 10.0f, player.getCentreY() - 1.5f, player.getWidth() * 0.3f, 3.0f);
    g.setColour (th.accent());
    g.fillRoundedRectangle (prog, 1.5f);
}

void MarketCard::paintDspMock (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = ctx.theme();
    g.setColour (t.bg().withAlpha (0.65f));
    g.fillRect (r);
    auto area = r.reduced (16.0f, 18.0f);
    g.setColour (t.textDim().withAlpha (0.25f));
    for (int i = 1; i < 4; ++i)
        g.fillRect (area.getX(), area.getY() + area.getHeight() * (float) i / 4.0f, area.getWidth(), 1.0f);
    // a stylised response curve, unique per plug-in id
    juce::Path curve;
    const float seed = (float) (data.id.hashCode() % 100) / 100.0f;
    for (int i = 0; i <= 60; ++i)
    {
        const float x = (float) i / 60.0f;
        const float y = 0.5f + 0.28f * std::sin ((x * (2.0f + seed * 3.0f) + seed) * juce::MathConstants<float>::pi)
                               * (0.4f + 0.6f * x * (1.0f - x) * 4.0f);
        const auto px = area.getX() + x * area.getWidth(), py = area.getY() + y * area.getHeight();
        if (i == 0) curve.startNewSubPath (px, py); else curve.lineTo (px, py);
    }
    g.setGradientFill (juce::ColourGradient (t.accent(), area.getX(), 0.0f, t.accent2(), area.getRight(), 0.0f, false));
    g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (t.textCol().withAlpha (0.85f));
    g.setFont (style::font (11.0f, true));
    g.drawText ("DSP 64 bits" + (data.paramCount > 0 ? "  ·  "_t + juce::String (data.paramCount) + " réglages"_t : juce::String()),
                r.reduced (10.0f, 6.0f).removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft);
}

void MarketCard::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat();
    const float rad = t.radius();
    style::paintGlass (g, *this, ctx, r, rad);

    // preview
    auto prev = previewArea().toFloat();
    juce::Path clip;
    clip.addRoundedRectangle (prev.getX(), prev.getY(), prev.getWidth(), prev.getHeight(), rad, rad, true, true, false, false);
    g.saveState();
    g.reduceClipRegion (clip);
    if (data.kind == Kind::Theme) paintThemeMock (g, prev);
    else if (data.kind == Kind::DspPlugin) paintDspMock (g, prev);
    else
    {
        g.setColour (t.bg().withAlpha (0.6f));
        g.fillRect (prev);
        if (data.renderer != nullptr)
            data.renderer->render (data.id, data.slot, g, prev.reduced (12.0f, 10.0f), true);
    }
    g.restoreState();
    g.setColour (t.border());
    g.fillRect (prev.getX(), prev.getBottom() - 1.0f, prev.getWidth(), 1.0f);

    // text
    auto body = getLocalBounds().withTrimmedTop (previewHeight).reduced (14, 10);
    g.setColour (t.textCol());
    g.setFont (style::font (15.0f, true));
    g.drawText (data.name, body.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setColour (t.textDim());
    g.setFont (style::font (12.0f));
    juce::String meta = data.author.isNotEmpty() ? data.author : juce::String ("Anonyme");
    if (data.version.isNotEmpty()) meta << "  ·  v"_t << data.version;
    g.drawText (meta, body.removeFromTop (18), juce::Justification::centredLeft, true);
    body.removeFromTop (4);
    g.setColour (t.textCol().withAlpha (0.72f));
    g.setFont (style::font (12.5f));
    const auto desc = data.unavailable.isNotEmpty() ? "Indisponible : "_t + data.unavailable : data.description;
    g.drawFittedText (desc, body.removeFromTop (50), juce::Justification::topLeft, 3, 0.9f);

    // chip
    auto chipRow = getLocalBounds().reduced (14, 12).removeFromBottom (30);
    const juce::String chip = data.kind == Kind::Theme ? "Thème"_t : data.kind == Kind::DspPlugin ? juce::String ("DSP") : juce::String ("Interface");
    g.setFont (style::font (11.0f, true));
    const float cw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), chip) + 18.0f;
    auto chipR = chipRow.toFloat().removeFromLeft (cw).withSizeKeepingCentre (cw, 20.0f);
    const auto chipCol = data.kind == Kind::Theme ? t.accent2() : data.kind == Kind::DspPlugin ? t.colour ("--nx-quality-low", juce::Colour (0xffffb547)) : t.accent();
    g.setColour (chipCol.withAlpha (0.16f));
    g.fillRoundedRectangle (chipR, 10.0f);
    g.setColour (chipCol);
    g.drawText (chip, chipR, juce::Justification::centred);
    if (data.enabled && data.installed)
    {
        g.setColour (t.textDim());
        g.setFont (style::font (11.5f));
        g.drawText (data.kind == Kind::Theme ? "Actif" : "Activé"_t, chipR.translated (cw + 8.0f, 0.0f).withWidth (60.0f), juce::Justification::centredLeft);
    }

    if (isMouseOver (true))
    {
        g.setColour (t.accent().withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
    }
    else if (data.enabled)
    {
        g.setColour (t.accent().withAlpha (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    }
}

//==============================================================================
MarketplacePage::MarketplacePage (AppContext& c) : ScrollPage (c), importButton ("Importer un plugin…"_t), folderButton ("Ouvrir le dossier des plugins")
{
    contentComp.addAndMakeVisible (header);
    filter.setTextToShowWhenEmpty ("Filtrer…"_t, ctx.theme().textDim());
    filter.setFont (style::font (14.0f));
    filter.setIndents (12, 0);
    filter.setJustification (juce::Justification::centredLeft);
    filter.onTextChange = [this] { build(); };
    contentComp.addAndMakeVisible (filter);
    contentComp.addChildComponent (importButton);
    emptyLabel.setFont (style::font (14.0f));
    emptyLabel.setJustificationType (juce::Justification::topLeft);
    contentComp.addChildComponent (emptyLabel);
    contentComp.addChildComponent (folderButton);
    importButton.onClick = [this] { importPlugin(); };
    folderButton.onClick = [this]
    {
        auto root = userPluginsRoot();
        root.getChildFile ("UI").createDirectory();
        root.getChildFile ("DSP").createDirectory();
        root.startAsProcess();
    };

    catalogue = std::make_unique<PluginManager>();
    catalogue->shareStateWith (ctx.plugins());
    catalogue->setSearchFolders ({ ctx.resourcesDirectory().getChildFile ("Catalog") });
}

MarketplacePage::~MarketplacePage() { stopTimer(); }

juce::StringArray MarketplacePage::tabIds() const
{
    return mode == Mode::Gallery ? juce::StringArray { "tout", "themes", "interface", "dsp" } : juce::StringArray { "catalogue", "installes" };
}

juce::StringArray MarketplacePage::tabLabels() const
{
    return mode == Mode::Gallery ? juce::StringArray { "Tout", "Thèmes"_t, "Interface", "DSP" }
                                 : juce::StringArray { "Catalogue", "Installés"_t };
}

bool MarketplacePage::isUserFolder (const juce::File& f) const
{
    return f.isAChildOf (ctx.userDataDirectory());
}

void MarketplacePage::setMode (Mode m, const juce::String& t)
{
    const bool modeChanged = m != mode;
    mode = m;
    tab = tabIds().contains (t) ? t : tabIds()[0];
    if (modeChanged) filter.clear();
    refresh();
}

void MarketplacePage::refresh()
{
    tabButtons.clear();
    const auto ids = tabIds(), labels = tabLabels();
    for (int i = 0; i < ids.size(); ++i)
    {
        auto* b = tabButtons.add (new juce::TextButton (labels[i]));
        b->setClickingTogglesState (false);
        b->setToggleState (ids[i] == tab, juce::dontSendNotification);
        b->onClick = [this, id = ids[i]] { ctx.navigate ({ mode == Mode::Gallery ? PageId::Marketplace : PageId::UserPlugins, id }); };
        contentComp.addAndMakeVisible (b);
    }
    importButton.setVisible (mode == Mode::UserPlugins);
    folderButton.setVisible (mode == Mode::UserPlugins);
    build();
}

void MarketplacePage::build()
{
    for (auto* c : cards) contentComp.removeChildComponent (c);
    cards.clear();
    emptyText.clear();
    const auto q = filter.getText().trim();
    auto matches = [&q] (const MarketCard::Data& d)
    {
        return q.isEmpty() || d.name.containsIgnoreCase (q) || d.description.containsIgnoreCase (q) || d.author.containsIgnoreCase (q);
    };
    std::vector<MarketCard::Data> list;
    auto& mgr = ctx.plugins();

    auto uiData = [this] (const UiPluginInfo& p, PluginManager* renderer)
    {
        MarketCard::Data d;
        d.kind = MarketCard::Kind::UiPlugin;
        d.id = p.id; d.name = p.name; d.author = p.author; d.version = p.version; d.description = p.description;
        d.slot = p.slots.contains ("home.top") ? juce::String ("home.top") : p.slots.contains ("nowplaying.side") ? juce::String ("nowplaying.side") : p.slots[0];
        d.enabled = p.enabled;
        d.removable = isUserFolder (p.folder);
        d.renderer = renderer;
        return d;
    };
    auto dspData = [this] (const DspPluginInfo& p)
    {
        MarketCard::Data d;
        d.kind = MarketCard::Kind::DspPlugin;
        d.id = p.id; d.name = p.name; d.author = p.author; d.description = p.description;
        d.enabled = p.enabled;
        d.unavailable = p.error;
        d.paramCount = (int) p.params.size();
        d.removable = isUserFolder (p.folder);
        return d;
    };

    if (mode == Mode::Gallery)
    {
        if (tab == "tout" || tab == "themes")
            for (auto& th : ctx.themes().getThemes())
            {
                MarketCard::Data d;
                d.kind = MarketCard::Kind::Theme;
                d.id = th.id; d.name = th.name; d.author = th.author; d.version = th.version; d.description = th.description;
                d.enabled = th.id == ctx.theme().id;
                d.removable = isUserFolder (th.folder);
                d.theme = th;
                list.push_back (d);
            }
        if (tab == "tout" || tab == "interface")
            for (auto& p : mgr.getUiPlugins()) list.push_back (uiData (p, &mgr));
        if (tab == "tout" || tab == "dsp")
            for (auto& p : mgr.getDspPlugins()) list.push_back (dspData (p));
    }
    else if (tab == "catalogue")
    {
        auto installed = [&mgr] (const juce::String& id, bool& enabled)
        {
            for (auto& p : mgr.getUiPlugins()) if (p.id == id) { enabled = p.enabled; return true; }
            for (auto& p : mgr.getDspPlugins()) if (p.id == id) { enabled = p.enabled; return true; }
            return false;
        };
        for (auto& p : catalogue->getUiPlugins())
        {
            auto d = uiData (p, catalogue.get());
            d.catalogue = true;
            d.installed = installed (p.id, d.enabled);
            d.removable = d.installed && isUserFolder (mgr.getPluginFolder (p.id));
            if (! d.installed) d.enabled = false;
            list.push_back (d);
        }
        for (auto& p : catalogue->getDspPlugins())
        {
            auto d = dspData (p);
            d.catalogue = true;
            d.installed = installed (p.id, d.enabled);
            d.removable = d.installed && isUserFolder (mgr.getPluginFolder (p.id));
            if (! d.installed) d.enabled = false;
            if (d.installed) d.unavailable.clear();
            list.push_back (d);
        }
        if (list.empty()) emptyText = "Le catalogue est vide (dossier Catalog introuvable à côté de l'application)."_t;
    }
    else
    {
        for (auto& p : mgr.getUiPlugins()) if (isUserFolder (p.folder)) list.push_back (uiData (p, &mgr));
        for (auto& p : mgr.getDspPlugins()) if (isUserFolder (p.folder)) list.push_back (dspData (p));
        if (list.empty())
            emptyText = "Aucun plugin dans votre dossier pour l'instant. Installez-en depuis le Catalogue, importez un dossier ou une archive .zip, "_t
                        "ou créez le vôtre : un manifest.json et un plugin.js suffisent (voir docs/PLUGINS.md)."_t;
    }

    for (auto& d : list)
    {
        if (! matches (d)) continue;
        auto* card = cards.add (new MarketCard (ctx, d));
        const auto data = d;
        card->onToggle = [this, data] (bool on)
        {
            auto& m = ctx.plugins();
            if (data.kind == MarketCard::Kind::Theme)
            {
                ctx.applyTheme (on ? data.id : juce::String ("nx-emerald"));
                ctx.showMessage ((on ? "Thème appliqué : "_t + data.name : juce::String ("Thème par défaut rétabli"_t)));
            }
            else if (data.kind == MarketCard::Kind::UiPlugin) m.setUiPluginEnabled (data.id, on);
            else m.setDspPluginEnabled (data.id, on);
            if (data.kind != MarketCard::Kind::Theme)
                ctx.showMessage (data.name + (on ? " activé"_t : " désactivé"_t)
                                 + (data.kind == MarketCard::Kind::DspPlugin && on ? " (le flux n'est plus bit-perfect)"_t : juce::String()));
            juce::Component::SafePointer<MarketplacePage> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe) safe->build(); });
        };
        card->onSettings = [this, data, card]
        {
            for (auto& p : ctx.plugins().getDspPlugins())
                if (p.id == data.id)
                {
                    auto panel = std::make_unique<DspParamsPanel> (ctx, p);
                    juce::CallOutBox::launchAsynchronously (std::move (panel), card->getScreenBounds(), nullptr);
                }
        };
        card->onInstall = [this, data]
        {
            juce::String err;
            if (installFromCatalogue (data.id, err)) ctx.showMessage (data.name + " installé et activé"_t);
            else ctx.showMessage ("Installation impossible : "_t + err);
            juce::Component::SafePointer<MarketplacePage> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe) safe->build(); });
        };
        card->onRemove = [this, data]
        {
            juce::String err;
            if (data.kind == MarketCard::Kind::Theme)
            {
                if (auto* th = ctx.themes().findTheme (data.id))
                {
                    if (ctx.theme().id == data.id) ctx.applyTheme ("nx-emerald");
                    th->folder.deleteRecursively();
                    ctx.themes().rescan();
                }
            }
            else if (! removeUserPlugin (data.id, err)) ctx.showMessage (err);
            else ctx.showMessage (data.name + " retiré"_t);
            juce::Component::SafePointer<MarketplacePage> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe) safe->build(); });
        };
        contentComp.addAndMakeVisible (card);
    }
    if (cards.isEmpty() && emptyText.isEmpty()) emptyText = "Rien ne correspond à « "_t + q + " »."_t;

    layoutContent (getWidth() - viewport.getScrollBarThickness());
    header.repaint();
    startTimerHz (30);
}

bool MarketplacePage::installFromCatalogue (const juce::String& id, juce::String& error)
{
    const auto src = catalogue->getPluginFolder (id);
    if (! src.isDirectory()) { error = "plugin introuvable dans le catalogue"; return false; }
    bool isDsp = false;
    for (auto& p : catalogue->getDspPlugins()) if (p.id == id) isDsp = true;
    auto dest = userPluginsRoot().getChildFile (isDsp ? "DSP" : "UI").getChildFile (src.getFileName());
    dest.getParentDirectory().createDirectory();
    if (dest.exists() && ! dest.deleteRecursively()) dest = dest.getNonexistentSibling (false);   // leftover still locked
    if (! src.copyDirectoryTo (dest)) { error = "copie impossible vers "_t + dest.getFullPathName(); return false; }
    auto& m = ctx.plugins();
    m.rescan();
    if (isDsp) m.setDspPluginEnabled (id, true);
    else m.setUiPluginEnabled (id, true);
    return true;
}

bool MarketplacePage::removeUserPlugin (const juce::String& id, juce::String& error)
{
    auto& m = ctx.plugins();
    const auto folder = m.getPluginFolder (id);
    if (! isUserFolder (folder)) { error = "ce plugin est livré avec NX Player et ne peut pas être retiré"_t; return false; }
    m.setUiPluginEnabled (id, false);
    m.setDspPluginEnabled (id, false);
    if (! folder.deleteRecursively())
    {
        // Windows keeps a loaded DLL locked: remove the manifest (the plug-in disappears now) and let the next
        // launch delete what is left.
        folder.getChildFile ("manifest.json").deleteFile();
        if (folder.getChildFile ("manifest.json").exists())
        {
            error = "fermez NX Player puis supprimez "_t + folder.getFullPathName();
            return false;
        }
        folder.getChildFile (".nx-uninstalled").create();
    }
    m.rescan();
    return true;
}

void MarketplacePage::importPlugin()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Plugin NX (dossier ou archive .zip)", juce::File(), "*.zip");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        auto src = fc.getResult();
        if (src == juce::File()) return;
        std::unique_ptr<juce::TemporaryFile> tmp;
        if (src.existsAsFile())
        {
            tmp = std::make_unique<juce::TemporaryFile> (".nxplugin");
            tmp->getFile().createDirectory();
            juce::ZipFile zip (src);
            if (zip.getNumEntries() == 0 || zip.uncompressTo (tmp->getFile()).failed()) { ctx.showMessage ("Archive illisible"_t); return; }
            src = tmp->getFile();
            if (! src.getChildFile ("manifest.json").existsAsFile())
                for (const auto& e : juce::RangedDirectoryIterator (src, false, "*", juce::File::findDirectories))
                    if (e.getFile().getChildFile ("manifest.json").existsAsFile()) { src = e.getFile(); break; }
        }
        const auto manifest = juce::JSON::parse (src.getChildFile ("manifest.json"));
        if (! manifest.isObject()) { ctx.showMessage ("manifest.json introuvable dans ce plugin"_t); return; }
        const bool isDsp = manifest.getProperty ("type", {}).toString() == "dsp" || manifest.hasProperty ("library");
        const auto name = src.getFileName().endsWith (".nxplugin") ? manifest.getProperty ("id", "plugin").toString() : src.getFileName();
        const auto dest = userPluginsRoot().getChildFile (isDsp ? "DSP" : "UI").getChildFile (juce::File::createLegalFileName (name));
        dest.getParentDirectory().createDirectory();
        if (dest.exists()) dest.deleteRecursively();
        if (! src.copyDirectoryTo (dest)) { ctx.showMessage ("Copie impossible"_t); return; }
        auto& m = ctx.plugins();
        m.rescan();
        const auto id = manifest.getProperty ("id", name).toString();
        if (isDsp) m.setDspPluginEnabled (id, true); else m.setUiPluginEnabled (id, true);
        ctx.showMessage ("Plugin importé et activé : "_t + manifest.getProperty ("name", id).toString());
        ctx.navigate ({ PageId::UserPlugins, "installes" });
    });
}

void MarketplacePage::layoutContent (int width)
{
    const int pad = 32;
    const int inner = juce::jmax (200, width - pad * 2);
    header.setBounds (pad, 24, inner, 80);
    int y = 24 + 84;

    auto row = juce::Rectangle<int> (pad, y, inner, 34);
    for (auto* b : tabButtons)
    {
        const int w = juce::jmax (84, juce::roundToInt (juce::GlyphArrangement::getStringWidth (style::font (14.0f, true), b->getButtonText())) + 36);
        b->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (8);
    }
    filter.setBounds (row.removeFromRight (juce::jmin (260, juce::jmax (140, row.getWidth()))));
    y += 34 + 14;

    if (mode == Mode::UserPlugins)
    {
        auto actions = juce::Rectangle<int> (pad, y, inner, 34);
        importButton.setBounds (actions.removeFromLeft (220));
        actions.removeFromLeft (10);
        folderButton.setBounds (actions.removeFromLeft (240));
        y += 34 + 16;
    }

    gridTop = y;
    const int gap = 16;
    const int cols = juce::jmax (1, (inner + gap) / (250 + gap));
    const int cardW = (inner - gap * (cols - 1)) / cols;
    for (int i = 0; i < cards.size(); ++i)
    {
        const int c = i % cols, r = i / cols;
        cards[i]->setBounds (pad + c * (cardW + gap), y + r * (MarketCard::cardHeight + gap), cardW, MarketCard::cardHeight);
    }
    emptyLabel.setText (emptyText, juce::dontSendNotification);
    emptyLabel.setColour (juce::Label::textColourId, ctx.theme().textDim());
    emptyLabel.setVisible (emptyText.isNotEmpty());
    emptyLabel.setBounds (pad, y, juce::jmin (inner, 720), 60);
    const int rowsUsed = (cards.size() + cols - 1) / cols;
    y += rowsUsed * (MarketCard::cardHeight + gap) + (emptyText.isNotEmpty() ? 60 : 0);
    setContentHeight (y + 60);
}

void MarketplacePage::Header::paint (juce::Graphics& g)
{
    const auto& t = page.ctx.theme();
    const bool gallery = page.mode == Mode::Gallery;
    drawPageTitle (g, t, getLocalBounds(), gallery ? juce::String ("Extensions & Thèmes"_t) : juce::String ("Plugins utilisateur"),
                   gallery ? juce::String ("Activez ou désactivez en un clic. Les thèmes s'appliquent tout de suite, les plug-ins apparaissent dans leurs emplacements."_t)
                           : juce::String ("Des plug-ins en plus, installés dans votre dossier en un clic. Les vôtres (importés) apparaissent dans « Installés »."_t));
}

void MarketplacePage::timerCallback()
{
    if (! isShowing()) return;
    const auto view = viewport.getViewArea();
    for (auto* c : cards)
        if (c->getData().kind == MarketCard::Kind::UiPlugin && view.intersects (c->getBounds()))
            c->repaint (c->previewArea());
}

} // namespace nx
