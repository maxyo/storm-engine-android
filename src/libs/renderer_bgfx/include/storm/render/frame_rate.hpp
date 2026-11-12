#pragma once

#include <cstdint>

namespace storm::render
{
// The frame rate the game window is paced at, frames per second (Android: Swappy and the ADPF target, the
// settings' «Частота кадров»; src/shim/frame_pacing.hpp). Applies at once and is logged; before the device
// exists it is the rate the device starts with. No effect where the renderer does not pace (desktop).
void SetFrameRate(uint32_t fps);
} // namespace storm::render
