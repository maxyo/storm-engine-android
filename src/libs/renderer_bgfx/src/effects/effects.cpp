#include "effects/effects.hpp"

#include "shim/device.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/sky_model.hpp"

#include <GLES2/gl2.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bgfx::gl
{
extern float g_stormMaxAnisotropy; // patches/bgfx-anisotropy-level.patch
}

namespace storm::effects
{

namespace
{
void log_sink(fx::LogLevel level, const char *message)
{
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
    {"fx_fxaa", "0 off, 1 on", [](fx::Settings &s, int v) { s.fxaa = on(v); }},
    {"fx_sharpen", "0 off, 1 on", [](fx::Settings &s, int v) { s.sharpen = on(v); }},
    {"fx_ssao", "0 off, 1 on", [](fx::Settings &s, int v) { s.ssao = on(v); }},
    {"fx_dither", "0 off, 1 on", [](fx::Settings &s, int v) { s.dither = on(v); }},
    {"fx_lens_flare", "0 off, 1 on", [](fx::Settings &s, int v) { s.lens_flare = on(v); }},
    {"fx_scene_grading", "0 off, 1 on", [](fx::Settings &s, int v) { s.scene_grading = on(v); }},
    {"fx_grade", "0 Оригинал, 1 Карибы, 2 Пасмурно",
     [](fx::Settings &s, int v) { s.grade = static_cast<fx::GradePreset>(std::clamp(v, 0, 2)); }},
    {"fx_vignette", "0..100 (%)", [](fx::Settings &s, int v) { s.vignette = std::clamp(v, 0, 100) / 100.0f; }},
    {"fx_film_grain", "0..100 (%)", [](fx::Settings &s, int v) { s.film_grain = std::clamp(v, 0, 100) / 100.0f; }},
    {"fx_msaa", "1, 2, 4", [](fx::Settings &s, int v) { s.msaa = static_cast<uint8_t>(std::clamp(v, 1, 4)); }},
    {"fx_alpha_to_coverage", "0 off, 1 on", [](fx::Settings &s, int v) { s.alpha_to_coverage = on(v); }},
    {"fx_anisotropy", "1, 4, 8", [](fx::Settings &s, int v) { s.anisotropy = static_cast<uint8_t>(std::clamp(v, 1, 16)); }},
    {"fx_contact_shadows", "0 off, 1 on", [](fx::Settings &s, int v) { s.contact_shadows = on(v); }},
    {"fx_water", "0 legacy, 1 low, 2 medium, 3 high",
     [](fx::Settings &s, int v) { s.water = static_cast<fx::WaterQuality>(std::clamp(v, 0, 3)); }},
    {"fx_water_refraction", "0 off, 1 on", [](fx::Settings &s, int v) { s.water_refraction = on(v); }},
    {"fx_sky", "0 off, 1 on", [](fx::Settings &s, int v) { s.sky_atmosphere = on(v); }},
    {"fx_clouds", "0 off, 1 on", [](fx::Settings &s, int v) { s.sky_clouds = on(v); }},
    {"fx_shadows", "0 off, 1 low, 2 soft",
     [](fx::Settings &s, int v) { s.shadows = static_cast<fx::ShadowQuality>(std::clamp(v, 0, 2)); }},
    {"fx_point_lights", "4, 8", [](fx::Settings &s, int v) { s.point_lights = static_cast<uint8_t>(v >= 8 ? 8 : 4); }},
    {"fx_light_per_pixel", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingPerPixel, v); }},
    {"fx_light_hemisphere", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingHemisphere, v); }},
    {"fx_light_specular", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingSpecular, v); }},
    {"fx_light_translucent", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingTranslucent, v); }},
    {"fx_light_point", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingPoint, v); }},
    {"fx_light_normal_map", "0 off, 1 on", [](fx::Settings &s, int v) { set_flag(s, fx::kLightingNormalMap, v); }},
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
    {[](const fx::Settings &s) { return s.shadows != fx::ShadowQuality::Off; },
     [](fx::Settings &s) { s.shadows = fx::ShadowQuality::Off; }, "fx_shadows",
     "sun shadows are not wired into this renderer yet: switched off"},
};

std::string describe(const fx::Settings &s)
{
    char text[512];
    snprintf(text, sizeof(text),
             "tonemap=%d bloom=%d(1/%u,%u) shafts=%d fxaa=%d sharpen=%d ssao=%d dither=%d flare=%d scene_grading=%d "
             "grade=%d vignette=%.2f grain=%.2f msaa=%u a2c=%d aniso=%u sky=%d clouds=%d water=%d shadows=%d "
             "lighting=0x%x contact=%d",
             s.tonemap, s.bloom, s.bloom_downscale, s.bloom_levels, s.sun_shafts, s.fxaa, s.sharpen, s.ssao, s.dither,
             s.lens_flare, s.scene_grading, static_cast<int>(s.grade), s.vignette, s.film_grain, s.msaa,
             s.alpha_to_coverage, s.anisotropy, s.sky_atmosphere, s.sky_clouds, static_cast<int>(s.water),
             static_cast<int>(s.shadows), s.lighting_flags, s.contact_shadows);
    return text;
}

bool has_extension(const char *name)
{
    const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (!extensions)
        return false;
    const size_t length = strlen(name);
    for (const char *p = extensions; (p = strstr(p, name)) != nullptr; p += length)
        if ((p == extensions || p[-1] == ' ') && (p[length] == ' ' || p[length] == '\0'))
            return true;
    return false;
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
    fx::RendererFacts facts;
    // The game's offscreen depth is a D24S8 BGFX_TEXTURE_RT texture (TexStore::create_gpu): sampleable. With MSAA
    // the pinned bgfx GL backend blits the depth into that texture too when the frame buffer is resolved
    // (FrameBufferGL::resolve, GL_DEPTH_BUFFER_BIT for readable depth attachments).
    facts.scene_depth = true;
    facts.msaa_depth = true;
    // bgfx has no capability bit for it; the GL backend applies it only with this extension.
    facts.anisotropy = has_extension("GL_EXT_texture_filter_anisotropic");
    if (facts.anisotropy)
    {
        constexpr GLenum kMaxAnisotropy = 0x84FF; // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
        glGetFloatv(kMaxAnisotropy, &device_max_anisotropy_);
    }
    support_ = fx::DetectSupport(facts);
    device_ready_ = true;
    apply();
    renderer_state_changed_ = false; // the scene target is created after this, with the constrained state
}

void Host::on_device_lost()
{
    post_.OnDeviceLost();
    lighting_.on_device_lost();
    sky_.OnDeviceLost();
    water_.OnDeviceLost();
    contact_.OnDeviceLost();
    device_ready_ = false;
}

void Host::shutdown()
{
    post_.Shutdown();
    lighting_.shutdown();
    sky_.Shutdown();
    water_.Shutdown();
    contact_.Shutdown();
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
    for (const auto &option : options_)
    {
        if (option.value < 0)
            continue; // the preset's value
        if (option.name == "fx_gpu_profile")
            profile = option.value != 0;
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
        next = fx::ConstrainSettings(next, support_, report_.changes);
        report_.support = support_.Describe();
        // Options the device can never honour, for the settings screen.
        fx::Settings all = fx::Settings::FromPreset(fx::QualityPreset::High);
        all.fxaa = true;
        std::vector<std::string> unused;
        const fx::Settings possible = fx::ConstrainSettings(all, support_, unused);
        if (possible.msaa <= 1)
            report_.disabled.push_back("fx_msaa");
        if (possible.anisotropy <= 1)
            report_.disabled.push_back("fx_anisotropy");
        if (!possible.ssao)
            report_.disabled.push_back("fx_ssao");
        if (possible.PostIsIdentity())
            for (const char *name : {"fx_tonemap", "fx_bloom", "fx_sun_shafts", "fx_fxaa", "fx_sharpen", "fx_dither",
                                     "fx_lens_flare", "fx_scene_grading", "fx_grade", "fx_vignette", "fx_film_grain"})
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
    const bool changed = !(next == settings_) || !device_ready_;
    settings_ = next;
    const fx::RendererState state = fx::RendererStateFor(settings_);
    renderer_state_changed_ = renderer_state_changed_ ||
                              (device_ready_ && (state.msaa_samples != renderer_state_.msaa_samples ||
                                                 state.max_anisotropy != renderer_state_.max_anisotropy));
    renderer_state_ = state;
    // The level of every anisotropic sampler (patches/bgfx-anisotropy-level.patch), applied at the next reset.
    bgfx::gl::g_stormMaxAnisotropy = static_cast<float>(state.max_anisotropy);
    if (device_ready_ && state.max_anisotropy > 1 && device_max_anisotropy_ < state.max_anisotropy)
        report_.changes.push_back("anisotropic filtering " + std::to_string(state.max_anisotropy) + "x: the device allows " +
                                  std::to_string(static_cast<int>(device_max_anisotropy_)) + "x");
    lighting_.configure(settings_, renderer_state_);
    report_.summary = describe(settings_);
    if (changed)
    {
        shim::logf("effects: preset %d (%s): %s", preset_,
                   std::string(fx::NameRu(static_cast<fx::QualityPreset>(preset_))).c_str(), report_.summary.c_str());
        for (const auto &line : report_.changes)
            shim::logf("effects: preset %d on this device: %s", preset_, line.c_str());
    }
}

void Host::set_weather(const render::EffectsWeather &weather)
{
    weather_ = weather;
    weather_this_frame_ = true;
    // The hemisphere ambient takes its tint from the sky dome; the aerial perspective needs a sky (no interiors).
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
    frame.target_width = targets.width;
    frame.target_height = targets.height;
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

void Host::end_frame(bool scene_frame)
{
    if (!scene_frame)
        scene_cut_pending_ = true;
    weather_last_frame_ = weather_this_frame_;
    if (!weather_this_frame_)
        lighting_.set_sky(nullptr); // interiors: no sky tint on the hemisphere
    weather_this_frame_ = false;
    lighting_.end_frame();
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
                    times_.post_ms += static_cast<double>(view.gpuTimeEnd - view.gpuTimeBegin) * to_ms;
            }
            times_.post_views = last_post_views_;
            profile_sum_frame_ += times_.frame_ms;
            profile_sum_post_ += times_.post_ms;
            ++profile_gpu_frames_;
        }
        profile_sum_cpu_ += static_cast<double>(stats->cpuTimeFrame) * 1000.0 / static_cast<double>(stats->cpuTimerFreq);
        profile_draws_ += stats->numDraw;
        if (++profile_count_ == kProfileFrames)
        {
            char gpu[96] = "gpu_frame_ms=n/a gpu_post_ms=n/a (no timer queries)";
            if (profile_gpu_frames_ > 0)
                snprintf(gpu, sizeof(gpu), "gpu_frame_ms=%.2f gpu_post_ms=%.2f", profile_sum_frame_ / profile_gpu_frames_,
                         profile_sum_post_ / profile_gpu_frames_);
            const Lighting::Counts counts = lighting_.last_frame();
            shim::logf("effects profile: preset %d %s cpu_frame_ms=%.2f draws=%.0f post_views=%u lit_draws=%u "
                       "baked_draws=%u coverage_draws=%u relief_draws=%u aerial_draws=%u",
                       preset_, gpu, profile_sum_cpu_ / kProfileFrames, static_cast<double>(profile_draws_) / kProfileFrames,
                       last_post_views_, counts.lit, counts.baked, counts.coverage, counts.relief, counts.aerial);
            profile_count_ = profile_gpu_frames_ = 0;
            profile_sum_frame_ = profile_sum_post_ = profile_sum_cpu_ = 0.0;
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
        std::vector<EffectsOptionInfo> list{{"fx_preset", "0 Оригинал, 1 Среднее, 2 Высокое"},
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
}

void SetEffectsTextureSource(IDirect3DBaseTexture9 *texture, const char *path)
{
    if (shim::TexStore *store = shim::store_of(texture))
        effects::Host::instance().lighting().normal_maps().on_texture_loaded(*store, path);
}

EffectsReport CurrentEffects()
{
    return effects::Host::instance().report();
}

void SetEffectsWeather(const EffectsWeather &weather)
{
    effects::Host::instance().set_weather(weather);
}
} // namespace storm::render
