// NX Player — MIT License
// System media controls: the keyboard's media keys (play / pause, next, previous) even when NX Player is in
// the background, and the "Now Playing" metadata shown by the system.
//   macOS   : MPRemoteCommandCenter + MPNowPlayingInfoCenter (MediaControls_mac.mm)
//   Windows : SystemMediaTransportControls through ISystemMediaTransportControlsInterop (win/MediaControls_win.cpp)
//   other   : no-op
#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace nx
{

struct NowPlayingInfo
{
    juce::String title, artist, album;
    double duration = 0.0, position = 0.0;
    bool playing = false, hasTrack = false;
    juce::String artworkId;          // changes when the artwork changes (avoids re-sending the image)
    juce::MemoryBlock artworkPng;    // empty = no artwork (yet)
};

class MediaControls
{
public:
    struct Callbacks
    {
        std::function<void()> play, pause, toggle, next, previous;
        std::function<void (double seconds)> seek;
    };

    virtual ~MediaControls() = default;
    virtual void update (const NowPlayingInfo& info) = 0;
    virtual juce::String getName() const = 0;

    /** Callbacks are called on the message thread. nativeWindow: the main window's handle (HWND on Windows). */
    static std::unique_ptr<MediaControls> create (Callbacks callbacks, void* nativeWindow);
};

} // namespace nx
