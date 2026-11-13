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
// Kelvin wakes: none below this speed (m/s); the wave height per metre of half beam at 5 m/s.
constexpr float kWakeMinSpeed = 0.7f;
constexpr float kWakeHeight = 0.3f;
constexpr float kWakeScale = 3.0f;
constexpr float kWavesQuietSeconds = 12.0f;
// The ripples' slope in the normal: the field's few-centimetre waves read at a phone's distance only stronger
// (with the physical per-pixel Fresnel of fs_port_sea2 they need more tilt than the old facing term gave them).
constexpr float kRippleSlopeGain = 4.0f;
// Metres one repeat of the game's foam texture (pena.tga, a photograph of churned water) covers.
constexpr double kFoamTile = 10.0;

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
    world.lamp_character_shadows = settings_.lamp_shadows > 0 && lighting_.lamp_shadows().interior();
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
    advance_sea_waves(device);
    set_sea_waves_uniforms();
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

void Host::set_sea_frame(const render::EffectsSeaFrame &frame)
{
    sea_frame_ = frame;
    sea_sources_.assign(frame.sources.begin(), frame.sources.end());
    sea_frame_.sources = sea_sources_;
    sea_frame_set_ = shim::Context::instance().submitted_frames();
}

void Host::advance_sea_waves(shim::Device &device)
{
    const uint32_t frame = shim::Context::instance().submitted_frames();
    if (!device_ready_ || !settings_.sea_waves || sea_frame_set_ != frame || sea_waves_frame_ == frame)
        return;
    sea_waves_frame_ = frame;
    // Nothing on the water for a while: the waves have died away (half their energy goes in a few seconds), and the
    // field stops stepping until something stirs it again.
    sea_waves_quiet_ = sea_sources_.empty() ? sea_waves_quiet_ + sea_frame_.dt : 0.0f;
    const int steps = sea_waves_.StepsDue(sea_frame_.dt);
    if (steps == 0 || sea_waves_quiet_ > kWavesQuietSeconds)
        return;
    // The sources nearest the field's centre first: the simulation takes the first fx::SeaWaves::kMaxSources.
    const auto &camera = sea_frame_.focus;
    const auto distance = [&](const render::EffectsWaveSource &s) {
        const float dx = s.position[0] - camera[0], dz = s.position[2] - camera[2];
        return std::sqrt(dx * dx + dz * dz) - s.half_length;
    };
    sea_wave_sources_.clear();
    for (const render::EffectsWaveSource &s : sea_sources_)
    {
        // Only what can reach the field: its half size plus the source's length.
        if (distance(s) > 0.5f * fx::SeaWaves::kExtent)
            continue;
        fx::WaveSource w;
        w.kind = s.kind == render::EffectsWaveSource::Kind::Hull ? fx::WaveSource::Kind::Hull : fx::WaveSource::Kind::Body;
        w.position = {s.position[0], s.position[2]};
        w.forward = s.forward;
        w.half_length = s.half_length;
        w.half_width = s.half_width;
        w.speed = s.speed;
        w.heave = s.heave;
        w.stroke = s.stroke;
        sea_wave_sources_.push_back(w);
    }
    std::sort(sea_wave_sources_.begin(), sea_wave_sources_.end(), [&](const fx::WaveSource &a, const fx::WaveSource &b) {
        const auto d = [&](const fx::WaveSource &s) {
            const double dx = s.position[0] - camera[0], dz = s.position[1] - camera[2];
            return dx * dx + dz * dz;
        };
        return d(a) < d(b);
    });
    std::array<bgfx::ViewId, fx::SeaWaves::kMaxSteps + 1> views{};
    for (int i = 0; i <= steps; ++i)
        views[i] = device.effects_view(true);
    const auto &focus = sea_frame_.focus;
    sea_waves_.Advance(std::span(views.data(), static_cast<size_t>(steps) + 1), {focus[0], focus[2]}, sea_wave_sources_,
                       support_.sea_waves_float);
    device.effects_view(true); // the sea goes on in a view of its own on its target
}

void Host::set_sea_waves_uniforms()
{
    if (!bgfx::isValid(u_sea_waves_))
    {
        u_sea_waves_ = bgfx::createUniform("u_seaWaves", bgfx::UniformType::Vec4, 4);
        u_sea_wakes_ = bgfx::createUniform("u_seaWakes", bgfx::UniformType::Vec4, 8);
        s_sea_waves_ = bgfx::createUniform("s_seaWaves", bgfx::UniformType::Sampler);
    }
    const bool frame = sea_frame_set_ == shim::Context::instance().submitted_frames();
    const auto &origin = sea_frame_.vertex_origin;
    auto &w = sea_waves_uniform_;
    w = {};
    const bool ripples = device_ready_ && frame && settings_.sea_waves && sea_waves_.Ready();
    if (ripples)
    {
        const auto centre = sea_waves_.Centre();
        w[0] = static_cast<float>((static_cast<double>(origin[0]) - centre[0]) / fx::SeaWaves::kExtent + 0.5);
        w[1] = static_cast<float>((static_cast<double>(origin[2]) - centre[1]) / fx::SeaWaves::kExtent + 0.5);
        w[2] = 1.0f / fx::SeaWaves::kExtent;
        w[3] = settings_.sea_waves_lift ? 1.0f : 0.0f;
        w[4] = kRippleSlopeGain;
        w[9] = sea_waves_.SurfaceFloat() ? 0.0f : 1.0f;
    }
    w[5] = 1.0f; // foam: the ripples' crests and the wakes' tracks
    w[7] = fx::SeaWaves::kCell;
    w[10] = sea_frame_.fresnel;
    w[11] = fx::SeaWaves::kSize;
    // The foam texture's uv at the sea's vertex origin, from the absolute world (the origin moves with the camera).
    const auto tile_uv = [](float world) {
        const double t = static_cast<double>(world) / kFoamTile;
        return static_cast<float>(t - std::floor(t));
    };
    w[12] = tile_uv(origin[0]);
    w[13] = tile_uv(origin[2]);
    w[14] = static_cast<float>(1.0 / kFoamTile);
    // Kelvin wakes: the hulls under way nearest the camera.
    auto &k = sea_wakes_uniform_;
    k = {};
    int wakes = 0;
    if (device_ready_ && frame && settings_.sea_wakes)
    {
        struct Wake
        {
            const render::EffectsWaveSource *source;
            float distance;
        };
        std::array<Wake, 4> best{};
        const auto &camera = sea_frame_.camera;
        for (const render::EffectsWaveSource &s : sea_sources_)
        {
            if (s.kind != render::EffectsWaveSource::Kind::Hull || s.speed < kWakeMinSpeed)
                continue;
            const float dx = s.position[0] - camera[0], dz = s.position[2] - camera[2];
            const Wake wake{&s, std::sqrt(dx * dx + dz * dz)};
            int slot = wakes < 4 ? wakes++ : -1;
            if (slot < 0)
            {
                slot = 0;
                for (int i = 1; i < 4; ++i)
                    if (best[i].distance > best[slot].distance)
                        slot = i;
                if (best[slot].distance <= wake.distance)
                    continue;
            }
            best[slot] = wake;
        }
        for (int i = 0; i < wakes; ++i)
        {
            const render::EffectsWaveSource &s = *best[i].source;
            float *a = k.data() + i * 8;
            // The V starts at the bow (sea vertex space: small numbers near the camera).
            a[0] = s.position[0] + s.forward[0] * s.half_length - origin[0];
            a[1] = s.position[2] + s.forward[1] * s.half_length - origin[2];
            a[2] = s.forward[0];
            a[3] = s.forward[1];
            const float u = s.speed;
            // k0 = g / U^2 (transverse wavelength 2 pi U^2 / g) of a speed kWakeScale times the hull's: the game's
            // ships sail at 2..7 m/s, whose real wakes (2.5..30 m waves) vanish into the sea's own detail.
            a[4] = 9.81f / (kWakeScale * u * u);
            a[5] = 2.0f * s.half_length + std::clamp(40.0f * u, 60.0f, 320.0f); // from the bow, about 40 s of travel
            a[6] = kWakeHeight * s.half_width * std::min(u / 5.0f, 1.4f) * std::min((u - kWakeMinSpeed) * 2.0f, 1.0f);
            a[7] = s.half_width;
        }
    }
    w[6] = static_cast<float>(wakes);
    w[8] = settings_.sea_waves_debug;
    if (wakes != sea_wakes_logged_)
    {
        shim::logf("effects: sea waves: %d wake(s) of %zu source(s)%s", wakes, sea_sources_.size(),
                   wakes ? "" : " (no hull under way)");
        sea_wakes_logged_ = wakes;
    }
    bgfx::setUniform(u_sea_waves_, w.data(), 4);
    bgfx::setUniform(u_sea_wakes_, k.data(), 8);
    if (ripples)
        bgfx::setTexture(fx::kShadowMapStage, s_sea_waves_, sea_waves_.Surface(), BGFX_SAMPLER_UVW_CLAMP);
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
