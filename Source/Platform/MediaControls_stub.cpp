// NX Player — MIT License
// Platforms without system media controls in this build (Linux; MPRIS is not implemented yet).
#include "MediaControls.h"

#if ! defined (__APPLE__) && ! defined (_WIN32)
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
const char* MediaControls::compiledBackend() { return "aucune (Linux)"; }
} // namespace nx
#endif
