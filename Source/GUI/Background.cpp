// NX Player — MIT License
#include "Background.h"
#include "../Ext/AudioAnalyzer.h"

namespace nx
{

BackgroundComponent::BackgroundComponent (AppContext& c) : ctx (c)
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);
    themeChanged();
    startTimerHz (30);
}

BackgroundComponent::~BackgroundComponent()
{
    stopTimer();
    worker.removeAllJobs (true, 3000);
}

void BackgroundComponent::themeChanged()
{
    const auto& t = ctx.theme();
    mode = t.background.getProperty ("type", "artwork").toString();
   #if NX_HAS_OPENGL
    shader.reset();
    shaderError.clear();
   #else
    if (mode == "shader") mode = t.background.getProperty ("fallback", "mesh").toString();
   #endif
    const auto key = artworkKey;
    artworkKey = "\x01";   // force a rebuild with the new blur / dim settings
    setArtwork (key, {});
    repaint();
}

void BackgroundComponent::resized()
{
    updateGlass();
}

void BackgroundComponent::setArtwork (const juce::String& key, const juce::String&)
{
    if (key == artworkKey) return;
    artworkKey = key;
    const int gen = ++generation;
    if (key.isEmpty())
    {
        previous = current;
        current = {};
        fade = 0.0f;
        return;
    }
    juce::Component::SafePointer<BackgroundComponent> safe (this);
    worker.addJob ([this, safe, key, gen]
    {
        auto art = ctx.artwork().loadNow (key, 512);
        juce::MessageManager::callAsync ([safe, art, gen]
        {
            if (safe == nullptr || safe->generation.load() != gen) return;
            safe->rebuildFromArtwork (art);
        });
    });
}

void BackgroundComponent::rebuildFromArtwork (const juce::Image& art)
{
    previous = current;
    fade = 0.0f;
    if (! art.isValid()) { current = {}; return; }

    const auto& t = ctx.theme();
    dominant = imaging::dominantColour (art);
    const float aspect = getHeight() > 0 ? (float) getWidth() / (float) getHeight() : 1.6f;
    const int w = 160, h = juce::jmax (40, (int) ((float) w / aspect));
    auto img = imaging::coverFit (art, w, h).convertedToFormat (juce::Image::RGB);
    imaging::adjustSaturation (img, t.number ("--nx-artwork-saturation", 1.2f));
    imaging::blur (img, juce::jmax (2, (int) (t.number ("--nx-artwork-blur", 48.0f) / 6.0f)));
    {
        juce::Graphics g (img);
        const float dim = juce::jlimit (0.0f, 0.95f, t.number ("--nx-artwork-dim", 0.58f));
        g.setColour (t.bg().withAlpha (dim));
        g.fillAll();
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0, 0, t.bg().withAlpha (0.55f), 0, (float) h, false));
        g.fillAll();
    }
    current = img;
    updateGlass();
}

void BackgroundComponent::renderMesh (double time)
{
    const auto& t = ctx.theme();
    const int w = 120, h = juce::jmax (40, getWidth() > 0 ? 120 * getHeight() / getWidth() : 75);
    if (! mesh.isValid() || mesh.getWidth() != w || mesh.getHeight() != h) mesh = juce::Image (juce::Image::RGB, w, h, true);
    const float speed = (float) t.background.getProperty ("speed", 0.15) * t.number ("--nx-animation-speed", 1.0f);
    const float level = 0.6f + 0.8f * ctx.analyzer().getLevel();
    juce::Graphics g (mesh);
    g.fillAll (t.bg());
    const juce::Colour cols[] = { t.accent(), t.accent2(), current.isValid() ? dominant : t.accent().withRotatedHue (0.15f) };
    for (int i = 0; i < 3; ++i)
    {
        const float ph = (float) time * speed + (float) i * 2.1f;
        const float cx = (0.5f + 0.38f * std::sin (ph * 0.7f + i)) * (float) w;
        const float cy = (0.5f + 0.34f * std::cos (ph * 0.9f + i * 1.7f)) * (float) h;
        const float r = (0.55f + 0.15f * std::sin (ph * 1.3f)) * (float) juce::jmax (w, h);
        juce::ColourGradient grad (cols[i].withAlpha (0.30f * level), cx, cy, cols[i].withAlpha (0.0f), cx + r, cy, true);
        g.setGradientFill (grad);
        g.fillRect (0, 0, w, h);
    }
}

void BackgroundComponent::updateGlass()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    const int w = juce::jmax (32, getWidth() / 6), h = juce::jmax (24, getHeight() / 6);
    juce::Image img (juce::Image::RGB, w, h, true);
    {
        juce::Graphics g (img);
        g.fillAll (ctx.theme().bg());
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        if (current.isValid() && mode == "artwork")
        {
            if (previous.isValid() && fade < 1.0f) g.drawImage (previous, img.getBounds().toFloat());
            g.setOpacity (previous.isValid() ? fade : juce::jmin (1.0f, fade + 0.0001f) );
            g.drawImage (current, img.getBounds().toFloat());
            g.setOpacity (1.0f);
        }
        else if (mesh.isValid())
        {
            g.drawImage (mesh, img.getBounds().toFloat());
        }
    }
    imaging::blur (img, 6);
    glass = img;
}

void BackgroundComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    bool dirty = false;
    if (fade < 1.0f)
    {
        fade = juce::jmin (1.0f, fade + (1.0f / 30.0f) / 0.8f * ctx.theme().number ("--nx-animation-speed", 1.0f));
        dirty = true;
    }
    const bool animated = mode == "mesh" || mode == "gradient" || mode == "shader" || ! current.isValid();
    if (animated)
    {
        renderMesh (now - time0);
        dirty = true;
    }
    if (dirty)
    {
        repaint();
        if (++glassCountdown >= (animated ? 6 : 2)) { glassCountdown = 0; updateGlass(); }
    }
}

void BackgroundComponent::paint (juce::Graphics& g)
{
    const auto& t = ctx.theme();
    const auto r = getLocalBounds().toFloat();
    g.fillAll (t.bg());

   #if NX_HAS_OPENGL
    if (mode == "shader" && t.shaderCode.isNotEmpty() && shaderError.isEmpty()
        && dynamic_cast<juce::OpenGLContext*> (juce::OpenGLContext::getCurrentContext()) != nullptr)
    {
        if (shader == nullptr) shader = std::make_unique<juce::OpenGLGraphicsContextCustomShader> (t.shaderCode);
        const auto res = shader->checkCompilation (g.getInternalContext());
        if (res.failed()) { shaderError = res.getErrorMessage(); juce::Logger::writeToLog ("shader : " + shaderError); }
        else
        {
            const float time = (float) (juce::Time::getMillisecondCounterHiRes() / 1000.0 - time0);
            const auto a1 = t.accent(), a2 = t.accent2();
            const float level = ctx.analyzer().getLevel();
            const float w = (float) getWidth(), h = (float) getHeight();
            shader->onShaderActivated = [time, a1, a2, level, w, h] (juce::OpenGLShaderProgram& p)
            {
                p.setUniform ("u_time", time);
                p.setUniform ("u_resolution", w, h);
                p.setUniform ("u_accent", a1.getFloatRed(), a1.getFloatGreen(), a1.getFloatBlue(), 1.0f);
                p.setUniform ("u_accent2", a2.getFloatRed(), a2.getFloatGreen(), a2.getFloatBlue(), 1.0f);
                p.setUniform ("u_level", level);
            };
            shader->fillRect (g.getInternalContext(), getLocalBounds());
            return;
        }
    }
   #endif

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (mode == "artwork" && (current.isValid() || previous.isValid()))
    {
        if (previous.isValid()) g.drawImage (previous, r);
        else if (! current.isValid()) g.drawImage (mesh, r);
        if (current.isValid())
        {
            g.setOpacity (fade);
            g.drawImage (current, r);
            g.setOpacity (1.0f);
        }
        else if (previous.isValid() && mesh.isValid())
        {
            g.setOpacity (fade);
            g.drawImage (mesh, r);
            g.setOpacity (1.0f);
        }
    }
    else if (mesh.isValid())
    {
        g.drawImage (mesh, r);
    }
    // subtle grain-free vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colours::black.withAlpha (0.35f), 0, 0, true));
    g.fillRect (r);
}

} // namespace nx
