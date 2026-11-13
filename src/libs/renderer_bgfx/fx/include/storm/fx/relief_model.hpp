// The relief maps' models: PBRify's normal, height and roughness networks (Kim2091/PBRify_Remix, 1x-PBRify_NormalV3,
// 1x-PBRify_Height and 1x-PBRify_RoughnessV2, CC0-1.0; the int8 files and their provenance: src/libs/relief/model,
// third_party/licenses/PBRify-*), the owner's choice for the normals, the height and the gloss (gloss_map.hpp). Each is a 1x SPAN network: a colour texture 1x3xHxW (0..1) in, three
// planes of the same size out. This header is the part around the models: their input (the texture at the map's size,
// wrap-padded: the textures tile), the crop, the axes and the normal map's mips. Running them (onnxruntime) is
// src/libs/relief's; these functions only call `infer`, so they are tested without the models.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace storm::fx
{

// Texels of wrap padding on every side: more than the networks' receptive field (about 41 px), so a tiling texture
// has no seam.
inline constexpr int kReliefModelPad = 48;

// The first 8 bytes of the sha256 of the model files the game runs (src/libs/relief/model): part of the relief maps'
// method (ReliefMethod), so other models never reuse their maps. src/libs/relief/CMakeLists.txt fails the build when a
// model file does not match its digest.
inline constexpr uint64_t kReliefNormalModelDigest = 0xfc91ce30e48deaf5ull; // pbrify-normal-int8.onnx
inline constexpr uint64_t kReliefHeightModelDigest = 0x01f251dc40e79fa7ull; // pbrify-height-int8.onnx
inline constexpr uint64_t kReliefRoughnessModelDigest = 0x860845c92142b7d1ull; // pbrify-roughness-int8.onnx

// A model's input: the texture's colour (R, G, B 0..1), box-downsampled from width x height to map_width x map_height
// (halvings, as NormalMapSize gives them), wrapped around by kReliefModelPad texels, as three planes.
std::vector<float> ReliefModelInput(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height);

// One model on one padded image (3 planes in, 3 planes out, the same size). False aborts (cancel or an inference error).
using ReliefInfer = std::function<bool(const float *image, int padded_width, int padded_height, float *planes)>;

// The model's output for the map: its three planes cropped to map_width x map_height (planar). Empty when `infer` failed.
std::vector<float> ReliefModelRun(std::span<const float> input, int map_width, int map_height, const ReliefInfer &infer);

// The normal network's planes as unit normals x, y, z per texel (interleaved): its axes are ours (+x along +u, +y along
// +v, z out of the surface).
std::vector<float> ReliefModelNormals(std::span<const float> planes, int width, int height);

// The height network's planes as one height per texel (their mean, 0..1, 1 the highest); the roughness network's the
// same way (their mean, 0..1, GGX's perceptual roughness: gloss_map.hpp GlossOfRoughness).
std::vector<float> ReliefModelHeight(std::span<const float> planes, int width, int height);

// The RG8 map of a normal field as the model gives it (the owner's choice: no rescale): every mip averaged from the one
// above and renormalised, tilts at most 0.99.
NormalMap NormalMapOfNormals(std::span<const float> normals, int width, int height);

} // namespace storm::fx
