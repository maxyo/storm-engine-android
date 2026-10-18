#pragma once
#include "colored_mesh.hpp"
namespace storm::render {
struct TexturedVertex { float x,y,z; uint32_t argb; float u,v,u1,v1; };
static_assert(sizeof(TexturedVertex)==32);
struct TextureDrawRange {
    uint32_t firstIndex=0,indexCount=0;
    int32_t texture0=-1,texture1=-1; // IDs belong to the selected backend's store.
};
struct TexturedDraw {
    std::span<const float,16> world;
    std::span<const TextureDrawRange> ranges;
    std::string_view material="Sky";
    uint32_t textureFactor=0xffffffffu;
    uint16_t view=0;
};
class TexturedMesh {
public:
    virtual ~TexturedMesh()=default;
    virtual void update(std::span<const TexturedVertex>,std::span<const uint16_t>)=0;
    virtual bool supportsMaterial(std::string_view) const=0;
    // Preserve range order and technique pass ordering; input is consumed now.
    virtual void submit(const TexturedDraw&)=0;
};
inline void validateTexturedMesh(std::span<const TexturedVertex> vertices,std::span<const uint16_t> indices) {
    if(vertices.empty()||vertices.size()>65536||indices.empty()||indices.size()>UINT32_MAX||indices.size()%3)
        throw std::invalid_argument("Invalid textured triangle mesh size");
    for(const auto& v:vertices)
        for(float x:{v.x,v.y,v.z,v.u,v.v,v.u1,v.v1})
            if(!std::isfinite(x)) throw std::invalid_argument("Non-finite textured vertex");
    for(auto i:indices) if(i>=vertices.size()) throw std::invalid_argument("Textured index out of bounds");
}
inline void validateTexturedDraw(const TexturedDraw& draw,size_t indices) {
    validateColorDraw({draw.world,draw.material,draw.view});
    for(const auto& range:draw.ranges)
        if(!range.indexCount||range.indexCount%3||range.firstIndex>indices||range.indexCount>indices-range.firstIndex)
            throw std::invalid_argument("Textured draw range out of bounds");
}
enum class SkyMaterial { single,transition,alphaMask,unsupported };
inline SkyMaterial skyMaterial(std::string_view name) {
    const auto equals=[&](std::string_view expected) {
        if(name.size()!=expected.size()) return false;
        for(size_t i=0;i<name.size();++i) {
            char ch=name[i];if(ch>='A'&&ch<='Z') ch=char(ch-'A'+'a');
            if(ch!=expected[i]) return false;
        }
        return true;
    };
    if(equals("sky")) return SkyMaterial::single;
    if(equals("skyblend")) return SkyMaterial::transition;
    if(equals("skyblend_alpha")) return SkyMaterial::alphaMask;
    return SkyMaterial::unsupported;
}
}
