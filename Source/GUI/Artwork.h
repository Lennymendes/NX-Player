// NX Player — MIT License
// Artwork loading (library cache files and http URLs), and the image processing behind the
// Cider-style look: fast blur (3 box passes ≈ Gaussian), saturation, dominant colour.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <list>
#include <map>
#include <mutex>

namespace nx
{

class Library;

namespace imaging
{
    /** In-place blur (three box passes), radius in pixels. Works on ARGB and RGB images. */
    void blur (juce::Image& image, int radius);
    void adjustSaturation (juce::Image& image, float amount);
    /** Average colour of the most saturated quarter of the pixels (accent extraction). */
    juce::Colour dominantColour (const juce::Image& image);
    /** Square crop scaled to size (cover fit). */
    juce::Image coverFit (const juce::Image& src, int w, int h);
    /** Placeholder artwork: gradient from a hash of the text + initials. */
    juce::Image placeholder (const juce::String& text, int size, juce::Colour a, juce::Colour b);
}

class ArtworkCache : public juce::ChangeBroadcaster
{
public:
    ArtworkCache (Library& library, juce::File diskCacheFolder);
    ~ArtworkCache() override;

    /** Returns the image if ready, else an invalid image and schedules the load (change message when done). */
    juce::Image get (const juce::String& key, int size);
    /** Blocking load (used by the background renderer thread and tests). */
    juce::Image loadNow (const juce::String& key, int size);

private:
    juce::Image loadOriginal (const juce::String& key);

    Library& library;
    juce::File diskCache;
    juce::ThreadPool pool { 2 };
    std::mutex lock;
    std::map<juce::String, juce::Image> images;
    std::list<juce::String> lru;
    std::map<juce::String, bool> pending;
};

} // namespace nx
