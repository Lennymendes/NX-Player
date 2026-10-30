// NX Player — MIT License
#include "Immersive.h"
#include "../Ext/AudioAnalyzer.h"
#include "../Engine/DeviceProfiles.h"
#include "../Audio/AudioDecoder.h"

namespace nx
{

//==============================================================================
void ImmersiveConfig::apply (const juce::var& json, const juce::String& origin)
{
    auto* o = json.getDynamicObject();
    if (o == nullptr) return;
    bool any = false;
    auto boolKey = [&] (const char* key, bool& field)
    {
        if (o->hasProperty (key)) { field = (bool) o->getProperty (key); any = true; }
    };
    if (o->hasProperty ("layout"))
    {
        const auto l = o->getProperty ("layout").toString();
        if (l == "split" || l == "centered" || l == "minimal") { layout = l; any = true; }
    }
    boolKey ("lyrics", lyrics);
    boolKey ("waveform", waveform);
    boolKey ("vu", vu);
    boolKey ("spectrum", spectrum);
    boolKey ("tech", tech);
    boolKey ("controls", controls);
    if (o->hasProperty ("artworkScale")) { artworkScale = juce::jlimit (0.5f, 1.4f, (float) o->getProperty ("artworkScale")); any = true; }
    if (o->hasProperty ("motion")) { motion = juce::jlimit (0.0f, 3.0f, (float) o->getProperty ("motion")); any = true; }
    if (o->hasProperty ("blur")) { blur = juce::jlimit (0.0f, 1.0f, (float) o->getProperty ("blur")); any = true; }
    if (any) source = origin;
}

void ImmersiveConfig::applyTheme (const Theme& t)
{
    auto* o = new juce::DynamicObject();
    juce::var v (o);
    auto flag = [&] (const char* css, const char* key)
    {
        const auto s = t.text (css).trim().toLowerCase();
        if (s.isEmpty()) return;
        o->setProperty (key, ! (s == "off" || s == "none" || s == "0" || s == "false" || s == "hidden"));
    };
    if (auto l = t.text ("--nx-immersive-layout").trim(); l.isNotEmpty()) o->setProperty ("layout", l);
    flag ("--nx-immersive-lyrics", "lyrics");
    flag ("--nx-immersive-waveform", "waveform");
    flag ("--nx-immersive-vu", "vu");
    flag ("--nx-immersive-spectrum", "spectrum");
    flag ("--nx-immersive-tech", "tech");
    if (t.text ("--nx-immersive-artwork-scale").isNotEmpty()) o->setProperty ("artworkScale", t.number ("--nx-immersive-artwork-scale", 1.0f));
    if (t.text ("--nx-immersive-motion").isNotEmpty()) o->setProperty ("motion", t.number ("--nx-immersive-motion", 1.0f));
    if (t.text ("--nx-immersive-blur").isNotEmpty()) o->setProperty ("blur", t.number ("--nx-immersive-blur", 1.0f));
    apply (v, juce::String ("thème"_t));
    // a theme can also ship immersive.json next to its theme.css
    if (t.folder.isDirectory())
        if (auto f = t.folder.getChildFile ("immersive.json"); f.existsAsFile())
            apply (juce::JSON::parse (f), juce::String ("thème"_t));
}

juce::var ImmersiveConfig::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("layout", layout);
    o->setProperty ("lyrics", lyrics);
    o->setProperty ("waveform", waveform);
    o->setProperty ("vu", vu);
    o->setProperty ("spectrum", spectrum);
    o->setProperty ("tech", tech);
    o->setProperty ("controls", controls);
    o->setProperty ("artworkScale", artworkScale);
    o->setProperty ("motion", motion);
    o->setProperty ("blur", blur);
    return juce::var (o);
}

ImmersiveConfig ImmersiveConfig::resolve (AppContext& ctx)
{
    ImmersiveConfig c;
    c.source = "défaut"_t;
    c.applyTheme (ctx.theme());
    c.apply (ctx.settings().get ("immersive"), "réglages"_t);
    c.apply (ctx.layout.getProperty ("immersive", {}), "plug-in");
    return c;
}

//==============================================================================
std::vector<juce::Colour> extractPalette (const juce::Image& image, int count)
{
    std::vector<juce::Colour> out;
    if (! image.isValid()) return out;
    const auto small = image.rescaled (48, 48, juce::Graphics::mediumResamplingQuality);
    struct P { float r, g, b, w; };
    std::vector<P> px;
    juce::Image::BitmapData bd (small, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            const float sat = c.getSaturation(), bri = c.getBrightness();
            px.push_back ({ c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), 0.25f + sat * 1.5f + (bri > 0.15f && bri < 0.95f ? 0.5f : 0.0f) });
        }
    // k-means, seeds spread over the brightness order
    std::sort (px.begin(), px.end(), [] (const P& a, const P& b) { return a.r + a.g + a.b < b.r + b.g + b.b; });
    std::vector<P> centres;
    for (int k = 0; k < count; ++k) centres.push_back (px[(size_t) ((k * 2 + 1) * (int) px.size() / (2 * count))]);
    std::vector<int> owner (px.size(), 0);
    for (int iter = 0; iter < 8; ++iter)
    {
        for (size_t i = 0; i < px.size(); ++i)
        {
            float best = 1e9f;
            for (int k = 0; k < count; ++k)
            {
                const float d = (px[i].r - centres[(size_t) k].r) * (px[i].r - centres[(size_t) k].r) + (px[i].g - centres[(size_t) k].g) * (px[i].g - centres[(size_t) k].g)
                              + (px[i].b - centres[(size_t) k].b) * (px[i].b - centres[(size_t) k].b);
                if (d < best) { best = d; owner[i] = k; }
            }
        }
        std::vector<P> sum ((size_t) count, P { 0, 0, 0, 0 });
        for (size_t i = 0; i < px.size(); ++i)
        {
            auto& s = sum[(size_t) owner[i]];
            s.r += px[i].r * px[i].w; s.g += px[i].g * px[i].w; s.b += px[i].b * px[i].w; s.w += px[i].w;
        }
        for (int k = 0; k < count; ++k)
            if (sum[(size_t) k].w > 0) centres[(size_t) k] = { sum[(size_t) k].r / sum[(size_t) k].w, sum[(size_t) k].g / sum[(size_t) k].w, sum[(size_t) k].b / sum[(size_t) k].w, sum[(size_t) k].w };
            else centres[(size_t) k].w = 0;
    }
    std::sort (centres.begin(), centres.end(), [] (const P& a, const P& b) { return a.w > b.w; });
    for (auto& c : centres) out.push_back (juce::Colour::fromFloatRGBA (c.r, c.g, c.b, 1.0f));
    return out;
}

//==============================================================================
void WaveformBar::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    auto r = getLocalBounds().toFloat();
    const auto times = r.removeFromBottom (18.0f);
    const double shown = dragTime >= 0 ? dragTime : position;
    const float progress = length > 0 ? (float) juce::jlimit (0.0, 1.0, shown / length) : 0.0f;
    // A-B loop zone
    auto& e = ctx.engine();
    if (e.isLoopActive() && length > 0)
    {
        const float a = r.getX() + r.getWidth() * (float) (e.getLoopA() / length), b = r.getX() + r.getWidth() * (float) (e.getLoopB() / length);
        g.setColour (t.accent().withAlpha (0.14f));
        g.fillRoundedRectangle (a, r.getY(), b - a, r.getHeight(), 4.0f);
    }
    if (analysis == nullptr)
    {
        auto bar = r.withSizeKeepingCentre (r.getWidth(), 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (bar, 2.5f);
        g.setColour (t.textCol());
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 2.5f);
    }
    else
        painter.paint (g, r, *analysis, progress, t.textCol(), juce::Colours::white.withAlpha (0.3f));
    g.setColour (t.textCol().withAlpha (0.75f));
    g.setFont (style::font (12.0f, true));
    g.drawText (style::formatTime (shown), times, juce::Justification::centredLeft);
    g.drawText ("-" + style::formatTime (juce::jmax (0.0, length - shown)), times, juce::Justification::centredRight);
}

void WaveformBar::mouseDrag (const juce::MouseEvent& e)
{
    if (length <= 0) return;
    dragTime = length * juce::jlimit (0.0, 1.0, (double) e.position.x / juce::jmax (1.0, (double) getWidth()));
    repaint();
}

void WaveformBar::mouseUp (const juce::MouseEvent& e)
{
    mouseDrag (e);
    if (dragTime >= 0 && onSeek) onSeek (dragTime);
    dragTime = -1;
}

//==============================================================================
ImmersiveView::ImmersiveView (AppContext& c)
    : ctx (c), artwork (c, 1600), lyricsView (c), waveform (c), overlaySlot (c, "immersive.overlay"),
      closeButton (style::Icon::Close, "Quitter la vue immersive (Échap)"_t, c),
      menuButton (style::Icon::Grid, "Widgets de la vue immersive"_t, c),
      prevButton (style::Icon::Previous, "Précédent"_t, c), playButton (style::Icon::Play, "Lecture / pause"_t, c), nextButton (style::Icon::Next, "Suivant"_t, c)
{
    artwork.cornerRadius = 18.0f;
    for (auto* comp : std::initializer_list<juce::Component*> { &artwork, &lyricsView, &waveform, &overlaySlot, &closeButton, &menuButton, &prevButton, &playButton, &nextButton })
        addAndMakeVisible (comp);
    playButton.primary = true;
    closeButton.onClick = [this] { close(); };
    menuButton.onClick = [this] { showMenu(); };
    prevButton.onClick = [this] { ctx.engine().previous(); };
    nextButton.onClick = [this] { ctx.engine().next(); };
    playButton.onClick = [this] { ctx.engine().togglePlayPause(); };
    waveform.onSeek = [this] (double s) { ctx.engine().seek (s); };
    ctx.artwork().addChangeListener (this);
    ctx.trackAnalyzer().addChangeListener (this);
    setWantsKeyboardFocus (false);
}

ImmersiveView::~ImmersiveView()
{
    ctx.artwork().removeChangeListener (this);
    ctx.trackAnalyzer().removeChangeListener (this);
}

void ImmersiveView::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &ctx.trackAnalyzer()) { waveform.setAnalysis (ctx.currentAnalysis()); repaint(); }
    else updatePalette();
}

void ImmersiveView::open()
{
    refreshConfig();
    setVisible (true);
    toFront (false);
    trackChanged();
    lastMouse = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz (30);
}

void ImmersiveView::close()
{
    stopTimer();
    setVisible (false);
    if (onClose) onClose();
}

void ImmersiveView::refreshConfig()
{
    config = ImmersiveConfig::resolve (ctx);
    overlaySlot.refreshPlugins();
    resized();
    repaint();
}

void ImmersiveView::trackChanged()
{
    const auto* t = ctx.currentTrack();
    artwork.setArtwork (t != nullptr ? t->artworkKey : juce::String(), t != nullptr ? (t->album.isNotEmpty() ? t->album : t->title) : juce::String ("NX"));
    waveform.setAnalysis (ctx.currentAnalysis());   // the scan itself is requested by the controller
    updatePalette();
    repaint();
}

void ImmersiveView::updatePalette()
{
    const auto* t = ctx.currentTrack();
    const auto key = t != nullptr ? t->artworkKey : juce::String();
    auto img = ctx.artwork().get (key, 320);
    if (! img.isValid()) img = imaging::placeholder (t != nullptr ? t->album : juce::String ("NX"), 128, ctx.theme().accent(), ctx.theme().accent2());
    if (key == paletteKey && ! palette.empty() && blurredArt.isValid() && ctx.artwork().get (key, 320).isValid()) return;
    paletteKey = key;
    palette = extractPalette (img, 4);
    blurredArt = img.rescaled (96, 96, juce::Graphics::mediumResamplingQuality).convertedToFormat (juce::Image::ARGB);
    imaging::blur (blurredArt, 10);
    repaint();
}

void ImmersiveView::renderField()
{
    // The colour field is rendered small (192 × 120) and scaled up: smooth, cheap, naturally blurred.
    if (! field.isValid()) field = juce::Image (juce::Image::ARGB, 192, 120, true);
    juce::Graphics g (field);
    const auto& t = ctx.theme();
    const auto base = palette.empty() ? t.bg() : palette.front().darker (0.85f);
    g.fillAll (base);
    if (blurredArt.isValid())
    {
        g.setOpacity (0.55f * config.blur);
        g.drawImage (blurredArt, field.getBounds().toFloat(), juce::RectanglePlacement::fillDestination);
        g.setOpacity (1.0f);
    }
    const double tm = phase * config.motion;
    for (size_t k = 0; k < palette.size(); ++k)
    {
        const float fx = 96.0f + 70.0f * (float) std::sin (tm * (0.13 + 0.05 * (double) k) + (double) k * 1.9);
        const float fy = 60.0f + 45.0f * (float) std::cos (tm * (0.11 + 0.04 * (double) k) + (double) k * 2.7);
        const float rad = 70.0f + 25.0f * (float) std::sin (tm * 0.2 + (double) k);
        g.setGradientFill (juce::ColourGradient (palette[k].withMultipliedSaturation (1.25f).withAlpha (0.55f), fx, fy,
                                                 palette[k].withAlpha (0.0f), fx + rad, fy, true));
        g.fillEllipse (fx - rad, fy - rad, rad * 2.0f, rad * 2.0f);
    }
}

void ImmersiveView::timerCallback()
{
    phase += 1.0 / 30.0;
    auto& e = ctx.engine();
    double dur = e.getDurationSeconds();
    if (const auto* t = ctx.currentTrack()) if (dur <= 0) dur = t->duration;
    waveform.setPosition (e.getPositionSeconds(), dur);
    playButton.setIcon (e.getState() == PlaybackEngine::State::Playing ? style::Icon::Pause : style::Icon::Play);
    // controls fade out after 3 s without mouse movement
    const double idle = juce::Time::getMillisecondCounterHiRes() / 1000.0 - lastMouse;
    const float alpha = config.controls ? (idle < 3.0 ? 1.0f : juce::jmax (0.0f, 1.0f - (float) (idle - 3.0) * 2.0f)) : 0.0f;
    for (auto* b : { &closeButton, &menuButton }) b->setAlpha (juce::jmax (0.25f, alpha));
    for (auto* b : { &prevButton, &playButton, &nextButton }) { b->setAlpha (alpha); b->setVisible (alpha > 0.01f); }
    repaint();
}

void ImmersiveView::mouseMove (const juce::MouseEvent&)
{
    lastMouse = juce::Time::getMillisecondCounterHiRes() / 1000.0;
}

void ImmersiveView::resized()
{
    auto r = getLocalBounds().reduced (juce::jmax (32, getWidth() / 24), juce::jmax (24, getHeight() / 22));
    closeButton.setBounds (getWidth() - 56, 18, 36, 36);
    menuButton.setBounds (getWidth() - 100, 18, 36, 36);

    auto bottom = r.removeFromBottom (config.waveform ? 150 : config.controls ? 120 : 30);
    auto controls = bottom.removeFromBottom (56);
    const int b = 48;
    auto row = controls.withSizeKeepingCentre (b * 3 + 40, b);
    prevButton.setBounds (row.removeFromLeft (b).reduced (6));
    row.removeFromLeft (20);
    playButton.setBounds (row.removeFromLeft (b));
    row.removeFromLeft (20);
    nextButton.setBounds (row.removeFromLeft (b).reduced (6));
    waveform.setVisible (config.waveform);
    waveform.setBounds (bottom.reduced (0, 4).withSizeKeepingCentre (juce::jmin (bottom.getWidth(), 1100), juce::jmin (88, bottom.getHeight())));
    r.removeFromBottom (12);

    const bool split = config.layout == "split" && config.lyrics;
    const bool minimal = config.layout == "minimal";
    lyricsView.setVisible (config.lyrics && ! minimal);
    overlaySlot.setVisible (! overlaySlot.isEmpty());
    if (! overlaySlot.isEmpty()) overlaySlot.setBounds (r.removeFromBottom (juce::jmin (150, overlaySlot.getPreferredHeight())).withWidth (juce::jmin (r.getWidth(), 520)));

    if (split)
    {
        auto left = r.removeFromLeft (r.getWidth() * 45 / 100);
        r.removeFromLeft (40);
        const int art = juce::roundToInt ((float) juce::jmin (left.getWidth(), left.getHeight() - 130) * juce::jlimit (0.5f, 1.0f, config.artworkScale));
        artwork.setBounds (left.removeFromTop (art).withSizeKeepingCentre (art, art));
        infoArea = left.removeFromTop (78);
        techArea = config.tech ? left.removeFromTop (44) : juce::Rectangle<int>();
        metersArea = (config.vu || config.spectrum) ? left.removeFromTop (juce::jmin (90, left.getHeight())) : juce::Rectangle<int>();
        lyricsView.setBounds (r);
    }
    else
    {
        auto info = r.removeFromBottom (config.tech ? 120 : 80);
        const int art = juce::roundToInt ((float) juce::jmin (r.getWidth(), r.getHeight()) * juce::jlimit (0.5f, 1.4f, config.artworkScale) * (minimal ? 0.8f : 1.0f));
        auto centre = r;
        if (config.lyrics && ! minimal && r.getWidth() > art + 300)
        {
            // centred artwork with the lyrics underneath on the side
            lyricsView.setBounds (r.removeFromRight (r.getWidth() - art - 60).withTrimmedLeft (20));
            centre = r;
        }
        else lyricsView.setVisible (false);
        artwork.setBounds (centre.withSizeKeepingCentre (juce::jmin (art, centre.getWidth()), juce::jmin (art, centre.getHeight())));
        infoArea = info.removeFromTop (78);
        techArea = config.tech ? info.removeFromTop (40) : juce::Rectangle<int>();
        metersArea = (config.vu || config.spectrum) ? juce::Rectangle<int> (getWidth() - 260, getHeight() - 260, 220, 90) : juce::Rectangle<int>();
    }
}

void ImmersiveView::paintMeters (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = ctx.theme();
    auto& an = ctx.analyzer();
    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = r.removeFromTop (r.getHeight() / (float) (2 - ch)).reduced (0, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
        const float level = juce::jlimit (0.0f, 1.0f, an.getRms (ch) * 1.4f);
        g.setGradientFill (juce::ColourGradient (t.accent(), bar.getX(), 0, juce::Colour (0xffffb547), bar.getRight(), 0, false));
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (bar.getHeight(), bar.getWidth() * level)), bar.getHeight() * 0.5f);
        const float peakX = bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, an.getPeak (ch));
        g.setColour (t.textCol());
        g.fillRect (peakX - 1.0f, bar.getY(), 2.0f, bar.getHeight());
    }
}

void ImmersiveView::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& t = ctx.theme();
    const auto& bands = ctx.analyzer().getSpectrum();
    const float w = r.getWidth() / (float) bands.size();
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const float h = juce::jmax (2.0f, bands[i] * r.getHeight());
        g.setGradientFill (juce::ColourGradient (t.accent2(), 0, r.getBottom(), t.accent(), 0, r.getY(), false));
        g.fillRoundedRectangle (r.getX() + (float) i * w + w * 0.15f, r.getBottom() - h, w * 0.7f, h, w * 0.3f);
    }
}

void ImmersiveView::paint (juce::Graphics& g)
{
    renderField();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (field, getLocalBounds().toFloat());
    // vignette for readability
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, (float) getWidth() * 0.5f, (float) getHeight() * 0.45f,
                                             juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true));
    g.fillAll();

    const auto& t = ctx.theme();
    auto info = infoArea;
    const bool centred = config.layout != "split" || ! config.lyrics;
    const auto just = centred ? juce::Justification::centred : juce::Justification::centredLeft;
    if (const auto* tr = ctx.currentTrack())
    {
        g.setColour (juce::Colours::white);
        g.setFont (style::font (30.0f, true));
        g.drawText (tr->title, info.removeFromTop (40), just, true);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (style::font (18.0f));
        g.drawText (tr->displayArtist() + (tr->album.isNotEmpty() ? " — "_t + tr->album : juce::String()), info.removeFromTop (28), just, true);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (style::font (22.0f, true));
        g.drawText ("Rien en lecture"_t, info, just);
    }

    if (config.tech && ! techArea.isEmpty())
    {
        const auto path = ctx.engine().getSignalPath();
        const auto plan = ctx.engine().getCurrentPlan();
        if (! path.nodes.empty())
        {
            const auto label = pipelineLabel (plan, toString (path.overall));
            const auto c = style::qualityColour (t, path.overall);
            g.setFont (style::font (14.0f, true));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 52.0f;
            auto pill = techArea.toFloat().withSizeKeepingCentre (w, 30.0f);
            if (! centred) pill = pill.withX ((float) techArea.getX());
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (pill, 15.0f);
            g.setColour (c.withAlpha (0.7f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 15.0f, 1.0f);
            auto led = pill.removeFromLeft (30.0f).withSizeKeepingCentre (9, 9);
            g.setColour (c.withAlpha (0.35f));
            g.fillEllipse (led.expanded (4.0f));
            g.setColour (c);
            g.fillEllipse (led);
            g.setColour (juce::Colours::white);
            g.drawText (label, pill.withTrimmedRight (14.0f), juce::Justification::centredLeft);
            // channels (from the file header) + Peak / RMS / DR once the background scan is done
            juce::String extra;
            if (const auto* tr = ctx.currentTrack()) extra = channelBadge (tr->channels);
            if (auto an = ctx.currentAnalysis()) extra << (extra.isNotEmpty() ? juce::String::fromUTF8 ("  \xc2\xb7  ") : juce::String()) << an->summary();
            if (extra.isNotEmpty())
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.setFont (style::font (13.0f, true));
                g.drawText (extra, techArea.toFloat().withLeft (pill.getRight() + 14.0f), juce::Justification::centredLeft, true);
            }
        }
    }
    if (! metersArea.isEmpty())
    {
        auto m = metersArea.toFloat();
        if (config.vu && config.spectrum)
        {
            paintMeters (g, m.removeFromTop (m.getHeight() * 0.4f));
            m.removeFromTop (6.0f);
            paintSpectrum (g, m);
        }
        else if (config.vu) paintMeters (g, m.withHeight (juce::jmin (m.getHeight(), 40.0f)));
        else paintSpectrum (g, m);
    }
}

void ImmersiveView::showMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Vue immersive"_t);
    auto set = [this] (const juce::String& key, const juce::var& value)
    {
        auto o = ctx.settings().get ("immersive");
        if (! o.isObject()) o = juce::var (new juce::DynamicObject());
        o.getDynamicObject()->setProperty (key, value);
        ctx.settings().set ("immersive", o);
        refreshConfig();
    };
    m.addItem ("Partagée : pochette + paroles"_t, true, config.layout == "split", [set] { set ("layout", "split"); });
    m.addItem ("Centrée : grande pochette"_t, true, config.layout == "centered", [set] { set ("layout", "centered"); });
    m.addItem ("Minimale"_t, true, config.layout == "minimal", [set] { set ("layout", "minimal"); });
    m.addSeparator();
    m.addItem ("Paroles"_t, true, config.lyrics, [set, v = config.lyrics] { set ("lyrics", ! v); });
    m.addItem ("Forme d'onde"_t, true, config.waveform, [set, v = config.waveform] { set ("waveform", ! v); });
    m.addItem ("VU-mètres"_t, true, config.vu, [set, v = config.vu] { set ("vu", ! v); });
    m.addItem ("Spectre"_t, true, config.spectrum, [set, v = config.spectrum] { set ("spectrum", ! v); });
    m.addItem ("Infos techniques (DSD / DXD)"_t, true, config.tech, [set, v = config.tech] { set ("tech", ! v); });
    m.addSeparator();
    m.addItem ("Exporter la disposition (immersive.json)…"_t, [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Exporter la disposition"_t, juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("immersive.json"), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            f.replaceWithText (juce::JSON::toString (config.toVar()));
            ctx.showMessage ("Disposition exportée : placez immersive.json dans le dossier d'un thème .nxtheme pour la partager"_t);
        });
    });
    m.addItem ("Revenir à la disposition du thème"_t, [this] { ctx.settings().set ("immersive", juce::var (new juce::DynamicObject())); refreshConfig(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

} // namespace nx
