// Effect textures through the engine's texture pipeline (see textures.cpp).
#pragma once

#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>

namespace storm::render::effekseer
{
using LogFn = void (*)(const char *format, ...);

// Effekseer texture loader: images are decoded once (bimg), shown as RGBA8 with mips at once and
// replaced by ETC2 from the shared texture service; later loads come from its disk cache.
Effekseer::TextureLoaderRef CreateCachedTextureLoader(const EffekseerRenderer::RendererRef &renderer,
                                                      const Effekseer::FileInterfaceRef &files, LogFn log);
// Main thread, once per frame: swaps finished ETC2 transcodes in.
void PollTextures();
// Drops pending transcodes (device shutdown).
void ReleasePendingTextures();

struct TextureStats
{
    uint32_t cache_hits = 0;   // ETC2 served from the disk cache, nothing decoded
    uint32_t decoded = 0;      // images decoded (cache miss, or ETC2 not used)
    uint32_t transcoding = 0;  // ETC2 jobs still running
};
TextureStats GetTextureStats();

} // namespace storm::render::effekseer
