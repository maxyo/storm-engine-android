#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

namespace storm::render {
// CPU boundary format; packed ARGB is a value, not backend byte-order storage.
struct ColorVertex { float x, y, z; uint32_t argb; };
static_assert(sizeof(ColorVertex)==16);
struct ColorDraw {
    std::span<const float,16> world;
    std::string_view material="SkyFog";
    uint16_t view=0;
};
// Owns GPU resources. Destroy before the device. No FVF, COM, mapped GPU memory
// or backend handles escape this boundary. Update copies input before returning.
class ColoredMesh {
public:
    virtual ~ColoredMesh()=default;
    // Nonempty indices replace topology; empty indices preserve it. First update
    // must provide triangle-list indices. Backends reject unsupported materials.
    virtual void update(std::span<const ColorVertex>,std::span<const uint16_t> indices={})=0;
    virtual bool supportsMaterial(std::string_view) const=0;
    virtual void submit(const ColorDraw&)=0;
};
inline void validateColorMesh(std::span<const ColorVertex> vertices,std::span<const uint16_t> indices) {
    if(vertices.empty()||vertices.size()>65536 || indices.size()>UINT32_MAX || indices.size()%3)
        throw std::invalid_argument("Invalid colored triangle mesh size");
    for(const auto& v:vertices) if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))
        throw std::invalid_argument("Non-finite colored mesh position");
    for(auto index:indices) if(index>=vertices.size()) throw std::invalid_argument("Colored mesh index out of bounds");
}
inline void validateColorDraw(const ColorDraw& draw) {
    for(float value:draw.world) if(!std::isfinite(value)) throw std::invalid_argument("Non-finite colored mesh transform");
    if(draw.material.empty()) throw std::invalid_argument("Missing colored mesh material");
}
inline bool isSkyFogMaterial(std::string_view name) {
    constexpr std::string_view expected="skyfog";
    if(name.size()!=expected.size()) return false;
    for(size_t i=0;i<name.size();++i) {
        char ch=name[i]; if(ch>='A'&&ch<='Z') ch=char(ch-'A'+'a');
        if(ch!=expected[i]) return false;
    }
    return true;
}
}
