// NX Player — MIT License
#include "Text.h"
#include "NXPlayerData.h"
#include <unordered_map>

namespace nx::i18n
{

namespace
{
    std::atomic<bool> english { false };

    struct Dictionary
    {
        std::unordered_map<juce::String, juce::String> map;
        Dictionary()
        {
            const auto json = juce::JSON::parse (juce::String::fromUTF8 (NXData::en_json, NXData::en_jsonSize));
            if (auto* o = json.getDynamicObject())
                for (auto& p : o->getProperties())
                    if (! p.name.toString().startsWith ("_")) map[p.name.toString()] = p.value.toString();
        }
    };

    const Dictionary& dictionary()
    {
        static const Dictionary d;   // thread-safe initialisation
        return d;
    }
}

void setLanguage (const juce::String& code)
{
    english = code.startsWithIgnoreCase ("en");
    if (english) dictionary();
}

juce::String getLanguage() { return english.load() ? "en" : "fr"; }
bool isEnglish() noexcept { return english.load (std::memory_order_relaxed); }
int dictionarySize() { return (int) dictionary().map.size(); }

juce::String translate (const juce::String& french)
{
    if (! english.load (std::memory_order_relaxed)) return french;
    const auto& m = dictionary().map;
    auto it = m.find (french);
    return it != m.end() ? it->second : french;
}

juce::StringArray missingTranslations (const juce::StringArray& texts)
{
    juce::StringArray out;
    for (auto& t : texts) if (dictionary().map.count (t) == 0) out.add (t);
    return out;
}

} // namespace nx::i18n
