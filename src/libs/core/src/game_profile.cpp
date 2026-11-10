#include "game_profile.hpp"

#include <optional>

#include <spdlog/spdlog.h>

#include "v_file_service.h"

namespace storm
{
namespace
{
std::optional<GameProfile> detected;
}

GameProfile initGameProfile()
{
    const auto root = fio->_GetExecutableDirectory();
    detected = detectGameProfile([](const std::string &path) { return fio->_FileOrDirectoryExists(path.c_str()); });
    spdlog::info("Game profile: {} (game directory {})", gameProfileName(*detected), root);
    return *detected;
}

GameProfile gameProfile()
{
    if (!detected)
        throw std::logic_error("The game profile is read before initGameProfile()");
    return *detected;
}
} // namespace storm
