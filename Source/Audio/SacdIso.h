// NX Player — MIT License
// SACD disc images (.iso, Scarlet Book): Master TOC (sector 510), master text (511), Area TOCs (2-channel
// "TWOCHTOC" and multichannel "MULCHTOC") with their track lists (SACDTRL1 / SACDTRL2) and track texts
// (SACDTTxt). Each track is exposed as a virtual path "<file>.iso#area=<0|1>&track=<n>" (area 0 = 2ch,
// area 1 = multichannel, tracks from 1) and played as raw DSD64: the audio sectors are demultiplexed (packet
// headers removed) into the MSB-first, byte-interleaved stream the DSF / DFF decoders also deliver.
// DST-compressed areas are listed but not decoded: opening one returns a clear error.
#pragma once

#include "AudioDecoder.h"
#include <vector>

namespace nx::sacd
{

constexpr int sectorSize = 2048;
constexpr int masterTocSector = 510, masterTextSector = 511;
constexpr int bytesPerFrame = 4704;                 // per channel and per 1/75 s at DSD64 (2 822 400 / 8 / 75)

struct Track
{
    int number = 0;                                  // 1..n inside its area
    juce::String title, performer;
    juce::uint32 startSector = 0, sectorCount = 0;   // SACDTRL1
    juce::int64 startFrame = -1, frameCount = 0;     // SACDTRL2, in 1/75 s (-1: no time list)
    double durationSeconds() const { return (double) frameCount / 75.0; }
};

struct Area
{
    int index = 0;                                   // 0 = 2-channel area, 1 = multichannel area
    int channels = 2;
    int frameFormat = 2;                             // 0 = DST, 2 = DSD 3-in-14, 3 = DSD 3-in-16
    int fsCode = 4;                                  // 4 = 64 x 44,1 kHz
    bool isDst() const { return frameFormat == 0; }
    juce::String description;
    std::vector<Track> tracks;
};

struct Disc
{
    juce::String albumTitle, albumArtist, publisher, copyright, discTitle, discArtist, catalog;
    int year = 0;
    std::vector<Area> areas;
};

/** Reads the TOCs; false (with the reason) when the file is not a SACD image. */
bool readDisc (juce::InputStream& in, Disc& disc, juce::String& error);
bool readDisc (const juce::File& iso, Disc& disc, juce::String& error);

juce::String virtualUri (const juce::File& iso, int area, int track);
bool isVirtualUri (const juce::String& uri);
bool parseVirtualUri (const juce::String& uri, juce::File& iso, int& area, int& track);
/** The file on disk behind a path (the .iso for a virtual track, the path itself otherwise). */
juce::File realFile (const juce::String& pathOrUri);

/** Decoder for one track of the image (DSD64, raw). Null + error for DST areas or a bad track. */
std::unique_ptr<AudioDecoder> openTrack (std::unique_ptr<juce::InputStream> in, int area, int track, juce::String& error);
std::unique_ptr<AudioDecoder> openTrack (const juce::String& uri, juce::String& error);

} // namespace nx::sacd
