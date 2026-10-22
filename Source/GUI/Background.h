// NX Player — MIT License
// The living background (Cider style): the current artwork, blurred, saturated and dimmed, cross-faded
// on track change; or a slowly drifting mesh gradient built from the theme / artwork colours; or a
// theme GLSL shader on OpenGL builds. Also produces the extra-blurred image glass panels show.
#pragma once

#include "AppContext.h"
#include "Artwork.h"

#if NX_HAS_OPENGL
 #include <juce_opengl/juce_opengl.h>
#endif

namespace nx
{

class BackgroundComponent : public juce::Component, private juce::Timer
{
public:
    explicit BackgroundComponent (AppContext& ctx);
    ~BackgroundComponent() override;

    void setArtwork (const juce::String& artworkKey, const juce::String& placeholderText);
    void themeChanged();
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Blurred image for glass panels (same aspect as this component). */
    const juce::Image& getGlassBackdrop() const noexcept { return glass; }
    juce::Colour getDominantColour() const noexcept { return dominant; }
    juce::String getMode() const { return mode; }

private:
    void timerCallback() override;
    void rebuildFromArtwork (const juce::Image& art);
    void renderMesh (double time);
    void updateGlass();

    AppContext& ctx;
    juce::String mode = "artwork";                      // artwork | mesh | gradient | shader
    juce::Image current, previous, glass, mesh;
    juce::Colour dominant { 0xff00f5a0 };
    float fade = 1.0f;
    juce::String artworkKey;
    double time0 = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    int glassCountdown = 0;
    juce::ThreadPool worker { 1 };
    std::atomic<int> generation { 0 };

   #if NX_HAS_OPENGL
    std::unique_ptr<juce::OpenGLGraphicsContextCustomShader> shader;
    juce::String shaderError;
   #endif
};

} // namespace nx
