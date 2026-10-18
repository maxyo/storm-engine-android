#pragma once
#include "texture_pixels.hpp"
#include <bgfx/bgfx.h>
#include <vector>

namespace storm::render {
// Explicit decoded RGBA8 upload + retained CPU sampling capability. This is not
// a render target or a wrapper around arbitrary bgfx textures. Destroy before shutdown.
class RgbaTexture final {
public:
    explicit RgbaTexture(const TexturePixels& pixels);
    ~RgbaTexture();
    RgbaTexture(const RgbaTexture&)=delete;
    RgbaTexture& operator=(const RgbaTexture&)=delete;
    bool sample(float u,float v,uint32_t& argb) const;
    // Backend-only binding/readback; never exposed by the renderer-neutral reader.
    bgfx::TextureHandle handle() const {return texture_;}
private:
    uint32_t width_=0,height_=0;
    std::vector<uint8_t> rgba_;
    bgfx::TextureHandle texture_=BGFX_INVALID_HANDLE;
};
}
