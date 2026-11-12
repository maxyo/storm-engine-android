// Reading the pixels of a game texture from its D3D-format CPU copy (the effects derive things from them when a
// texture is loaded: normal maps, whether its alpha cuts holes).
#pragma once

#include "shim/shim.hpp"
#include "storm/fx/cutout.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace storm::effects
{

// The formats decode_rgba reads: DXT and the game's uncompressed colour formats.
bool readable_format(D3DFORMAT format);
// One level as RGBA8 (R first), `width` x `height` texels. The format must be a readable_format.
std::vector<uint8_t> decode_rgba(D3DFORMAT format, const uint8_t *data, uint32_t width, uint32_t height);

// The alpha the game's models are tested against (Renderer::SetCommonStates: D3DRS_ALPHAREF 0xa0, GREATER; the
// location's models never change it).
inline constexpr float kGameAlphaReference = 160.0f / 255.0f;

// The texture's alpha mask when its alpha cuts holes into what it covers (fx::MakeAlphaMask on a level of about 128
// texels across, against the game's alpha reference); null for opaque textures, for formats that cannot be read and
// when the level's CPU copy is gone.
std::shared_ptr<const fx::AlphaMask> texture_cutout(shim::TexStore &store);

} // namespace storm::effects
