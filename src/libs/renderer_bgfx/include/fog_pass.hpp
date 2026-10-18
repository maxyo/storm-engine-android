#pragma once
#include <bgfx/bgfx.h>
#include <vector>
#include "fog_sphere_mesh.hpp"

namespace storm::render {
// bgfx must be initialized by the owner. Destroy passes before bgfx::shutdown.
// Input is the same script-colored sphere consumed by weather/sky.cpp.
class FogPass final : public ColoredMesh {
public:
    FogPass(const bgfx::Memory* vertexShader, const bgfx::Memory* fragmentShader);
    ~FogPass() override;
    FogPass(const FogPass&) = delete;
    FogPass& operator=(const FogPass&) = delete;
    // First update must include indices (makeFogSphere's default). Subsequent
    // updates replace vertex colors/positions; sphere topology stays fixed.
    void update(const weather::FogMesh& mesh) { update(mesh.vertices, mesh.indices); }
    void update(std::span<const ColorVertex> vertices, std::span<const uint16_t> indices={}) override;
    bool supportsMaterial(std::string_view name) const override { return isSkyFogMaterial(name); }
    void submit(const ColorDraw& draw) override;
    void submit(bgfx::ViewId view, const float* world, bool enabled = true);
private:
    bgfx::VertexLayout layout_;
    uint32_t vertexCount_=0;
    std::vector<uint16_t> topology_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices_ = BGFX_INVALID_HANDLE;
};
}
