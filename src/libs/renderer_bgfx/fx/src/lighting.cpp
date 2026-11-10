#include "storm/fx/lighting.hpp"

#include "gpu_util.hpp"
#include "storm/fx/log.hpp"

#include <algorithm>
#include <utility>

namespace storm::fx
{

Lighting::~Lighting()
{
    Shutdown();
}

void Lighting::Shutdown()
{
    detail::Destroy(u_light_);
    detail::Destroy(u_point_);
    detail::Destroy(u_point_count_);
}

void Lighting::OnDeviceLost()
{
    u_light_ = BGFX_INVALID_HANDLE;
    u_point_ = BGFX_INVALID_HANDLE;
    u_point_count_ = BGFX_INVALID_HANDLE;
}

void Lighting::Apply(const LightingUniforms &uniforms)
{
    if (!bgfx::isValid(u_light_))
        u_light_ = bgfx::createUniform("u_fxLight", bgfx::UniformType::Vec4, 5);
    bgfx::setUniform(u_light_, uniforms.u_fxLight.data(), 5);
}

void Lighting::Apply(const PointLightUniforms &uniforms)
{
    if (!bgfx::isValid(u_point_))
    {
        u_point_ = bgfx::createUniform("u_fxPoint", bgfx::UniformType::Vec4, kMaxPointLights * 4);
        u_point_count_ = bgfx::createUniform("u_fxPointCount", bgfx::UniformType::Vec4);
    }
    if (uniforms.dropped > 0)
    {
        dropped_ = std::max(dropped_, uniforms.dropped);
        if (!dropped_reported_)
        {
            dropped_reported_ = true;
            Log(LogLevel::Info, "a draw call is reached by %d more point lights than the budget of %d allows; the weakest are left out",
                uniforms.dropped, static_cast<int>(uniforms.u_fxPointCount[0]));
        }
    }
    const auto count = static_cast<uint16_t>(uniforms.u_fxPointCount[0]);
    if (count > 0)
        bgfx::setUniform(u_point_, uniforms.u_fxPoint.data(), static_cast<uint16_t>(count * 4));
    bgfx::setUniform(u_point_count_, uniforms.u_fxPointCount.data());
}

int Lighting::TakeDroppedLights()
{
    return std::exchange(dropped_, 0);
}

} // namespace storm::fx
