#include "storm/fx/shadows.hpp"

#include "gpu_util.hpp"
#include "storm/fx/log.hpp"

#include <bx/math.h>

#include <algorithm>

namespace storm::fx
{
using namespace detail;

Shadows::~Shadows()
{
    Shutdown();
}

void Shadows::Shutdown()
{
    Destroy(target_); // owns the map texture
    map_ = BGFX_INVALID_HANDLE;
    Destroy(caster_);
    Destroy(s_map_);
    Destroy(u_matrix_);
    Destroy(u_params_);
    map_size_ = 0;
}

void Shadows::OnDeviceLost()
{
    target_ = BGFX_INVALID_HANDLE;
    map_ = BGFX_INVALID_HANDLE;
    caster_ = BGFX_INVALID_HANDLE;
    s_map_ = BGFX_INVALID_HANDLE;
    u_matrix_ = BGFX_INVALID_HANDLE;
    u_params_ = BGFX_INVALID_HANDLE;
    map_size_ = 0;
}

void Shadows::EnsureResources(int map_size)
{
    if (!bgfx::isValid(s_map_))
    {
        s_map_ = bgfx::createUniform("s_fxShadowMap", bgfx::UniformType::Sampler);
        u_matrix_ = bgfx::createUniform("u_fxShadowMtx", bgfx::UniformType::Mat4);
        u_params_ = bgfx::createUniform("u_fxShadow", bgfx::UniformType::Vec4);
    }
    if (map_size_ == map_size && bgfx::isValid(target_))
        return;
    Destroy(target_);
    // Linear filtering on a compare sampler is the hardware 2x2 percentage-closer filter.
    map_ = bgfx::createTexture2D(static_cast<uint16_t>(map_size), static_cast<uint16_t>(map_size), false, 1, bgfx::TextureFormat::D16,
                                 BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | kClampLinear);
    target_ = bgfx::createFrameBuffer(1, &map_, true);
    map_size_ = map_size;
}

bgfx::ProgramHandle Shadows::CasterProgram()
{
    if (!bgfx::isValid(caster_))
        caster_ = LoadProgram("vs_fx_shadow_caster", "fs_fx_shadow_caster");
    return caster_;
}

bool Shadows::Begin(const Settings &settings, const ShadowFrame &frame)
{
    const int size = ShadowMapSize(settings.shadows);
    params_ = {0.0f, 0.0f, 0.0f, 0.0f}; // strength 0: receivers are fully lit
    if (size == 0)
        return false;
    if (!SupportsShadowMap())
    {
        STORM_FX_ERROR_ONCE(reported_bind_, "Shadows::Begin(): no D16 depth render target on this device (Support::shadow_map)");
        return false;
    }
    EnsureResources(size);
    // A low sun gives endless, shimmering shadows; fade them out before it sets.
    const float strength = std::clamp(frame.sun_dir[1] * 8.0f, 0.0f, 1.0f) * 0.82f;
    if (strength <= 0.0f)
        return false;

    const ShadowSetup setup = ComputeShadowSetup(frame.sun_dir, frame.focus, frame.radius, frame.caster_reach, size);
    const bgfx::Caps *caps = bgfx::getCaps();
    // The setup's projection maps depth to 0..1; GL clip space wants -1..1.
    std::array<float, 16> projection = setup.projection;
    if (caps->homogeneousDepth)
    {
        projection[10] *= 2.0f;
        projection[14] = projection[14] * 2.0f - 1.0f;
    }
    bgfx::setViewName(frame.view, "fx shadow map");
    bgfx::setViewMode(frame.view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(frame.view, target_);
    bgfx::setViewRect(frame.view, 0, 0, static_cast<uint16_t>(size), static_cast<uint16_t>(size));
    bgfx::setViewClear(frame.view, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(frame.view, setup.view.data(), projection.data());
    bgfx::touch(frame.view);

    // Receivers: camera view space -> world -> light clip -> uv and 0..1 depth.
    float inverse_view[16], world_to_clip[16], view_to_clip[16];
    bx::mtxInverse(inverse_view, frame.camera_view.data());
    bx::mtxMul(world_to_clip, setup.view.data(), setup.projection.data());
    bx::mtxMul(view_to_clip, inverse_view, world_to_clip);
    const float flip = caps->originBottomLeft ? 0.5f : -0.5f;
    const float bias[16] = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, flip, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f};
    bx::mtxMul(view_to_shadow_.data(), view_to_clip, bias);
    const float depth_range = 2.0f * frame.radius + frame.caster_reach;
    params_ = {1.0f / static_cast<float>(size), strength, 1.5f * setup.texel_world, 1.5f * setup.texel_world / depth_range};
    return true;
}

void Shadows::Bind()
{
    if (!bgfx::isValid(map_))
    {
        STORM_FX_ERROR_ONCE(reported_bind_, "Shadows::Bind() before Shadows::Begin(): no shadow map exists");
        return;
    }
    bgfx::setUniform(u_matrix_, view_to_shadow_.data());
    bgfx::setUniform(u_params_, params_.data());
    bgfx::setTexture(kShadowMapStage, s_map_, map_);
}

} // namespace storm::fx
