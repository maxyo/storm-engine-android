// Gloss maps derived on the device from the player's own colour textures: how polished each texel is (0 matte .. 1 a
// mirror), for the highlights of the sun and the lamps (fx_lighting.sh fxGlossSpec, ff_fragment.sh).
//
// The texture's material class comes from its path (ClassifyGlossName: an ordered table of folder / name rules written
// from TEHO's texture folders, like the relief classes): metal, glass, leather and lacquered wood are glossy, painted
// hulls and wet cave stone less, dry wood, stone and plaster a little, cloth and ground not at all. Each class sets a
// base gloss and how much the texel's own cues move it: brighter than its surroundings (a worn, polished spot) up,
// darker (a gap, grime) down, uniform areas a little up; in the mixed building atlases the colour also tells plaster,
// wood, lacquer and window glass apart, on metal it tells rust from polished brass and iron.
//
// Ready models were looked at first: PBRify_Remix's roughness model (CC0, SPAN) on TEHO textures is 0.68..0.95
// explained (R²) by a linear fit of luminance, its high pass and saturation, it reads dark texels as smooth (crevices,
// black backgrounds) and costs 1..3 s per 512 texture on a host core against ~10 ms here; the class prior it lacks is
// what separates metal from wood. So the derivation is ours (no model, no inference queue).
//
// Derived from the player's files: cached on the device in the relief directory (relief_store.hpp WriteGlossMap),
// never shipped or committed.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace storm::fx
{

enum class GlossClass : uint8_t
{
    None,        // no gloss map: characters, UI, sky and weather, effects, foliage, water, light maps, unknown
    Matte,       // cloth, sails, ropes, ground, sand, grass ground, plaster
    DryWood,     // planks, decks, beams, barrels, crates, roofs
    PaintedWood, // ship hulls
    Lacquered,   // furniture, cabins, chests, doors
    Stone,       // stone, brick, cobbles
    WetStone,    // caves, grottos, crypts, dungeons
    Building,    // one atlas per house: plaster, beams, doors, windows
    Interior,    // the atlas of a room: plaster, polished wood, lacquer
    Tile,        // glazed tiles, marble
    Leather,     // bags, belts, books
    Metal,       // cannons, guns, iron, chains, gold, lanterns
    Glass,       // windows, bottles
};
const char *GlossClassName(GlossClass gloss);

struct GlossVerdict
{
    GlossClass gloss = GlossClass::None;
    const char *rule = nullptr; // the table entry that decided, nullptr when no rule matched (no gloss map)
};
// On a normalized path (NormalizeTexturePath).
GlossVerdict ClassifyGlossName(std::string_view normalized_path);

// Larger side of the gloss maps: highlights vary more slowly than relief; Высокая uploads it whole, Средняя from the
// next mip (kGlossMapSize / 2).
inline constexpr int kGlossMapSize = 256;
// Version of the gloss maps' derivation: bump when DeriveGlossMap or the class parameters change.
inline constexpr uint32_t kGlossMethod = 0x47000004u;
// The method of a texture's gloss map in its key (fx::ReliefKey): the version and the texture's class.
uint64_t GlossMethod(GlossClass gloss);

// How much of the texture's colour tints its highlight (metal reflects in its own colour, the rest in the light's).
float GlossMetalTint(GlossClass gloss);

// `rgba`: width * height texels, 4 bytes each (R first). Deterministic. The top mip is the texture halved until its
// larger side is at most kGlossMapSize; every mip averages the gloss of the one above. NormalMapFormat::R8 (the store
// packs it as the G of an EAC RG11 material texture: relief_store.hpp WriteGlossMap). Empty map on bad input or
// GlossClass::None.
NormalMap DeriveGlossMap(std::span<const uint8_t> rgba, int width, int height, GlossClass gloss);

} // namespace storm::fx
