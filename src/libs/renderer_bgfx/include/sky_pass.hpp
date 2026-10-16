#pragma once
#include "textured_mesh.hpp"
#include "texture_store.hpp"
namespace storm::render {
class SkyPass final: public TexturedMesh {
public:
    // Store and bgfx context outlive this pass; all resources are backend-owned.
    SkyPass(TextureStore&,const bgfx::Memory* vs,const bgfx::Memory* fs);
    ~SkyPass() override;
    SkyPass(const SkyPass&)=delete;
    SkyPass& operator=(const SkyPass&)=delete;
    void update(std::span<const TexturedVertex>,std::span<const uint16_t>) override;
    bool supportsMaterial(std::string_view name) const override {return skyMaterial(name)!=SkyMaterial::unsupported;}
    void submit(const TexturedDraw&) override;
private:
    TextureStore& textures_;
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_=BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vb_=BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ib_=BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texture0_=BGFX_INVALID_HANDLE,texture1_=BGFX_INVALID_HANDLE,parameters_=BGFX_INVALID_HANDLE;
    size_t indexCount_=0;
};
}
