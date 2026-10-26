// NX Player — MIT License
// Windows: media keys, the volume flyout and the lock screen through SystemMediaTransportControls (C++/WinRT),
// attached to the main window with ISystemMediaTransportControlsInterop. Built with MSVC only.
#include "../MediaControls.h"
#include <juce_events/juce_events.h>

#if __has_include (<winrt/Windows.Media.h>)

#include <windows.h>
#include <systemmediatransportcontrolsinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#pragma comment (lib, "windowsapp")

namespace nx
{
namespace
{
    using namespace winrt::Windows::Media;
    using namespace winrt::Windows::Storage::Streams;

    winrt::hstring hs (const juce::String& s) { return winrt::hstring (s.toWideCharPointer()); }

    winrt::Windows::Foundation::TimeSpan span (double seconds)
    {
        return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan> (std::chrono::duration<double> (juce::jmax (0.0, seconds)));
    }

    class WinMediaControls final : public MediaControls
    {
    public:
        WinMediaControls (Callbacks cb, HWND window) : callbacks (std::move (cb))
        {
            auto interop = winrt::get_activation_factory<SystemMediaTransportControls, ISystemMediaTransportControlsInterop>();
            winrt::check_hresult (interop->GetForWindow (window, winrt::guid_of<SystemMediaTransportControls>(), winrt::put_abi (smtc)));
            smtc.IsEnabled (true);
            smtc.IsPlayEnabled (true);
            smtc.IsPauseEnabled (true);
            smtc.IsNextEnabled (true);
            smtc.IsPreviousEnabled (true);
            smtc.IsStopEnabled (false);

            std::weak_ptr<int> weak = token;
            auto calls = callbacks;
            buttonToken = smtc.ButtonPressed ([weak, calls] (SystemMediaTransportControls const&, SystemMediaTransportControlsButtonPressedEventArgs const& args)
            {
                std::function<void()> fn;
                switch (args.Button())
                {
                    case SystemMediaTransportControlsButton::Play:     fn = calls.play; break;
                    case SystemMediaTransportControlsButton::Pause:    fn = calls.pause; break;
                    case SystemMediaTransportControlsButton::Next:     fn = calls.next; break;
                    case SystemMediaTransportControlsButton::Previous: fn = calls.previous; break;
                    default: break;
                }
                if (fn) juce::MessageManager::callAsync ([weak, fn] { if (! weak.expired()) fn(); });
            });
            seekToken = smtc.PlaybackPositionChangeRequested ([weak, calls] (SystemMediaTransportControls const&, PlaybackPositionChangeRequestedEventArgs const& args)
            {
                const double t = std::chrono::duration<double> (args.RequestedPlaybackPosition()).count();
                if (calls.seek) juce::MessageManager::callAsync ([weak, calls, t] { if (! weak.expired()) calls.seek (t); });
            });
        }

        ~WinMediaControls() override
        {
            try
            {
                smtc.ButtonPressed (buttonToken);
                smtc.PlaybackPositionChangeRequested (seekToken);
                smtc.DisplayUpdater().ClearAll();
                smtc.IsEnabled (false);
            }
            catch (...) {}
        }

        void update (const NowPlayingInfo& now) override
        {
            try
            {
                smtc.PlaybackStatus (! now.hasTrack ? MediaPlaybackStatus::Stopped
                                                    : now.playing ? MediaPlaybackStatus::Playing : MediaPlaybackStatus::Paused);
                auto du = smtc.DisplayUpdater();
                du.Type (MediaPlaybackType::Music);
                auto music = du.MusicProperties();
                music.Title (hs (now.title));
                music.Artist (hs (now.artist));
                music.AlbumTitle (hs (now.album));

                if (now.artworkId != artworkId)
                {
                    artworkId = now.artworkId;
                    if (now.artworkPng.getSize() > 0)
                    {
                        // Written asynchronously (blocking on the UI thread's STA is not allowed), then attached.
                        InMemoryRandomAccessStream stream;
                        DataWriter writer (stream);
                        writer.WriteBytes (winrt::array_view<const uint8_t> ((const uint8_t*) now.artworkPng.getData(),
                                                                             (const uint8_t*) now.artworkPng.getData() + now.artworkPng.getSize()));
                        auto updater = du;
                        writer.StoreAsync().Completed ([stream, writer, updater] (auto const&, auto) mutable
                        {
                            try
                            {
                                writer.DetachStream();
                                stream.Seek (0);
                                updater.Thumbnail (RandomAccessStreamReference::CreateFromStream (stream));
                                updater.Update();
                            }
                            catch (...) {}
                        });
                    }
                    else du.Thumbnail (nullptr);
                }
                du.Update();

                SystemMediaTransportControlsTimelineProperties timeline;
                timeline.StartTime (span (0.0));
                timeline.MinSeekTime (span (0.0));
                timeline.EndTime (span (now.duration));
                timeline.MaxSeekTime (span (now.duration));
                timeline.Position (span (now.position));
                smtc.UpdateTimelineProperties (timeline);
            }
            catch (...) {}
        }

        juce::String getName() const override { return "SystemMediaTransportControls"; }

    private:
        Callbacks callbacks;
        SystemMediaTransportControls smtc { nullptr };
        winrt::event_token buttonToken, seekToken;
        juce::String artworkId;
        std::shared_ptr<int> token = std::make_shared<int> (0);
    };

    struct NoMediaControls final : MediaControls
    {
        void update (const NowPlayingInfo&) override {}
        juce::String getName() const override { return "aucune"; }
    };
}

std::unique_ptr<MediaControls> MediaControls::create (Callbacks callbacks, void* nativeWindow)
{
    if (nativeWindow != nullptr)
    {
        try { return std::make_unique<WinMediaControls> (std::move (callbacks), (HWND) nativeWindow); }
        catch (...) {}
    }
    return std::make_unique<NoMediaControls>();
}

} // namespace nx

#else

namespace nx
{
namespace
{
    struct NoMediaControls final : MediaControls
    {
        void update (const NowPlayingInfo&) override {}
        juce::String getName() const override { return "aucune"; }
    };
}
std::unique_ptr<MediaControls> MediaControls::create (Callbacks, void*) { return std::make_unique<NoMediaControls>(); }
} // namespace nx

#endif
