#include "effects/shadows.hpp"

#include "shim/device.hpp"
#include "storm/fx/shadow_model.hpp"
#include "storm/render/frame_owner.hpp"

#include <bx/math.h>

#include <cmath>

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
// Static map: the location geometry around the player (Средняя 1024², Высокая 2048²: 16 / 8 cm texels), re-drawn
// after a walk of a quarter of its radius or a turn of the sun by half a degree.
constexpr bgfx::ViewId kStaticShadowView = render::kPreGameViewBegin + 3;
constexpr float kStaticRadius = 80.0f;
constexpr float kStaticRedrawDistance = kStaticRadius * 0.25f;
constexpr float kStaticRedrawCos = 0.99996f; // cos(0.5°)
constexpr uint8_t kStaticStage = 9;           // fx_lighting.sh s_fxShadowStatic
} // namespace

bool SunShadows::prepare(shim::Device &device, const std::array<float, 16> &view)
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
    if (active_ && !positions_.empty())
    {
        const float dx = frame.focus[0] - static_focus_[0], dz = frame.focus[2] - static_focus_[2];
        const float turn = sun_dir_[0] * static_sun_[0] + sun_dir_[1] * static_sun_[1] + sun_dir_[2] * static_sun_[2];
        if (!static_valid_ || geometry_dirty_ || dx * dx + dz * dz > kStaticRedrawDistance * kStaticRedrawDistance ||
            turn < kStaticRedrawCos)
            draw_static(device, frame.focus);
    }
    return active_;
}

void SunShadows::set_geometry(const std::vector<fx::Vec3> &positions, const std::vector<uint32_t> &indices)
{
    if (positions == positions_ && indices == indices_)
        return;
    positions_ = positions;
    indices_ = indices;
    geometry_dirty_ = true;
    static_valid_ = false;
}

void SunShadows::draw_static(shim::Device &device, const fx::Vec3 &focus)
{
    const int size = settings_.shadows == fx::ShadowQuality::Soft ? 2048 : 1024;
    if (!bgfx::isValid(static_fb_) || static_size_ != size)
    {
        if (bgfx::isValid(static_fb_))
            bgfx::destroy(static_fb_);
        static_map_ = bgfx::createTexture2D(static_cast<uint16_t>(size), static_cast<uint16_t>(size), false, 1,
                                            bgfx::TextureFormat::D16,
                                            BGFX_TEXTURE_RT | BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        static_fb_ = bgfx::createFrameBuffer(1, &static_map_, true);
        static_size_ = size;
    }
    if (geometry_dirty_ || !bgfx::isValid(static_vb_))
    {
        if (bgfx::isValid(static_vb_))
            bgfx::destroy(static_vb_);
        if (bgfx::isValid(static_ib_))
            bgfx::destroy(static_ib_);
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        static_vb_ = bgfx::createVertexBuffer(
            bgfx::copy(positions_.data(), static_cast<uint32_t>(positions_.size() * sizeof(fx::Vec3))), layout);
        static_ib_ = bgfx::createIndexBuffer(
            bgfx::copy(indices_.data(), static_cast<uint32_t>(indices_.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);
        geometry_dirty_ = false;
    }
    const fx::ShadowSetup setup =
        fx::ComputeShadowSetup({sun_dir_[0], sun_dir_[1], sun_dir_[2]}, focus, kStaticRadius, kCasterReach, size);
    const bgfx::Caps *caps = bgfx::getCaps();
    std::array<float, 16> projection = setup.projection;
    if (caps->homogeneousDepth)
    {
        projection[10] *= 2.0f;
        projection[14] = projection[14] * 2.0f - 1.0f;
    }
    find_shaders();
    const bgfx::ProgramHandle program = device.program(vs_caster_[0], fs_caster_); // logs a failure
    bgfx::setViewName(kStaticShadowView, "fx sun shadow map (location)");
    bgfx::setViewMode(kStaticShadowView, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(kStaticShadowView, static_fb_);
    bgfx::setViewRect(kStaticShadowView, 0, 0, static_cast<uint16_t>(size), static_cast<uint16_t>(size));
    bgfx::setViewClear(kStaticShadowView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(kStaticShadowView, setup.view.data(), projection.data());
    bgfx::touch(kStaticShadowView);
    if (bgfx::isValid(program))
    {
        bgfx::setVertexBuffer(0, static_vb_);
        bgfx::setIndexBuffer(static_ib_);
        bgfx::setState(fx::Shadows::kCasterState);
        bgfx::submit(kStaticShadowView, program);
    }
    float world_to_clip[16];
    bx::mtxMul(world_to_clip, setup.view.data(), setup.projection.data());
    const float flip = caps->originBottomLeft ? 0.5f : -0.5f;
    const float bias[16] = {0.5f, 0.0f, 0.0f, 0.0f, 0.0f, flip, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f};
    bx::mtxMul(world_to_static_.data(), world_to_clip, bias);
    const float depth_range = 2.0f * kStaticRadius + kCasterReach;
    static_params_ = {1.0f / static_cast<float>(size), 1.0f, 1.5f * setup.texel_world, 1.5f * setup.texel_world / depth_range};
    static_view_ = {};
    static_focus_ = focus;
    static_sun_ = {sun_dir_[0], sun_dir_[1], sun_dir_[2]};
    static_valid_ = true;
    if (!static_reported_)
        shim::logf("effects: sun shadows: the location's still geometry (%zu triangles) casts into a %d² static map over "
                   "%.0f m around the player",
                   indices_.size() / 3, size, kStaticRadius);
    static_reported_ = true;
}

void SunShadows::bind(const std::array<float, 16> &view)
{
    shadows_.Bind(view);
    if (!bgfx::isValid(u_static_))
    {
        s_static_ = bgfx::createUniform("s_fxShadowStatic", bgfx::UniformType::Sampler);
        u_static_mtx_ = bgfx::createUniform("u_fxShadowStaticMtx", bgfx::UniformType::Mat4);
        u_static_ = bgfx::createUniform("u_fxShadowStatic", bgfx::UniformType::Vec4);
        const uint16_t one = 0xffff;
        open_map_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::D16, BGFX_SAMPLER_COMPARE_LEQUAL,
                                          bgfx::copy(&one, sizeof(one)));
    }
    const bool on = static_valid_ && !positions_.empty();
    if (on && view != static_view_)
    {
        float inverse_view[16];
        bx::mtxInverse(inverse_view, view.data());
        bx::mtxMul(view_to_static_.data(), inverse_view, world_to_static_.data());
        static_view_ = view;
    }
    const std::array<float, 4> off{0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_static_mtx_, view_to_static_.data());
    bgfx::setUniform(u_static_, on ? static_params_.data() : off.data());
    bgfx::setTexture(kStaticStage, s_static_, on ? static_map_ : open_map_,
                     BGFX_SAMPLER_COMPARE_LEQUAL | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
}

void SunShadows::shutdown()
{
    shadows_.Shutdown();
    if (bgfx::isValid(static_fb_))
        bgfx::destroy(static_fb_);
    if (bgfx::isValid(open_map_))
        bgfx::destroy(open_map_);
    if (bgfx::isValid(static_vb_))
        bgfx::destroy(static_vb_);
    if (bgfx::isValid(static_ib_))
        bgfx::destroy(static_ib_);
    for (bgfx::UniformHandle *u : {&s_static_, &u_static_mtx_, &u_static_})
        if (bgfx::isValid(*u))
            bgfx::destroy(*u);
    on_device_lost();
}

void SunShadows::on_device_lost()
{
    shadows_.OnDeviceLost();
    static_fb_ = BGFX_INVALID_HANDLE;
    static_map_ = open_map_ = BGFX_INVALID_HANDLE;
    static_vb_ = BGFX_INVALID_HANDLE;
    static_ib_ = BGFX_INVALID_HANDLE;
    s_static_ = u_static_mtx_ = u_static_ = BGFX_INVALID_HANDLE;
    static_valid_ = false;
    geometry_dirty_ = true;
    static_size_ = 0;
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
