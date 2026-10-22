// NX Player — MIT License
#pragma once

#include "OutputBackend.h"

namespace nx
{
#if JUCE_WINDOWS
/** ASIO (PCM + DSD natif, when built with NX_ASIO_SDK_DIR) then WASAPI exclusive (integer formats). */
std::vector<std::unique_ptr<OutputBackend>> createWindowsBackends();
#endif
}
