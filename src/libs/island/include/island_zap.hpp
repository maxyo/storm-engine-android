#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace storm::island
{
// Where the zipped map of a game map file ("resource/foam/<dir>/<name>.tga") is made when the game does not ship it:
// under the cache root, never beside the game file (the game folder is never written).
inline std::filesystem::path ZapCachePath(const std::filesystem::path &cache_root, const std::string &game_file)
{
    const std::filesystem::path relative = std::filesystem::path(game_file).lexically_normal();
    if (relative.is_absolute() || relative.empty() || *relative.begin() == "..")
        throw std::invalid_argument("island zap: not a path inside the game folder: " + game_file);
    return cache_root / "Island" / (relative.string() + ".zap");
}
} // namespace storm::island
