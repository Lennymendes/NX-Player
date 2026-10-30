// NX Player — MIT License
#include "ImageDecode.h"
#include <webp/decode.h>
#include <stb_image.h>

namespace nx::imagedec
{

namespace
{
    constexpr int maxSide = 16384;

    juce::Image fromRgb (const uint8_t* rgb, int w, int h, int channels)
    {
        if (rgb == nullptr || w <= 0 || h <= 0 || w > maxSide || h > maxSide) return {};
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* s = rgb + (size_t) y * (size_t) w * (size_t) channels;
            for (int x = 0; x < w; ++x, s += channels)
                bd.setPixelColour (x, y, juce::Colour (s[0], s[1], s[2]));
        }
        return img;
    }
}

bool isStandard (const void* data, size_t size)
{
    return juce::ImageFileFormat::loadFrom (data, size).isValid();
}

juce::Image decode (const void* data, size_t size)
{
    if (data == nullptr || size < 16) return {};
    if (auto img = juce::ImageFileFormat::loadFrom (data, size); img.isValid()) return img;

    const auto* p = static_cast<const uint8_t*> (data);
    if (size > 12 && std::memcmp (p, "RIFF", 4) == 0 && std::memcmp (p + 8, "WEBP", 4) == 0)
    {
        int w = 0, h = 0;
        if (WebPGetInfo (p, size, &w, &h) && w <= maxSide && h <= maxSide)
        {
            if (auto* rgb = WebPDecodeRGB (p, size, &w, &h))
            {
                auto img = fromRgb (rgb, w, h, 3);
                WebPFree (rgb);
                return img;
            }
        }
        return {};
    }

    int w = 0, h = 0, n = 0;
    // 3 requested channels: stb converts CMYK / YCCK JPEG, 16-bit PNG and palettes to plain 8-bit RGB
    if (auto* rgb = stbi_load_from_memory (p, (int) size, &w, &h, &n, 3))
    {
        auto img = fromRgb (rgb, w, h, 3);
        stbi_image_free (rgb);
        return img;
    }
    return {};
}

juce::Image decode (const juce::File& file)
{
    juce::MemoryBlock mb;
    return file.existsAsFile() && file.getSize() < 64 * 1024 * 1024 && file.loadFileAsData (mb) ? decode (mb.getData(), mb.getSize()) : juce::Image();
}

juce::MemoryBlock toStandardJpeg (const juce::Image& image)
{
    juce::MemoryBlock out;
    if (! image.isValid()) return out;
    juce::MemoryOutputStream stream (out, false);
    juce::JPEGImageFormat fmt;
    fmt.setQuality (0.92f);
    if (! fmt.writeImageToStream (image, stream)) out.reset();
    return out;
}

} // namespace nx::imagedec
