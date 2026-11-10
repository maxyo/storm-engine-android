#pragma once

// The script base version the engine accepts: the iScriptVersion global of the game's scripts,
// checked after the program starts (a mismatch is a hard error) and returned by
// GetEngineVersion(). Each supported game ships its own value.

#include "game_profile.hpp"

#include <cstdint>

namespace storm
{
constexpr int32_t engineScriptVersion(const GameProfile profile)
{
    return profile == GameProfile::CaribbeanLegend ? 2230981 : 54128;
}
} // namespace storm
