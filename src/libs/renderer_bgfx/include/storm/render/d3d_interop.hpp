// Bridges for code that holds shim-backed D3D objects and needs the bgfx side.
#pragma once

#include <cstdint>

struct IDirect3DBaseTexture9;

namespace storm::render
{
// Uploads the texture if needed and returns its bgfx::TextureHandle::idx (0xffff on failure)
// with the size of the top mip level.
uint16_t BgfxTextureOf(IDirect3DBaseTexture9 *texture, uint16_t *width, uint16_t *height);
} // namespace storm::render
