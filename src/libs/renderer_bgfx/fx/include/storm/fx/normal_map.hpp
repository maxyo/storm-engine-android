// Normal maps derived on the device from the game's grayscale height ("bump")
// textures, and the explicit rule that decides which textures are height maps.
//
// The game uses those textures as a 2x multiply detail layer on the second
// texture stage (GEOM_SERVICE_R::SetMaterial, texture[1], second UV set). The
// derived map adds lighting relief on the same coordinates. Derived textures
// come from the player's files: cache them on the device, never ship them.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::fx
{

struct NormalMapOptions
{
    float strength = 2.0f; // height of a full black-to-white step, in texels of the top mip
    bool wrap = true;      // texture tiles: filters wrap around the edges
    bool pre_blur = true;  // one 3x3 binomial pass first: removes DXT block noise from the height

    bool operator==(const NormalMapOptions &) const = default;
};

struct NormalMap
{
    int width = 0;
    int height = 0;
    int mip_count = 0;
    // RG8, all mips, top first, tightly packed: x and y of the unit tangent-space normal as 0.5 + 0.5 * n.
    // z = sqrt(1 - x² - y²) is rebuilt in the shader. +x follows +u, +y follows +v, z points out of the surface.
    std::vector<uint8_t> rg;
};

// Bump this when the derivation changes, and put NormalMapCacheKey() into the cache key of the device cache.
inline constexpr uint32_t kNormalMapVersion = 1;
uint64_t NormalMapCacheKey(const NormalMapOptions &options);

// `height`: width * height values, row by row (any one colour channel of the bump texture). Deterministic.
// Every mip is derived from the height field downsampled to that size (Scharr filter), so distant surfaces
// flatten correctly instead of keeping a noisy, shortened normal.
NormalMap DeriveNormalMap(std::span<const uint8_t> height, int width, int height_px, const NormalMapOptions &options = {});

// ---- which textures are height maps ----

enum class HeightMapReason
{
    ManifestInclude,  // listed with '+' in the manifest
    ManifestExclude,  // listed with '-' in the manifest
    NameAndGrayscale, // name rule matched and the pixels are grey: a height map
    NameNoMatch,      // name rule did not match
    ExcludedFolder,   // name matched, but the folder holds something else (animated sea bump frames)
    NotGrayscale,     // name matched, but the texture is coloured: treated as an ordinary texture
};

struct HeightMapVerdict
{
    bool is_height_map = false;
    HeightMapReason reason = HeightMapReason::NameNoMatch;
};

// Override list. Text format, one texture per line, path relative to RESOURCE/Textures without extension,
// case-insensitive, '/' or '\': "+path" forces a height map, "-path" forbids one, '#' starts a comment.
struct HeightMapManifest
{
    std::vector<std::string> include;
    std::vector<std::string> exclude;

    static HeightMapManifest Parse(std::string_view text);
};

// "locations\\Bump_City.tga.tx" -> "locations/bump_city".
std::string NormalizeTexturePath(std::string_view path);
// The name rule: the file name contains "bump".
bool NameSuggestsHeightMap(std::string_view normalized_path);
// R, G and B agree within what DXT compression of a grey image produces. `rgba`: 4 bytes per pixel.
bool IsGrayscale(std::span<const uint8_t> rgba);

// The whole decision for one texture; `rgba` may be any mip (64x64 is plenty). Nothing is guessed silently:
// the first decision for each path is logged (LogLevel::Info) with its reason.
class HeightMapClassifier
{
  public:
    explicit HeightMapClassifier(HeightMapManifest manifest = {});
    HeightMapVerdict Classify(std::string_view path, std::span<const uint8_t> rgba);

  private:
    HeightMapManifest manifest_;
    std::vector<std::string> logged_;
};

} // namespace storm::fx
