#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace storm::island
{
// Where what the engine makes for a game map file ("resource/foam/<dir>/<name>.tga") is kept when the game does not
// ship it: under the cache root, never beside the game file (the game folder is never written). `extension`: ".zap" (the
// zipped map) or ".box" (the box it was made for).
inline std::filesystem::path IslandCachePath(const std::filesystem::path &cache_root, const std::string &game_file,
                                             const char *extension)
{
    const std::filesystem::path relative = std::filesystem::path(game_file).lexically_normal();
    if (relative.is_absolute() || relative.empty() || *relative.begin() == "..")
        throw std::invalid_argument("island cache: not a path inside the game folder: " + game_file);
    return cache_root / "Island" / (relative.string() + extension);
}
} // namespace storm::island
