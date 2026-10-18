#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
namespace storm::render {
// An ID in the selected renderer's vertex-buffer table, valid only while its
// owning stream lives. It is neither a COM pointer nor a process-global ID.
struct VertexBufferId {
    int32_t value=-1;
    explicit operator bool() const {return value>=0;}
};
// Geometry VERTEX0 layout: float3 position, float3 normal, ARGB32, float2 UV.
inline constexpr size_t skinnedVertexStride=36,skinnedColorOffset=24;
inline uint32_t skinnedVertexCount(std::span<const std::byte> bytes) {
    if(bytes.empty()||bytes.size()%skinnedVertexStride||bytes.size()>UINT32_MAX)
        throw std::invalid_argument("Invalid skinned vertex upload size");
    return uint32_t(bytes.size()/skinnedVertexStride);
}
class DynamicVertexStream {
public:
    virtual ~DynamicVertexStream()=default;
    // Copy a complete pose before returning. Reuse allocation for equal sizes.
    virtual void upload(std::span<const std::byte>)=0;
    virtual VertexBufferId id() const=0;
};
}
