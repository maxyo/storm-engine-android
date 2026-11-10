// Bridges for code that holds shim-backed D3D objects and needs the bgfx side.
#pragma once

#include <cstdint>

struct IDirect3DBaseTexture9;

namespace storm::render
{
// Uploads the texture if needed and returns its bgfx::TextureHandle::idx (0xffff on failure)
// with the size of the top mip level.
uint16_t BgfxTextureOf(IDirect3DBaseTexture9 *texture, uint16_t *width, uint16_t *height);

// The texture's contents are final (loaded from a file): the shim drops its CPU copy once the GPU
// texture exists. A later LockRect is a bug, reported as a TRAP (the level reads as zero).
void MarkTextureImmutable(IDirect3DBaseTexture9 *texture);
} // namespace storm::render
