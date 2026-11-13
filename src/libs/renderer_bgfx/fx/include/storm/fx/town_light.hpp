// Our own lightmaps for the towns (Средняя / Высокая): the sun's shadows of the real hour and the sky's occlusion
// (alleys, under awnings, by the walls), on the texels of the game's lightmaps (the same texture stage and texture
// coordinates, one texture fetch as before).
//
// The game's lightmaps (lighting\<time>\...\shadow.tga) were baked by hand for a near-noon sun, whatever the hour:
// a 8:00 street has noon shadows. Measured on FortFrance / SentJons ground: luminance = 0.04 + 0.14 sun + 0.26 sky
// (R² 0.46; sun alone 0.38), the sun's visibility along their bake direction (fx/sun_fit.hpp) and the sky's from
// ray casting the place's geometry. The rest is texture-like variation in the paint.
//
// Here:
//   * RasterizeLightmap: the texels a lightmap's triangles cover (world position and normal of each), plus a
//     gutter of texels outside the triangles that take their nearest covered texel (bilinear filtering reads them);
//   * SkyVisibility (once per place) and SunVisibility (per sun direction, soft: a few rays over the sun's disk and
//     the texel) by ray casting the place's still geometry (fx::Occluders);
//   * CalibrateTownLight: how the game's lightmap was lit, fitted on its texels to its sun (the fitted bake
//     direction) and sky;
//   * ComposeTownLight: the game's lightmap relit for the hour's sun (blended between two sun directions as the hour
//     runs): its paint (the ground's colours are painted into it) and tone stay, its shadows move.
#pragma once

#include "storm/fx/bounce.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/fx/visibility.hpp"
#include "storm/fx/sun_fit.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

struct LightmapTexels
{
    int width = 0, height = 0;
    std::vector<uint32_t> texel;  // y * width + x of each covered texel
    std::vector<Vec3> position;   // its world position (on the surface)
    std::vector<Vec3> normal;     // the surface's unit normal there (the side the lightmap lights)
    std::vector<uint32_t> gutter; // texels outside every triangle: [2i] the texel, [2i + 1] the covered one it copies
};

// The texels of lightmap `lightmap` (`triangle_lightmap` per triangle) at `width` x `height`.
LightmapTexels RasterizeLightmap(std::span<const Vec3> positions, std::span<const std::array<float, 2>> uvs,
                                 std::span<const uint32_t> indices, std::span<const uint32_t> triangle_lightmap,
                                 uint32_t lightmap, int width, int height, int gutter = 2);

// 0..1 per texel: the share of the sky (cosine weighted, within `reach` metres) each texel sees.
std::vector<uint8_t> SkyVisibility(const Occluders &occluders, const LightmapTexels &texels, float reach = 10.0f,
                                   int rays = 32, unsigned threads = 1);
// 0..1 per texel: the sun's direct light on it, the share of `sun` (unit, towards the sun) the texel sees (over
// `samples` rays spread across the sun's disk and the texel) times the cosine to its normal; 0 facing away.
std::vector<uint8_t> SunVisibility(const Occluders &occluders, const LightmapTexels &texels, const Vec3 &sun,
                                   float texel_metres, int samples = 4, unsigned threads = 1);

// How the game's lightmap was lit: its luminance = a + b sun + c sky on its texels (the sun's visibility along the
// direction it was baked with, the sky's visibility). The rest is its paint: the ground's colours (grass, sand, the
// paths) are painted into the same texture.
struct TownLightTone
{
    float a = 0.0f, b = 0.0f, c = 0.0f;
    float r2 = 0.0f; // how much of the game lightmap's luminance the least squares explain
    uint32_t edges = 0; // shadow edge pairs b was measured at (too few: b from the least squares)
    bool valid = false;
    // The sky the game's lightmap behaves as if it had, per bin of our sky visibility (bin centres (i + 0.5) / 8): the
    // mean of (luminance - a - b sun) / c over the bin's texels, at least our own (kept where a bin has too few
    // texels). The game painted its narrow alleys and gaps between houses nearly as bright as the open street: with
    // this as the lighting it was baked with, our sky darkens them as much as they are closed.
    static constexpr int kSkyBins = 8;
    float game_sky[kSkyBins] = {0.0625f, 0.1875f, 0.3125f, 0.4375f, 0.5625f, 0.6875f, 0.8125f, 0.9375f};
    float GameSky(float k) const;
};

// The game lightmap's lighting (`rgba`: its texels, RGBA8 at the same size) from its sun visibility along the direction
// it was baked with and its sky visibility.
TownLightTone CalibrateTownLight(const LightmapTexels &texels, std::span<const uint8_t> rgba,
                                 std::span<const uint8_t> sun, std::span<const uint8_t> sky);

// The town's walls and props without a lightmap (Средняя / Высокая): their baked vertex colours carry the game's own
// sun (along the direction its lightmaps were baked with) and its ambient, which knows nothing of how closed a
// place is (alleys, gaps between houses read as bright as the open street). They are relit per pixel like our
// lightmaps (ff_fragment.sh, u_fxTownWall): our sun with its shadow, our sky from the visibility volume and the
// bounce now, over the game's sun and the game's sky then, both with the lightmaps' tone (a + b sun + c sky).
// The game's sky on the walls is measured here: over the vertices in the shade of the bake's sun, their mean
// luminance per bin of our sky (read one cell off the surface, as the shader does) relative to the open ones,
// turned into the sky the tone gives that luminance (at least ours: where the game's walls darken as much as our
// sky closes, nothing changes).
struct TownWallTone
{
    static constexpr int kSkyBins = TownLightTone::kSkyBins;
    float game_sky[kSkyBins] = {0.0625f, 0.1875f, 0.3125f, 0.4375f, 0.5625f, 0.6875f, 0.8125f, 0.9375f};
    float relative[kSkyBins] = {}; // the shaded vertices' luminance per bin over the open ones' (the log)
    uint32_t count[kSkyBins] = {};
    uint32_t vertices = 0, shaded = 0;
    bool valid = false; // enough shaded open vertices to compare with
};
// `colors`: D3DCOLOR per position; `skip`: per triangle, 1 for those under a lightmap (relit as texels).
TownWallTone CalibrateTownWalls(std::span<const Vec3> positions, std::span<const uint32_t> colors,
                                std::span<const uint32_t> indices, std::span<const uint8_t> skip,
                                const VisibilityVolume &volume, const TownLightTone &tone, const Vec3 &bake,
                                const Occluders &occluders, unsigned threads = 1);

// The bounce on the texels (fx/bounce.hpp): per texel RGB, the irradiance the field gives one cell (`offset` metres)
// off the surface along its normal, relative to the sun's (a white sun of 1), 0..1 in 0..255.
std::vector<uint8_t> TexelBounce(const LightmapTexels &texels, const BounceField &field, float offset, unsigned threads = 1);

// Our lightmap (RGBA8, width x height, the gutter filled): the game's (`game`) relit, its paint kept: each texel times
// the lighting of the hour's sun (`sun_a` blended towards `sun_b` by `t`) over the lighting it was baked with
// (`baked`), both from `tone` and the sky visibility. With the bounce (`bounce_a` blended towards `bounce_b` by `tb`,
// TexelBounce; empty: none) the sun's light reflected by the surfaces around adds to the sun's own on both sides of
// the ratio, per channel: the hour's sun's bounce now, the bake's sun's (`bounce_baked`) then, so the game's paint
// loses the bounce of its own sun where ours moved it.
struct TownBounce
{
    std::span<const uint8_t> a, b, baked; // RGB per texel
    float t = 1.0f;
};
void ComposeTownLight(const LightmapTexels &texels, const TownLightTone &tone, std::span<const uint8_t> game,
                      std::span<const uint8_t> baked, std::span<const uint8_t> sun_a, std::span<const uint8_t> sun_b,
                      float t, std::span<const uint8_t> sky, std::span<uint8_t> rgba, const TownBounce &bounce = {});

} // namespace storm::fx
