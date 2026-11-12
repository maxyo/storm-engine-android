#include "effects/shadows.hpp"

#include "shim/device.hpp"
#include "storm/render/frame_owner.hpp"

namespace storm::effects
{

namespace
{
// Shadowed region: a sphere of this radius whose centre lies this far in front of the camera (the third-person
// cameras look at the player from a few metres behind), casters up to kCasterReach towards the sun (masts, palms).
constexpr float kRadius = 40.0f;
constexpr float kAhead = 18.0f;
constexpr float kCasterReach = 120.0f;
constexpr bgfx::ViewId kShadowView = render::kPreGameViewBegin;
} // namespace

bool SunShadows::prepare(const std::array<float, 16> &view)
{
    if (prepared_)
        return active_;
    prepared_ = true;
    active_ = false;
    if (!enabled() || !sun_present_)
        return false;
    // Camera position -t * R^T and forward (+z of the row-vector view: the third column).
    fx::ShadowFrame frame;
    frame.view = kShadowView;
    frame.sun_dir = {sun_dir_[0], sun_dir_[1], sun_dir_[2]};
    for (int i = 0; i < 3; ++i)
    {
        const float eye = -(view[12] * view[i * 4 + 0] + view[13] * view[i * 4 + 1] + view[14] * view[i * 4 + 2]);
        frame.focus[i] = eye + view[i * 4 + 2] * kAhead;
    }
    frame.radius = kRadius;
    frame.caster_reach = kCasterReach;
    active_ = shadows_.Begin(settings_, frame);
    return active_;
}

void SunShadows::find_shaders()
{
    if (fs_caster_)
        return;
    vs_caster_[0] = shim::shader_id_by_name("vs_ff_caster");
    vs_caster_[1] = shim::shader_id_by_name("vs_ff_caster_skin");
    fs_caster_ = shim::shader_id_by_name("fs_ff_caster");
}

bool SunShadows::prepare_programs(shim::Device &device, const fx::Settings &settings, int &budget)
{
    if (settings.shadows == fx::ShadowQuality::Off)
        return true;
    find_shaders();
    return device.prepare_program(vs_caster_[0], fs_caster_, budget) &&
           device.prepare_program(vs_caster_[1], fs_caster_, budget);
}

void SunShadows::submit_caster(shim::Device &device, const float *world, bool skinned)
{
    find_shaders();
    const bgfx::ProgramHandle program = device.program(vs_caster_[skinned ? 1 : 0], fs_caster_); // logs a failure
    // The receiver's scissor and stencil stay with the receiver.
    bgfx::setScissor(UINT16_MAX);
    bgfx::setStencil(BGFX_STENCIL_NONE);
    bgfx::setTransform(world);
    bgfx::setState(fx::Shadows::kCasterState);
    if (bgfx::isValid(program))
        bgfx::submit(kShadowView, program);
    else
        bgfx::discard();
}

void SunShadows::end_frame()
{
    prepared_ = false;
    active_ = false;
}

} // namespace storm::effects
