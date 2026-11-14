#include "storm/render/texture_cache.hpp"

#include "../shim/shim.hpp"
#include "core/cache.hpp"
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

namespace
{
texture::DerivedShape coreShape(const DerivedDataShape &shape)
{
    texture::DerivedShape core;
    std::copy(std::begin(shape.magic), std::end(shape.magic), core.magic);
    core.version = shape.version;
    core.width = shape.width;
    core.height = shape.height;
    core.levels = shape.levels;
    return core;
}
} // namespace

uint64_t DerivedDataHash(const void *data, size_t size, uint64_t seed) { return texture::hash64(data, size, seed); }

bool DerivedDataCacheEnabled() { return !texture::Service::instance().cache_directory().empty(); }

std::optional<std::vector<uint8_t>> ReadDerivedData(uint64_t key, const DerivedDataShape &shape, size_t max_size)
{
    const std::filesystem::path &directory = texture::Service::instance().cache_directory();
    if (directory.empty())
        return std::nullopt;
    std::optional<texture::Blob> blob = texture::read_derived_file(directory, key, coreShape(shape), max_size);
    if (!blob)
        return std::nullopt;
    return std::vector<uint8_t>(blob->data.get(), blob->data.get() + blob->size);
}

bool WriteDerivedData(uint64_t key, const DerivedDataShape &shape, std::span<const uint8_t> data)
{
    const std::filesystem::path &directory = texture::Service::instance().cache_directory();
    return !directory.empty() &&
           texture::write_derived_file(directory, key, coreShape(shape), data.data(), data.size()) != 0;
}

} // namespace storm::render
