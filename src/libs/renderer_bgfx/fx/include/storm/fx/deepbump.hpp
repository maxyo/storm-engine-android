// Normal maps from colour textures with DeepBump (github.com/HugoTini/DeepBump, GPL-3.0; the model file and its
// provenance: third_party/licenses/DeepBump-*). DeepBump is a U-Net that turns 256x256 grey tiles into tangent-space
// normals. This header is the part around the model: the grey input, the overlapping tiles and their blend (the
// add-on's own scheme with "MEDIUM" overlap), the axes, the mips and the strength. Running the model (onnxruntime) is
// src/libs/relief's; these functions only call `infer` for each tile, so they are tested without the model.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr int kDeepBumpTile = 256;
inline constexpr int kDeepBumpOverlap = 64; // DeepBump's "MEDIUM": a quarter of the tile

// The model's input for a texture: the mean of R, G and B (0..1), box-downsampled from width x height to
// map_width x map_height (halvings, as NormalMapSize gives them).
std::vector<float> DeepBumpInput(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height);

// One tile: kDeepBumpTile² grey values in, 3 x kDeepBumpTile² normal components out (planar x, y, z, 0..1, the model's
// axes). False aborts (cancel or an inference error).
using DeepBumpInfer = std::function<bool(const float *tile, float *normals)>;

// Tiles that DeepBumpNormals runs for a width x height input.
int DeepBumpTileCount(int width, int height);

// Unit normals x, y, z per texel (interleaved), in our axes: +x along +u, +y along +v (the model's y points up the
// image), z out of the surface. The image wraps around its edges, the tiles overlap by kDeepBumpOverlap and blend with
// linear ramps. Empty when `infer` failed.
std::vector<float> DeepBumpNormals(std::span<const float> grey, int width, int height, const DeepBumpInfer &infer);

// The relief's RMS tilt |n.xy| over the top mip of an RG8 map.
float TiltRms(const NormalMap &rg8);

// The RG8 map of a normal field: every mip averaged from the one above and renormalised, every tilt scaled so the top
// mip's RMS tilt is `target_tilt` (the owner's choice: DeepBump's shape at the strength of our own derivation for the
// same texture, TiltRms(DeriveNormalMap(...))), at most 0.99.
NormalMap DeepBumpNormalMap(std::span<const float> normals, int width, int height, float target_tilt);

} // namespace storm::fx
