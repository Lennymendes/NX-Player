// NX Player — MIT License
// Immersive view (full screen Now Playing, Cider 2 style): an animated background made of the artwork's
// colours, the HD artwork, large synced lyrics, a waveform progress bar and clean controls.
//
// Modular / community: what is shown and where comes from an ImmersiveConfig merged from
//   1. defaults   2. the theme (theme folder immersive.json, or CSS --nx-immersive-* variables)
//   3. the user's settings (Réglages › Interface, or the view's own menu)   4. plug-ins (nx.ui.setLayout({ immersive: {...} }))
// and UI plug-ins can draw in the "immersive.overlay" slot.
#pragma once

#include "NowPlaying.h"
#include "Waveform.h"

namespace nx
{

struct ImmersiveConfig
{
    juce::String layout = "split";     // split | centered | minimal
    bool lyrics = true, waveform = true, vu = false, spectrum = false, tech = true, controls = true;
    float artworkScale = 1.0f;         // relative size of the artwork
    float motion = 1.0f;               // background animation speed (0 = still)
    float blur = 1.0f;                 // 0..1: how much of the blurred artwork shows through the colour field
    juce::String source;               // where the last values came from (tests / debug): "défaut", "thème", "réglages", "plug-in"

    /** Applies the keys of a JSON object ({ "layout": "centered", "lyrics": false, ... }). */
    void apply (const juce::var& json, const juce::String& origin);
    /** Applies the theme's CSS variables (--nx-immersive-layout, --nx-immersive-lyrics: off, ...). */
    void applyTheme (const Theme& t);
    juce::var toVar() const;
    static ImmersiveConfig resolve (AppContext& ctx);
};

/** Dominant colours of an image (k-means on a 48 × 48 thumbnail, saturated colours favoured). */
std::vector<juce::Colour> extractPalette (const juce::Image& image, int count = 4);

class WaveformBar : public juce::Component
{
public:
    explicit WaveformBar (AppContext& c) : ctx (c) {}
    void setAnalysis (std::shared_ptr<const TrackAnalysis> a) { analysis = std::move (a); repaint(); }
    void setPosition (double pos, double len) { position = pos; length = len; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    std::function<void (double)> onSeek;
    bool hasPeaks() const { return analysis != nullptr; }
private:
    AppContext& ctx;
    std::shared_ptr<const TrackAnalysis> analysis;
    WaveformPainter painter;
    double position = 0, length = 0, dragTime = -1;
};

class ImmersiveView : public juce::Component, private juce::Timer, private juce::ChangeListener
{
public:
    explicit ImmersiveView (AppContext& c);
    ~ImmersiveView() override;
    void open();
    void close();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void trackChanged();
    void setLyrics (const Lyrics& l, const juce::String& status) { lyricsView.statusText = status; lyricsView.setLyrics (l); }
    void refreshConfig();
    const ImmersiveConfig& getConfig() const { return config; }
    const std::vector<juce::Colour>& getPalette() const { return palette; }
    bool hasWaveform() const { return waveform.hasPeaks(); }
    std::function<void()> onClose;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* b) override;
    void updatePalette();
    void renderField();
    void showMenu();
    void paintMeters (juce::Graphics&, juce::Rectangle<float>);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);

    AppContext& ctx;
    ImmersiveConfig config;
    ArtworkView artwork;
    LyricsView lyricsView;
    WaveformBar waveform;
    PluginSlot overlaySlot;
    IconButton closeButton, menuButton, prevButton, playButton, nextButton;
    std::vector<juce::Colour> palette;
    juce::String paletteKey;
    juce::Image field, blurredArt;
    double phase = 0.0, lastMouse = 0.0;
    juce::Rectangle<int> infoArea, metersArea, techArea;
};

} // namespace nx
