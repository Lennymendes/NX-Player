// NX Player — MIT License
// Cover art decoding that never gives up: JUCE's decoders first, then libwebp (WebP) and stb_image
// (progressive / CMYK JPEG, 16-bit PNG...), always converted to a standard 8-bit RGB image.
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace nx::imagedec
{
juce::Image decode (const void* data, size_t size);
juce::Image decode (const juce::File& file);
/** True when JUCE alone reads these bytes (the stored file can be served as is). */
bool isStandard (const void* data, size_t size);
/** The image re-encoded as a baseline 8-bit RGB JPEG (empty on failure). */
juce::MemoryBlock toStandardJpeg (const juce::Image& image);
}
