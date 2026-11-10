// DXT texture handling on GPUs without S3TC: DXT1/3/5 textures are transcoded
// to ETC2 on worker threads and kept in an on-disk cache. Until a texture's
// ETC2 version is ready, the shim shows it uncompressed (top mip dropped for
// large textures). See src/texture/core/service.hpp.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace storm::render
{

enum class TextureCompression : uint8_t
{
    Auto, // BC if supported, else ETC2 if supported, else bgfx decodes to RGBA8
    Bc,   // upload DXT as is
    Etc2, // transcode to ETC2 whenever the GPU supports ETC2
    Rgba, // decode DXT to RGBA8
};

// engine.ini `texture_compression = auto|bc|etc2|rgba`
std::optional<TextureCompression> ParseTextureCompression(std::string_view value);

struct TextureCacheConfig
{
    TextureCompression mode = TextureCompression::Auto;
    std::filesystem::path cache_directory; // per-user storage, never the game resources
    uint64_t max_bytes = 3ull << 30;       // LRU eviction above this total
};

// Call before the first texture is created. Calling again restarts the service.
void ConfigureTextureCache(const TextureCacheConfig &config);
// Joins the transcode workers and logs the summary.
void ShutdownTextureCache();

} // namespace storm::render
