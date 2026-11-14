// The relief's material texture (sampler unit 7, fx_lighting.sh): R the height for the parallax and the cavity
// (FX_LIGHT_PARALLAX: fxParallax, fxParallaxShadow, fxReliefCavity), G the gloss (gloss_map.hpp, the highlights).
//
// The height is PBRify's (relief_model.hpp, the owner's choice), made once with the normal map. An atlas holds several
// surfaces side by side (planks, plaster, a counter, an empty strip): each is normalised against its own neighbourhood
// (the local mean and spread over kHeightRegionTexels), so a plank floor next to a deep recess keeps its own seams
// instead of a few percent of the whole texture's range; then its 2 % / 98 % become 0 and 1. The depth is the class's
// own (ReliefDepthTexels).
//
// Derived from the player's files: kept with its normal map in the relief directory (relief_store.hpp), never shipped.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

// Bump when the height or the material's packing changes (it is part of the relief maps' method, ReliefMethod).
inline constexpr uint32_t kHeightMapVersion = 6; // 4: PBRify's height, normalised per region; 5: every class, the grey height
                                                 // maps' own; 6: the parallax trust (parallax_trust.hpp), halved depths

// Whether a class's textures get a material texture (and so the parallax and the cavity): every class with relief
// (the owner: the parallax wherever it can work), the grey height maps too (their own heights).
bool ReliefHasHeight(ReliefClass relief);

// Whether a class's textures take the parallax by default: masonry only (the owner: stone walls, cobbles and pavement, brick;
// the house atlases with their roof tiles). Everything else - wood and furniture, props, plaster ground, metal, the game's
// grey height maps - keeps its normals and its cavity and no parallax; the shipped list (parallax_exclusions.hpp) names the
// exceptions: the plank floors, decks and piers that take it, signs that do not.
bool ReliefTakesParallax(ReliefClass relief);

// The depth of a class's height in texels of the 512 map (Settings::parallax_strength scales them all): stone, brick and
// cobbles 3, the buildings' atlases (stone, plaster and beams in one) 2, the grey height maps 1.5, plaster, ground and
// cloth 1.25, wood 1, props 0.75, metal 0.375: a quarter of the first values (12, 8, 6, 5, 4, 3, 1.5): the owner found the
// parallax far too strong, on walls and on everything else. A depth in texels is already proportional to the texel's size
// in the world: a plank's texture, small on the object, gets a small depth, a wall's large one, between the limits of
// kParallaxMaxDepth.
float ReliefDepthTexels(ReliefClass relief);

// The deepest the parallax goes, in metres: where a texture is stretched over a large surface (a house wall's atlas),
// its depth in texels is a lot of centimetres. The shallowest is a fifth of it (5 mm): the texture of a plank, a post
// or a plank, its texel a fraction of a millimetre, would get a depth nobody sees.
inline constexpr float kParallaxMaxDepth = 0.025f;

// How much of the sun's light the relief's own shadow takes where it falls (fx_lighting.sh fxParallaxShadow).
inline constexpr float kParallaxShadow = 0.8f;
// How much light the relief's cavity takes in its deepest joints (fx_lighting.sh fxReliefCavity).
inline constexpr float kReliefCavity = 0.6f;

// The sigma, in texels of the 512 map, of the neighbourhood a height is normalised against (an atlas region's scale).
inline constexpr float kHeightRegionTexels = 24.0f;

// Sets `map.material` (RG8 with the mips of `map.rg`: R the height, G the gloss) and `map.depth` (texels of the top mip)
// for a texture of class `relief`. `height`: the height model's, 0..1, one per texel of the map's top mip; `gloss`: R8
// at the same size, or empty (G 0: no gloss class); `albedo`: the colour texture at the same size (RGBA8) for the
// parallax trust (map.trust, map.trust_coverage; the height goes to the top where it is not trusted), or empty (all
// trusted). Deterministic. False (no material) for a map that is not Rg8, a
// class without height, a flat height or mismatched sizes.
bool AddReliefMaterial(NormalMap &map, ReliefClass relief, std::span<const float> height, std::span<const uint8_t> gloss,
                       std::span<const uint8_t> albedo = {});

// The heights of a grey height ("bump") texture at the size of its map: its red channel (0..1), box-halved from
// width x height to map_width x map_height (halvings, as NormalMapSize gives them).
std::vector<float> GreyHeights(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height);

} // namespace storm::fx
