// Time for what is only there for the eye (a flame's flicker): the real frame time, so a time scale does not speed it
// up, and none while the game is paused (time scale 0, game delta 0), so the frozen scene stays still.
#pragma once

#include <cstdint>

namespace storm
{

// `game_ms`: the scaled frame delta (core.GetDeltaTime()); `real_ms`: the unscaled one (core.GetRDeltaTime()).
constexpr float CosmeticDeltaSeconds(uint32_t game_ms, uint32_t real_ms)
{
    return game_ms == 0 ? 0.0f : static_cast<float>(real_ms) * 0.001f;
}

} // namespace storm
