// The relief maps on the device: one file per texture in the relief directory (the engine's cache root,
// fs::GetCachePath(), + "/relief": on Android the app's cache, cleared with it; engine.ini relief_dir overrides).
// The game makes them from its files with DeepBump while it runs (src/libs/relief, renderer_bgfx
// effects/relief_generator.hpp) and reads them. Derived from the player's files: never ship or commit them.
//
// A texture's key covers the method and the map's options and the texture itself: its normalized path and the bytes of
// its first mip level no larger than kReliefProbeSize, or its smallest (the game may skip top mips, texture_degradation, so the top level
// is not the same on every device; that level is). Its file holds the map as EAC RG11 with all mips.
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
// Larger side of the prepared maps (the owner's choice: DeepBump at 512; the GPU's bilinear filter upsamples them over
// the textures, Высокая uploads them whole, Средняя from the next mip).
inline constexpr int kReliefMapSize = 512;
// Tag of the height maps ("bump" textures) the game derives itself (DeriveNormalMap) into the same directory.
inline constexpr uint32_t kReliefHeightMethod = 0x48000001u;
// Bump when the prepared maps change in a way ReliefMethod cannot see (DeepBump's post-processing, the file layout).
// The model and the height maps' derivation are in ReliefMethod. 5: DeepBump's own strength (no rescale to our
// luminance derivation, the owner's choice).
inline constexpr uint32_t kReliefMethodVersion = 5;

// The method of a texture's map: everything its making depends on, so a change never reuses a map made before it.
// DeepBump maps (the same for every colour class): kReliefMethodVersion, the model (kDeepBumpModelDigest) and the map
// size; height maps: kReliefHeightMethod and their
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

} // namespace storm::fx
