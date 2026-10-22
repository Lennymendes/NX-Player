// NX Player — MIT License
// Tag readers: ID3v2 (MP3, DSF, DFF, WAV, AIFF), FLAC Vorbis comments + PICTURE, WAV LIST/INFO,
// MP4 / M4A ilst. Keys are normalised to lower case: title, artist, albumartist, album, genre,
// composer, year, tracknumber, tracktotal, discnumber, lyrics, isrc, label, conductor, credits...
#pragma once

#include <juce_core/juce_core.h>

namespace nx
{

struct TagData
{
    juce::StringPairArray fields;
    juce::StringArray credits;          // "Role : Name" entries (TIPL / TMCL / PERFORMER=...)
    juce::MemoryBlock picture;
    juce::String pictureMime;

    juce::String get (const juce::String& key) const { return fields.getValue (key, {}); }
    void set (const juce::String& key, const juce::String& value)
    {
        const auto v = value.trim();
        if (v.isNotEmpty() && fields.getValue (key, {}).isEmpty()) fields.set (key, v);
    }
    int getInt (const juce::String& key) const { return get (key).upToFirstOccurrenceOf ("/", false, false).trim().getIntValue(); }
};

/** Reads whatever tags the file carries. Returns false only when the file cannot be opened. */
bool readTags (const juce::File& file, TagData& out);
bool readTags (juce::InputStream& in, const juce::String& fileNameHint, TagData& out);

/** Exposed for tests. */
void parseId3v2 (const uint8_t* data, size_t size, TagData& out);
void parseVorbisComment (const uint8_t* data, size_t size, TagData& out);

} // namespace nx
