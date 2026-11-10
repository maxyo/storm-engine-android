#include "storm/render/texture_cache.hpp"

#include "../shim/shim.hpp"
#include "core/service.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace storm::render
{

std::optional<TextureCompression> ParseTextureCompression(std::string_view value)
{
    std::string lower(value);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (lower == "auto")
        return TextureCompression::Auto;
    if (lower == "bc")
        return TextureCompression::Bc;
    if (lower == "etc2")
        return TextureCompression::Etc2;
    if (lower == "rgba")
        return TextureCompression::Rgba;
    return std::nullopt;
}

void ConfigureTextureCache(const TextureCacheConfig &config)
{
    texture::Config core;
    core.mode = static_cast<texture::Mode>(config.mode);
    core.directory = config.cache_directory;
    core.max_bytes = config.max_bytes;
    texture::Service::instance().configure(core, &shim::logf);
}

void ShutdownTextureCache() { texture::Service::instance().shutdown(); }

} // namespace storm::render
