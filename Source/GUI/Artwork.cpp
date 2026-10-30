// NX Player — MIT License
#include "Artwork.h"
#include "../Library/Library.h"
#include "../Core/ImageDecode.h"
#include <juce_cryptography/juce_cryptography.h>

namespace nx
{

namespace imaging
{
    namespace
    {
        void boxPass (std::vector<float>& src, std::vector<float>& dst, int w, int h, int ch, int r, bool horizontal)
        {
            const int len = horizontal ? w : h, lines = horizontal ? h : w;
            const float inv = 1.0f / (float) (2 * r + 1);
            for (int line = 0; line < lines; ++line)
            {
                auto idx = [&] (int i) { const int cl = juce::jlimit (0, len - 1, i); return horizontal ? (line * w + cl) * ch : (cl * w + line) * ch; };
                for (int c = 0; c < ch; ++c)
                {
                    float acc = 0.0f;
                    for (int i = -r; i <= r; ++i) acc += src[(size_t) (idx (i) + c)];
                    for (int i = 0; i < len; ++i)
                    {
                        dst[(size_t) (idx (i) + c)] = acc * inv;
                        acc += src[(size_t) (idx (i + r + 1) + c)] - src[(size_t) (idx (i - r) + c)];
                    }
                }
            }
        }
    }

    void blur (juce::Image& image, int radius)
    {
        if (! image.isValid() || radius <= 0) return;
        const int w = image.getWidth(), h = image.getHeight(), ch = 4;
        std::vector<float> a ((size_t) w * h * ch), b (a.size());
        {
            juce::Image::BitmapData bd (image, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto c = bd.getPixelColour (x, y);
                    float* p = &a[(size_t) ((y * w + x) * ch)];
                    p[0] = c.getFloatRed(); p[1] = c.getFloatGreen(); p[2] = c.getFloatBlue(); p[3] = c.getFloatAlpha();
                }
        }
        const int r = juce::jmax (1, radius / 2);
        for (int pass = 0; pass < 3; ++pass)
        {
            boxPass (a, b, w, h, ch, r, true);
            boxPass (b, a, w, h, ch, r, false);
        }
        juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float* p = &a[(size_t) ((y * w + x) * ch)];
                bd.setPixelColour (x, y, juce::Colour::fromFloatRGBA (p[0], p[1], p[2], image.hasAlphaChannel() ? p[3] : 1.0f));
            }
    }

    void adjustSaturation (juce::Image& image, float amount)
    {
        if (! image.isValid() || std::abs (amount - 1.0f) < 0.01f) return;
        juce::Image::BitmapData bd (image, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height; ++y)
            for (int x = 0; x < bd.width; ++x)
            {
                const auto c = bd.getPixelColour (x, y);
                bd.setPixelColour (x, y, c.withSaturation (juce::jlimit (0.0f, 1.0f, c.getSaturation() * amount)));
            }
    }

    juce::Colour dominantColour (const juce::Image& image)
    {
        if (! image.isValid()) return juce::Colours::grey;
        auto small = image.rescaled (24, 24, juce::Graphics::lowResamplingQuality);
        std::vector<juce::Colour> px;
        juce::Image::BitmapData bd (small, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < bd.height; ++y) for (int x = 0; x < bd.width; ++x) px.push_back (bd.getPixelColour (x, y));
        std::sort (px.begin(), px.end(), [] (juce::Colour a, juce::Colour b) { return a.getSaturation() * a.getBrightness() > b.getSaturation() * b.getBrightness(); });
        float r = 0, g = 0, bl = 0;
        const size_t n = std::max<size_t> (1, px.size() / 4);
        for (size_t i = 0; i < n; ++i) { r += px[i].getFloatRed(); g += px[i].getFloatGreen(); bl += px[i].getFloatBlue(); }
        return juce::Colour::fromFloatRGBA (r / (float) n, g / (float) n, bl / (float) n, 1.0f);
    }

    juce::Image coverFit (const juce::Image& src, int w, int h)
    {
        if (! src.isValid() || w <= 0 || h <= 0) return {};
        juce::Image out (juce::Image::ARGB, w, h, true);
        juce::Graphics g (out);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const float scale = juce::jmax ((float) w / (float) src.getWidth(), (float) h / (float) src.getHeight());
        const float dw = (float) src.getWidth() * scale, dh = (float) src.getHeight() * scale;
        g.drawImage (src, juce::Rectangle<float> (((float) w - dw) * 0.5f, ((float) h - dh) * 0.5f, dw, dh));
        return out;
    }

    juce::Image placeholder (const juce::String& text, int size, juce::Colour a, juce::Colour b)
    {
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        const auto h = (juce::uint32) text.hashCode();
        const float hue = (float) (h % 360) / 360.0f;
        const auto c1 = a.withRotatedHue (hue * 0.25f).darker (0.6f), c2 = b.withRotatedHue (-hue * 0.25f).darker (1.4f);
        g.setGradientFill (juce::ColourGradient (c1, 0, 0, c2, (float) size, (float) size, false));
        g.fillAll();
        juce::String initials;
        for (auto& w : juce::StringArray::fromTokens (text, " ", {}))
            if (w.isNotEmpty() && initials.length() < 2 && juce::CharacterFunctions::isLetterOrDigit (w[0])) initials << w.substring (0, 1).toUpperCase();
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions ((float) size * 0.34f, juce::Font::bold));
        g.drawText (initials.isEmpty() ? juce::String ("NX") : initials, img.getBounds(), juce::Justification::centred);
        return img;
    }
}

//==============================================================================
ArtworkCache::ArtworkCache (Library& l, juce::File folder) : library (l), diskCache (std::move (folder))
{
    diskCache.createDirectory();
}

ArtworkCache::~ArtworkCache() { pool.removeAllJobs (true, 4000); }

juce::Image ArtworkCache::loadOriginal (const juce::String& key)
{
    if (key.isEmpty()) return {};
    if (key.startsWith ("http"))
    {
        auto file = diskCache.getChildFile (juce::SHA256 (key.toUTF8()).toHexString().substring (0, 32) + ".img");
        if (! file.existsAsFile())
        {
            juce::MemoryBlock mb;
            if (! juce::URL (key).readEntireBinaryStream (mb) || mb.getSize() < 64) return {};
            file.replaceWithData (mb.getData(), mb.getSize());
        }
        return imagedec::decode (file);
    }
    const auto f = library.getArtworkFile (key);
    return imagedec::decode (f);
}

juce::Image ArtworkCache::loadNow (const juce::String& key, int size)
{
    const auto id = key + "@" + juce::String (size);
    {
        std::lock_guard<std::mutex> l (lock);
        auto it = images.find (id);
        if (it != images.end()) return it->second;
    }
    auto img = imaging::coverFit (loadOriginal (key), size, size);
    std::lock_guard<std::mutex> l (lock);
    images[id] = img;
    lru.push_back (id);
    while (lru.size() > 600) { images.erase (lru.front()); lru.pop_front(); }
    return img;
}

juce::Image ArtworkCache::get (const juce::String& key, int size)
{
    if (key.isEmpty()) return {};
    const auto id = key + "@" + juce::String (size);
    {
        std::lock_guard<std::mutex> l (lock);
        auto it = images.find (id);
        if (it != images.end()) return it->second;
        if (pending[id]) return {};
        pending[id] = true;
    }
    pool.addJob ([this, key, size, id]
    {
        loadNow (key, size);
        {
            std::lock_guard<std::mutex> l (lock);
            pending.erase (id);
        }
        sendChangeMessage();
    });
    return {};
}

} // namespace nx
