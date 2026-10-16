#pragma once
#include <bgfx/bgfx.h>
#include "fog_sphere_mesh.hpp"

namespace storm::render {
// bgfx must be initialized by the owner. Destroy passes before bgfx::shutdown.
// Input is the same script-colored sphere consumed by weather/sky.cpp.
class FogPass final {
public:
    FogPass(const bgfx::Memory* vertexShader, const bgfx::Memory* fragmentShader);
    ~FogPass();
    FogPass(const FogPass&) = delete;
    FogPass& operator=(const FogPass&) = delete;
    // First update must include indices (makeFogSphere's default). Subsequent
    // updates replace vertex colors/positions; sphere topology stays fixed.
    void update(const weather::FogMesh& mesh);
    void submit(bgfx::ViewId view, const float* world, bool enabled = true);
private:
    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle vertices_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices_ = BGFX_INVALID_HANDLE;
};
}
