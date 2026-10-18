#include "legacy_textured_mesh.hpp"
#include "dx9render.h"
#include <cstring>
#include <string>
namespace storm::render {
namespace {
class LegacyTexturedMesh final: public TexturedMesh {
    VDX9RENDER& device_;
    int32_t vb_=-1,ib_=-1;
    size_t vertexCount_=0,indexCount_=0;
public:
    explicit LegacyTexturedMesh(VDX9RENDER& device):device_(device) {}
    ~LegacyTexturedMesh() override {
        if(vb_>=0) device_.ReleaseVertexBuffer(vb_);
        if(ib_>=0) device_.ReleaseIndexBuffer(ib_);
    }
    void update(std::span<const TexturedVertex> vertices,std::span<const uint16_t> indices) override {
        validateTexturedMesh(vertices,indices);
        const int32_t vb=device_.CreateVertexBuffer(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2,vertices.size_bytes(),D3DUSAGE_WRITEONLY);
        int32_t ib=-1;
        try {
            ib=device_.CreateIndexBuffer(indices.size_bytes());
            if(vb<0||ib<0) throw std::runtime_error("Textured mesh allocation failed");
            auto* data=device_.LockVertexBuffer(vb);
            if(!data) throw std::runtime_error("Textured vertex upload failed");
            std::memcpy(data,vertices.data(),vertices.size_bytes()); device_.UnLockVertexBuffer(vb);
            data=device_.LockIndexBuffer(ib);
            if(!data) throw std::runtime_error("Textured index upload failed");
            std::memcpy(data,indices.data(),indices.size_bytes()); device_.UnLockIndexBuffer(ib);
        } catch(...) {
            if(vb>=0) device_.ReleaseVertexBuffer(vb);
            if(ib>=0) device_.ReleaseIndexBuffer(ib);
            throw;
        }
        if(vb_>=0) device_.ReleaseVertexBuffer(vb_);
        if(ib_>=0) device_.ReleaseIndexBuffer(ib_);
        vb_=vb;ib_=ib;vertexCount_=vertices.size();indexCount_=indices.size();
    }
    bool supportsMaterial(std::string_view name) const override {return !name.empty();}
    void submit(const TexturedDraw& draw) override {
        validateTexturedDraw(draw,indexCount_);
        if(vb_<0||ib_<0) throw std::logic_error("Textured mesh requires upload");
        D3DMATRIX world;std::memcpy(&world,draw.world.data(),sizeof(world));
        device_.SetTransform(D3DTS_WORLD,&world);
        device_.SetRenderState(D3DRS_TEXTUREFACTOR,draw.textureFactor);
        const std::string technique(draw.material);
        if(device_.TechniqueExecuteStart(technique.c_str())) do {
            for(const auto& range:draw.ranges) {
                device_.TextureSet(0,range.texture0);
                device_.TextureSet(1,range.texture1);
                device_.DrawBuffer(vb_,sizeof(TexturedVertex),ib_,0,vertexCount_,range.firstIndex,range.indexCount/3);
            }
        } while(device_.TechniqueExecuteNext());
    }
};
}
std::unique_ptr<TexturedMesh> makeLegacyTexturedMesh(VDX9RENDER& device) {return std::make_unique<LegacyTexturedMesh>(device);}
}
