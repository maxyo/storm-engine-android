// DXT texture handling on GPUs without S3TC: DXT1/3/5 textures are transcoded
// to ETC2 on worker threads and kept in an on-disk cache. Until a texture's
// ETC2 version is ready, the shim shows it uncompressed (top mip dropped for
// large textures). See src/texture/core/service.hpp.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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
    std::filesystem::path cache_directory; // fs::GetCachePath() / "TextureCache", never the game resources
    uint64_t max_bytes = 3ull << 30;       // LRU eviction above this total
};

// Call before the first texture is created. Calling again restarts the service.
void ConfigureTextureCache(const TextureCacheConfig &config);
// Joins the transcode workers and logs the summary.
void ShutdownTextureCache();

// Data derived from game files other than textures (the geometry library's far LODs), kept in the texture cache
// directory under its budget and LRU eviction. `magic` names the kind, `version` its format; `width`, `height`,
// `levels` describe the payload's shape as the kind defines it. All of them and the key must match on reading.
struct DerivedDataShape
{
    char magic[8];
    uint32_t version = 0;
    uint32_t width = 0, height = 0, levels = 0;
};
// The hash the cache keys with (64 bits, xxHash64).
uint64_t DerivedDataHash(const void *data, size_t size, uint64_t seed);
// False when the cache is not configured (no directory): derived data is computed on every use.
bool DerivedDataCacheEnabled();
// The payload or nothing: no entry, or an entry whose header, size or checksum does not match (it is deleted).
std::optional<std::vector<uint8_t>> ReadDerivedData(uint64_t key, const DerivedDataShape &shape, size_t max_size);
// Writes atomically; false when the cache is not configured or the write failed.
bool WriteDerivedData(uint64_t key, const DerivedDataShape &shape, std::span<const uint8_t> data);

} // namespace storm::render
