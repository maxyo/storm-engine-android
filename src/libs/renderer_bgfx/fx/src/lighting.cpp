#include "storm/fx/lighting.hpp"

#include "gpu_util.hpp"

namespace storm::fx
{

Lighting::~Lighting()
{
    Shutdown();
}

void Lighting::Shutdown()
{
    detail::Destroy(u_light_);
}

void Lighting::OnDeviceLost()
{
    u_light_ = BGFX_INVALID_HANDLE;
}

void Lighting::Apply(const LightingUniforms &uniforms)
{
    if (!bgfx::isValid(u_light_))
        u_light_ = bgfx::createUniform("u_fxLight", bgfx::UniformType::Vec4, 5);
    bgfx::setUniform(u_light_, uniforms.u_fxLight.data(), 5);
}

} // namespace storm::fx
