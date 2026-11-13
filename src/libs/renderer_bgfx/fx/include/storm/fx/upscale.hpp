// Neural ×2 textures (the owner's choice): PBRify's 4x-PBRify_UpscalerSPANV4 (Kim2091/PBRify_Remix, CC0-1.0; the int8
// file and its provenance: src/libs/relief/model, third_party/licenses/PBRify-*) upscales a colour texture ×4 on the
// player's device, from the player's own game files; the result is box-filtered back to ×2 in linear light, its mip chain
// rebuilt and encoded for the GPU's tier, and cached in the upscale directory (the engine's cache root,
// fs::GetCachePath(), + "/upscale": on Android the app's cache, cleared with it). Never shipped, never committed.
//
// This header is the CPU part the game and the preparation share: which textures get a ×2, the cache file, the GPU
// budget and the choice of the textures that are resident at ×2 (by their distance to the camera). Running the model is
// src/libs/relief/upscale.hpp; the residency on the GPU is renderer_bgfx src/effects/upscale.hpp.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace storm::fx
{

// The first 8 bytes of the sha256 of src/libs/relief/model/pbrify-upscale-int8.onnx: part of UpscaleMethod and of the
// relief maps' method (their textures of up to kReliefMapSize are upscaled first, ReliefMapsFromUpscale). The build
// checks the file against it.
inline constexpr uint64_t kUpscaleModelDigest = 0x5148c5c90562bdffull;
// Bump when the ×2 changes in a way UpscaleMethod cannot see (padding, filters, mips, the file layout, the GPU graph
// pbrify-upscale.ncnn.*, which the relief's device choice may run instead of the int8 model).
inline constexpr uint32_t kUpscaleVersion = 2; // 2: on a GPU device the ×2 comes from ncnn's fp16 graph (backend.hpp)
// The model's tiles (texels of the source) and their padding on every side: more than the network's receptive field
// (21 3x3 convolutions in a row: 21 texels), so neither tiles nor a tiling texture's edges show a seam.
inline constexpr int kUpscaleTile = 256;
inline constexpr int kUpscalePad = 24;

enum class UpscaleTarget : uint8_t
{
    None,      // no ×2: UI, sky, effects, water, foliage and other alpha-cut textures, lightmaps, height maps
    Surface,   // the colour textures of the relief classes (stone, wood, buildings, plaster, metal, props)
    Character, // the characters' textures (RESOURCE/Textures/Characters): the skinned draws sample them as any other
};
const char *UpscaleTargetName(UpscaleTarget target);

// From the texture's normalized path and its relief class (ReliefClassifier); `cutout`: its alpha cuts holes (the
// alpha test of foliage, nets, rigging: the network does not keep a cut edge). A lightmap or a texture of the
// interfaces never has a relief class.
UpscaleTarget ClassifyUpscale(std::string_view normalized_path, ReliefClass relief, bool cutout);

// The largest texture that gets a ×2 (its larger side; the ×2 is at most 2048, the size every GLES 3.0 GPU takes). The
// game's 2048 textures (347 of its 1987 candidates) have that density already; their ×2 would be 4096² at 10.7 MB each
// in the cache and on the GPU and 16 times a 512's inference.
inline constexpr int kUpscaleMaxSource = 1024;
bool UpscaleSize(int width, int height);

// Whether the padding wraps around (a texture that tiles: stone, wood, plaster) or mirrors (atlases: buildings, props,
// metal, characters, whose edges are no neighbours of each other).
bool UpscaleWraps(UpscaleTarget target, ReliefClass relief);

// The relief maps of a texture whose larger side is at most this are made from its ×2 (the models run on the ×2 at up
// to twice the map's size and are box-filtered to the map, src/libs/relief): half the height noise and DXT steps of
// the maps made from the texture itself (local-testing/pom/pbrify-set x2.json, inter.json). Larger textures have the
// detail already.
bool ReliefMapsFromUpscale(int width, int height);

// The method of a ×2: kUpscaleVersion, the model, and the GPU tier it is encoded for (`bc`: BC1/BC3 on GPUs with S3TC,
// else ETC2 RGB8/RGBA8, as the device's DXT textures).
uint64_t UpscaleMethod(bool bc);

// The cache: one file "<key>.x2" per texture, key = ReliefKey(path, probe level, UpscaleMethod(bc), larger side of the
// level the ×2 is made from: the loaded texture's top, which texture_degradation may make smaller than the file's). A KTX 1.1
// container (bimg) after a header with its size and checksum; written atomically.
std::filesystem::path UpscaleFile(const std::filesystem::path &directory, uint64_t key);
bool WriteUpscaled(const std::filesystem::path &directory, uint64_t key, std::span<const uint8_t> ktx);
// The KTX container, or nothing when there is no file. A damaged file is deleted and reported in `error`. A read
// renews the file's time (the eviction removes the least recently used first).
std::optional<std::vector<uint8_t>> ReadUpscaled(const std::filesystem::path &directory, uint64_t key,
                                                 std::string *error = nullptr);
bool HasUpscaled(const std::filesystem::path &directory, uint64_t key);
// Removes the least recently used files until the directory holds at most `max_bytes`; returns what it holds then.
uint64_t TrimUpscaled(const std::filesystem::path &directory, uint64_t max_bytes);
// The upscale directory's limit on the storage (the ×2 of a whole game fits; TrimUpscaled after each pass).
inline constexpr uint64_t kUpscaleCacheBytes = 1536ull << 20;

// GPU memory the ×2 textures may take together, from the device's RAM (RAM / 32, 64..384 MB: a 4 GB phone 128 MB,
// 8 GB 256 MB, 12 GB and more 384 MB). The ×2 of a texture takes four times its memory, so only the nearest are
// resident; the others stay at their own size.
uint64_t UpscaleBudgetBytes(uint64_t device_ram_bytes);
// A texture drawn within kUpscaleNear metres (its draws' bounding spheres to the camera) takes its ×2 while the budget
// allows; a resident ×2 stays until kUpscaleFar (no flicker at the border).
inline constexpr float kUpscaleNear = 25.0f;
inline constexpr float kUpscaleFar = 35.0f;

struct UpscaleCandidate
{
    float distance = 0.0f; // nearest draw this period, metres; negative: not drawn
    uint64_t bytes = 0;    // GPU memory of its ×2
    bool resident = false;
};
struct UpscalePlan
{
    std::vector<bool> keep;  // per candidate: resident at ×2 after the plan
    uint64_t bytes = 0;      // their memory
    int limited = 0;         // candidates within kUpscaleNear left at their own size by the budget
    float nearest_limited = -1.0f; // the nearest of them
};
// Nearest first: every candidate within kUpscaleNear (or resident and within kUpscaleFar) while the budget allows.
UpscalePlan PlanUpscaleResidency(std::span<const UpscaleCandidate> candidates, uint64_t budget_bytes);

} // namespace storm::fx
