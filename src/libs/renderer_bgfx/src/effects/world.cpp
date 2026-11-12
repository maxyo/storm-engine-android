// World passes of the graphics effects: the analytic sky, contact shadows and the improvements of the game's own sea.
// The consumer entities (SKY, shadow) call them where they would draw their legacy passes; the draws go into the shim's current game
// view in submission order, with the D3D camera of that moment as the bgfx view transform.
#include "effects/effects.hpp"

#include "shim/device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace storm::effects
{

namespace
{
// D3D clip space -> backend clip space, as Device::apply_ff_uniforms builds it for the fixed-function draws (without
// depth bias): y flipped on bottom-left-origin backends, D3D's half-pixel offset, depth 0..1 -> -1..1 where the
// backend wants it. The fx draws then land on exactly the pixels and depths of the shim's own geometry.
std::array<float, 16> backend_projection(const shim::Device &device, const D3DMATRIX &proj)
{
    const auto &context = shim::Context::instance();
    const float vw = static_cast<float>(std::max<DWORD>(1, device.s.viewport.Width));
    const float vh = static_cast<float>(std::max<DWORD>(1, device.s.viewport.Height));
    const float fy = context.flip_y() ? -1.0f : 1.0f;
    float fix[4][4] = {{1, 0, 0, 0}, {0, fy, 0, 0}, {0, 0, 1, 0}, {1.0f / vw, -fy / vh, 0, 1}};
    if (context.caps()->homogeneousDepth)
    {
        const float keep = 1.0f - 2e-6f;
        fix[2][2] = 2.0f * keep;
        fix[3][2] = -keep;
    }
    std::array<float, 16> out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k)
                out[r * 4 + c] += proj.m[r][k] * fix[k][c];
    return out;
}

// D3D perspective: _33 = f / (f - n), _43 = -n f / (f - n). Leaves the planes as they are for other matrices.
void clip_planes(const D3DMATRIX &p, float &near_plane, float &far_plane)
{
    if (p.m[2][2] != 0.0f && p.m[2][2] != 1.0f)
    {
        near_plane = -p.m[3][2] / p.m[2][2];
        far_plane = p.m[3][2] / (1.0f - p.m[2][2]);
    }
}

float seconds()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

render::EffectsWorld Host::world() const
{
    render::EffectsWorld world;
    if (!device_ready_)
        return world;
    world.contact_shadows = settings_.contact_shadows;
    world.lens_flare = settings_.lens_flare;
    world.lamp_character_shadows = settings_.lamp_shadows > 0 && lighting_.lamp_shadows().has_geometry();
    return world;
}

void Host::update_sky(float camera_height)
{
    sky_.Update(weather_input(), seconds(), camera_height);
}

bool Host::begin_world_pass(WorldPass &pass, const render::EffectsMatrix &view, bool fresh)
{
    shim::Device *device = shim::Device::current;
    if (!device || !device_ready_ || !shim::Context::instance().ready())
        return false;
    pass.view_mtx = view;
    pass.projection = backend_projection(*device, device->s.proj);
    std::array<float, 32> transform;
    std::copy(pass.view_mtx.begin(), pass.view_mtx.end(), transform.begin());
    std::copy(pass.projection.begin(), pass.projection.end(), transform.begin() + 16);
    pass.view = device->effects_view(fresh);
    // One view transform per bgfx view: a second camera in the same shim view (rare) gets a view of its own.
    if (pass.view == transform_view_ && transform != transform_)
        pass.view = device->effects_view(true);
    if (pass.view != transform_view_ || transform != transform_)
    {
        bgfx::setViewTransform(pass.view, pass.view_mtx.data(), pass.projection.data());
        transform_view_ = pass.view;
        transform_ = transform;
    }
    // Camera position: -t * R^T of the row-vector view matrix.
    const auto &v = view;
    for (int i = 0; i < 3; ++i)
        pass.camera[i] = -(v[12] * v[i * 4 + 0] + v[13] * v[i * 4 + 1] + v[14] * v[i * 4 + 2]);
    clip_planes(device->s.proj, pass.near_plane, pass.far_plane);
    camera_height_ = pass.camera[1];
    return true;
}

void Host::prepare_sea(shim::Device &device)
{
    if (!bgfx::isValid(u_sea_fx_))
    {
        u_sea_fx_ = bgfx::createUniform("u_seaFx", bgfx::UniformType::Vec4, 3);
        s_sea_depth_ = bgfx::createUniform("s_seaDepth", bgfx::UniformType::Sampler);
    }
    float near_plane = 0.1f, far_plane = 4000.0f;
    clip_planes(device.s.proj, near_plane, far_plane);
    bool soft_edges = device_ready_ && settings_.sea_soft_edges;
    if (soft_edges)
    {
        // The scene distance behind the sea: the depth of the current target, read in a view of its own before the
        // first sea draw into this target in this frame; the sea then continues in a new view on the same target.
        shim::TexStore *color = device.rt->store.get();
        shim::TexStore *depth = device.ds ? device.ds->store.get() : nullptr;
        // A multisampled target is read through its resolve textures, which hold the depth too only where the
        // backend resolves it (Support::msaa_depth; ConstrainSettings keeps the soft edges off otherwise).
        const bool readable = depth && (!color->target_flags || support_.msaa_depth);
        const uint32_t frame = shim::Context::instance().submitted_frames();
        if (!readable)
        {
            soft_edges = false;
            if (!sea_depth_logged_)
                shim::logf("ERROR: effects: the sea is drawn into a target without a readable depth: no soft sea edges "
                           "there (logged once)");
            sea_depth_logged_ = true;
        }
        else if (sea_depth_frame_ != frame || sea_depth_target_ != device.rt)
        {
            depth->sync();
            const bgfx::ViewId capture = device.effects_view(true);
            sea_depth_.Capture(capture, depth->handle, static_cast<uint16_t>(color->width),
                               static_cast<uint16_t>(color->height), near_plane, far_plane);
            device.effects_view(true);
            sea_depth_frame_ = frame;
            sea_depth_target_ = device.rt;
        }
    }
    const bool specular = device_ready_ && settings_.sea_specular && has_weather();
    const auto &sun = weather_.sun_dir;
    const float strength = settings_.sea_specular_strength;
    const float sea_fx[12] = {
        settings_.sea_band_limit ? 1.0f : 0.0f, soft_edges ? 1.0f : 0.0f, specular ? 1.0f : 0.0f, far_plane,
        sun[0], sun[1], sun[2], settings_.sea_specular_power,
        weather_.sun_color[0] * strength, weather_.sun_color[1] * strength, weather_.sun_color[2] * strength,
        1.0f / std::max(settings_.sea_edge_depth, 0.01f),
    };
    bgfx::setUniform(u_sea_fx_, sea_fx, 3);
    if (soft_edges) // otherwise the shader does not read the unit
        bgfx::setTexture(fx::kNormalMapStage, s_sea_depth_, sea_depth_.Texture(), BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
}

void Host::contact_shadow(const std::array<float, 3> &ground, const render::EffectsMatrix &view)
{
    if (!settings_.contact_shadows)
        return;
    WorldPass pass;
    if (!begin_world_pass(pass, view))
        return;
    contact_.Add(ground, 0.45f, 0.5f);
    contact_.Submit(pass.view, settings_);
}

} // namespace storm::effects

namespace storm::render
{
EffectsWorld CurrentEffectsWorld()
{
    return effects::Host::instance().world();
}

void EffectsContactShadow(const std::array<float, 3> &ground, const EffectsMatrix &view)
{
    effects::Host::instance().contact_shadow(ground, view);
}
} // namespace storm::render
