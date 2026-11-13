// Mip chains made from a texture's top level. The game's .tx files carry their own mips, but almost every chain of
// the world textures stops at 64x64 (the GPU then samples that level for everything smaller: distant walls, ground and
// islands shimmer), they were averaged in gamma space (high-contrast detail turns darker in the distance), colour of
// cut-out texels bleeds into the edges of foliage and rigging, and cut-out shapes thin out level by level. The
// transcode workers (service.hpp) therefore rebuild every DXT texture's chain from its top level down to 1x1:
//   * colour is averaged in linear light (sRGB transfer function), weighted by alpha;
//   * a cut-out alpha (mostly 0 or 1) keeps the share of texels that pass the game's alpha test
//     (coverage-preserving alpha, Castaño, "Computing Alpha Mipmaps", 2010);
//   * any other alpha is averaged.
#pragma once

#include <cstdint>
#include <vector>

namespace storm::texture
{

// The game's alpha reference for its alpha-tested models (Renderer::SetCommonStates: D3DRS_ALPHAREF 0xa0).
inline constexpr uint8_t kCutoutReference = 160;

// Levels of a complete chain for a width x height texture (down to 1x1).
uint32_t full_mip_levels(uint32_t width, uint32_t height);

// True when the alpha of `texels` RGBA8 texels is a cut-out mask: both sides of kCutoutReference occur and at most a
// small share of the texels lies in between.
bool is_cutout_alpha(const uint8_t *rgba, size_t texels);

// Levels 1 .. levels-1 (each RGBA8, R first, max(w >> l, 1) x max(h >> l, 1)) of the chain whose level 0 is `rgba`
// (width x height RGBA8). `cutout`: preserve the alpha-test coverage of level 0 (see is_cutout_alpha).
std::vector<std::vector<uint8_t>> build_mip_chain(const uint8_t *rgba, uint32_t width, uint32_t height, uint32_t levels,
                                                  bool cutout);

} // namespace storm::texture
