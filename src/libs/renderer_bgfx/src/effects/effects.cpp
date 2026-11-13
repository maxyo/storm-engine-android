#include "effects/effects.hpp"
#include "effects/thermal.hpp"
#include "effects/relief_generator.hpp"

#include "effects/casters.hpp"
#include "effects/texture_pixels.hpp"
#include "shim/device.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/cutout.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/sky_model.hpp"
#include "storm/render/program_cache.hpp"
#include "texture/core/formats.hpp"


#include <algorithm>
#include <iterator>
#include <cctype>
#include <cmath>
#include <cstring>

// patches/bgfx-anisotropy-level.patch: the level cap of each backend.
namespace bgfx::gl
{
extern float g_stormMaxAnisotropy;
}
namespace bgfx::vk
{
extern float g_stormMaxAnisotropy;
}

namespace storm::effects
{

namespace
{
void log_sink(fx::LogLevel level, const char *message)
{
    if (level == fx::LogLevel::Debug)
        shim::logd("fx: %s", message);
    else
        shim::logf("%sfx: %s", level == fx::LogLevel::Error ? "ERROR: " : "", message);
}

// One engine option: an integer from engine.ini / the app's graphics preferences. -1 keeps the preset's value.
struct Option
{
    const char *name;
    const char *values;
    void (*apply)(fx::Settings &, int);
};

bool on(int value)
{
    return value != 0;
}

void set_flag(fx::Settings &s, uint32_t flag, int value)
{
    if (on(value))
        s.lighting_flags |= flag;
    else
        s.lighting_flags &= ~flag;
}

const Option kOptions[] = {
    {"fx_tonemap", "0 off, 1 on", [](fx::Settings &s, int v) { s.tonemap = on(v); }},
    {"fx_bloom", "0 off, 1 on", [](fx::Settings &s, int v) { s.bloom = on(v); }},
    {"fx_sun_shafts", "0 off, 1 on", [](fx::Settings &s, int v) { s.sun_shafts = on(v); }},
    {"fx_smaa", "0 off, 1 on", [](fx::Settings &s, int v) { s.smaa = on(v); }},
    {"fx_render_height", "the 3D scene's height in pixels on taller displays, upscaled by FSR 1: 360..4320, 0 = native",
     [](fx::Settings &s, int v) { s.render_height = static_cast<uint16_t>(std::clamp(v, 0, 4320)); }},
    {"fx_sharpen", "0 off, 1 on (FSR 1 RCAS)", [](fx::Settings &s, int v) { s.sharpen = on(v); }},
    {"fx_sharpness", "0..100 % RCAS strength (100 = the reference's maximum)",
     [](fx::Settings &s, int v) { s.sharpness = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_ssao", "0 off, 1 on", [](fx::Settings &s, int v) { s.ssao = on(v); }},
    {"fx_dither", "0 off, 1 on", [](fx::Settings &s, int v) { s.dither = on(v); }},
    {"fx_lens_flare", "0 off, 1 on", [](fx::Settings &s, int v) { s.lens_flare = on(v); }},
    {"fx_scene_grading", "0 off, 1 on", [](fx::Settings &s, int v) { s.scene_grading = on(v); }},
    {"fx_grade", "0 Оригинал, 1 Карибы, 2 Пасмурно",
     [](fx::Settings &s, int v) { s.grade = static_cast<fx::GradePreset>(std::clamp(v, 0, 2)); }},
    {"fx_msaa", "1, 2, 4", [](fx::Settings &s, int v) { s.msaa = static_cast<uint8_t>(std::clamp(v, 1, 4)); }},
    {"fx_alpha_to_coverage", "0 off, 1 on", [](fx::Settings &s, int v) { s.alpha_to_coverage = on(v); }},
    {"fx_anisotropy", "1, 4, 8", [](fx::Settings &s, int v) { s.anisotropy = static_cast<uint8_t>(std::clamp(v, 1, 16)); }},
    {"fx_contact_shadows", "0 off, 1 on", [](fx::Settings &s, int v) { s.contact_shadows = on(v); }},
    {"fx_sea_band_limit", "0 off, 1 on", [](fx::Settings &s, int v) { s.sea_band_limit = on(v); }},
    {"fx_sea_soft_edges", "0 off, 1 on", [](fx::Settings &s, int v) { s.sea_soft_edges = on(v); }},
    {"fx_sea_specular", "0 off, 1 on", [](fx::Settings &s, int v) { s.sea_specular = on(v); }},
    {"fx_sea_waves", "0 off, 1 on: ripples from swimmers, boats and hulls", [](fx::Settings &s, int v) { s.sea_waves = on(v); }},
    {"fx_sea_waves_lift", "0 off, 1 on: the ripples also raise the sea surface",
     [](fx::Settings &s, int v) { s.sea_waves_lift = on(v); }},
    {"fx_sea_wakes", "0 off, 1 on: Kelvin wakes behind ships", [](fx::Settings &s, int v) { s.sea_wakes = on(v); }},
    {"fx_sea_waves_debug", "0 off, 1 ripple slope red, wake slope green, crest foam blue",
     [](fx::Settings &s, int v) { s.sea_waves_debug = static_cast<uint8_t>(std::clamp(v, 0, 1)); }},
    {"fx_lamp_shadows", "lamps that relight a location with their shadows, 0, 8 or 16",
     [](fx::Settings &s, int v) { s.lamp_shadows = static_cast<uint8_t>(std::clamp(v, 0, 16)); }},
    {"fx_lamp_soft", "0 3x3, 1 5x5 filter of the lamp shadows", [](fx::Settings &s, int v) { s.lamp_soft = on(v); }},
    {"fx_lamp_moving", "0, 1, 2: lamp slots for carried lanterns and torches (their shadows move with them)",
     [](fx::Settings &s, int v) { s.lamp_moving = static_cast<uint8_t>(std::clamp(v, 0, 2)); }},
    {"fx_lamp_gain", "0..300 % of the lamps' strength on baked surfaces",
     [](fx::Settings &s, int v) { s.lamp_gain = static_cast<float>(std::clamp(v, 0, 300)) / 100.0f; }},
    {"fx_lamp_baked", "0..100 %: the part of the lamp light the baked colours already hold",
     [](fx::Settings &s, int v) { s.lamp_baked = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_lamp_tint", "0..100 %: the share of a lamp's own colour in the light it adds to baked surfaces",
     [](fx::Settings &s, int v) { s.lamp_tint = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_lamp_model_lift", "0..100 %: lit models of an interior brighter than the room light around them",
     [](fx::Settings &s, int v) { s.lamp_model_lift = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_lamp_debug", "0 off, 1 every lamp in its own colour, 2 the shadow mask",
     [](fx::Settings &s, int v) { s.lamp_debug = static_cast<uint8_t>(std::clamp(v, 0, 2)); }},
    {"fx_ao_volume", "interiors: 0 off, 1 as the game's data, 2 moderate, 3 strong",
     [](fx::Settings &s, int v) { s.ao_volume = static_cast<uint8_t>(std::clamp(v, 0, 3)); }},
    {"fx_shadows", "0 off, 1 low, 2 soft",
     [](fx::Settings &s, int v) { s.shadows = static_cast<fx::ShadowQuality>(std::clamp(v, 0, 2)); }},
    {"fx_town_light", "0 the game's town lightmaps, 1 our own for the live sun",
     [](fx::Settings &s, int v) { s.town_light = on(v); }},
    {"fx_town_walls", "0 the game's baked walls in towns, 1 relit by our sun, sky and bounce (with fx_town_light)",
     [](fx::Settings &s, int v) { s.town_walls = on(v); }},
    {"fx_bounce", "0 off, 1 one bounce of indirect light (lit surfaces light what is around them)",
     [](fx::Settings &s, int v) { s.bounce = on(v); }},
    {"fx_bounce_debug", "0 off, 1 lit and baked surfaces show the bounce's light over a dim grey picture",
     [](fx::Settings &s, int v) { s.bounce_debug = on(v); }},
    {"fx_visibility_debug", "0 off, 1 lit and baked surfaces show the visibility volume (red sky, green open)",
     [](fx::Settings &s, int v) { s.visibility_debug = on(v); }},
    {"fx_town_light_debug", "0 off, 1 our town lightmaps show the sun's visibility",
     [](fx::Settings &s, int v) { s.town_light_debug = on(v); }},
    {"fx_point_lights", "4, 8", [](fx::Settings &s, int v) { s.point_lights = static_cast<uint8_t>(v >= 8 ? 8 : 4); }},
    {"fx_light_per_pixel", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingPerPixel, v); }},
    {"fx_light_hemisphere", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingHemisphere, v); }},
    {"fx_light_specular", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingSpecular, v); }},
    {"fx_light_translucent", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingTranslucent, v); }},
    {"fx_sail_light", "0..300 % sunlight through sails",
     [](fx::Settings &s, int v) { s.sail_light = static_cast<float>(std::clamp(v, 0, 300)) / 100.0f; }},
    {"fx_light_point", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingPoint, v); }},
    {"fx_light_normal_map", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingNormalMap, v); }},
    {"fx_normal_map_strength", "0..100 % relief strength",
     [](fx::Settings &s, int v) { s.normal_map_strength = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f; }},
    {"fx_gloss", "0 off, 1 on: gloss maps shape the sun's and the lamps' highlights",
     [](fx::Settings &s, int v) { s.gloss = on(v); }},
    {"fx_gloss_strength", "10..300 % of the gloss maps' highlights",
     [](fx::Settings &s, int v) { s.gloss_strength = static_cast<float>(std::clamp(v, 10, 300)) / 100.0f; }},
    {"fx_gloss_debug", "0 off, 1 the gloss (magenta: no map), 2 the relief normals, 3 the sun the bake shows, 4 the metal mask",
     [](fx::Settings &s, int v) { s.gloss_debug = static_cast<uint8_t>(std::clamp(v, 0, 4)); }},
    {"fx_light_rim", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingRim, v); }},
    {"fx_light_linear", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingLinear, v); }},
    {"fx_aerial", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingAerial, v); }},
    {"fx_wind", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingWind, v); }},
};

const Option *find_option(std::string_view name)
{
    for (const Option &option : kOptions)
        if (name == option.name)
            return &option;
    return nullptr;
}

// Parts of fx that this renderer does not drive yet. Requesting them is not ignored quietly: they are switched
// off when the settings are applied, logged, and reported to the settings screen like a missing device feature.
struct Pending
{
    bool (*requested)(const fx::Settings &);
    void (*off)(fx::Settings &);
    const char *names; // option names to grey out
    const char *message;
};

const Pending kPending[] = {
    {[](const fx::Settings &s) { return (s.lighting_flags & fx::kLightingWind) != 0; },
     [](fx::Settings &s) { s.lighting_flags &= ~fx::kLightingWind; }, "fx_wind",
     "wind sway is not wired into the fixed-function emulator yet (needs per-technique vertex weights): switched off"},
};

std::string describe(const fx::Settings &s)
{
    char text[512];
    snprintf(text, sizeof(text),
             "tonemap=%d bloom=%d(1/%u,%u) shafts=%d smaa=%d scene_height=%u sharpen=%d(%.2f) ssao=%d dither=%d flare=%d scene_grading=%d "
             "grade=%d msaa=%u a2c=%d aniso=%u sea=%d%d%d waves=%d%d%d shadows=%d "
             "lighting=0x%x contact=%d",
             s.tonemap, s.bloom, s.bloom_downscale, s.bloom_levels, s.sun_shafts, s.smaa, s.render_height, s.sharpen,
             static_cast<double>(s.sharpness), s.ssao, s.dither,
             s.lens_flare, s.scene_grading, static_cast<int>(s.grade), s.msaa,
             s.alpha_to_coverage, s.anisotropy, s.sea_band_limit, s.sea_soft_edges, s.sea_specular,
             s.sea_waves, s.sea_waves_lift, s.sea_wakes, static_cast<int>(s.shadows), s.lighting_flags, s.contact_shadows);
    return text;
}
} // namespace

Host &Host::instance()
{
    static Host host;
    return host;
}

void Host::on_device_created()
{
    fx::SetLogSink(log_sink);
    fx::SetBakeThermal(&device_hot); // the ray bakes (town light, visibility) pause while the device is hot
    fx::RendererFacts facts;
    // The game's offscreen depth is a D24S8 BGFX_TEXTURE_RT texture (TexStore::create_gpu): sampleable. With MSAA
    // the pinned bgfx GL backend blits the depth into that texture too when the frame buffer is resolved
    // (FrameBufferGL::resolve, GL_DEPTH_BUFFER_BIT for readable depth attachments).
    facts.scene_depth = true;
    facts.msaa_depth = true;
    // bgfx has no capability bit for it; the backends apply it only where the device has it.
    const shim::DeviceInfo &device = shim::Context::instance().device_info();
    facts.anisotropy = device.anisotropy;
    if (facts.anisotropy)
        device_max_anisotropy_ = device.max_anisotropy;
    // bgfx's GL backend never sets BGFX_CAPS_FORMAT_TEXTURE_VERTEX: the device's own limit decides.
    facts.vertex_texture = device.vertex_texture_units > 0;
    support_device_ = fx::DetectSupport(facts);
    device_ready_ = true;
    // Nothing is drawn yet: the device starts on the plain legacy path (no effect, no MSAA, not even «Низкая»'s
    // upscale) and switches to the request like any later change (advance_switch), once its programs exist and the
    // self-test passed.
    activate(fx::Settings{}, fx::Support{});
    apply();
    renderer_state_changed_ = false; // the scene target is created after this, with the active state
}

void Host::on_device_lost()
{
    post_.OnDeviceLost();
    self_test_.OnDeviceLost();
    lighting_.on_device_lost();
    sky_.OnDeviceLost();
    sea_depth_.OnDeviceLost();
    depth_copy_.OnDeviceLost();
    u_sea_fx_ = s_sea_depth_ = BGFX_INVALID_HANDLE;
    sea_depth_frame_ = UINT32_MAX;
    sea_waves_.OnDeviceLost();
    u_sea_waves_ = u_sea_wakes_ = s_sea_waves_ = BGFX_INVALID_HANDLE;
    sea_waves_frame_ = UINT32_MAX;
    contact_.OnDeviceLost();
    ao_.on_device_lost();
    ao_bound_ = nullptr;
    lighting_.set_ao_volume(nullptr);
    visibility_.on_device_lost();
    lighting_.set_visibility(nullptr);
    device_ready_ = false;
}

void Host::shutdown()
{
    post_.Shutdown();
    self_test_.Shutdown();
    lighting_.shutdown();
    sky_.Shutdown();
    sea_depth_.Shutdown();
    depth_copy_.Shutdown();
    sea_waves_.Shutdown();
    sea_waves_frame_ = UINT32_MAX;
    for (bgfx::UniformHandle *uniform : {&u_sea_fx_, &s_sea_depth_, &u_sea_waves_, &u_sea_wakes_, &s_sea_waves_})
        if (bgfx::isValid(*uniform))
        {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    sea_depth_frame_ = UINT32_MAX;
    contact_.Shutdown();
    ao_.shutdown();
    ao_bound_ = nullptr;
    lighting_.set_ao_volume(nullptr);
    bounce_.shutdown();
    visibility_.shutdown();
    lighting_.set_visibility(nullptr);
    device_ready_ = false;
}

render::EffectsReport Host::configure(int preset, const std::vector<render::EffectsOption> &options)
{
    preset_ = std::clamp(preset, 0, 2);
    options_ = options;
    apply();
    return report_;
}

void Host::apply()
{
    report_ = {};
    report_.preset = preset_;
    requested_ = fx::Settings::FromPreset(static_cast<fx::QualityPreset>(preset_));
    bool profile = false;
    caps_profile_ = fx::CapsProfile::Device;
    for (const auto &option : options_)
    {
        if (option.value < 0)
            continue; // the preset's value
        if (option.name == "fx_gpu_profile")
            profile = option.value != 0;
        else if (option.name == "fx_caps_profile")
            caps_profile_ = static_cast<fx::CapsProfile>(std::clamp(option.value, 0, static_cast<int>(fx::CapsProfile::Count) - 1));
        else if (const Option *known = find_option(option.name))
            known->apply(requested_, option.value);
        else
            report_.errors.push_back("unknown graphics option '" + option.name + "'");
    }
    for (const auto &error : report_.errors)
        shim::logf("ERROR: effects: %s", error.c_str());
    if (device_ready_ && profile != profile_)
    {
        bgfx::setDebug(profile ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
        shim::logf("effects profile: GPU timer queries per view %s (fx_gpu_profile)", profile ? "on" : "off");
    }
    profile_ = device_ready_ && profile;

    fx::Settings next = requested_;
    for (const Pending &pending : kPending)
        if (pending.requested(next))
        {
            pending.off(next);
            report_.changes.push_back(pending.message);
        }
    if (device_ready_)
    {
        // What the device can do, narrowed by a forced GPU-family profile (fx_caps_profile, development) and by
        // the stages the self-test found broken on this device.
        target_support_ = fx::RestrictSupport(support_device_, caps_profile_);
        if (caps_profile_ != fx::CapsProfile::Device)
            report_.changes.push_back(std::string("caps profile forced: ") + fx::CapsProfileName(caps_profile_));
        if (msaa_broken_)
        {
            target_support_.msaa_samples = 1;
            target_support_.msaa_depth = false;
        }
        if (post_broken_)
            target_support_.lut_3d = false;
        next = fx::ConstrainSettings(next, target_support_, report_.changes);
        report_.support = target_support_.Describe();
        // Options the device can never honour, for the settings screen.
        fx::Settings all = fx::Settings::FromPreset(fx::QualityPreset::High);
        all.smaa = true;
        std::vector<std::string> unused;
        const fx::Settings possible = fx::ConstrainSettings(all, target_support_, unused);
        if (possible.msaa <= 1)
            report_.disabled.push_back("fx_msaa");
        if (possible.anisotropy <= 1)
            report_.disabled.push_back("fx_anisotropy");
        if (!possible.ssao)
            report_.disabled.push_back("fx_ssao");
        if (!possible.sea_waves_lift)
            report_.disabled.push_back("fx_sea_waves_lift");
        if (!possible.smaa && !possible.PostIsIdentity())
            report_.disabled.push_back("fx_smaa");
        if (possible.render_height == 0) // no post chain: no upscale, no RCAS
            for (const char *name : {"fx_render_height", "fx_sharpen", "fx_sharpness"})
                report_.disabled.push_back(name);
        if (possible.PostIsIdentity())
            for (const char *name : {"fx_tonemap", "fx_bloom", "fx_sun_shafts", "fx_smaa", "fx_dither", "fx_lens_flare", "fx_scene_grading", "fx_grade"})
                report_.disabled.push_back(name);
    }
    for (const Pending &pending : kPending)
    {
        std::string names = pending.names;
        for (size_t start = 0, end; start < names.size(); start = end + 1)
        {
            end = names.find(' ', start);
            if (end == std::string::npos)
                end = names.size();
            report_.disabled.push_back(names.substr(start, end - start));
        }
    }
    const bool changed = !(next == target_) || !device_ready_;
    const fx::RendererState state = fx::RendererStateFor(next);
    if (device_ready_ && state.max_anisotropy > 1 && device_max_anisotropy_ < state.max_anisotropy)
        report_.changes.push_back("anisotropic filtering " + std::to_string(state.max_anisotropy) + "x: the device allows " +
                                  std::to_string(static_cast<int>(device_max_anisotropy_)) + "x");
    report_.summary = describe(next);
    target_ = next;
    if (!device_ready_)
        activate(next, target_support_); // nothing is drawn before the device exists
    else if (!(target_ == settings_) && !switching_)
    {
        // The current settings keep drawing until the new ones are ready (advance_switch).
        switching_ = true;
        switch_started_ = std::chrono::steady_clock::now();
        switch_frames_ = 0;
        switch_programs_ = 0;
    }
    else if (target_ == settings_)
    {
        switching_ = false;
        support_ = target_support_; // the same settings, also valid on the new support (ConstrainSettings left them)
    }
    report_.pending = switching_;
    if (changed)
    {
        shim::logf("effects: preset %d (%s)%s: %s", preset_,
                   std::string(fx::NameRu(static_cast<fx::QualityPreset>(preset_))).c_str(),
                   switching_ ? " requested, prepared while the current settings draw" : "", report_.summary.c_str());
        for (const auto &line : report_.changes)
            shim::logf("effects: preset %d on this device: %s", preset_, line.c_str());
    }
}

void Host::activate(const fx::Settings &settings, const fx::Support &support)
{
    settings_ = settings;
    support_ = support;
    const fx::RendererState state = fx::RendererStateFor(settings_);
    const bool depth_read = scene_depth_read();
    renderer_state_changed_ = renderer_state_changed_ ||
                              (device_ready_ && (state.msaa_samples != renderer_state_.msaa_samples ||
                                                 state.render_height != renderer_state_.render_height ||
                                                 state.max_anisotropy != renderer_state_.max_anisotropy ||
                                                 depth_read != depth_read_));
    depth_read_ = depth_read;
    renderer_state_ = state;
    // The level of every anisotropic sampler (patches/bgfx-anisotropy-level.patch: one cap per backend), applied
    // at the next reset.
    bgfx::gl::g_stormMaxAnisotropy = bgfx::vk::g_stormMaxAnisotropy = static_cast<float>(state.max_anisotropy);
    lighting_.configure(settings_, renderer_state_);
}

bool Host::needs_self_test(const fx::Settings &settings)
{
    return !settings.PostIsIdentity() || settings.render_height > 0 || fx::RendererStateFor(settings).msaa_samples > 1;
}

void Host::set_weather(const render::EffectsWeather &weather)
{
    weather_ = weather;
    weather_this_frame_ = true;
    // The hemisphere ambient takes its tint from the sky dome; the aerial perspective needs a sky (no interiors).
    lighting_.shadows().set_sun(weather.sun_dir, weather.sun_present);
    lighting_.set_weather_sun(weather.sun_dir, weather.sun_color, weather.sun_present);
    if (settings_.lighting_flags & (fx::kLightingHemisphere | fx::kLightingAerial))
        lighting_.set_sky(&sky_state());
}

const fx::SkyState &Host::sky_state()
{
    // One sky state for the fx sky and water, the scene grade and the hemisphere ambient. ComputeSky costs ~2 ms on
    // the host; Sky::Update rebuilds it only when the weather moves (SkyNeedsRebuild), not every frame.
    update_sky(camera_height_);
    return sky_.State();
}

fx::WeatherInput Host::weather_input() const
{
    fx::WeatherInput input;
    input.sun_dir = weather_.sky_sun_dir;
    input.sun_color = weather_.sun_color;
    input.ambient_color = weather_.ambient;
    input.fog_color = weather_.fog_color;
    input.fog_density = weather_.fog_density;
    input.overcast = weather_.overcast;
    input.wind = weather_.wind_dir;
    input.wind_speed = weather_.wind_speed;
    return input;
}

uint16_t Host::submit_post(const PostTargets &targets)
{
    fx::PostChain::Frame frame;
    frame.scene_color = targets.scene_color;
    frame.scene_width = targets.width;
    frame.scene_height = targets.height;
    frame.target = targets.target;
    frame.target_width = targets.target_width;
    frame.target_height = targets.target_height;
    last_post_size_ = {targets.width, targets.height, targets.target_width, targets.target_height};
    frame.first_view = targets.first_view;
    frame.flip_v = false; // the target is the game's next offscreen picture, same row order as the scene
    frame.scene_depth = targets.scene_depth;
    // D3D perspective: _33 = f / (f - n), _43 = -n f / (f - n).
    const auto &p = targets.projection;
    if (p[10] != 0.0f && p[10] != 1.0f)
    {
        frame.near_plane = -p[14] / p[10];
        frame.far_plane = p[14] / (1.0f - p[10]);
    }
    frame.projection_scale = {p[0], p[5]};
    frame.wobble = {.on = wobble_.on, .amplitude = wobble_.amplitude, .phase = wobble_.phase};

    // A frame without weather has no sky (interiors).
    const bool weather = has_weather();
    if (weather && weather_.sun_present)
    {
        // Sun on the screen (lens flare, shafts): the direction through the camera rotation and the projection.
        const auto &v = targets.view;
        const auto &d = weather_.sun_dir;
        float e[3];
        for (int c = 0; c < 3; ++c)
            e[c] = d[0] * v[0 * 4 + c] + d[1] * v[1 * 4 + c] + d[2] * v[2 * 4 + c];
        float clip[4];
        for (int c = 0; c < 4; ++c)
            clip[c] = e[0] * p[0 * 4 + c] + e[1] * p[1 * 4 + c] + e[2] * p[2 * 4 + c];
        if (clip[3] > 1e-4f)
        {
            const float x = clip[0] / clip[3], y = clip[1] / clip[3];
            // fx takes the uv with the origin at the top-left of the picture on a top-left-origin backend; the
            // shim's targets keep D3D's row order on every backend, which on GL is fx's flipped convention.
            const float v_top = 0.5f - 0.5f * y;
            frame.sun_uv = {0.5f + 0.5f * x, bgfx::getCaps()->originBottomLeft ? 1.0f - v_top : v_top};
            // Fades in as the sun clears the horizon; clouds hide it.
            frame.sun_visibility =
                std::clamp(weather_.sun_dir[1] * 8.0f, 0.0f, 1.0f) * (1.0f - std::clamp(weather_.overcast, 0.0f, 1.0f));
        }
        frame.sun_color = weather_.sun_color;
        frame.sun_glare = sun_glare_;
        frame.sun_ghosts = sun_ghosts_;
    }
    if (settings_.scene_grading)
        frame.scene_grade = fx::SceneGrade(sky_state(), weather_.overcast, weather ? 0.0f : 1.0f);
    frame.scene_cut = scene_cut_pending_;
    scene_cut_pending_ = false;
    const uint16_t used = post_.Submit(settings_, frame);
    last_post_first_ = targets.first_view;
    last_post_views_ = used;
    return used;
}

void Host::advance_switch()
{
    shim::Device *device = shim::Device::current;
    if (!switching_ || !device_ready_ || !device || self_test_.Running())
        return;
    ++switch_frames_;
    // The programs of the new settings, one per frame: each is a shader link (tens of milliseconds on a phone,
    // seconds on a first run under ANGLE); linked all at once in the first frame of the new settings they held
    // the picture for seconds. While the previous frame's programs all came from the program cache (loaded, none
    // linked: storm/render/program_cache.hpp) a few more go per frame.
    const render::ProgramCacheStats cache = render::CurrentProgramCacheStats();
    const bool from_cache = cache.loaded != switch_cache_.loaded && cache.stored == switch_cache_.stored;
    switch_cache_ = cache;
    const int per_frame = from_cache ? kCachedProgramsPerFrame : kProgramsPerFrame;
    int budget = per_frame;
    // The post chain's programs also on «Низкая»: the underwater wobble runs on every preset.
    const bool ready = post_.Prepare(target_, budget) && (!target_.sea_soft_edges || sea_depth_.Prepare(budget)) &&
                       (!target_.sea_waves || sea_waves_.Prepare(budget)) &&
                       (target_.render_height == 0 || depth_copy_.Prepare(budget)) &&
                       contact_.Prepare(target_, budget) &&
                       lighting_.prepare_programs(*device, target_, budget);
    switch_programs_ += per_frame - budget;
    if (!ready)
        return;
    // The effect path of the new settings on a known picture (views kPostViewBegin..), once per settings: a
    // settings set that passed on this device is not tested again.
    if (needs_self_test(target_) && std::find(verified_.begin(), verified_.end(), target_) == verified_.end())
    {
        tested_ = target_;
        self_test_.Submit(tested_, render::kPostViewBegin, post_);
        return;
    }
    switching_ = false;
    report_.pending = false;
    activate(target_, target_support_);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - switch_started_).count();
    shim::logf("effects: preset %d (%s) active after %u frames, %.0f ms (%u programs prepared)", preset_,
               std::string(fx::NameRu(static_cast<fx::QualityPreset>(preset_))).c_str(), switch_frames_, ms,
               switch_programs_);
}

void Host::end_frame(bool scene_frame, uint32_t frame)
{
    if (self_test_.Running())
    {
        const fx::SelfTest::Result result = self_test_.Poll(frame);
        if (result.done && result.failed == fx::SelfTest::Stage::None)
        {
            shim::logf("effects self-test passed on %s (preset %d, MSAA %ux)",
                       shim::Context::instance().device_info().renderer.c_str(), preset_,
                       fx::RendererStateFor(tested_).msaa_samples);
            verified_.push_back(tested_);
        }
        if (result.done && result.failed != fx::SelfTest::Stage::None)
        {
            const bool scene = result.failed == fx::SelfTest::Stage::SceneTarget;
            shim::logf("ERROR: effects self-test failed on %s (%s): %s. %s switched off; %s",
                       shim::Context::instance().device_info().renderer.c_str(),
                       bgfx::getRendererName(bgfx::getRendererType()), result.detail.c_str(),
                       scene ? "MSAA" : "the post-processing chain", support_.Describe().c_str());
            // Like a missing capability: ConstrainSettings switches the stage off and reports it.
            (scene ? msaa_broken_ : post_broken_) = true;
            notice_ = std::string("Графика: ") + (scene ? "сглаживание MSAA" : "постобработка") +
                      " не работает на этом GPU и отключена.";
            apply();
        }
    }
    if (!scene_frame)
        scene_cut_pending_ = true;
    weather_last_frame_ = weather_this_frame_;
    if (!weather_this_frame_)
    {
        lighting_.set_sky(nullptr); // interiors: no sky tint on the hemisphere
        lighting_.shadows().set_sun({0.0f, 1.0f, 0.0f}, false); // and no sun shadows
    }
    weather_this_frame_ = false;
    sun_glare_ = sun_ghosts_ = 0.0f;
    lighting_.end_frame();
    {
        const auto now = std::chrono::steady_clock::now();
        const float dt = last_frame_end_ == std::chrono::steady_clock::time_point{}
                             ? 0.0f
                             : std::chrono::duration<float>(now - last_frame_end_).count();
        last_frame_end_ = now;
        // Our lightmaps for the hour's sun, for the next frame's draws.
        lighting_.town_light().update(weather_.sun_dir, weather_.sun_color, weather_last_frame_ && weather_.sun_present,
                                      std::min(dt, 0.25f));
    }
    ao_bound_ = ao_.update(settings_.ao_volume > 0); // for the next frame's draws
    lighting_.set_ao_volume(ao_bound_);
    {
        // The visibility volume and the bounce in its second block, for the next frame's draws.
        const Visibility::Bound *volume = visibility_.update();
        TownLight &town = lighting_.town_light();
        town.set_bounce_scene(settings_.bounce ? bounce_.scene() : nullptr);
        town.set_volume(visibility_.volume());
        Bounce::Light light;
        light.sun = weather_.sun_dir;
        light.sun_present = weather_last_frame_ && weather_.sun_present;
        light.sun_color = weather_last_frame_ ? weather_.sun_color : std::array<float, 3>{0.0f, 0.0f, 0.0f};
        const LampShadows &lamps = lighting_.lamp_shadows();
        light.lamps = &lamps.lamps();
        light.holders = &lamps.holders();
        light.lamp_layout = lamps.layout();
        light.town_sun = town.active();
        light.town_field = town.sun_bounce();
        bounce_.set_enabled(settings_.bounce);
        bounce_.update(visibility_, volume, light);
        lighting_.set_visibility(volume);
    }
    transform_view_ = UINT16_MAX; // the shim's views start again next frame
    times_ = {};
    if (!profile_)
    {
        last_post_views_ = 0;
        return;
    }
    // GPU timer queries per view (BGFX_DEBUG_PROFILER, option fx_gpu_profile=1): the whole frame and the post chain,
    // averaged and logged every kProfileFrames presented frames with the CPU frame time and draw calls. Timer
    // results lag a few frames; a backend without timer queries reports none (logged as n/a).
    const bgfx::Stats *stats = bgfx::getStats();
    if (stats)
    {
        if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd > stats->gpuTimeBegin)
        {
            const double to_ms = 1000.0 / static_cast<double>(stats->gpuTimerFreq);
            times_.frame_ms = static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * to_ms;
            for (uint16_t i = 0; i < stats->numViews; ++i)
            {
                const bgfx::ViewStats &view = stats->viewStats[i];
                if (last_post_views_ && view.view >= last_post_first_ && view.view < last_post_first_ + last_post_views_)
                {
                    const double ms = static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * to_ms;
                    times_.post_ms += ms;
                    // The pass names of fx::PostChain (SetupView).
                    if (std::strncmp(view.name, "fx smaa", 7) == 0)
                        times_.smaa_ms += ms;
                    else if (std::strcmp(view.name, "fx fsr easu") == 0)
                        times_.easu_ms += ms;
                    else if (std::strcmp(view.name, "fx fsr rcas") == 0)
                        times_.rcas_ms += ms;
                }
            }
            times_.post_views = last_post_views_;
            profile_sum_frame_ += times_.frame_ms;
            profile_sum_post_ += times_.post_ms;
            profile_sum_smaa_ += times_.smaa_ms;
            profile_sum_easu_ += times_.easu_ms;
            profile_sum_rcas_ += times_.rcas_ms;
            ++profile_gpu_frames_;
        }
        profile_sum_cpu_ += static_cast<double>(stats->cpuTimeFrame) * 1000.0 / static_cast<double>(stats->cpuTimerFreq);
        profile_draws_ += stats->numDraw;
        if (++profile_count_ == kProfileFrames)
        {
            char gpu[192] = "gpu_frame_ms=n/a gpu_post_ms=n/a (no timer queries)";
            if (profile_gpu_frames_ > 0)
                snprintf(gpu, sizeof(gpu), "gpu_frame_ms=%.2f gpu_post_ms=%.2f gpu_smaa_ms=%.2f gpu_easu_ms=%.2f gpu_rcas_ms=%.2f",
                         profile_sum_frame_ / profile_gpu_frames_, profile_sum_post_ / profile_gpu_frames_,
                         profile_sum_smaa_ / profile_gpu_frames_, profile_sum_easu_ / profile_gpu_frames_,
                         profile_sum_rcas_ / profile_gpu_frames_);
            const Lighting::Counts counts = lighting_.last_frame();
            shim::logf("effects profile: preset %d scene=%ux%u display=%ux%u %s cpu_frame_ms=%.2f draws=%.0f post_views=%u lit_draws=%u "
                       "baked_draws=%u coverage_draws=%u relief_draws=%u gloss_draws=%u aerial_draws=%u shadowed_draws=%u casters=%u ao_draws=%u lamp_draws=%u lamp_casters=%u lamp_slots=%d lamp_static_faces=%d sun_static_tiles=%u sun_static_triangles=%u lightmapped_draws=%u town_relit_draws=%u town_wall_draws=%u",
                       preset_, last_post_size_[0], last_post_size_[1], last_post_size_[2], last_post_size_[3], gpu,
                       profile_sum_cpu_ / kProfileFrames, static_cast<double>(profile_draws_) / kProfileFrames,
                       last_post_views_, counts.lit, counts.baked, counts.coverage, counts.relief, counts.glossy, counts.aerial, counts.shadowed, counts.casters, counts.ao, counts.lamp_lit, counts.lamp_casters,
                       lighting_.lamp_shadows().last_slots(), lighting_.lamp_shadows().last_static_faces(),
                       lighting_.shadows().last_static_casters()[0], lighting_.shadows().last_static_casters()[1], lighting_.town_light().last_counts()[0], lighting_.town_light().last_counts()[1], counts.town_walls);
            profile_count_ = profile_gpu_frames_ = 0;
            profile_sum_frame_ = profile_sum_post_ = profile_sum_cpu_ = 0.0;
            profile_sum_smaa_ = profile_sum_easu_ = profile_sum_rcas_ = 0.0;
            profile_draws_ = 0;
        }
    }
    last_post_views_ = 0;
}

} // namespace storm::effects

namespace storm::render
{
const std::vector<EffectsOptionInfo> &EffectsOptionNames()
{
    static const std::vector<EffectsOptionInfo> names = [] {
        std::vector<EffectsOptionInfo> list{{"fx_preset", "0 Низкая, 1 Средняя, 2 Высокая"},
                                            {"fx_caps_profile", "0 device, 1 gles30-minimal, 2 mali-g5x, 3 mali-g7x, 4 adreno-6xx, "
                                                                "5 adreno-7xx-8xx, 6 powervr, 7 xclipse (development)"},
                                            {"fx_gpu_profile", "1: log GPU timer averages every 30 frames (measurements)"}};
        for (const auto &option : effects::kOptions)
            list.push_back({option.name, option.values});
        return list;
    }();
    return names;
}

EffectsReport ConfigureEffects(int preset, const std::vector<EffectsOption> &options)
{
    return effects::Host::instance().configure(preset, options);
}

void EffectsSceneComplete()
{
    if (shim::Device *device = shim::Device::current)
        device->scene_complete();
}

void SetEffectsTechnique(const char *name)
{
    effects::Host::instance().lighting().set_technique(name);
}

void SetEffectsSeaFrame(const EffectsSeaFrame &frame)
{
    effects::Host::instance().set_sea_frame(frame);
}

void SetEffectsWorldOrigin(const std::array<float, 3> &origin)
{
    effects::Host::instance().lighting().set_world_origin(origin);
}

void SetEffectsLightmaps(std::string_view name, std::span<const std::array<float, 3>> positions,
                         std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
                         std::span<const uint32_t> triangle_lightmap, std::span<const EffectsLightmap> lightmaps)
{
    effects::Host::instance().lighting().town_light().set_lightmaps(name, positions, uvs, indices, triangle_lightmap, lightmaps);
    // The lightmapped ground is where one walks: the visibility volume covers it.
    std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const uint32_t i : indices)
        if (i < positions.size())
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::min(lo[k], positions[i][k]);
                hi[k] = std::max(hi[k], positions[i][k]);
            }
    effects::Host::instance().visibility().set_focus(name, lo, hi);
}

void SetEffectsLocationGeometry(const EffectsLocationGeometry &geometry)
{
    effects::Host::instance().lighting().lamp_shadows().set_geometry(geometry);
    effects::Host::instance().bounce().set_geometry(geometry);
    // The geometry with the triangles of textures that cut holes replaced by the pieces the alpha test keeps
    // (effects/casters.hpp: one rule for every pass), pieces of `piece` metres shrunk to `shrink` of their size.
    const auto cut_out = [&](float piece, float shrink, std::vector<fx::Vec3> &points, std::vector<uint32_t> &triangles) {
        points.assign(geometry.positions.begin(), geometry.positions.end());
        std::vector<bool> cut(geometry.indices.size(), false);
        std::vector<uint32_t> pieces;
        if (geometry.uvs.size() == geometry.positions.size())
            for (const auto &surface : geometry.surfaces)
            {
                const shim::TexStore *store = shim::store_of(surface.texture);
                const effects::CasterMaterial material = effects::location_caster_material(store);
                if (material.cut <= 0.0f || surface.first_index + surface.index_count > geometry.indices.size())
                    continue;
                std::fill(cut.begin() + surface.first_index, cut.begin() + surface.first_index + surface.index_count, true);
                fx::AppendCutTriangles(points, pieces, geometry.uvs,
                                       geometry.indices.subspan(surface.first_index, surface.index_count), *store->cutout,
                                       material.cut, piece, shrink);
            }
        for (size_t i = 0; i < geometry.indices.size(); ++i)
            if (!cut[i])
                triangles.push_back(geometry.indices[i]);
        triangles.insert(triangles.end(), pieces.begin(), pieces.end());
    };
    effects::Lighting &lighting = effects::Host::instance().lighting();
    std::vector<fx::Vec3> points;
    std::vector<uint32_t> triangles;
    // The visibility volume rays through the collision data, as the location's own traces do.
    effects::Host::instance().visibility().set_geometry(
        std::string(geometry.name),
        std::vector<fx::Vec3>(geometry.collision_positions.begin(), geometry.collision_positions.end()),
        std::vector<uint32_t>(geometry.collision_indices.begin(), geometry.collision_indices.end()), geometry.interior,
        geometry.room_lo, geometry.room_hi);
    if (geometry.interior)
    {
        lighting.shadows().set_casters({});
        lighting.town_light().set_occluders({}, {}, {}, {}, {}, {});
        // The AO volume of an interior, from pieces shrunk to points at the bake's occupancy cell size of 0.1 m (holes
        // narrower than a cell are below what the volume resolves).
        const bool room = geometry.room_hi[0] > geometry.room_lo[0] && geometry.room_hi[1] > geometry.room_lo[1] &&
                          geometry.room_hi[2] > geometry.room_lo[2];
        if (room)
            cut_out(0.1f, 0.02f, points, triangles);
    }
    else
    {
        // Outdoors it casts the sun's shadows (in tiles, with its textures' holes) and closes the sky and the sun of our
        // own lightmaps: palm leaves dapple them as they do the colour pass.
        lighting.shadows().set_casters(geometry);
        std::vector<fx::Occluders::Cut> cut(geometry.indices.size() / 3);
        if (geometry.uvs.size() == geometry.positions.size())
            for (const auto &surface : geometry.surfaces)
            {
                const shim::TexStore *store = shim::store_of(surface.texture);
                const effects::CasterMaterial material = effects::location_caster_material(store);
                if (material.cut <= 0.0f || surface.first_index + surface.index_count > geometry.indices.size())
                    continue;
                for (uint32_t t = surface.first_index / 3; t < (surface.first_index + surface.index_count) / 3; ++t)
                    cut[t] = {store->cutout, material.cut};
            }
        lighting.town_light().set_occluders(geometry.name, geometry.positions, geometry.uvs, geometry.indices, std::move(cut),
                                            geometry.colors);
    }
    effects::Host::instance().ao_volumes().set_geometry(std::string(geometry.name), std::move(points), std::move(triangles),
                                                        geometry.room_lo, geometry.room_hi,
                                                        std::vector<fx::Vec3>(geometry.walk.begin(), geometry.walk.end()));
}

void SetEffectsLamps(std::span<const EffectsPointLight> lamps)
{
    std::vector<fx::Lamp> converted;
    converted.reserve(lamps.size());
    for (const EffectsPointLight &l : lamps)
        converted.push_back({l.position, l.color, l.range, l.att0, l.att1, l.att2});
    effects::Host::instance().lighting().lamp_shadows().set_lamps(converted);
}

void SetEffectsDynamicLights(std::span<const EffectsPointLight> lights)
{
    fx::PointLight converted[fx::kMaxPointLights * 4];
    const size_t count = std::min(lights.size(), std::size(converted));
    for (size_t i = 0; i < count; ++i)
    {
        const EffectsPointLight &l = lights[i];
        converted[i] = {.position = l.position, .color = l.color, .range = l.range, .att0 = l.att0, .att1 = l.att1,
                        .att2 = l.att2};
    }
    effects::Host::instance().lighting().set_dynamic_lights(std::span(converted, count));
    // The same lights for the lamps: the nearest to the camera take a moving slot and cast shadows.
    std::vector<fx::Lamp> moving;
    moving.reserve(lights.size());
    for (const EffectsPointLight &l : lights)
        moving.push_back({l.position, l.color, l.range, l.att0, l.att1, l.att2});
    effects::Host::instance().lighting().lamp_shadows().set_moving(std::move(moving));
}

void SetEffectsShipLights(std::span<const EffectsShip> ships, std::span<const EffectsShipLight> lights)
{
    std::vector<effects::LampShadows::ShipLight> converted;
    converted.reserve(lights.size());
    for (const EffectsShipLight &l : lights)
        converted.push_back({{l.light.position, l.light.color, l.light.range, l.light.att0, l.light.att1, l.light.att2},
                             l.ship, l.local});
    effects::Host::instance().lighting().lamp_shadows().set_ship_lights({ships.begin(), ships.end()}, std::move(converted));
}

bool EffectsShipGeometryWanted(uint64_t ship, uint64_t signature)
{
    return effects::Host::instance().lighting().lamp_shadows().ship_geometry_wanted(ship, signature);
}

void SetEffectsShipGeometry(uint64_t ship, uint64_t signature, const EffectsShipGeometry &geometry)
{
    effects::Host::instance().lighting().lamp_shadows().set_ship_geometry(ship, signature, geometry);
}

EffectsShipDraw::EffectsShipDraw(uint64_t ship)
{
    effects::Host::instance().lighting().lamp_shadows().set_drawn_ship(ship);
}

EffectsShipDraw::~EffectsShipDraw()
{
    effects::Host::instance().lighting().lamp_shadows().set_drawn_ship(0);
}

void SetEffectsTextureSource(storm::shim::BaseTexture *texture, const char *path, const std::filesystem::path &file)
{
    if (shim::TexStore *store = shim::store_of(texture))
    {
        store->cutout = effects::texture_cutout(*store);
        store->albedo = effects::texture_albedo(*store);
        effects::Host::instance().lighting().normal_maps().on_texture_loaded(*store, path, file);
        std::string lower(path ? path : "");
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        store->lightmap = lower.find("lighting") != std::string::npos && lower.find("shadow.tga") != std::string::npos;
    }
}

void ConfigureRelief(const ReliefConfig &config)
{
    effects::ConfigureRelief({config.directory, config.textures, config.model});
}

void ShutdownRelief()
{
    effects::ReliefGenerator::instance().shutdown();
}

void RemoveOldReliefStores(const std::vector<std::filesystem::path> &directories, const std::filesystem::path &live)
{
    std::error_code error;
    const std::filesystem::path current = std::filesystem::weakly_canonical(live, error);
    // `a` is `b` or one of its ancestors (lexically, after resolving links and dots).
    const auto within = [](const std::filesystem::path &a, const std::filesystem::path &b) {
        auto ai = a.begin(), bi = b.begin();
        for (; ai != a.end() && bi != b.end(); ++ai, ++bi)
            if (*ai != *bi)
                return false;
        return ai == a.end() || (ai->empty() && std::next(ai) == a.end());
    };
    for (const std::filesystem::path &old : directories)
    {
        error.clear();
        if (!std::filesystem::is_directory(old, error))
            continue;
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(old, error);
        if (!live.empty() && (within(resolved, current) || within(current, resolved)))
        {
            shim::logf("ERROR: effects: %s is not removed as an old relief store: it is or holds the relief directory in "
                       "use (%s)",
                       old.string().c_str(), current.string().c_str());
            continue;
        }
        uintmax_t bytes = 0, files = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(old, error))
            if (entry.is_regular_file(error))
            {
                bytes += entry.file_size(error);
                ++files;
            }
        std::filesystem::remove_all(old, error);
        if (error)
            shim::logf("ERROR: effects: the old relief store %s could not be removed (%s)", old.string().c_str(),
                       error.message().c_str());
        else
            shim::logf("effects: the old relief store %s of earlier builds removed (%ju files, %.1f MB)", old.string().c_str(),
                       files, static_cast<double>(bytes) / 1048576.0);
    }
}

std::string TakeEffectsNotice()
{
    return effects::Host::instance().take_notice();
}

EffectsReport CurrentEffects()
{
    return effects::Host::instance().report();
}

void SetEffectsWeather(const EffectsWeather &weather)
{
    effects::Host::instance().set_weather(weather);
}

void SetEffectsWobble(const EffectsWobble &wobble)
{
    effects::Host::instance().set_wobble(wobble);
}

void SetEffectsSunGlare(float glare, float ghosts)
{
    effects::Host::instance().set_sun_glare(glare, ghosts);
}
} // namespace storm::render
