// World passes of the graphics effects: the analytic sky, the fx water and contact shadows. The consumer entities
// (SKY, SEA, shadow) call them where they would draw their legacy passes; the draws go into the shim's current game
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

float seconds()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
}

fx::Vec3 decode(const std::array<float, 3> &rgb)
{
    return {rgb[0], rgb[1], rgb[2]};
}
} // namespace

render::EffectsWorld Host::world() const
{
    render::EffectsWorld world;
    if (!device_ready_)
        return world;
    world.sky = settings_.sky_atmosphere;
    world.water = settings_.water != fx::WaterQuality::Legacy;
    world.contact_shadows = settings_.contact_shadows;
    world.lens_flare = settings_.lens_flare;
    return world;
}

void Host::update_sky(float camera_height)
{
    sky_.Update(weather_input(), seconds(), camera_height);
}

bool Host::sky_fog_color(std::array<float, 3> &rgb)
{
    if (!device_ready_ || !settings_.sky_atmosphere || !weather_this_frame_)
        return false;
    update_sky(camera_height_);
    // The panorama stores sqrt(linear): the game's colours are gamma-encoded the same way.
    const fx::Vec3 &horizon = sky_.State().horizon;
    for (int i = 0; i < 3; ++i)
        rgb[i] = std::sqrt(std::clamp(horizon[i], 0.0f, 1.0f));
    return true;
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
    // D3D perspective: _33 = f / (f - n), _43 = -n f / (f - n).
    const D3DMATRIX &p = device->s.proj;
    if (p.m[2][2] != 0.0f && p.m[2][2] != 1.0f)
    {
        pass.near_plane = -p.m[3][2] / p.m[2][2];
        pass.far_plane = p.m[3][2] / (1.0f - p.m[2][2]);
    }
    camera_height_ = pass.camera[1];
    return true;
}

void Host::draw_sky(const render::EffectsMatrix &view)
{
    if (!settings_.sky_atmosphere)
        return;
    if (!has_weather() && !sky_weather_logged_)
    {
        sky_weather_logged_ = true;
        shim::logf("ERROR: effects: the fx sky is drawn without a WEATHER entity: it shows the default weather");
    }
    WorldPass pass;
    if (!begin_world_pass(pass, view))
        return;
    update_sky(pass.camera[1]);
    sky_.Submit(pass.view, settings_, pass.view_mtx.data(), pass.projection.data());
}

void Host::draw_water(const render::EffectsWaterMesh &mesh, const render::EffectsMatrix &view)
{
    if (settings_.water == fx::WaterQuality::Legacy || !mesh.vertex_count || !mesh.index_count)
        return;
    const bgfx::VertexLayout &layout = fx::Water::MeshLayout();
    if (bgfx::getAvailTransientVertexBuffer(mesh.vertex_count, layout) < mesh.vertex_count ||
        bgfx::getAvailTransientIndexBuffer(mesh.index_count) < mesh.index_count)
    {
        if (!water_buffers_logged_)
            shim::logf("ERROR: effects: no transient buffer space for the fx water (%u vertices, %u indices): sea not drawn",
                       mesh.vertex_count, mesh.index_count);
        water_buffers_logged_ = true;
        return;
    }
    WorldPass pass;
    if (!begin_world_pass(pass, view))
        return;
    update_sky(pass.camera[1]);
    if (settings_.water == fx::WaterQuality::High)
    {
        // The snapshot of the scene behind the water: colour and depth of the current target, read in a view of
        // its own; the water then continues in a new view on the same target.
        shim::Device &device = *shim::Device::current;
        shim::TexStore *color = device.rt->store.get();
        shim::TexStore *depth = device.ds ? device.ds->store.get() : nullptr;
        // A multisampled target is read through its resolve textures, which hold the depth too only where the
        // backend resolves it (Support::msaa_depth).
        if (depth && (!color->target_flags || support_.msaa_depth))
        {
            color->sync();
            depth->sync();
            const bgfx::ViewId capture = device.effects_view(true);
            water_.CaptureScene(capture, color->handle, depth->handle, static_cast<uint16_t>(color->width),
                                static_cast<uint16_t>(color->height), pass.near_plane, pass.far_plane);
            if (!begin_world_pass(pass, view, true))
                return;
        }
        else if (!water_capture_logged_)
        {
            water_capture_logged_ = true;
            shim::logf("ERROR: effects: High water needs a readable depth of the target to see the scene behind the "
                       "sea; Water refuses the frame");
        }
    }
    fx::WaterFrame frame;
    frame.view = pass.view;
    frame.camera = pass.camera;
    frame.time = seconds();
    frame.crest_height = mesh.crest_height;
    frame.far_plane = pass.far_plane;
    fx::WaterShading shading;
    const render::EffectsWaterShading &in = mesh.shading;
    shading.water_color = decode(in.water_color);
    shading.sky_color = decode(in.sky_color);
    shading.reflection = in.reflection;
    shading.transparency = in.transparency;
    shading.fresnel = in.fresnel;
    shading.bump_speed = in.bump_speed;
    shading.foam = in.foam;
    shading.foam_v = in.foam_v;
    shading.foam_k = in.foam_k;
    shading.foam_uv = in.foam_uv;

    bgfx::TransientVertexBuffer vertices;
    bgfx::TransientIndexBuffer indices;
    bgfx::allocTransientVertexBuffer(&vertices, mesh.vertex_count, layout);
    bgfx::allocTransientIndexBuffer(&indices, mesh.index_count);
    memcpy(vertices.data, mesh.vertices, static_cast<size_t>(mesh.vertex_count) * layout.getStride());
    memcpy(indices.data, mesh.indices, static_cast<size_t>(mesh.index_count) * sizeof(uint16_t));
    const bgfx::ProgramHandle program = water_.PrepareMesh(settings_, frame, shading, sky_, mesh.world_offset);
    if (!bgfx::isValid(program))
    {
        bgfx::discard(); // Water logged why; drop the state it set
        return;
    }
    bgfx::setVertexBuffer(0, &vertices);
    bgfx::setIndexBuffer(&indices);
    bgfx::submit(pass.view, program);
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

bool EffectsSkyFogColor(std::array<float, 3> &rgb)
{
    return effects::Host::instance().sky_fog_color(rgb);
}

void EffectsDrawSky(const EffectsMatrix &view)
{
    effects::Host::instance().draw_sky(view);
}

void EffectsDrawWater(const EffectsWaterMesh &mesh, const EffectsMatrix &view)
{
    effects::Host::instance().draw_water(mesh, view);
}

void EffectsContactShadow(const std::array<float, 3> &ground, const EffectsMatrix &view)
{
    effects::Host::instance().contact_shadow(ground, view);
}
} // namespace storm::render
