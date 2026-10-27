// NX Player — MIT License
#pragma once

#include "Style.h"
#include "Artwork.h"

namespace nx
{

class IconButton : public juce::Button
{
public:
    IconButton (style::Icon i, const juce::String& tooltip, AppContext& c) : juce::Button (tooltip), icon (i), ctx (c)
    {
        setTooltip (tooltip);
        setWantsKeyboardFocus (false);   // the space bar stays play / pause instead of re-clicking the button
    }
    void setIcon (style::Icon i) { icon = i; repaint(); }
    bool active = false;          // accent colour (shuffle on, lyrics shown...)
    bool primary = false;         // big round play button
    void paintButton (juce::Graphics& g, bool hi, bool down) override;
private:
    style::Icon icon;
    AppContext& ctx;
};

class ArtworkView : public juce::Component, private juce::ChangeListener
{
public:
    ArtworkView (AppContext& c, int pixelSize);
    ~ArtworkView() override;
    void setArtwork (const juce::String& key, const juce::String& placeholderText);
    void paint (juce::Graphics&) override;
    float cornerRadius = 10.0f;
    bool shadow = true;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    AppContext& ctx;
    int size;
    juce::String key, placeholderText;
    juce::Image placeholderImage;
};

/** Format + quality pill of the player bar; click shows the Signal Path. */
class QualityBadge : public juce::Component
{
public:
    explicit QualityBadge (AppContext& c) : ctx (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void update (const juce::String& text, PathQuality q) { label = text; quality = q; repaint(); }
    const juce::String& getLabel() const noexcept { return label; }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;
private:
    AppContext& ctx;
    juce::String label;
    PathQuality quality = PathQuality::BitPerfect;
};

class SeekBar : public juce::Component
{
public:
    explicit SeekBar (AppContext& c) : ctx (c) {}
    void setPosition (double pos, double duration) { if (! dragging) { position = pos; length = duration; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void (double)> onSeek;
    bool showTimes = true;

    /** A-B loop markers (seconds, −1 = not set). Dragging a marker calls onLoopEdited (a, b). */
    void setLoop (double a, double b) { if (draggingMarker == 0) { loopA = a; loopB = b; repaint(); } }
    std::function<void (double, double)> onLoopEdited;
    void mouseMove (const juce::MouseEvent& e) override;
private:
    double xToTime (float x) const;
    float timeToX (double t) const;
    int markerAt (float x) const;      // 1 = A, 2 = B, 0 = none
    AppContext& ctx;
    double position = 0, length = 0;
    double loopA = -1.0, loopB = -1.0;
    bool dragging = false;
    int draggingMarker = 0;
};

/** The [A-B] button of the player bar: first click = A, second = B (loop on), third = off. */
class LoopButton : public juce::Button
{
public:
    explicit LoopButton (AppContext& c) : juce::Button ("A-B"), ctx (c)
    {
        setWantsKeyboardFocus (false);
        setTooltip ("Boucle A-B : 1er clic = début (I), 2e = fin et boucle (O), 3e = arrêt (L)"_t);
    }
    int stage = 0;   // 0 off, 1 A set, 2 looping
    void paintButton (juce::Graphics& g, bool hi, bool down) override;
private:
    AppContext& ctx;
};

/** Hosts the UI plug-ins registered for one slot of the interface. */
class PluginSlot : public juce::Component, private juce::Timer
{
public:
    PluginSlot (AppContext& c, juce::String slotName, bool horizontal = false);
    void paint (juce::Graphics&) override;
    void refreshPlugins();
    int getPreferredHeight() const;
    bool isEmpty() const { return ids.isEmpty(); }
private:
    void timerCallback() override { repaint(); }
    AppContext& ctx;
    juce::String slot;
    bool horizontal;
    juce::StringArray ids;
};

struct AlbumCardData
{
    AlbumInfo album;
    juce::String subtitle;
};

class AlbumGrid : public juce::Component, private juce::ChangeListener
{
public:
    explicit AlbumGrid (AppContext& c);
    ~AlbumGrid() override;
    void setAlbums (std::vector<AlbumInfo> albums);
    /** Height needed at a given width. */
    int heightForWidth (int width) const;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const AlbumInfo&)> onOpen, onPlay;
    int maxRows = 0;          // 0 = unlimited
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::Rectangle<int> cardBounds (int index) const;
    int columns() const;
    int indexAt (juce::Point<int>) const;
    AppContext& ctx;
    std::vector<AlbumInfo> albums;
    int hover = -1;
};

class TrackList : public juce::Component, private juce::ChangeListener
{
public:
    explicit TrackList (AppContext& c);
    ~TrackList() override;
    void setTracks (std::vector<TrackInfo> tracks, bool showArtwork = true, bool showAlbum = true, bool numbered = false);
    int getTotalHeight() const { return (int) tracks.size() * rowHeight + 8; }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    const std::vector<TrackInfo>& getTracks() const noexcept { return tracks; }
    static constexpr int rowHeight = 54;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void download (const std::vector<TrackInfo>& list);
    AppContext& ctx;
    std::vector<TrackInfo> tracks;
    std::vector<juce::Rectangle<int>> downloadButtons;   // per row, filled while painting (remote tracks only)
    bool artwork = true, album = true, numbered = false;
    int hover = -1, selected = -1;
};

/** Scrollable page content with a title. Children are laid out by the page. */
class ScrollPage : public juce::Component
{
public:
    explicit ScrollPage (AppContext& c);
    void resized() override;
    juce::Component& content() { return contentComp; }
    void setContentHeight (int h);
    virtual void layoutContent (int width) = 0;
    virtual void refresh() {}
    void paint (juce::Graphics&) override {}
protected:
    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component contentComp;
};

void drawPageTitle (juce::Graphics& g, const Theme& t, juce::Rectangle<int> r, const juce::String& title, const juce::String& subtitle = {});

} // namespace nx
