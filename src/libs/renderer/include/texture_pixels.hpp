#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace storm::render {
enum class PixelOrder { rgba8, bgra8 };
struct TexturePixels {
    std::span<const uint8_t> bytes;
    uint32_t width=0,height=0;
    size_t pitch=0;
    PixelOrder order=PixelOrder::rgba8;
};
// CPU read capability, not a promise of synchronous GPU readback. UV follows
// SKY's legacy endpoint mapping floor((size-1)*clamp(uv)), including 1 -> last.
inline bool sampleArgb(const TexturePixels& image,float u,float v,uint32_t& result) {
    if(!image.width||!image.height||!std::isfinite(u)||!std::isfinite(v)||
       image.pitch<size_t(image.width)*4||image.pitch>SIZE_MAX/image.height||
       image.bytes.size()<image.pitch*image.height) return false;
    const auto x=uint32_t(float(image.width-1)*std::clamp(u,0.f,1.f));
    const auto y=uint32_t(float(image.height-1)*std::clamp(v,0.f,1.f));
    const auto* pixel=image.bytes.data()+size_t(y)*image.pitch+size_t(x)*4;
    const uint8_t r=pixel[image.order==PixelOrder::rgba8?0:2];
    const uint8_t b=pixel[image.order==PixelOrder::rgba8?2:0];
    result=(uint32_t(pixel[3])<<24)|(uint32_t(r)<<16)|(uint32_t(pixel[1])<<8)|b;
    return true;
}
class TextureColorReader {
public:
    virtual ~TextureColorReader()=default;
    // False means unavailable (invalid ID, unsupported format or no CPU copy).
    // Caller chooses a fallback; implementations must leave result untouched.
    virtual bool sampleTextureArgb(int32_t texture,float u,float v,uint32_t& result)=0;
};
}
