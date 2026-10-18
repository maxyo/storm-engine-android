#include "legacy_colored_mesh.hpp"
#include "dx9render.h"
#include <cstring>
#include <string>
#include <vector>

namespace storm::render {
namespace {
class LegacyColoredMesh final:public ColoredMesh {
    VDX9RENDER& device_;
    int32_t vertices_=-1,indices_=-1;
    size_t vertexCount_=0;
    std::vector<uint16_t> topology_;
public:
    explicit LegacyColoredMesh(VDX9RENDER& device):device_(device) {}
    ~LegacyColoredMesh() override {
        if(vertices_>=0) device_.ReleaseVertexBuffer(vertices_);
        if(indices_>=0) device_.ReleaseIndexBuffer(indices_);
    }
    void update(std::span<const ColorVertex> vertices,std::span<const uint16_t> indices) override {
        validateColorMesh(vertices,indices.empty()?std::span<const uint16_t>(topology_):indices);
        if(indices.empty()&&topology_.empty()) throw std::invalid_argument("First mesh update needs topology");
        std::vector<uint16_t> newTopology(indices.begin(),indices.end());
        // Allocate replacements before discarding the previous valid resources.
        const bool newVertices=vertices_<0||vertexCount_!=vertices.size();
        const bool newIndices=!indices.empty();
        int32_t vb=vertices_,ib=indices_;
        try {
            if(newVertices) vb=device_.CreateVertexBuffer(D3DFVF_XYZ|D3DFVF_DIFFUSE,vertices.size_bytes(),D3DUSAGE_WRITEONLY);
            if(newIndices) ib=device_.CreateIndexBuffer(indices.size_bytes());
            if(vb<0||ib<0) throw std::runtime_error("Legacy colored mesh allocation failed");
            auto* data=device_.LockVertexBuffer(vb);
            if(!data) throw std::runtime_error("Legacy colored mesh vertex upload failed");
            std::memcpy(data,vertices.data(),vertices.size_bytes()); device_.UnLockVertexBuffer(vb);
            if(newIndices) {
                data=device_.LockIndexBuffer(ib);
                if(!data) throw std::runtime_error("Legacy colored mesh index upload failed");
                std::memcpy(data,indices.data(),indices.size_bytes()); device_.UnLockIndexBuffer(ib);
            }
        } catch(...) {
            if(newVertices&&vb>=0) device_.ReleaseVertexBuffer(vb);
            if(newIndices&&ib>=0) device_.ReleaseIndexBuffer(ib);
            throw;
        }
        if(newVertices&&vertices_>=0) device_.ReleaseVertexBuffer(vertices_);
        if(newIndices&&indices_>=0) device_.ReleaseIndexBuffer(indices_);
        vertices_=vb; indices_=ib; vertexCount_=vertices.size();
        if(newIndices) topology_=std::move(newTopology);
    }
    // The legacy technique loader resolves names; preserve game/mod overrides.
    bool supportsMaterial(std::string_view name) const override {return !name.empty();}
    void submit(const ColorDraw& draw) override {
        validateColorDraw(draw);
        if(vertices_<0||indices_<0) throw std::logic_error("Colored mesh requires update before submit");
        D3DMATRIX world; std::memcpy(&world,draw.world.data(),sizeof(world));
        device_.SetTransform(D3DTS_WORLD,&world);
        const std::string technique(draw.material);
        device_.DrawBuffer(vertices_,sizeof(ColorVertex),indices_,0,vertexCount_,0,topology_.size()/3,technique.c_str());
    }
};
}
std::unique_ptr<ColoredMesh> makeLegacyColoredMesh(VDX9RENDER& device) {return std::make_unique<LegacyColoredMesh>(device);}
}
