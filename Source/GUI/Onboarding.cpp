// NX Player — MIT License
#include "Onboarding.h"
#include "DeviceArt.h"
#include "../Providers/MediaProvider.h"
#include "../Providers/UpnpProvider.h"
#include "../Library/LibraryScanner.h"

namespace nx
{

OnboardingWizard::OnboardingWizard (AppContext& c)
    : ctx (c), skipButton ("Passer"_t), backButton ("Précédent"_t), nextButton ("Suivant"_t),
      folderButton ("Choisir le dossier musical…"_t), nasButton ("Rechercher les serveurs NAS / Jellyfin"_t), nasConnect ("Ajouter"_t)
{
    for (auto* b : { &skipButton, &backButton, &nextButton, &folderButton, &nasButton, &nasConnect }) addChildComponent (b);
    addChildComponent (nasAddress);
    addChildComponent (deviceBox);
    nextButton.setToggleState (true, juce::dontSendNotification);
    skipButton.onClick = [this] { finish(); };
    backButton.onClick = [this] { setStep (step - 1); };
    nextButton.onClick = [this] { if (step + 1 >= numSteps) finish(); else setStep (step + 1); };
    nasAddress.setTextToShowWhenEmpty ("Adresse IP du NAS (ex. 192.168.1.20)"_t, ctx.theme().textDim());
    nasAddress.setFont (style::font (14.0f));
    nasAddress.setIndents (12, 0);
    nasAddress.setJustification (juce::Justification::centredLeft);
    folderButton.onClick = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Dossier de musique"_t, juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        juce::Component::SafePointer<OnboardingWizard> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe, chooser] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (safe == nullptr || ! dir.isDirectory()) return;
            safe->ctx.library().addFolder (dir);
            safe->ctx.scanner().requestFullScan();
            safe->addedFolders.addIfNotAlreadyThere (dir.getFullPathName());
            safe->repaint();
        });
    };
    nasButton.onClick = [this] { searchNas(); };
    auto connect = [this]
    {
        auto text = nasAddress.getText().trim();
        auto* upnp = dynamic_cast<UpnpProvider*> (ctx.providers().getProvider ("upnp"));
        if (text.isEmpty() || upnp == nullptr) return;
        nasStatus = "Connexion à "_t + text + "…"_t;
        juce::Component::SafePointer<OnboardingWizard> safe (this);
        juce::Thread::launch ([safe, upnp, text]
        {
            const auto before = upnp->getServers().size();
            const bool ok = text.containsChar ('/') ? upnp->addServerFromLocation (text.startsWith ("http") ? text : "http://" + text)
                                                    : upnp->discoverHost (text.upToFirstOccurrenceOf (":", false, false)).size() > before;
            juce::MessageManager::callAsync ([safe, ok]
            {
                if (safe == nullptr) return;
                safe->ctx.saveNasServers();
                safe->nasStatus = ok ? "Serveur ajouté"_t : "Aucun serveur UPnP / DLNA à cette adresse"_t;
                safe->repaint();
            });
        });
        repaint();
    };
    nasConnect.onClick = connect;
    nasAddress.onReturnKey = connect;
    deviceBox.onChange = [this]
    {
        if (deviceBox.getText().isNotEmpty() && deviceBox.getText() != ctx.engine().getDeviceName()) ctx.selectDevice (deviceBox.getText());
        repaint();
    };
    setInterceptsMouseClicks (true, true);
}

void OnboardingWizard::start()
{
    setVisible (true);
    toFront (true);
    setStep (0);
    startTimerHz (30);
}

void OnboardingWizard::finish()
{
    ctx.settings().set ("onboardingDone", true);
    stopTimer();
    setVisible (false);
    if (onFinished) onFinished();
}

void OnboardingWizard::setStep (int s)
{
    step = juce::jlimit (0, numSteps - 1, s);
    layoutStep();
    repaint();
}

juce::Rectangle<int> OnboardingWizard::card() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (1000, getWidth() - 60), juce::jmin (680, getHeight() - 60));
}

void OnboardingWizard::resized() { layoutStep(); }

void OnboardingWizard::layoutStep()
{
    auto c = card();
    auto bottom = c.reduced (32, 24).removeFromBottom (40);
    skipButton.setVisible (true);
    skipButton.setBounds (bottom.removeFromLeft (120));
    nextButton.setVisible (true);
    nextButton.setButtonText (step + 1 >= numSteps ? "Terminer"_t : "Suivant"_t);
    nextButton.setBounds (bottom.removeFromRight (150));
    bottom.removeFromRight (10);
    backButton.setVisible (step > 0);
    backButton.setBounds (bottom.removeFromRight (130));
    skipButton.setButtonText ("Passer"_t);
    backButton.setButtonText ("Précédent"_t);

    const bool sources = step == 3;
    folderButton.setVisible (sources);
    nasButton.setVisible (sources);
    nasAddress.setVisible (sources);
    nasConnect.setVisible (sources);
    auto body = c.reduced (40, 0).withTrimmedTop (178).withTrimmedBottom (90);
    if (sources)
    {
        auto left = body.removeFromLeft (body.getWidth() / 2 - 12);
        folderButton.setBounds (left.withHeight (40).withY (left.getY() + 76).withWidth (juce::jmin (left.getWidth(), 300)));
        auto right = body.withTrimmedLeft (24);
        nasButton.setBounds (right.withHeight (40).withY (right.getY() + 76).withWidth (juce::jmin (right.getWidth(), 340)));
        auto addr = right.withHeight (36).withY (right.getY() + 128);
        nasConnect.setBounds (addr.removeFromRight (100));
        addr.removeFromRight (8);
        nasAddress.setBounds (addr);
    }
    const bool device = step == 2;
    deviceBox.setVisible (device);
    if (device)
    {
        deviceBox.clear (juce::dontSendNotification);
        deviceBox.addItemList (ctx.engine().getBackend().getDeviceNames(), 1);
        deviceBox.setText (ctx.engine().getDeviceName(), juce::dontSendNotification);
        deviceBox.setBounds (c.getX() + 40 + (c.getWidth() - 80) / 2 + 20, c.getY() + 178 + 6, juce::jmin (380, (c.getWidth() - 80) / 2 - 20), 34);
    }
    for (auto* b : { &skipButton, &backButton, &nextButton, &folderButton, &nasButton, &nasConnect }) b->toFront (false);
}

void OnboardingWizard::searchNas()
{
    auto* upnp = dynamic_cast<UpnpProvider*> (ctx.providers().getProvider ("upnp"));
    if (upnp == nullptr || nasSearching) return;
    nasSearching = true;
    nasStatus = "Recherche des serveurs UPnP / DLNA du réseau…"_t;
    juce::Component::SafePointer<OnboardingWizard> safe (this);
    juce::Thread::launch ([safe, upnp]
    {
        const auto found = upnp->discover (3000);
        juce::MessageManager::callAsync ([safe, n = (int) found.size()]
        {
            if (safe == nullptr) return;
            safe->nasSearching = false;
            safe->ctx.saveNasServers();
            safe->nasStatus = n > 0 ? juce::String (n) + " serveur(s) trouvé(s)"_t : juce::String ("Aucun serveur trouvé : entrez l'adresse IP du NAS"_t);
            safe->repaint();
        });
    });
    repaint();
}

void OnboardingWizard::clickTile (const juce::String& kind, const juce::String& id)
{
    if (kind == "lang") ctx.setLanguage (id);
    else if (kind == "theme") ctx.applyTheme (id);
    else if (kind == "layout")
    {
        ctx.setLayoutOption ("sidebar", id == "compact" ? "compact" : "wide");
        ctx.setLayoutOption ("player", id == "top" ? "top" : "bottom");
        ctx.settings().set ("layoutPreset", id);
    }
    else if (kind == "profile")
    {
        auto s = ctx.engine().getSettings();
        const auto family = familyFromName (ctx.engine().getDeviceName());
        if (family != DeviceFamily::None)
        {
            s.hardwareProfile = HardwareProfile::Auto;
            familySetProfile (s, family, id.getIntValue());
            ctx.engine().setSettings (s);
            ctx.applyEngineSettings();
            ctx.showMessage ("Profil activé : "_t + profileName (family, id.getIntValue()));
        }
    }
    layoutStep();
    repaint();
}

void OnboardingWizard::mouseUp (const juce::MouseEvent& e)
{
    for (auto& t : tiles)
        if (t.bounds.contains (e.getPosition())) { clickTile (t.kind, t.id); return; }
}

//==============================================================================
void OnboardingWizard::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    tiles.clear();
    // backdrop: the blurred interface, darkened, with two slow colour glows
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    const float w = (float) getWidth(), h = (float) getHeight();
    for (int k = 0; k < 2; ++k)
    {
        const float x = w * (0.3f + 0.4f * (float) k) + 60.0f * (float) std::sin (phase * 0.4 + k);
        const float y = h * (0.35f + 0.3f * (float) k) + 40.0f * (float) std::cos (phase * 0.33 + k * 2);
        g.setGradientFill (juce::ColourGradient ((k == 0 ? t.accent() : t.accent2()).withAlpha (0.16f), x, y, juce::Colours::transparentBlack, x + w * 0.35f, y, true));
        g.fillAll();
    }

    auto c = card().toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (c, 22.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 50, { 0, 20 }).drawForPath (g, shape);
    style::paintGlass (g, *this, ctx, c, 22.0f, true);

    auto r = card().reduced (40, 28);
    // header: logo, step dots
    auto head = r.removeFromTop (34);
    auto mark = head.removeFromLeft (30).toFloat().withSizeKeepingCentre (26, 26);
    g.setGradientFill (juce::ColourGradient (t.accent(), mark.getX(), mark.getY(), t.accent2(), mark.getRight(), mark.getBottom(), false));
    g.drawEllipse (mark.reduced (2), 3.0f);
    head.removeFromLeft (10);
    g.setColour (t.textCol());
    g.setFont (style::font (16.0f, true));
    g.drawText ("Bienvenue dans NX Player"_t, head, juce::Justification::centredLeft);
    for (int i = 0; i < numSteps; ++i)
    {
        auto dot = juce::Rectangle<float> (i == step ? 26.0f : 8.0f, 8.0f).withPosition ((float) head.getRight() - 120.0f + (float) i * 22.0f + (i > step ? 18.0f : 0.0f), (float) head.getCentreY() - 4.0f);
        g.setColour (i <= step ? t.accent() : t.textDim().withAlpha (0.4f));
        g.fillRoundedRectangle (dot, 4.0f);
    }
    r.removeFromTop (18);
    const juce::String titles[] = { "Langue & thème"_t, "Disposition de l'interface"_t, "Votre matériel audio"_t, "Vos sources musicales"_t };
    const juce::String subs[] = { "Choisissez la langue de l'interface et son apparence. Tout se change ensuite dans les réglages."_t,
                                  "Comment voulez-vous organiser le lecteur ?"_t,
                                  "NX Player a détecté votre interface : activez le profil optimal en un clic."_t,
                                  "Indiquez où se trouve votre musique : un dossier local et / ou un serveur NAS / Jellyfin du réseau."_t };
    g.setColour (t.textDim());
    g.setFont (style::font (12.5f, true));
    g.drawText ("ÉTAPE "_t + juce::String (step + 1) + " / " + juce::String (numSteps), r.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (t.textCol());
    g.setFont (style::font (30.0f, true));
    g.drawText (titles[step], r.removeFromTop (40), juce::Justification::centredLeft);
    g.setColour (t.textDim());
    g.setFont (style::font (14.0f));
    g.drawText (subs[step], r.removeFromTop (24), juce::Justification::centredLeft, true);
    r.removeFromTop (16);
    r.removeFromBottom (64);

    switch (step)
    {
        case 0: paintLanguage (g, r); break;
        case 1: paintLayout (g, r); break;
        case 2: paintDevice (g, r); break;
        default: paintSources (g, r); break;
    }
}

namespace
{
    void tileFrame (juce::Graphics& g, const Theme& t, juce::Rectangle<float> r, bool on, bool hot)
    {
        g.setColour (on ? t.accent().withAlpha (0.16f) : juce::Colours::white.withAlpha (hot ? 0.08f : 0.04f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (on ? t.accent() : t.border().withMultipliedAlpha (hot ? 2.0f : 1.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, on ? 2.0f : 1.0f);
        if (on)
        {
            auto tick = juce::Rectangle<float> (22, 22).withPosition (r.getRight() - 30.0f, r.getY() + 8.0f);
            g.setColour (t.accent());
            g.fillEllipse (tick);
            style::drawIcon (g, style::Icon::Check, tick.reduced (4.5f), t.colour ("--nx-on-accent", juce::Colours::black), 2.6f);
        }
    }
}

void OnboardingWizard::paintLanguage (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& t = ctx.theme();
    const auto mouse = getMouseXYRelative();
    auto langs = r.removeFromTop (96);
    const std::pair<const char*, const char*> list[] = { { "fr", "Français" }, { "en", "English" } };
    const int lw = (langs.getWidth() - 16) / 2;
    for (int i = 0; i < 2; ++i)
    {
        auto tile = langs.removeFromLeft (lw);
        langs.removeFromLeft (16);
        const bool on = i18n::getLanguage() == list[i].first;
        tileFrame (g, t, tile.toFloat(), on, tile.contains (mouse));
        auto inner = tile.reduced (20, 16);
        style::drawIcon (g, style::Icon::Globe, inner.removeFromLeft (40).toFloat().withSizeKeepingCentre (30, 30), on ? t.accent() : t.textDim());
        inner.removeFromLeft (12);
        g.setColour (t.textCol());
        g.setFont (style::font (20.0f, true));
        g.drawText (juce::String::fromUTF8 (list[i].second), inner.removeFromTop (34), juce::Justification::bottomLeft);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawText (i == 0 ? juce::String::fromUTF8 ("Interface en fran\xc3\xa7" "ais") : juce::String ("Interface in English"), inner, juce::Justification::topLeft);
        tiles.push_back ({ "lang", list[i].first, tile });
    }
    r.removeFromTop (24);
    g.setColour (t.textDim());
    g.setFont (style::font (12.5f, true));
    g.drawText ("THÈME"_t, r.removeFromTop (20), juce::Justification::centredLeft);
    r.removeFromTop (8);
    const auto themes = ctx.themes().getThemes();
    const int n = juce::jmax (1, juce::jmin (5, (int) themes.size()));
    const int tw = (r.getWidth() - 14 * (n - 1)) / n;
    auto row = r.removeFromTop (juce::jmin (180, r.getHeight()));
    for (int i = 0; i < n; ++i)
    {
        const auto& th = themes[(size_t) i];
        auto tile = row.removeFromLeft (tw);
        row.removeFromLeft (14);
        const bool on = ctx.theme().id == th.id;
        tileFrame (g, t, tile.toFloat(), on, tile.contains (mouse));
        // swatch: background + glass + accents
        auto sw = tile.reduced (14).removeFromTop (tile.getHeight() - 60).toFloat();
        g.setColour (th.bg());
        g.fillRoundedRectangle (sw, 10.0f);
        g.setGradientFill (juce::ColourGradient (th.accent().withAlpha (0.65f), sw.getX(), sw.getBottom(), th.accent2().withAlpha (0.35f), sw.getRight(), sw.getY(), false));
        g.fillRoundedRectangle (sw.reduced (sw.getWidth() * 0.35f, sw.getHeight() * 0.25f).translated (sw.getWidth() * 0.12f, 0), 8.0f);
        g.setColour (th.surface().withAlpha (0.9f));
        g.fillRoundedRectangle (sw.withWidth (sw.getWidth() * 0.26f).reduced (5.0f), 6.0f);
        g.setColour (th.accent());
        g.fillRoundedRectangle (sw.getX() + 10.0f, sw.getY() + 12.0f, sw.getWidth() * 0.14f, 4.0f, 2.0f);
        g.setColour (t.textCol());
        g.setFont (style::font (14.0f, true));
        g.drawText (th.name, tile.reduced (16, 12).removeFromBottom (24), juce::Justification::centredLeft, true);
        tiles.push_back ({ "theme", th.id, tile });
    }
}

void OnboardingWizard::paintLayout (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& t = ctx.theme();
    const auto mouse = getMouseXYRelative();
    const juce::String current = ctx.settings().get ("layoutPreset", "cider").toString();
    struct L { const char* id; juce::String name, desc; bool compact, top; };
    const L layouts[] = { { "cider", "Cider 2 moderne"_t, "Barre latérale étendue, lecteur en bas"_t, false, false },
                          { "compact", "Compacte"_t, "Barre latérale en icônes, plus de place pour la musique"_t, true, false },
                          { "top", "Lecteur en haut"_t, "Barre de lecture en haut, à la Roon"_t, false, true } };
    const int tw = (r.getWidth() - 32) / 3;
    auto row = r.removeFromTop (juce::jmin (r.getHeight(), 330));
    for (int i = 0; i < 3; ++i)
    {
        const auto& l = layouts[i];
        auto tile = row.removeFromLeft (tw);
        row.removeFromLeft (16);
        tileFrame (g, t, tile.toFloat(), current == l.id, tile.contains (mouse));
        // wireframe preview
        auto pv = tile.reduced (18).removeFromTop (tile.getHeight() - 110).toFloat();
        g.setColour (t.bg().brighter (0.05f));
        g.fillRoundedRectangle (pv, 10.0f);
        auto inner = pv.reduced (8.0f);
        auto player = l.top ? inner.removeFromTop (inner.getHeight() * 0.17f) : inner.removeFromBottom (inner.getHeight() * 0.17f);
        if (l.top) inner.removeFromTop (6.0f); else inner.removeFromBottom (6.0f);
        g.setColour (t.accent().withAlpha (0.35f));
        g.fillRoundedRectangle (player, 5.0f);
        g.setColour (t.textCol().withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (player.getHeight() * 0.55f, player.getHeight() * 0.55f).withCentre (player.getCentre()));
        auto side = inner.removeFromLeft (l.compact ? inner.getWidth() * 0.1f : inner.getWidth() * 0.26f);
        inner.removeFromLeft (6.0f);
        g.setColour (t.surface().brighter (0.15f));
        g.fillRoundedRectangle (side, 5.0f);
        for (int k = 0; k < 6; ++k)
        {
            g.setColour (k == 1 ? t.accent() : t.textDim().withAlpha (0.5f));
            if (l.compact) g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ side.getCentreX(), side.getY() + 14.0f + (float) k * 14.0f }));
            else g.fillRoundedRectangle (side.getX() + 6.0f, side.getY() + 10.0f + (float) k * 13.0f, side.getWidth() * (k == 1 ? 0.7f : 0.55f), 4.0f, 2.0f);
        }
        const int cols = 3;
        const float cw = (inner.getWidth() - 12.0f) / (float) cols;
        for (int k = 0; k < 6; ++k)
        {
            auto cell = juce::Rectangle<float> (inner.getX() + (float) (k % cols) * (cw + 6.0f), inner.getY() + (float) (k / cols) * (cw * 0.9f + 6.0f), cw, cw * 0.9f);
            if (cell.getBottom() > inner.getBottom()) break;
            g.setGradientFill (juce::ColourGradient (t.accent().withAlpha (0.35f + 0.08f * (float) k), cell.getX(), cell.getY(), t.accent2().withAlpha (0.25f), cell.getRight(), cell.getBottom(), false));
            g.fillRoundedRectangle (cell, 4.0f);
        }
        auto text = tile.reduced (20, 16).removeFromBottom (64);
        g.setColour (t.textCol());
        g.setFont (style::font (17.0f, true));
        g.drawText (l.name, text.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (t.textDim());
        g.setFont (style::font (13.0f));
        g.drawFittedText (l.desc, text, juce::Justification::topLeft, 2);
        tiles.push_back ({ "layout", l.id, tile });
    }
}

void OnboardingWizard::paintDevice (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& t = ctx.theme();
    const auto mouse = getMouseXYRelative();
    const auto device = ctx.engine().getDeviceName();
    const auto family = familyFromName (device);
    auto left = r.removeFromLeft (r.getWidth() / 2).toFloat();
    r.removeFromLeft (20);
    // big drawing
    auto stage = left.removeFromTop (left.getHeight() - 70.0f);
    g.setGradientFill (juce::ColourGradient (t.accent().withAlpha (0.12f), stage.getCentreX(), stage.getCentreY(), juce::Colours::transparentBlack, stage.getRight(), stage.getBottom(), true));
    g.fillRoundedRectangle (stage, 16.0f);
    deviceart::draw (g, family, stage.reduced (24.0f), t, 0.6f + 0.25f * (float) std::sin (phase * 2.0), true);
    g.setColour (t.textCol());
    g.setFont (style::font (20.0f, true));
    g.drawText (device.isNotEmpty() ? device : juce::String ("Aucun périphérique audio"_t), left.removeFromTop (30), juce::Justification::centred, true);
    g.setColour (t.accent());
    g.setFont (style::font (13.5f, true));
    g.drawText ((family != DeviceFamily::None ? deviceart::familyName (family) + " · "_t : juce::String()) + connectionLabel (device, ctx.getCurrentBackendName())
                    + " · "_t + ctx.getCurrentBackendName(), left.removeFromTop (24), juce::Justification::centred, true);

    // right: choose the device, then the profiles
    auto rr = r;
    g.setColour (t.textDim());
    g.setFont (style::font (12.5f, true));
    g.drawText ("SORTIE AUDIO"_t, rr.removeFromTop (6).withHeight (0), juce::Justification::centredLeft);
    rr.removeFromTop (48);   // device combo
    if (family == DeviceFamily::None)
    {
        g.setColour (t.textCol());
        g.setFont (style::font (16.0f, true));
        g.drawText ("Lecture bit-perfect automatique"_t, rr.removeFromTop (28), juce::Justification::centredLeft);
        g.setColour (t.textDim());
        g.setFont (style::font (13.5f));
        g.drawFittedText ("PCM à la fréquence du fichier en accès exclusif, DSD en natif ou en DoP si le DAC l'accepte, sinon converti en DXD 64 bits. "_t
                          "Branchez une Merging Anubis ou une RME ADI-2 Pro : leurs profils dédiés apparaîtront ici."_t,
                          rr.removeFromTop (90), juce::Justification::topLeft, 5);
        return;
    }
    const auto s = ctx.engine().getSettings();
    const int current = familyCurrentProfile (s, family);
    const int count = familyProfileCount (family);
    for (int i = 0; i < count; ++i)
    {
        auto tile = rr.removeFromTop (juce::jmin (96, (rr.getHeight() - 10 * (count - 1)) / count));
        rr.removeFromTop (10);
        tileFrame (g, t, tile.toFloat(), i == current, tile.contains (mouse));
        auto in = tile.reduced (16, 10);
        g.setColour (i == current ? t.accent() : t.textCol());
        g.setFont (style::font (15.0f, true));
        g.drawText (profileName (family, i).fromFirstOccurrenceOf (juce::String::fromUTF8 ("\xe2\x80\x94 "), false, false) + (i == 0 ? "  ·  optimal"_t : juce::String()),
                    in.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (12.0f));
        g.drawFittedText (profileDescription (family, i), in.withTrimmedRight (26), juce::Justification::topLeft, 3, 0.9f);
        tiles.push_back ({ "profile", juce::String (i), tile });
    }
}

void OnboardingWizard::paintSources (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& t = ctx.theme();
    auto left = r.removeFromLeft (r.getWidth() / 2 - 12);
    r.removeFromLeft (24);
    auto block = [&] (juce::Rectangle<int> area, style::Icon icon, const juce::String& title, const juce::String& text)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (area.toFloat(), 14.0f);
        g.setColour (t.border());
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 14.0f, 1.0f);
        auto in = area.reduced (18, 14);
        auto head = in.removeFromTop (30);
        style::drawIcon (g, icon, head.removeFromLeft (30).toFloat().withSizeKeepingCentre (24, 24), t.accent());
        head.removeFromLeft (8);
        g.setColour (t.textCol());
        g.setFont (style::font (17.0f, true));
        g.drawText (title, head, juce::Justification::centredLeft);
        g.setColour (t.textDim());
        g.setFont (style::font (12.5f));
        g.drawFittedText (text, in.removeFromTop (22), juce::Justification::topLeft, 1);
        return in;
    };
    auto in = block (left, style::Icon::Folder, "Dossier local"_t, "Disque interne ou externe, partage SMB / NFS monté"_t);
    in.removeFromTop (62);
    g.setFont (style::font (13.0f));
    for (auto& f : ctx.library().getFolders())
    {
        g.setColour (addedFolders.contains (f) ? t.accent() : t.textCol().withAlpha (0.85f));
        style::drawIcon (g, style::Icon::Check, in.removeFromTop (0).withHeight (22).removeFromLeft (18).toFloat().reduced (2.0f), t.accent(), 2.2f);
        g.drawText (f, in.removeFromTop (22).withTrimmedLeft (24), juce::Justification::centredLeft, true);
    }
    auto nasIn = block (r, style::Icon::Network, "NAS / Jellyfin"_t, "Serveur UPnP / DLNA (MinimServer, Synology, Jellyfin…)"_t);
    nasIn.removeFromTop (108);
    g.setColour (t.textDim());
    g.setFont (style::font (12.5f));
    g.drawText (nasStatus, nasIn.removeFromTop (22), juce::Justification::centredLeft, true);
    if (auto* upnp = dynamic_cast<UpnpProvider*> (ctx.providers().getProvider ("upnp")))
        for (auto& sv : upnp->getServers())
        {
            g.setColour (t.textCol());
            g.drawText (juce::String::fromUTF8 ("\xe2\x97\x8f ") + sv.name, nasIn.removeFromTop (22), juce::Justification::centredLeft, true);
        }
}

} // namespace nx
