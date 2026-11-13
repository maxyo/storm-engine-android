#pragma once

#include <cstdint>
#include <vector>

namespace storm::render
{
// The frame rate the game window is paced at, frames per second (Android: Swappy and the ADPF target, the
// settings' «Частота кадров»; src/shim/frame_pacing.hpp). Applies at once and is logged; before the device
// exists it is the rate the device starts with. No effect where the renderer does not pace (desktop).
void SetFrameRate(uint32_t fps);
// While on, the intervals between the window's presents (ms) are kept until TakePresentIntervals takes them (the
// automatic graphics preset measures with them); off drops them. Nothing is kept where the renderer does not pace.
void SamplePresentIntervals(bool on);
std::vector<float> TakePresentIntervals();
} // namespace storm::render
