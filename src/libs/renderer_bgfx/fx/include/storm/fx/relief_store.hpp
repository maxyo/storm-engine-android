// The relief maps (and the gloss maps, below) on the device: one file per texture in the relief directory (the engine's cache root,
// fs::GetCachePath(), + "/relief": on Android the app's cache, cleared with it; engine.ini relief_dir overrides).
// The game makes them from its files with PBRify's models while it runs (src/libs/relief, renderer_bgfx
// effects/relief_generator.hpp) and reads them. Derived from the player's files: never ship or commit them.
//
// A texture's key covers the method and the map's options and the texture itself: its normalized path and the bytes of
// its first mip level no larger than kReliefProbeSize, or its smallest (the game may skip top mips, texture_degradation, so the top level
// is not the same on every device; that level is). Its file holds the normal map as EAC RG11 with all mips and, for the
// classes with a height (height_map.hpp ReliefHasHeight), the material texture (R height, G gloss) as EAC RG11 with all
// mips and the height's depth in texels.
#pragma once

#include "storm/fx/normal_map.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace storm::fx
{

inline constexpr int kReliefProbeSize = 64;
// Larger side of the prepared maps (512: the GPU's bilinear filter upsamples them over the textures, Высокая uploads
// them whole, Средняя from the next mip).
inline constexpr int kReliefMapSize = 512;
// Tag of the height maps ("bump" textures) the game derives itself (DeriveNormalMap) into the same directory.
inline constexpr uint32_t kReliefHeightMethod = 0x48000002u; // 2: the file layout with the height
// Bump when the prepared maps change in a way ReliefMethod cannot see (the models' post-processing, the file layout).
// The models, the height's version, the gloss's and the height maps' derivation are in ReliefMethod. 6: PBRify's normals
// and height instead of DeepBump (the owner's choice), the file with the material texture; 7: the material's gloss
// from PBRify's roughness; 8: the parallax trust beside the material (file layout 4).
inline constexpr uint32_t kReliefMethodVersion = 8;

// The method of a texture's map: everything its making depends on, so a change never reuses a map made before it.
// PBRify maps (the same for every colour class with a height): kReliefMethodVersion, the models
// (kReliefNormalModelDigest, kReliefHeightModelDigest, kReliefRoughnessModelDigest), the map size, kHeightMapVersion and kGlossMethod; height maps: kReliefHeightMethod and their
// derivation's options. ReliefClass::None has none (0).
uint64_t ReliefMethod(ReliefClass relief);

// Index of the probe level: the first of `levels` levels (sizes halving from width x height) whose larger side is at
// most kReliefProbeSize, or the last level when none is that small; -1 when that level would be smaller than a 4x4
// block on a side (no relief).
int ReliefProbeLevel(int width, int height, int levels);

// `method`: ReliefMethod of the texture's class.
uint64_t ReliefKey(std::string_view normalized_path, std::span<const uint8_t> probe_level, int probe_width,
                   int probe_height, uint64_t method, int map_size);

std::filesystem::path ReliefFile(const std::filesystem::path &directory, uint64_t key);
// Writes atomically (a temporary file renamed into place). False on an I/O error.
bool WriteReliefMap(const std::filesystem::path &directory, uint64_t key, const NormalMap &map);
// The map, or nothing when there is no file. A damaged file or one of another layout is deleted and reported in `error`.
std::optional<NormalMap> ReadReliefMap(const std::filesystem::path &directory, uint64_t key, std::string *error = nullptr);
bool HasReliefMap(const std::filesystem::path &directory, uint64_t key);

// The gloss maps (gloss_map.hpp) in the same directory: key ReliefKey(..., GlossMethod(class), kGlossMapSize), one file
// "<key>.gloss" each: the material texture of a texture without height, EAC RG11 with all mips (R the metal mask,
// G the gloss: GlossMaterial); the same writing and checks as the relief maps.
std::filesystem::path GlossFile(const std::filesystem::path &directory, uint64_t key);
bool WriteGlossMap(const std::filesystem::path &directory, uint64_t key, const NormalMap &map);
std::optional<NormalMap> ReadGlossMap(const std::filesystem::path &directory, uint64_t key, std::string *error = nullptr);
bool HasGlossMap(const std::filesystem::path &directory, uint64_t key);

} // namespace storm::fx
