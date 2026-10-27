// NX Player — MIT License
// UTF-8 literals for juce::String: "Réglages"_t. (juce::String (const char*) expects ASCII.)
// The interface is written in French; "…"_t also translates the literal when the language is English
// (dictionary Resources/i18n/en.json, key = the French text). Strings without a translation stay French.
#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

namespace nx::i18n
{
/** "fr" (default) or "en". */
void setLanguage (const juce::String& code);
juce::String getLanguage();
bool isEnglish() noexcept;
/** French text -> current language (identity in French). */
juce::String translate (const juce::String& french);
/** French texts of the dictionary that have no English entry yet (tests / tools). */
juce::StringArray missingTranslations (const juce::StringArray& frenchTexts);
int dictionarySize();
}

namespace nx
{
inline juce::String operator""_t (const char* s, size_t n)
{
    auto str = juce::String::fromUTF8 (s, (int) n);
    return i18n::isEnglish() ? i18n::translate (str) : str;
}
/** For text built at run time (labels stored in tables...). */
inline juce::String tr (const juce::String& french) { return i18n::isEnglish() ? i18n::translate (french) : french; }
}
using nx::operator""_t;
