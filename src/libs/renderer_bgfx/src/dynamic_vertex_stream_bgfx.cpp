#include "dynamic_vertex_stream_bgfx.hpp"
#include <cstring>
#include <vector>
namespace storm::render {
BgfxDynamicVertexStream::BgfxDynamicVertexStream() {
    layout_.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true)
        .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
    if(layout_.getStride()!=skinnedVertexStride) throw std::logic_error("Skinned layout mismatch");
}
BgfxDynamicVertexStream::~BgfxDynamicVertexStream() {if(bgfx::isValid(buffer_)) bgfx::destroy(buffer_);}
VertexBufferId BgfxDynamicVertexStream::id() const {return {bgfx::isValid(buffer_)?int32_t(buffer_.idx):-1};}
void BgfxDynamicVertexStream::upload(std::span<const std::byte> bytes) {
    const auto count=skinnedVertexCount(bytes);
    std::vector<std::byte> rgba(bytes.begin(),bytes.end());
    for(uint32_t i=0;i<count;++i) {
        const size_t offset=size_t(i)*skinnedVertexStride+skinnedColorOffset;
        uint32_t argb;std::memcpy(&argb,bytes.data()+offset,sizeof(argb));
        const uint8_t color[]{uint8_t(argb>>16),uint8_t(argb>>8),uint8_t(argb),uint8_t(argb>>24)};
        std::memcpy(rgba.data()+offset,color,sizeof(color));
    }
    if(!bgfx::isValid(buffer_)||count_!=count) {
        const auto next=bgfx::createDynamicVertexBuffer(count,layout_);
        if(!bgfx::isValid(next)) throw std::runtime_error("bgfx dynamic vertex allocation failed");
        if(bgfx::isValid(buffer_)) bgfx::destroy(buffer_);
        buffer_=next;count_=count;
    }
    bgfx::update(buffer_,0,bgfx::copy(rgba.data(),uint32_t(rgba.size())));
}
void BgfxDynamicVertexStream::bind(uint8_t stream,uint32_t first,uint32_t count) const {
    if(!bgfx::isValid(buffer_)) throw std::logic_error("Dynamic stream requires upload");
    if(first>=count_ || (count!=UINT32_MAX && count>count_-first)) throw std::invalid_argument("Dynamic stream range out of bounds");
    bgfx::setVertexBuffer(stream,buffer_,first,count);
}
}
