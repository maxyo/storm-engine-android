#pragma once
#include "dynamic_vertex_stream.hpp"
#include <bgfx/bgfx.h>
namespace storm::render {
class BgfxDynamicVertexStream final:public DynamicVertexStream {
public:
    BgfxDynamicVertexStream();
    ~BgfxDynamicVertexStream() override;
    BgfxDynamicVertexStream(const BgfxDynamicVertexStream&)=delete;
    BgfxDynamicVertexStream& operator=(const BgfxDynamicVertexStream&)=delete;
    void upload(std::span<const std::byte>) override;
    VertexBufferId id() const override;
    // Backend material submission binds this resource, not a synthetic D3D VB.
    void bind(uint8_t stream=0,uint32_t first=0,uint32_t count=UINT32_MAX) const;
private:
    bgfx::VertexLayout layout_;
    bgfx::DynamicVertexBufferHandle buffer_=BGFX_INVALID_HANDLE;
    uint32_t count_=0;
};
}
