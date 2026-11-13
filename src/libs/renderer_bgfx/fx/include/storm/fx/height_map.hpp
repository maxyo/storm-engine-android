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
inline constexpr uint32_t kHeightMapVersion = 5; // 4: PBRify's height, normalised per region; 5: every class, the
                                                 // grey height maps' own

// Whether a class's textures get a material texture (and so the parallax and the cavity): every class with relief
// (the owner: the parallax wherever it can work), the grey height maps too (their own heights).
bool ReliefHasHeight(ReliefClass relief);

// The depth of a class's height in texels of the 512 map (Settings::parallax_strength scales them all): stone, brick and
// cobbles 12, the buildings' atlases (stone, plaster and beams in one) 8, the grey height maps 6, plaster, ground and
// cloth 5, wood 4, props 3, metal 1.5.
float ReliefDepthTexels(ReliefClass relief);

// How much of the sun's light the relief's own shadow takes where it falls (fx_lighting.sh fxParallaxShadow).
inline constexpr float kParallaxShadow = 0.8f;
// How much light the relief's cavity takes in its deepest joints (fx_lighting.sh fxReliefCavity).
inline constexpr float kReliefCavity = 0.6f;

// The sigma, in texels of the 512 map, of the neighbourhood a height is normalised against (an atlas region's scale).
inline constexpr float kHeightRegionTexels = 24.0f;

// Sets `map.material` (RG8 with the mips of `map.rg`: R the height, G the gloss) and `map.depth` (texels of the top mip)
// for a texture of class `relief`. `height`: the height model's, 0..1, one per texel of the map's top mip; `gloss`: R8
// at the same size, or empty (G 0: no gloss class). Deterministic. False (no material) for a map that is not Rg8, a
// class without height, a flat height or mismatched sizes.
bool AddReliefMaterial(NormalMap &map, ReliefClass relief, std::span<const float> height, std::span<const uint8_t> gloss);

// The heights of a grey height ("bump") texture at the size of its map: its red channel (0..1), box-halved from
// width x height to map_width x map_height (halvings, as NormalMapSize gives them).
std::vector<float> GreyHeights(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height);

} // namespace storm::fx
