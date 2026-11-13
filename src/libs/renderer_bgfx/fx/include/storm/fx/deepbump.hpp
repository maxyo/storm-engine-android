// Normal maps from colour textures with DeepBump (github.com/HugoTini/DeepBump, GPL-3.0; the model file and its
// provenance: third_party/licenses/DeepBump-*). DeepBump is a U-Net that turns grey images into tangent-space normals.
// This header is the part around the model: the grey input, the wrapped padding, the axes, the mips and the strength.
// Running the model (onnxruntime) is src/libs/relief's; these functions only call `infer`, so they are tested without
// the model.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace storm::fx
{

// The model is fully convolutional: it runs once on the whole texture, wrapped around by kDeepBumpPad texels on every
// side (the texture tiles) and grown to a multiple of 32 (its five 2x downsamplings).
inline constexpr int kDeepBumpPad = 64;

// The first 8 bytes of the sha256 of the model the game runs (src/libs/relief/model/deepbump256-int8.onnx): part of
// the relief maps' method (ReliefMethod), so another model never reuses their maps. src/libs/relief/CMakeLists.txt
// fails the build when the model file does not match it.
inline constexpr uint64_t kDeepBumpModelDigest = 0x24b0975701c94500ull;
void DeepBumpInputSize(int width, int height, int &padded_width, int &padded_height);

// The model's input for a texture: the mean of R, G and B (0..1), box-downsampled from width x height to
// map_width x map_height (halvings, as NormalMapSize gives them).
std::vector<float> DeepBumpInput(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height);

// The model on one padded image: padded_width x padded_height grey values in, 3 planes of normal components out (x, y,
// z, 0..1, the model's axes). False aborts (cancel or an inference error).
using DeepBumpInfer = std::function<bool(const float *image, int padded_width, int padded_height, float *normals)>;

// Unit normals x, y, z per texel (interleaved), in our axes: +x along +u, +y along +v (the model's y points up the
// image), z out of the surface. Empty when `infer` failed.
std::vector<float> DeepBumpNormals(std::span<const float> grey, int width, int height, const DeepBumpInfer &infer);

// The RG8 map of a normal field as the model gives it (the owner's choice: DeepBump's own strength): every mip averaged
// from the one above and renormalised, tilts at most 0.99.
NormalMap DeepBumpNormalMap(std::span<const float> normals, int width, int height);

} // namespace storm::fx
