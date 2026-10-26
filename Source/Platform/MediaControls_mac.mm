// NX Player — MIT License
// macOS: media keys / Control Centre / Touch Bar through MPRemoteCommandCenter, metadata through
// MPNowPlayingInfoCenter. Compiled with ARC (see CMakeLists.txt).
#include "MediaControls.h"
#include <juce_events/juce_events.h>

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

#include <vector>

namespace nx
{
namespace
{
    NSString* ns (const juce::String& s) { return [NSString stringWithUTF8String: s.toRawUTF8()]; }

    class MacMediaControls final : public MediaControls
    {
    public:
        explicit MacMediaControls (Callbacks cb) : callbacks (std::move (cb))
        {
            MPRemoteCommandCenter* centre = [MPRemoteCommandCenter sharedCommandCenter];
            std::weak_ptr<int> weak = token;

            auto hook = [this, weak] (MPRemoteCommand* command, std::function<void()> fn)
            {
                command.enabled = YES;
                id target = [command addTargetWithHandler: ^MPRemoteCommandHandlerStatus (MPRemoteCommandEvent*)
                {
                    juce::MessageManager::callAsync ([weak, fn] { if (! weak.expired() && fn) fn(); });
                    return MPRemoteCommandHandlerStatusSuccess;
                }];
                targets.push_back ({ command, target });
            };
            hook (centre.playCommand, callbacks.play);
            hook (centre.pauseCommand, callbacks.pause);
            hook (centre.togglePlayPauseCommand, callbacks.toggle);
            hook (centre.nextTrackCommand, callbacks.next);
            hook (centre.previousTrackCommand, callbacks.previous);

            auto seek = callbacks.seek;
            MPRemoteCommand* position = centre.changePlaybackPositionCommand;
            position.enabled = YES;
            id target = [position addTargetWithHandler: ^MPRemoteCommandHandlerStatus (MPRemoteCommandEvent* event)
            {
                const double t = ((MPChangePlaybackPositionCommandEvent*) event).positionTime;
                juce::MessageManager::callAsync ([weak, seek, t] { if (! weak.expired() && seek) seek (t); });
                return MPRemoteCommandHandlerStatusSuccess;
            }];
            targets.push_back ({ position, target });

            for (MPRemoteCommand* unused in @[ centre.skipForwardCommand, centre.skipBackwardCommand, centre.seekForwardCommand,
                                               centre.seekBackwardCommand, centre.ratingCommand, centre.likeCommand, centre.dislikeCommand ])
                unused.enabled = NO;
        }

        ~MacMediaControls() override
        {
            for (auto& t : targets) [t.command removeTarget: t.target];
            targets.clear();
            MPNowPlayingInfoCenter* info = [MPNowPlayingInfoCenter defaultCenter];
            info.nowPlayingInfo = nil;
            info.playbackState = MPNowPlayingPlaybackStateStopped;
        }

        void update (const NowPlayingInfo& now) override
        {
            MPNowPlayingInfoCenter* centre = [MPNowPlayingInfoCenter defaultCenter];
            if (! now.hasTrack)
            {
                centre.nowPlayingInfo = nil;
                centre.playbackState = MPNowPlayingPlaybackStateStopped;
                return;
            }
            NSMutableDictionary* info = [NSMutableDictionary dictionary];
            info[MPMediaItemPropertyTitle] = ns (now.title);
            info[MPMediaItemPropertyArtist] = ns (now.artist);
            info[MPMediaItemPropertyAlbumTitle] = ns (now.album);
            info[MPMediaItemPropertyPlaybackDuration] = @(now.duration);
            info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(now.position);
            info[MPNowPlayingInfoPropertyPlaybackRate] = @(now.playing ? 1.0 : 0.0);
            info[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);

            if (now.artworkId != artworkId)
            {
                artworkId = now.artworkId;
                artwork = nil;
                if (now.artworkPng.getSize() > 0)
                {
                    NSData* data = [NSData dataWithBytes: now.artworkPng.getData() length: now.artworkPng.getSize()];
                    NSImage* image = [[NSImage alloc] initWithData: data];
                    if (image != nil)
                        artwork = [[MPMediaItemArtwork alloc] initWithBoundsSize: image.size
                                                                  requestHandler: ^NSImage* (CGSize) { return image; }];
                }
            }
            if (artwork != nil) info[MPMediaItemPropertyArtwork] = artwork;

            centre.nowPlayingInfo = info;
            centre.playbackState = now.playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
        }

        juce::String getName() const override { return "MPRemoteCommandCenter"; }

    private:
        struct Target { MPRemoteCommand* command; id target; };
        Callbacks callbacks;
        std::vector<Target> targets;
        juce::String artworkId;
        MPMediaItemArtwork* artwork = nil;
        std::shared_ptr<int> token = std::make_shared<int> (0);
    };
}

std::unique_ptr<MediaControls> MediaControls::create (Callbacks callbacks, void*)
{
    return std::make_unique<MacMediaControls> (std::move (callbacks));
}

const char* MediaControls::compiledBackend() { return "MPRemoteCommandCenter + MPNowPlayingInfoCenter"; }
} // namespace nx
