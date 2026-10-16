#pragma once
#include "textured_mesh.hpp"
#include <algorithm>
namespace storm::weather {
struct SkyMesh {
    std::array<render::TexturedVertex,40> vertices;
    std::array<uint16_t,60> indices;
};
inline SkyMesh makeSkyMesh(float size) {
    if(!std::isfinite(size)) throw std::invalid_argument("Invalid sky size");
    constexpr float lo=1.f/1024.f, hi=1.f-lo;
    constexpr std::array<std::array<float,3>,8> corners{{{-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1},
                                                      {-1,1,-1},{1,1,-1},{1,1,1},{-1,1,1}}};
    constexpr std::array<uint8_t,20> order{1,5,4,0,2,6,5,1,3,7,6,2,0,4,7,3,5,6,7,4};
    constexpr std::array<std::array<float,2>,4> sideUv{{{hi,1},{hi,lo},{lo,lo},{lo,1}}};
    constexpr std::array<std::array<float,2>,4> topUv{{{lo,lo},{lo,hi},{hi,hi},{hi,lo}}};
    SkyMesh mesh{};
    for(size_t i=0;i<20;++i) {
        const auto& p=corners[order[i]];
        const auto& uv=i<16?sideUv[i%4]:topUv[i%4];
        auto& vertex=mesh.vertices[i];
        vertex={p[0]*size,p[1]*size,p[2]*size,0xffffffffu,uv[0],uv[1],uv[0],uv[1]};
        // Populate both UV sets before copying the reflection hemisphere.
        mesh.vertices[i+20]=vertex;
        mesh.vertices[i+20].y=-vertex.y;
    }
    for(uint16_t i=0;i<10;++i) {
        const uint16_t base=uint16_t(i*4);
        const std::array<uint16_t,6> face{base,uint16_t(base+1),uint16_t(base+2),base,uint16_t(base+2),uint16_t(base+3)};
        std::copy(face.begin(),face.end(),mesh.indices.begin()+i*6);
    }
    return mesh;
}
}
