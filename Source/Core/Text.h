// NX Player — MIT License
// UTF-8 literals for juce::String: "Réglages"_t. (juce::String (const char*) expects ASCII.)
#pragma once

#include <juce_core/juce_core.h>

namespace nx
{
inline juce::String operator""_t (const char* s, size_t n) { return juce::String::fromUTF8 (s, (int) n); }
}
using nx::operator""_t;
