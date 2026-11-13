// Gloss maps of the player's own colour textures: how polished each texel is (0 matte .. 1 a mirror), for the
// highlights of the sun and the lamps (fx_lighting.sh fxGlossSpec, ff_fragment.sh).
//
// The gloss is PBRify's roughness network's (1x-PBRify_RoughnessV2, relief_model.hpp; the owner's choice), run by
// src/libs/relief at kGlossMapSize with the normal and height networks, and corrected for this game (GlossOfRoughness):
//   * its roughness is GGX's perceptual one; the highlight is a Blinn-Phong of power 2^(3 + 4.5 g): g is the power
//     that matches GGX, log2(2 / r^4 - 2), on that scale;
//   * texels below their surroundings in PBRify's height (mortar, gaps, grooves) keep a quarter of it;
//   * dark texels lose it (the model reads the baked dirt and black backgrounds of old textures as smooth), except on
//     metal;
//   * on metal the texels that read as metal by their colour (not rust, not the carriages' wood: GlossMetalMask) keep
//     at least half a gloss and tint their highlight;
//   * matte classes are capped (cloth, sails, ground, plaster 0.1; stone 0.35).
// Which textures get a gloss map and which are metal is the path's class (ClassifyGlossName: an ordered table of folder
// / name rules written from TEHO's texture folders, like the relief classes).
//
// Derived from the player's files: cached on the device in the relief directory (relief_store.hpp WriteGlossMap),
// never shipped or committed.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

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
inline constexpr GlossClass kLastGlossClass = GlossClass::Glass; // keep it the last one
const char *GlossClassName(GlossClass gloss);

struct GlossVerdict
{
    GlossClass gloss = GlossClass::None;
    const char *rule = nullptr; // the table entry that decided, nullptr when no rule matched (no gloss map)
};
// On a normalized path (NormalizeTexturePath).
GlossVerdict ClassifyGlossName(std::string_view normalized_path);

// Larger side of the gloss maps (the size the roughness network runs at): highlights vary more slowly than relief;
// Высокая uploads it whole, Средняя from the next mip (kGlossMapSize / 2).
inline constexpr int kGlossMapSize = 256;
// Version of the gloss: bump when GlossOfRoughness, GlossMetalMask or the class caps change (the roughness network's
// digest is part of GlossMethod too). 5: PBRify's roughness.
inline constexpr uint32_t kGlossMethod = 0x47000005u;
// The method of a texture's gloss map in its key (fx::ReliefKey): the version, the roughness network and the class.
uint64_t GlossMethod(GlossClass gloss);

// How much a metal texel's colour tints its highlight (times its GlossMetalMask): metal reflects in its own colour,
// the rest in the light's.
float GlossMetalTint(GlossClass gloss);

// A gloss field at the network's size: `gloss` 0..1 and, for metal, `metal` (GlossMetalMask), one per texel.
struct GlossField
{
    int width = 0, height = 0;
    std::vector<float> gloss, metal;
};

// Per texel of a metal texture, 0..1: how much it reads as polished metal by its colour (1), not rust (orange-brown,
// saturated, dark, grainy) or the gun carriages' brown wood (0). `rgba` width x height, 4 bytes a texel.
std::vector<float> GlossMetalMask(std::span<const uint8_t> rgba, int width, int height);

// The gloss of a texture of class `gloss` from the roughness network's output. All width x height: `roughness` the
// network's (the mean of its planes, 0..1), `height` the height network's at the same size (0..1), `rgba` the colour
// the networks read (4 bytes a texel). Deterministic. Empty on bad input or GlossClass::None.
GlossField GlossOfRoughness(std::span<const float> roughness, std::span<const float> height, std::span<const uint8_t> rgba,
                            int width, int height_texels, GlossClass gloss);

// The gloss map file's material texture of a texture without a relief height (relief_store.hpp WriteGlossMap): RG8 with
// all mips, each the box average of the one above; R the metal mask (0 off metal; ff_fragment.sh tints the highlight by
// it), G the gloss.
NormalMap GlossMaterial(const GlossField &field);

} // namespace storm::fx
