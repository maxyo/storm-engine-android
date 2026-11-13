// Normal maps of the player's own textures and the rules that decide which textures get one.
//
// Two sources:
//   * the game's grey height ("bump") textures: the 2x multiply detail layer on the second texture stage
//     (GEOM_SERVICE_R::SetMaterial, texture[1], second UV set). Their values are heights; the game derives their maps
//     itself (DeriveNormalMap).
//   * ordinary colour textures of stone, wood, plaster and ground: PBRify's normals as the model gives them
//     (relief_model.hpp, made by src/libs/relief; the owner's choice).
// Which textures get relief comes from one ordered table of path / name rules (ClassifyTextureName) written from TEHO's
// texture folders. Anything the table does not know gets no relief (and is logged once).
//
// Derived textures come from the player's files: cache them on the device (keyed by the source pixels and
// NormalMapCacheKey), never ship or commit them.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace storm::fx
{

// Material class of a texture for the relief: which textures get a map, and where it comes from.
enum class ReliefClass : uint8_t
{
    None,      // no relief: characters, sails and flags, rigging, UI, water, glass, foliage, sky, effects, light maps
    HeightMap, // grey "bump" texture: its values are heights
    Stone,
    Wood,
    Building, // one atlas per building: its walls, beams, doors
    Plaster,  // plaster, ceilings, floors, ground, sand
    Metal,    // cannons, guns, iron, chains, lanterns, weapons: fine pitting that the gloss highlights show
    Prop,     // what no rule names: the props of the locations and the ships, items, island textures
};
const char *ReliefClassName(ReliefClass relief);

struct NormalMapOptions
{
    // Height of a full black-to-white step of the red channel, in texels (see max_size).
    float strength = 2.0f;
    bool wrap = true;     // texture tiles: filters wrap around the edges
    int pre_blur = 1;     // 3x3 binomial passes first: remove DXT block noise
    // The derived top mip is the texture halved until its larger side is at most this. `strength` is in texels of the
    // map derived at kNormalMapReferenceSize, so a smaller map has the same relief.
    int max_size = 1024;

    bool operator==(const NormalMapOptions &) const = default;
};

inline constexpr int kNormalMapReferenceSize = 1024;

// The derivation options of the grey height maps (ReliefClass::HeightMap).
NormalMapOptions HeightMapOptions();

// How NormalMap::rg holds the texels. DeriveNormalMap makes RG8; the game stores and uploads EAC RG11 (half the
// memory, core in OpenGL ES 3.0), encoded by the renderer's texture module.
enum class NormalMapFormat : uint8_t
{
    Rg8,     // 2 bytes per texel
    EacRg11, // 16 bytes per 4x4 block (R block, then G block); levels smaller than 4 texels take one block per axis
};

struct NormalMap
{
    int width = 0;
    int height = 0;
    int mip_count = 0;
    NormalMapFormat format = NormalMapFormat::Rg8;
    // All mips, top first, tightly packed: x and y of the unit tangent-space normal as 0.5 + 0.5 * n.
    // z = sqrt(1 - x² - y²) is rebuilt in the shader. +x follows +u, +y follows +v, z points out of the surface.
    std::vector<uint8_t> rg;
    // The relief's material texture (height_map.hpp AddReliefMaterial; sampler unit 7): the same mips and format as `rg`
    // (RG8 beside Rg8, EAC RG11 beside EacRg11), R the height (0 the deepest, 1 the top), G the gloss (gloss_map.hpp).
    // Empty: no material (the grey "bump" textures' maps).
    std::vector<uint8_t> material;
    // What 0..1 of the material's height spans, in texels of the top mip (DropTopMips keeps it in texels of the new
    // top); 0: a flat height (no parallax, no cavity).
    float depth = 0.0f;
};

// Bump this when the derivation changes, and put NormalMapCacheKey() into the cache key of the device cache.
inline constexpr uint32_t kNormalMapVersion = 3;
uint64_t NormalMapCacheKey(const NormalMapOptions &options);

// Size of the derived top mip for a `width` x `height` texture (max_size applied).
void NormalMapSize(int width, int height, const NormalMapOptions &options, int &out_width, int &out_height);

// `rgba`: width * height texels, 4 bytes each (R first), row by row. Deterministic. Every mip is derived from the
// height field downsampled to that size (Scharr filter), so distant surfaces flatten correctly instead of keeping a
// noisy, shortened normal. Empty map on bad input.
NormalMap DeriveNormalMap(std::span<const uint8_t> rgba, int width, int height, const NormalMapOptions &options);

// Removes top mips (of `rg` and `material`) until the larger side is at most `max_size` (the preset's resolution of the
// resident map).
void DropTopMips(NormalMap &map, int max_size);
// Bytes of one level, and of all mips of a map whose top mip is width x height.
size_t NormalMapLevelBytes(NormalMapFormat format, int width, int height);
size_t NormalMapBytes(int width, int height, NormalMapFormat format = NormalMapFormat::Rg8);

// ---- which textures get relief ----

// "Resource\\Textures\\Locations\\Bump_City.tga.tx" -> "locations/bump_city": lower case, '/', without the
// textures folder and the extensions.
std::string NormalizeTexturePath(std::string_view path);
// The height-map name rule: the file name contains "bump".
bool NameSuggestsHeightMap(std::string_view normalized_path);
// R, G and B agree within what DXT compression of a grey image produces. `rgba`: 4 bytes per pixel.
bool IsGrayscale(std::span<const uint8_t> rgba);

struct ReliefVerdict
{
    ReliefClass relief = ReliefClass::None;
    // The table entry that decided (a folder "ships/", a name part "brick"; "ships/parus" = name part in a folder),
    // "bump + grey" for height maps, nullptr when no rule matched (unknown texture: no relief).
    const char *rule = nullptr;
    bool by_folder = false; // the rule names a whole folder
};

// The rules table alone, on a normalized path. Height maps are decided by ReliefClassifier (they need the pixels).
ReliefVerdict ClassifyTextureName(std::string_view normalized_path);

// The whole decision for one texture; `rgba` may be any mip (64x64 is plenty; read only for "bump" names). Every
// first decision for a path is logged at LogLevel::Debug with its rule.
class ReliefClassifier
{
  public:
    ReliefVerdict Classify(std::string_view path, std::span<const uint8_t> rgba);

  private:
    std::vector<std::string> logged_;
};

} // namespace storm::fx
