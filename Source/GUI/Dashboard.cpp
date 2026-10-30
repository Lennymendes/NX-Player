// NX Player — MIT License
#include "Dashboard.h"
#include "DeviceArt.h"
#include "../Audio/MemoryPlayStream.h"
#include "../Remote/RemoteServer.h"
#include <qrcodegen.hpp>

namespace nx
{

std::unique_ptr<juce::Component> makeHardwareDashboard (AppContext& ctx) { return std::make_unique<HardwareDashboard> (ctx); }
std::unique_ptr<juce::Component> makeRemotePanel (AppContext& ctx) { return std::make_unique<RemotePanel> (ctx); }

//==============================================================================
HardwareDashboard::HardwareDashboard (AppContext& c) : ctx (c)
{
    refresh();
    startTimerHz (30);
}

void HardwareDashboard::refresh()
{
    snapshot = monitor.sample();
    caps = ctx.engine().getCapabilities();
    status = ctx.engine().getOutputStatus();
}

void HardwareDashboard::timerCallback()
{
    perf::retune (*this, 30, 2);
    const int rate = perf::hz (30, 2);
    phase += 1.0 / rate;
    if (++ticks % rate == 0) refresh();    // counters once a second, the flow animation at 30 fps
    repaint();
}

HardwareDashboard::RateSupport HardwareDashboard::supportFor (const DeviceCapabilities& caps, double rate, bool dsd)
{
    auto has = [&caps] (double r) { for (auto x : caps.sampleRates) if (std::abs (x - r) < 0.5) return true; return false; };
    if (! dsd) return has (rate) ? RateSupport::Pcm : RateSupport::None;
    for (auto x : caps.nativeDsdRates) if (std::abs (x - rate) < 1.0) return RateSupport::Native;
    return has (rate / 16.0) ? RateSupport::DoP : RateSupport::None;
}

namespace
{
    void card (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, const juce::String& title)
    {
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (t.border().withMultipliedAlpha (1.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
        g.setColour (t.textDim());
        g.setFont (style::font (11.5f, true));
        g.drawText (title, r.reduced (18, 12).removeFromTop (16), juce::Justification::centredLeft);
    }

    void gauge (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, const juce::String& label, const juce::String& value, float fraction)
    {
        auto top = r.removeFromTop (17);
        g.setColour (t.textCol().withAlpha (0.85f));
        g.setFont (style::font (12.5f));
        g.drawText (label, top, juce::Justification::centredLeft);
        g.setColour (t.textCol());
        g.setFont (style::font (12.5f, true));
        g.drawText (value, top, juce::Justification::centredRight);
        auto bar = r.removeFromTop (6).withY (r.getY() + 3);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (bar, 3.0f);
        if (fraction >= 0.0f)
        {
            const auto c = fraction > 0.85f ? juce::Colour (0xffff6b6b) : fraction > 0.65f ? juce::Colour (0xffffb547) : t.accent();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.7f), bar.getX(), 0, c, bar.getRight(), 0, false));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * juce::jlimit (0.0f, 1.0f, fraction))), 3.0f);
        }
    }

    void statusLine (juce::Graphics& g, juce::Rectangle<float> r, const Theme& t, bool on, const juce::String& text)
    {
        auto dot = r.removeFromLeft (14).withSizeKeepingCentre (8, 8);
        if (on) { g.setColour (t.accent().withAlpha (0.3f)); g.fillEllipse (dot.expanded (4)); }
        g.setColour (on ? t.accent() : t.textDim().withAlpha (0.6f));
        g.fillEllipse (dot);
        r.removeFromLeft (6);
        g.setColour (on ? t.textCol() : t.textDim());
        g.setFont (style::font (12.5f));
        g.drawText (text, r, juce::Justification::centredLeft, true);
    }

    juce::String gb (juce::int64 bytes) { return juce::String ((double) bytes / (1024.0 * 1024.0 * 1024.0), 1).replaceCharacter ('.', ',') + " Go"; }
    juce::String mb (juce::int64 bytes) { return juce::String ((int) (bytes / (1024 * 1024))) + " Mo"; }
}

void HardwareDashboard::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat();
    style::paintGlass (g, *this, ctx, r, t.radius());
    r = r.reduced (16);
    const float linkW = juce::jmin (190.0f, r.getWidth() * 0.2f);
    const float cardW = (r.getWidth() - linkW) * 0.5f;
    auto left = r.removeFromLeft (cardW);
    auto right = r.removeFromRight (cardW);
    auto link = r;

    // ---- COMPUTER ---------------------------------------------------------------------------
    card (g, left, t, "ORDINATEUR"_t);
    auto c = left.reduced (18, 12);
    c.removeFromTop (22);
    {
        auto head = c.removeFromTop (54);
        auto icon = head.removeFromLeft (64);
        // computer glyph: screen + stand
        auto screen = icon.withSizeKeepingCentre (56, 38).translated (0, -5);
        g.setColour (juce::Colour (0xff15191d));
        g.fillRoundedRectangle (screen, 4.0f);
        g.setGradientFill (juce::ColourGradient (t.accent().withAlpha (0.35f), screen.getX(), screen.getY(), t.accent2().withAlpha (0.15f), screen.getRight(), screen.getBottom(), false));
        g.fillRoundedRectangle (screen.reduced (3.0f), 2.5f);
        g.setColour (juce::Colour (0xff8c939b));
        g.fillRect (screen.getCentreX() - 4.0f, screen.getBottom(), 8.0f, 7.0f);
        g.fillRoundedRectangle (screen.getCentreX() - 14.0f, screen.getBottom() + 6.0f, 28.0f, 3.0f, 1.5f);
        head.removeFromLeft (10);
        g.setColour (t.textCol());
        g.setFont (style::font (15.5f, true));
        g.drawText (snapshot.osName, head.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setColour (t.textDim());
        g.setFont (style::font (12.0f));
        g.drawText (snapshot.cpuName + (snapshot.cpuCores > 0 ? "  ·  "_t + juce::String (snapshot.cpuCores) + " cœurs"_t : juce::String()),
                    head.removeFromTop (18), juce::Justification::centredLeft, true);
    }
    c.removeFromTop (8);
    const float memFrac = snapshot.memUsed >= 0 && snapshot.memTotal > 0 ? (float) ((double) snapshot.memUsed / (double) snapshot.memTotal) : -1.0f;
    gauge (g, c.removeFromTop (30), t, "Mémoire vive (RAM)"_t,
           snapshot.memUsed >= 0 ? gb (snapshot.memUsed) + " / " + gb (snapshot.memTotal) : gb (snapshot.memTotal), memFrac);
    c.removeFromTop (6);
    gauge (g, c.removeFromTop (30), t, "Charge CPU"_t, snapshot.cpuLoad >= 0 ? juce::String (juce::roundToInt (snapshot.cpuLoad * 100.0)) + " %" : "…"_t,
           (float) snapshot.cpuLoad);
    c.removeFromTop (10);
    const auto eff = ctx.engine().getSettings();
    const auto resident = MemoryPlayStream::residentBytes();
    statusLine (g, c.removeFromTop (20), t, eff.memoryPlay,
                eff.memoryPlay ? "Memory Play : actif"_t + (resident > 0 ? " · "_t + mb (resident) + " en RAM"_t : juce::String())
                               : juce::String ("Memory Play : désactivé (lecture disque)"_t));
    const bool preventing = (bool) ctx.settings().get ("preventSleep", true);
    statusLine (g, c.removeFromTop (20), t, ctx.isSleepBlocked(),
                ! SleepBlocker::isSupported() ? juce::String ("Mise en veille : non gérée sur ce système"_t)
                : ctx.isSleepBlocked() ? juce::String ("Veille empêchée : lecture en cours"_t)
                : preventing ? juce::String ("Veille empêchée pendant la lecture (prêt)"_t)
                             : juce::String ("Mise en veille autorisée"_t));
    if (snapshot.appMemory > 0)
        statusLine (g, c.removeFromTop (20), t, true, "NX Player : "_t + mb (snapshot.appMemory) + " · moteur 64 bits"_t);

    // ---- AUDIO DEVICE ------------------------------------------------------------------------
    card (g, right, t, "PÉRIPHÉRIQUE AUDIO"_t);
    auto d = right.reduced (18, 12);
    d.removeFromTop (22);
    const auto device = status.open && status.deviceName.isNotEmpty() ? status.deviceName : ctx.engine().getDeviceName();
    const auto family = familyFromName (device) != DeviceFamily::None ? familyFromName (device)
                                                                     : activeFamily (ctx.engine().getSettings(), device);
    g.setColour (t.textCol());
    g.setFont (style::font (15.5f, true));
    g.drawText (device.isNotEmpty() ? device : juce::String ("Aucun périphérique"_t), d.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setColour (t.textDim());
    g.setFont (style::font (12.0f));
    g.drawText (family != DeviceFamily::None ? deviceart::familyName (family) + " · "_t + ctx.getCurrentBackendName() : ctx.getCurrentBackendName(),
                d.removeFromTop (18), juce::Justification::centredLeft, true);
    const float level = ctx.engine().getState() == PlaybackEngine::State::Playing ? 0.5f + 0.5f * (float) std::sin (phase * 3.0) * 0.6f : 0.0f;
    deviceart::draw (g, family, d.removeFromTop (92).reduced (0, 6), t, juce::jlimit (0.0f, 1.0f, level), true);
    d.removeFromTop (6);

    // rate matrix
    const double currentRate = status.open ? status.sampleRate : 0.0;
    auto rowOf = [&] (const juce::String& label, const std::vector<std::pair<juce::String, double>>& rates, bool dsd)
    {
        auto line = d.removeFromTop (24);
        g.setColour (t.textDim());
        g.setFont (style::font (11.0f, true));
        g.drawText (label, line.removeFromLeft (44), juce::Justification::centredLeft);
        const float w = juce::jmin (64.0f, (line.getWidth() - 4.0f * (float) (rates.size() - 1)) / (float) rates.size());
        for (auto& [text, rate] : rates)
        {
            auto chip = line.removeFromLeft (w).reduced (0, 3);
            line.removeFromLeft (4);
            const auto support = supportFor (caps, rate, dsd);
            const bool active = ! dsd ? std::abs (currentRate - rate) < 1.0 && status.wire == WireFormat::Pcm
                                      : (status.wire == WireFormat::NativeDsd && std::abs (currentRate - rate) < 1.0)
                                        || (status.wire == WireFormat::DoP && std::abs (currentRate * 16.0 - rate) < 1.0);
            const auto col = dsd ? style::qualityColour (t, PathQuality::BitPerfect) : (rate >= 352000 ? t.accent2() : t.accent());
            if (active)
            {
                g.setColour (col.withAlpha (0.35f));
                g.fillRoundedRectangle (chip.expanded (2.0f), 7.0f);
            }
            if (support == RateSupport::Pcm || support == RateSupport::Native)
            {
                g.setColour (col.withAlpha (active ? 1.0f : 0.85f));
                g.fillRoundedRectangle (chip, 6.0f);
                g.setColour (t.bg());
            }
            else if (support == RateSupport::DoP)
            {
                g.setColour (col.withAlpha (0.18f));
                g.fillRoundedRectangle (chip, 6.0f);
                g.setColour (col);
                g.drawRoundedRectangle (chip.reduced (0.5f), 6.0f, 1.2f);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (chip, 6.0f);
                g.setColour (t.textDim().withAlpha (0.45f));
            }
            g.setFont (style::font (11.5f, true));
            g.drawText (text, chip, juce::Justification::centred);
        }
    };
    rowOf ("PCM", { { "44,1", 44100 }, { "88,2", 88200 }, { "176,4", 176400 }, { "352,8", 352800 } }, false);
    rowOf ("", { { "48", 48000 }, { "96", 96000 }, { "192", 192000 }, { "384", 384000 } }, false);
    rowOf ("DSD", { { "64", 2822400 }, { "128", 5644800 }, { "256", 11289600 }, { "512", 22579200 }, { "1024", 45158400 } }, true);
    g.setColour (t.textDim());
    g.setFont (style::font (10.5f));
    g.drawText ("Plein : accepté nativement  ·  contour : DSD en DoP  ·  halo : en cours"_t, d.removeFromTop (16), juce::Justification::centredLeft, true);

    // ---- LINK ----------------------------------------------------------------------------------
    const auto kind = connectionKind (device, status.backendName);
    const float y = left.getY() + 132.0f;
    juce::Line<float> line (left.getRight() + 6.0f, y, right.getX() - 6.0f, y);
    const bool playing = ctx.engine().getState() == PlaybackEngine::State::Playing;
    g.setColour (t.accent().withAlpha (0.25f));
    g.drawLine (line, 3.0f);
    g.setGradientFill (juce::ColourGradient (t.accent(), line.getStartX(), y, t.accent2(), line.getEndX(), y, false));
    g.drawLine (line, 1.5f);
    if (playing)
        for (int k = 0; k < 4; ++k)
        {
            const float f = (float) std::fmod (phase * 0.6 + k * 0.25, 1.0);
            const auto p = line.getPointAlongLineProportionally (f);
            g.setColour (t.accent2().withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (p));
            g.setColour (t.accent2());
            g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (p));
        }
    auto label = juce::Rectangle<float> (link.getWidth() - 8.0f, 26.0f).withCentre ({ link.getCentreX(), y - 26.0f });
    const auto kindText = kind == ConnectionKind::Network ? juce::String ("Réseau"_t) : kind == ConnectionKind::BuiltIn ? juce::String ("Intégré"_t)
                        : kind == ConnectionKind::Virtual ? juce::String ("Virtuel") : juce::String ("USB");
    g.setColour (t.bg().withAlpha (0.85f));
    g.fillRoundedRectangle (label, 13.0f);
    g.setColour (t.accent().withAlpha (0.6f));
    g.drawRoundedRectangle (label.reduced (0.5f), 13.0f, 1.0f);
    style::drawIcon (g, kind == ConnectionKind::Network ? style::Icon::Network : style::Icon::Signal, label.removeFromLeft (28).reduced (6.0f), t.accent());
    g.setColour (t.textCol());
    g.setFont (style::font (12.5f, true));
    g.drawText (kindText, label.withTrimmedRight (10.0f), juce::Justification::centred);

    const bool exclusive = status.open ? status.exclusive : ctx.engine().getSettings().exclusive;
    auto lock = juce::Rectangle<float> (link.getWidth() - 8.0f, 44.0f).withCentre ({ link.getCentreX(), y + 36.0f });
    g.setColour ((exclusive ? style::qualityColour (t, PathQuality::BitPerfect) : t.textDim()).withAlpha (0.16f));
    g.fillRoundedRectangle (lock, 10.0f);
    auto li = lock.removeFromLeft (30).reduced (6.0f, 11.0f);
    style::drawIcon (g, style::Icon::Lock, li, exclusive ? style::qualityColour (t, PathQuality::BitPerfect) : t.textDim());
    g.setColour (exclusive ? t.textCol() : t.textDim());
    g.setFont (style::font (11.5f, true));
    g.drawFittedText (exclusive ? (status.open ? "Accès exclusif\n(Hog / Integer Mode)"_t : "Accès exclusif\ndemandé"_t) : "Partagé\n(mixeur du système)"_t,
                      lock.toNearestInt().withTrimmedRight (4), juce::Justification::centredLeft, 2);
    if (status.open)
    {
        g.setColour (t.textDim());
        g.setFont (style::font (11.0f));
        g.drawText (formatRate (status.sampleRate) + (status.deviceBits > 0 ? " · "_t + juce::String (status.deviceBits) + " bits" : juce::String()),
                    juce::Rectangle<float> (link.getWidth(), 18.0f).withCentre ({ link.getCentreX(), y + 72.0f }), juce::Justification::centred);
    }
}

//==============================================================================
void QrCodeView::setText (const juce::String& t)
{
    if (t == text) return;
    text = t;
    modules.clear();
    size = 0;
    try
    {
        const auto qr = qrcodegen::QrCode::encodeText (text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
        size = qr.getSize();
        modules.resize ((size_t) (size * size));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) modules[(size_t) (y * size + x)] = qr.getModule (x, y);
    }
    catch (...) { size = 0; }
    repaint();
}

void QrCodeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float side = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (side, side);
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (r, 10.0f);
    if (size <= 0) return;
    const float cell = side / (float) (size + 8);   // quiet zone of 4 modules
    g.setColour (juce::Colour (0xff0a0c0e));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (modules[(size_t) (y * size + x)])
                g.fillRect (r.getX() + (float) (x + 4) * cell, r.getY() + (float) (y + 4) * cell, cell + 0.4f, cell + 0.4f);
}

//==============================================================================
RemotePanel::RemotePanel (AppContext& c) : ctx (c), openButton ("Ouvrir dans le navigateur"_t), copyButton ("Copier le lien"_t)
{
    addAndMakeVisible (qr);
    addAndMakeVisible (openButton);
    addAndMakeVisible (copyButton);
    openButton.onClick = [this] { juce::URL (ctx.remote().getUrl()).launchInDefaultBrowser(); };
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (ctx.remote().getUrl()); ctx.showMessage ("Lien NX Remote copié"_t); };
    qr.setText (ctx.remote().getUrl());
    startTimerHz (2);
}

void RemotePanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    qr.setBounds (r.removeFromLeft (r.getHeight()));
    r.removeFromLeft (20);
    auto buttons = r.removeFromBottom (36);
    openButton.setBounds (buttons.removeFromLeft (220));
    buttons.removeFromLeft (10);
    copyButton.setBounds (buttons.removeFromLeft (150));
}

void RemotePanel::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    style::paintGlass (g, *this, ctx, getLocalBounds().toFloat(), t.radius());
    qr.setText (ctx.remote().getUrl());
    auto r = getLocalBounds().reduced (16);
    r.removeFromLeft (r.getHeight() + 20);
    r.removeFromBottom (46);
    g.setColour (t.textCol());
    g.setFont (style::font (18.0f, true));
    g.drawText ("Scannez avec votre téléphone"_t, r.removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (t.textDim());
    g.setFont (style::font (13.0f));
    g.drawFittedText ("Même réseau Wi-Fi que cet ordinateur. Bibliothèque, lecture, volume, boucle A-B et profils Anubis / RME depuis le navigateur du téléphone, "_t
                      "sans application à installer."_t, r.removeFromTop (76), juce::Justification::topLeft, 4, 1.0f);
    r.removeFromTop (6);
    g.setColour (t.accent());
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    g.drawFittedText (ctx.remote().getUrl(), r.removeFromTop (40), juce::Justification::topLeft, 2);
    g.setColour (t.textDim());
    g.setFont (style::font (12.0f));
    g.drawText (juce::String (ctx.remote().getRequestCount()) + " requête(s) depuis l'activation"_t, r.removeFromTop (20), juce::Justification::centredLeft);
}

} // namespace nx
