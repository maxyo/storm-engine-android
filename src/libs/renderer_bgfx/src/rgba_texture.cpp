#include "rgba_texture.hpp"
#include <stdexcept>
#include <cstring>
namespace storm::render {
RgbaTexture::RgbaTexture(const TexturePixels& pixels) {
    uint32_t unused;
    if(!sampleArgb(pixels,0,0,unused)||pixels.width>UINT16_MAX||pixels.height>UINT16_MAX||
       uint64_t(pixels.width)*pixels.height*4>UINT32_MAX)
        throw std::invalid_argument("Invalid decoded texture upload");
    if(pixels.width>bgfx::getCaps()->limits.maxTextureSize||pixels.height>bgfx::getCaps()->limits.maxTextureSize)
        throw std::invalid_argument("Texture dimensions exceed device capability");
    if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA8,0))
        throw std::runtime_error("RGBA8 texture upload unsupported");
    width_=pixels.width; height_=pixels.height;
    rgba_.resize(size_t(width_)*height_*4);
    for(uint32_t y=0;y<height_;++y) {
        auto* row=rgba_.data()+size_t(y)*width_*4;
        std::memcpy(row,pixels.bytes.data()+size_t(y)*pixels.pitch,size_t(width_)*4);
        if(pixels.order==PixelOrder::bgra8)
            for(uint32_t x=0;x<width_;++x) std::swap(row[x*4],row[x*4+2]);
    }
    texture_=bgfx::createTexture2D(uint16_t(width_),uint16_t(height_),false,1,bgfx::TextureFormat::RGBA8,
        0,bgfx::copy(rgba_.data(),uint32_t(rgba_.size())));
    if(!bgfx::isValid(texture_)) throw std::runtime_error("RGBA8 texture allocation failed");
}
RgbaTexture::~RgbaTexture() {if(bgfx::isValid(texture_)) bgfx::destroy(texture_);}
bool RgbaTexture::sample(float u,float v,uint32_t& color) const {
    return sampleArgb({rgba_,width_,height_,size_t(width_)*4,PixelOrder::rgba8},u,v,color);
}
}
