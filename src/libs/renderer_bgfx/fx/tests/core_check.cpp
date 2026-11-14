// Unit checks for the CPU side of the fx package: grading LUTs, tone curve,
// baked noise, the sky model, lighting and point-light selection, the shadow cascade and the
// explicit handling of unsupported settings. No GPU is needed.
#include "storm/fx/ao_volume.hpp"
#include "storm/fx/bounce.hpp"
#include "storm/fx/cutout.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/sun_fit.hpp"
#include "storm/fx/town_light.hpp"
#include "storm/fx/visibility.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/light_set.hpp"
#include "storm/fx/lighting_model.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/noise_bake.hpp"
#include "storm/fx/relief_model.hpp"
#include "storm/fx/gloss_map.hpp"
#include "storm/fx/height_map.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/parallax_exclusions.hpp"
#include "storm/fx/parallax_probe.hpp"
#include "storm/fx/parallax_trust.hpp"
#include "storm/fx/relief_store.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/wave_clock.hpp"
#include "storm/fx/shadow_model.hpp"
#include "storm/fx/sky_model.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>

using namespace storm::fx;

namespace
{
int failures = 0;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                                        \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (false)

bool Near(float a, float b, float eps)
{
    return std::fabs(a - b) <= eps;
}

float Chroma(std::array<float, 3> c)
{
    return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
}

void CheckSettings()
{
    const Settings original = Settings::FromPreset(QualityPreset::Low);
    CHECK(original.PostIsIdentity());
    CHECK(!original.sea_band_limit && !original.sea_soft_edges && !original.sea_specular);
    CHECK(original.lighting_flags == kLightingNone);
    const Settings medium = Settings::FromPreset(QualityPreset::Medium);
    const Settings high = Settings::FromPreset(QualityPreset::High);
    CHECK(!medium.PostIsIdentity() && !high.PostIsIdentity());
    CHECK(medium.bloom_downscale == 4 && high.bloom_downscale == 2);
    // FSR 1 on every preset, by an internal scene height; «Низкая» otherwise the original look.
    CHECK(original.sharpen && medium.sharpen && high.sharpen);
    // Monotonic: a higher preset never renders the scene smaller («Низкая» is the fastest).
    CHECK(original.render_height > 0 && original.render_height <= medium.render_height &&
          medium.render_height <= high.render_height);
    // On the owner's 3200x1440 panel all three scale; a 1080-line phone scales Низкая and Средняя; 720 lines none.
    CHECK(RenderScale(original.render_height, 1440) == 0.625f && RenderScale(medium.render_height, 1440) < 0.7f &&
          RenderScale(high.render_height, 1440) < 0.8f);
    CHECK(RenderScale(original.render_height, 1080) < 0.85f && RenderScale(medium.render_height, 1080) < 1.0f &&
          RenderScale(high.render_height, 1080) == 1.0f);
    CHECK(RenderScale(original.render_height, 720) == 1.0f && RenderScale(medium.render_height, 720) == 1.0f &&
          RenderScale(high.render_height, 720) == 1.0f);
    CHECK(RenderScale(900, 2160) == 0.5f && RenderScale(0, 2160) == 1.0f); // never below FSR 1's range
    // MSAA is the primary anti-aliasing of the presets; SMAA is left to the player. Optional looks stay off.
    CHECK(medium.msaa == 4 && high.msaa == 4 && !medium.smaa && !high.smaa && original.msaa == 1);
    CHECK(medium.anisotropy == 4 && high.anisotropy == 8 && original.anisotropy == 1);
    CHECK(medium.dither && !original.dither);
    // The game's own sea everywhere: the band limit from «Средняя», soft edges at «Высокая», the specular optional.
    CHECK(medium.sea_band_limit && !medium.sea_soft_edges && high.sea_band_limit && high.sea_soft_edges);
    CHECK(!medium.sea_specular && !high.sea_specular);
    // Waves from objects: Средняя and Высокая, never Низкая; without vertex fetch only the lift goes, loudly.
    CHECK(medium.sea_waves && medium.sea_waves_lift && medium.sea_wakes && high.sea_waves && high.sea_waves_lift &&
          high.sea_wakes);
    CHECK(!original.sea_waves && !original.sea_waves_lift && !original.sea_wakes);
    {
        Support no_vtf;
        no_vtf.sea_waves_vertex = false;
        std::vector<std::string> changes;
        const Settings constrained = ConstrainSettings(high, no_vtf, changes);
        CHECK(constrained.sea_waves && !constrained.sea_waves_lift && constrained.sea_wakes);
        bool reported = false;
        for (const std::string &line : changes)
            reported |= line.find("wave lift") != std::string::npos;
        CHECK(reported);
    }
    // Gloss maps on «Высокая» only for now (their cost on «Средняя» waits for phone numbers).
    CHECK((medium.lighting_flags & kLightingPerPixel) && !(medium.lighting_flags & kLightingSpecular) && !medium.gloss &&
          high.gloss && !original.gloss && medium.gloss_map_size == 128 && high.gloss_map_size == 256 &&
          high.gloss_strength == 1.0f);
    CHECK(medium.shadows == ShadowQuality::Low && high.shadows == ShadowQuality::Soft && original.shadows == ShadowQuality::Off);
    CHECK(!medium.ssao && high.ssao && !original.ssao);
    CHECK(original.ao_volume == 0 && medium.ao_volume == 2 && high.ao_volume == 2); // Низкая: no shadows, as the original
    CHECK(original.lamp_shadows == 0 && medium.lamp_shadows == 8 && high.lamp_shadows == 16);
    CHECK(!original.town_light && medium.town_light && high.town_light); // Низкая keeps the game's town lightmaps
    CHECK(!medium.lamp_soft && high.lamp_soft && high.lamp_baked == 0.25f && high.lamp_gain == 1.0f && high.lamp_debug == 0);
    CHECK(medium.lamp_model_lift == 0.2f && high.lamp_model_lift == 0.2f); // the owner: characters a little above the room
    CHECK(medium.lamp_tint == 0.25f && high.lamp_tint == 0.25f); // the owner: a quarter of the lamps' own colour on walls
    CHECK(original.lamp_moving == 0 && medium.lamp_moving == 1 && high.lamp_moving == 2); // carried lanterns, ship lanterns
    // «Средняя»: the cheap looks (tone curve, the grade of the place and hour, aerial perspective, rim light) under
    // the restrained Caribbean grade; «Высокая» adds the sun glare and sun shafts. «Низкая» has none of them.
    for (const Settings *preset : {&medium, &high})
        CHECK(preset->tonemap && preset->tonemap_strength == 0.6f && preset->scene_grading && preset->grade == GradePreset::Caribbean &&
              preset->grade_strength <= 0.4f && (preset->lighting_flags & kLightingRim) && (preset->lighting_flags & kLightingAerial));
    CHECK(!medium.lens_flare && !medium.sun_shafts);
    CHECK(high.lens_flare && high.lens_flare_intensity == 0.5f && high.sun_shafts && high.sun_shafts_intensity == 0.35f);
    CHECK(!original.tonemap && !original.scene_grading && !original.lens_flare && !original.sun_shafts);
    Settings graded = original;
    graded.grade = GradePreset::Caribbean;
    CHECK(!graded.PostIsIdentity());
    graded.grade_strength = 0.0f;
    CHECK(graded.PostIsIdentity());
    CHECK(NameRu(QualityPreset::High) == "Высокая" && NameRu(GradePreset::Overcast) == "Пасмурно");
}

void CheckLut()
{
    // Identity: every texel is its own coordinate, and the trilinear fetch returns the input.
    const int n = kDefaultLutSize;
    const std::vector<uint8_t> identity = BuildLutRgba8(ColorGrade::Preset(GradePreset::Original), n);
    CHECK(identity.size() == static_cast<size_t>(n) * n * n * 4);
    bool exact = true;
    for (int b = 0; b < n; ++b)
        for (int g = 0; g < n; ++g)
            for (int r = 0; r < n; ++r)
            {
                const uint8_t *t = &identity[((static_cast<size_t>(b) * n + g) * n + r) * 4];
                const auto expect = [](int i) { return static_cast<int>(std::lround(i * 255.0f / (n - 1))); };
                exact = exact && t[0] == expect(r) && t[1] == expect(g) && t[2] == expect(b) && t[3] == 255;
            }
    CHECK(exact);
    for (float v : {0.0f, 0.1f, 0.37f, 0.5f, 0.93f, 1.0f})
    {
        const auto out = SampleLut(identity, n, {v, 1.0f - v, 0.5f * v});
        CHECK(Near(out[0], v, 0.003f) && Near(out[1], 1.0f - v, 0.003f) && Near(out[2], 0.5f * v, 0.003f));
    }

    const ColorGrade caribbean = ColorGrade::Preset(GradePreset::Caribbean);
    const ColorGrade overcast = ColorGrade::Preset(GradePreset::Overcast);
    CHECK(ColorGrade{}.IsIdentity() && !caribbean.IsIdentity() && !overcast.IsIdentity());

    // Black and white stay (nearly) black and white, greys stay monotonic.
    for (const ColorGrade &grade : {caribbean, overcast})
    {
        CHECK(grade.Apply({0.0f, 0.0f, 0.0f})[1] < 0.03f);
        CHECK(grade.Apply({1.0f, 1.0f, 1.0f})[1] > 0.93f);
        float previous = -1.0f;
        for (int i = 0; i <= 64; ++i)
        {
            const float v = static_cast<float>(i) / 64.0f;
            const auto out = grade.Apply({v, v, v});
            const float luma = 0.2126f * out[0] + 0.7152f * out[1] + 0.0722f * out[2];
            CHECK(luma >= previous - 1e-4f);
            previous = luma;
            for (float c : out)
                CHECK(c >= 0.0f && c <= 1.0f);
        }
    }
    // «Карибы» is warmer and more saturated, «Пасмурно» cooler and less saturated.
    const std::array<float, 3> sea{0.20f, 0.45f, 0.60f}, grey{0.5f, 0.5f, 0.5f};
    CHECK(Chroma(caribbean.Apply(sea)) > Chroma(sea) * 1.08f);
    CHECK(Chroma(overcast.Apply(sea)) < Chroma(sea) * 0.85f);
    CHECK(caribbean.Apply(grey)[0] > caribbean.Apply(grey)[2] + 0.02f);
    CHECK(overcast.Apply(grey)[2] > overcast.Apply(grey)[0] + 0.02f);
    // Neither preset changes mid-grey brightness by much.
    CHECK(Near(caribbean.Apply(grey)[1], 0.5f, 0.05f) && Near(overcast.Apply(grey)[1], 0.5f, 0.06f));
    // Interpolation: the end points are the presets, the middle is in between.
    const ColorGrade half = ColorGrade::Lerp(ColorGrade{}, caribbean, 0.5f);
    CHECK(Near(half.saturation, 0.5f * (1.0f + caribbean.saturation), 1e-6f));
    CHECK(ColorGrade::Lerp(ColorGrade{}, caribbean, 0.0f).IsIdentity());
    // A LUT of another size has the right number of texels.
    CHECK(BuildLutRgba8(caribbean, 16).size() == 16u * 16u * 16u * 4u);
}

void CheckToneCurve()
{
    // Mid grey keeps its brightness, the curve is monotonic and stays inside 0..1.
    const float grey = std::sqrt(0.18f);
    CHECK(Near(TonemapTransfer(grey), grey, 0.004f));
    float previous = -1.0f;
    for (int i = 0; i <= 100; ++i)
    {
        const float v = TonemapTransfer(static_cast<float>(i) / 100.0f);
        CHECK(v >= previous && v >= 0.0f && v <= 1.0f);
        previous = v;
    }
    // AgX shape: slightly darker shadows, white rolled off instead of clipped (the full curve takes it to about
    // 0.87, the default strength keeps it above 0.9).
    CHECK(TonemapTransfer(0.2f) < 0.2f);
    CHECK(TonemapTransfer(1.0f) > 0.85f && TonemapTransfer(1.0f) < 0.9f);
    CHECK(TonemapTransfer(1.0f, 1.0f, Settings{}.tonemap_strength) > 0.9f);
    // At the default strength shadows are only mildly deepened.
    const Settings defaults;
    CHECK(TonemapTransfer(0.1f, 1.0f, defaults.tonemap_strength) > 0.055f);
    CHECK(Near(TonemapTransfer(0.37f, 1.0f, 0.0f), 0.37f, 1e-5f));
    CHECK(Near(ExpandLdr(1.0f), 3.0f, 1e-4f) && Near(ExpandLdr(0.0f), 0.0f, 1e-6f));
}

void CheckNoise()
{
    for (int i = 0; i < 300; ++i)
    {
        const float u = 0.013f * i, v = 0.007f * i;
        CHECK(Near(PeriodicFbm(u, v, 4, 3, 9u), PeriodicFbm(u + 1.0f, v + 2.0f, 4, 3, 9u), 2e-3f));
        CHECK(std::fabs(PeriodicFbm(u, v, 4, 3, 9u)) <= 1.0f);
    }
}

void CheckSky()
{
    const auto sky_for = [](float sun_height, float overcast) {
        WeatherInput weather;
        const float c = std::sqrt(1.0f - sun_height * sun_height);
        weather.sun_dir = {c, sun_height, 0.0f};
        weather.sun_color = {0.86f, 0.82f, 0.78f};
        weather.ambient_color = {0.37f, 0.37f, 0.35f};
        weather.fog_color = {0.31f, 0.47f, 0.61f};
        weather.overcast = overcast;
        return ComputeSky(weather);
    };
    const SkyState noon = sky_for(0.85f, 0.0f);
    CHECK(noon.lut.size() == static_cast<size_t>(kSkyLutWidth) * kSkyLutHeight * 4);
    const Vec3 zenith = SkyColor(noon, {0.0f, 1.0f, 0.0f});
    const Vec3 horizon = SkyColor(noon, {-1.0f, 0.0f, 0.0f});
    // Clear day: blue zenith, brighter and paler horizon.
    CHECK(zenith[2] > zenith[0] * 1.8f && zenith[2] > zenith[1]);
    CHECK(horizon[0] + horizon[1] + horizon[2] > zenith[0] + zenith[1] + zenith[2]);
    CHECK(horizon[2] / std::max(horizon[0], 1e-4f) < zenith[2] / std::max(zenith[0], 1e-4f));
    CHECK(noon.night < 0.01f);

    // Sunset: the horizon towards the sun is warm, the opposite side is not; sunlight reddens.
    const SkyState sunset = sky_for(0.03f, 0.0f);
    const Vec3 towards = SkyColor(sunset, {0.999f, 0.04f, 0.0f});
    const Vec3 away = SkyColor(sunset, {-0.999f, 0.04f, 0.0f});
    CHECK(towards[0] > towards[2] * 1.15f);
    CHECK(towards[0] / std::max(towards[2], 1e-4f) > 1.3f * away[0] / std::max(away[2], 1e-4f));
    CHECK(sunset.sun_light[0] > sunset.sun_light[2] * 1.2f);
    CHECK(noon.sun_light[0] < noon.sun_light[2] * 1.5f);

    // Night: dark but not black, no direct light.
    const SkyState night = sky_for(-0.5f, 0.0f);
    const Vec3 night_zenith = SkyColor(night, {0.0f, 1.0f, 0.0f});
    CHECK(night.night > 0.99f);
    CHECK(night_zenith[2] > 0.002f && night_zenith[2] < 0.06f);
    CHECK(night.sun_light[0] < 1e-3f);

    // Overcast: grey, darker dome, weaker sun.
    const SkyState storm = sky_for(0.85f, 1.0f);
    const Vec3 storm_zenith = SkyColor(storm, {0.0f, 1.0f, 0.0f});
    CHECK(storm_zenith[2] / std::max(storm_zenith[0], 1e-4f) < 1.6f);
    CHECK(storm.sun_light[1] < noon.sun_light[1] * 0.5f);

    // `horizon` is the average of the horizon row.
    CHECK(noon.horizon[1] > 0.5f * horizon[1] && noon.horizon[1] < 2.0f * horizon[1]);
    // Below the horizon the panorama clamps to the horizon row.
    const Vec3 below = SkyColor(noon, {-0.8f, -0.6f, 0.0f});
    CHECK(Near(below[1], horizon[1], 1e-4f));

    WeatherInput a, b;
    CHECK(!SkyNeedsRebuild(a, b));
    b.wind = {0.0f, 1.0f};
    CHECK(!SkyNeedsRebuild(a, b));
    b.overcast = 0.5f;
    CHECK(SkyNeedsRebuild(a, b));
}

void CheckLighting()
{
    LightingInput input;
    input.sun_dir = {0.6f, 0.8f, 0.0f};
    input.sun_diffuse = {0.9f, 0.8f, 0.7f};
    input.ambient = {0.4f, 0.4f, 0.4f};
    input.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const LightingUniforms plain = ComputeLighting(input, LightingMaterial::Hull(), nullptr, Settings{});
    CHECK(Near(plain.u_fxLight[0][0], 0.6f, 1e-6f) && Near(plain.u_fxLight[0][1], 0.8f, 1e-6f));
    CHECK(Near(plain.u_fxLight[2][1], 1.0f, 1e-6f));
    // Sky and ground ambient average to the flat ambient the legacy pipeline used.
    for (int i = 0; i < 3; ++i)
        CHECK(Near(0.5f * (plain.u_fxLight[3][i] + plain.u_fxLight[4][i]), 0.4f, 1e-5f));
    CHECK(plain.u_fxLight[3][1] > plain.u_fxLight[4][1]);
    CHECK(plain.u_fxLight[0][3] == LightingMaterial::Hull().specular_power);

    // A rotated camera rotates the vectors, not their length.
    input.view = {0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 5, 6, 7, 1};
    const LightingUniforms rotated = ComputeLighting(input, LightingMaterial::Sail(), nullptr, Settings{});
    CHECK(Near(rotated.u_fxLight[0][2], 0.6f, 1e-6f) && Near(rotated.u_fxLight[0][1], 0.8f, 1e-6f));
    CHECK(rotated.u_fxLight[2][3] == LightingMaterial::Sail().translucency);
    // The sunlight through sails scales with Settings::sail_light (fx_sail_light) and only lights sails.
    Settings brighter;
    brighter.sail_light = 2.0f;
    CHECK(Near(ComputeLighting(input, LightingMaterial::Sail(), nullptr, brighter).u_fxLight[2][3],
               2.0f * LightingMaterial::Sail().translucency, 1e-6f));
    CHECK(ComputeLighting(input, LightingMaterial::Hull(), nullptr, brighter).u_fxLight[2][3] == 0.0f);

    // With a sky the upper hemisphere takes its hue (bluer than the lower one).
    SkyState sky;
    sky.dome = {0.2f, 0.4f, 0.8f};
    const LightingUniforms tinted = ComputeLighting(input, LightingMaterial::Character(), &sky, Settings{});
    // Relief scale: per material (the texture class sets the strength), scaled by the preset; none on characters.
    Settings half;
    half.normal_map_strength = 0.5f;
    CHECK(Near(ComputeLighting(input, LightingMaterial::Stone(), nullptr, half).u_fxLight[5][0], 0.5f, 1e-6f));
    CHECK(LightingMaterial::Stone().relief == LightingMaterial::Hull().relief && LightingMaterial::Hull().relief > LightingMaterial::Sail().relief);
    CHECK(tinted.u_fxLight[5][0] == 0.0f);
    CHECK(tinted.u_fxLight[3][2] > tinted.u_fxLight[3][0]);
    CHECK(tinted.u_fxLight[4][0] > tinted.u_fxLight[4][2]);

    Settings s;
    CHECK(LightingDefines(s).empty() && PointLightBudget(s) == 0);
    s.lighting_flags = kLightingPerPixel | kLightingHemisphere;
    CHECK(LightingDefines(s) == "FX_LIGHT_PER_PIXEL;FX_LIGHT_HEMISPHERE");
    s.lighting_flags = kLightingPoint; // baked geometry: dynamic point lights only
    CHECK(LightingDefines(s) == "FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=4" && PointLightBudget(s) == 4);
    CHECK(LightingDefines(Settings::FromPreset(QualityPreset::Medium)) ==
          "FX_LIGHT_PER_PIXEL;FX_LIGHT_TRANSLUCENT;FX_LIGHT_SHADOW;FX_LIGHT_RIM;FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=4;"
          "FX_LIGHT_NORMALMAP;FX_AERIAL");
    CHECK(LightingDefines(Settings::FromPreset(QualityPreset::High)) ==
          "FX_LIGHT_PER_PIXEL;FX_LIGHT_SPECULAR;FX_LIGHT_TRANSLUCENT;FX_LIGHT_SHADOW;FX_LIGHT_SHADOW_SOFT;"
          "FX_LIGHT_RIM;FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=8;FX_LIGHT_NORMALMAP;FX_AERIAL");
    CHECK(PointLightBudget(Settings::FromPreset(QualityPreset::High)) == 8);
}

// Nothing is downgraded silently: every unsupported request is reported and fixed explicitly.
void CheckSupport()
{
    Support all;
    all.lut_3d = all.shadow_map = all.scene_depth = all.normal_map = true;
    all.msaa_samples = 4;
    all.msaa_depth = all.anisotropy = all.ao_volume = all.smaa = all.sea_waves_vertex = all.sea_waves_float = true;
    for (QualityPreset preset : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High})
    {
        const Settings settings = Settings::FromPreset(preset);
        CHECK(ValidateSettings(settings, all).empty());
        std::vector<std::string> changes;
        CHECK(ConstrainSettings(settings, all, changes) == settings && changes.empty());
        // The original look never depends on a capability; only «Низкая»'s upscale needs the post chain, and
        // without it the scene is native and nothing else changes.
        if (preset == QualityPreset::Low)
        {
            changes.clear();
            Settings native = settings;
            native.render_height = 0;
            native.sharpen = false; // RCAS is part of the upscale
            CHECK(ConstrainSettings(settings, Support{}, changes) == native && changes.size() == 1);
            CHECK(ValidateSettings(native, Support{}).empty());
        }
    }
    const Settings high = Settings::FromPreset(QualityPreset::High);

    // No sampleable scene depth: occlusion and the sea's soft edges off.
    Support no_depth = all;
    no_depth.scene_depth = false;
    // ... and the render scale: the 3D drawn after the scene needs the scene depth on the display-size picture.
    CHECK(ValidateSettings(high, no_depth).size() == 3);
    std::vector<std::string> changes;
    Settings constrained = ConstrainSettings(high, no_depth, changes);
    CHECK(!constrained.ssao && !constrained.sea_soft_edges && constrained.sea_band_limit && constrained.render_height == 0);
    CHECK(changes.size() == 3);
    CHECK(constrained.shadows == high.shadows && constrained.bloom && constrained.lighting_flags == high.lighting_flags);
    CHECK(ValidateSettings(constrained, no_depth).empty());

    // No shadow maps.
    Support no_shadow = all;
    no_shadow.shadow_map = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_shadow, changes);
    CHECK(constrained.shadows == ShadowQuality::Off && constrained.lamp_shadows == 0 && changes.size() == 2); // sun, lamps
    CHECK(LightingDefines(constrained).find("FX_LIGHT_SHADOW") == std::string::npos);

    // No R8 3D textures: no AO volume, said once; the rest stays.
    Support no_ao = all;
    no_ao.ao_volume = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_ao, changes);
    // ... and no lamp grid or room light: no lamp shadows either, each said once.
    CHECK(constrained.ao_volume == 0 && constrained.lamp_shadows == 0 && changes.size() == 2 && constrained.shadows == high.shadows);
    // The slots of the lights that move are some of the lamp slots: at most two, never more than there are.
    Settings many_moving = high;
    many_moving.lamp_moving = 5;
    changes.clear();
    CHECK(ConstrainSettings(many_moving, all, changes).lamp_moving == 2 && changes.size() == 1);
    Settings no_lamps = high; // without lamp slots the number says nothing and is left alone
    no_lamps.lamp_shadows = 0;
    changes.clear();
    CHECK(ConstrainSettings(no_lamps, all, changes).lamp_moving == 2 && changes.empty());
    Settings five_lamps = high;
    five_lamps.lamp_shadows = 5;
    changes.clear();
    CHECK(ConstrainSettings(five_lamps, all, changes).lamp_shadows == 8 && changes.size() == 1);
    // An AO volume without the fx lighting path has no shader to apply it: switched off, said.
    Settings unlit = high;
    unlit.lighting_flags = kLightingNone;
    unlit.shadows = ShadowQuality::Off;
    unlit.lamp_shadows = 0;
    unlit.town_light = false;
    unlit.town_walls = false;
    unlit.bounce = false;
    unlit.gloss = false;
    unlit.parallax = ParallaxQuality::Off;
    unlit.parallax_shadow = false;
    unlit.relief_cavity = false;
    changes.clear();
    CHECK(ConstrainSettings(unlit, all, changes).ao_volume == 0 && changes.size() == 1);
    // Gloss without per-pixel highlights has nothing to shape: switched off, said.
    Settings matte = high;
    matte.lighting_flags &= ~static_cast<uint32_t>(kLightingSpecular);
    changes.clear();
    CHECK(!ConstrainSettings(matte, all, changes).gloss && changes.size() == 1);
    Settings too_strong = high;
    too_strong.ao_volume = 9;
    changes.clear();
    CHECK(ConstrainSettings(too_strong, all, changes).ao_volume == kAoVolumeLevels && changes.size() == 1);
    // Our own town light relights through the fx lighting path: without it, switched off, said.
    Settings flat_town = high;
    flat_town.lighting_flags = kLightingNone;
    flat_town.shadows = ShadowQuality::Off;
    flat_town.lamp_shadows = 0;
    flat_town.ao_volume = 0;
    flat_town.bounce = false;
    flat_town.town_walls = false;
    flat_town.gloss = false;
    flat_town.parallax = ParallaxQuality::Off;
    flat_town.parallax_shadow = false;
    flat_town.relief_cavity = false;
    changes.clear();
    CHECK(!ConstrainSettings(flat_town, all, changes).town_light && changes.size() == 1);
    // So does the bounce.
    Settings flat_bounce = flat_town;
    flat_bounce.town_light = false;
    flat_bounce.bounce = true;
    changes.clear();
    CHECK(!ConstrainSettings(flat_bounce, all, changes).bounce && changes.size() == 1);
    CHECK(high.bounce && Settings::FromPreset(QualityPreset::Medium).bounce && !Settings::FromPreset(QualityPreset::Low).bounce);

    // No 3D textures: the post chain is reduced to its plain copy; MSAA is the renderer's and stays.
    Support no_lut = all;
    no_lut.lut_3d = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_lut, changes);
    CHECK(!constrained.tonemap && !constrained.bloom && !constrained.sun_shafts && !constrained.ssao && !constrained.dither);
    CHECK(constrained.msaa == 4 && constrained.PostIsIdentity());
    CHECK(ValidateSettings(constrained, no_lut).empty());

    // MSAA without depth resolve: the multisampled scene keeps MSAA and gives up what needs depth.
    Support no_msaa_depth = all;
    no_msaa_depth.msaa_depth = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_msaa_depth, changes);
    CHECK(constrained.msaa == 4 && !constrained.ssao && !constrained.sea_soft_edges && constrained.render_height == 0 &&
          changes.size() == 3);
    // Only 2x for the scene target formats (per-format caps): 4x is lowered to 2x, reported, alpha to coverage stays.
    Support msaa_x2 = all;
    msaa_x2.msaa_samples = 2;
    changes.clear();
    constrained = ConstrainSettings(high, msaa_x2, changes);
    CHECK(constrained.msaa == 2 && constrained.alpha_to_coverage && !constrained.smaa && changes.size() == 1 &&
          changes[0] == "MSAA 4x is not available for the scene target (RGBA8 + D24S8): 2x instead");
    CHECK(ValidateSettings(constrained, msaa_x2).empty() && !ValidateSettings(high, msaa_x2).empty());
    // No MSAA at all: alpha to coverage goes with it, and then depth-based effects are fine again.
    Support no_msaa = no_msaa_depth;
    no_msaa.msaa_samples = 1;
    changes.clear();
    constrained = ConstrainSettings(high, no_msaa, changes);
    CHECK(constrained.msaa == 1 && !constrained.alpha_to_coverage && constrained.ssao && constrained.sea_soft_edges);
    // ... and SMAA takes over the anti-aliasing, reported (PowerVR, minimal GLES, a failed MSAA self-test).
    CHECK(constrained.smaa && !high.smaa);
    CHECK(std::find(changes.begin(), changes.end(), std::string("MSAA unavailable: SMAA instead")) != changes.end());
    CHECK(ValidateSettings(constrained, no_msaa).empty());
    // Without the post chain there is no SMAA either: MSAA goes, reported as no anti-aliasing at all.
    const std::string no_aa =
        "MSAA unavailable and SMAA needs the post-processing chain (3D textures) and R8/RG8 textures: no anti-aliasing";
    Support no_msaa_no_lut = no_msaa;
    no_msaa_no_lut.lut_3d = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_msaa_no_lut, changes);
    CHECK(constrained.msaa == 1 && !constrained.smaa && constrained.PostIsIdentity() && constrained.render_height == 0);
    CHECK(std::find(changes.begin(), changes.end(), no_aa) != changes.end());
    // ... nor without its tables: SMAA asked for explicitly is switched off, reported.
    Support no_msaa_no_r8 = no_msaa;
    no_msaa_no_r8.smaa = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_msaa_no_r8, changes);
    CHECK(constrained.msaa == 1 && !constrained.smaa && std::find(changes.begin(), changes.end(), no_aa) != changes.end());
    Settings smaa_on = high;
    smaa_on.smaa = true;
    changes.clear();
    CHECK(!ConstrainSettings(smaa_on, no_msaa_no_r8, changes).smaa &&
          std::find(changes.begin(), changes.end(), std::string("SMAA needs R8 and RG8 textures: switched off")) != changes.end());
    // An explicit MSAA off (fx_msaa=1) is the player's choice, not a missing capability: no SMAA is added.
    Settings msaa_off = high;
    msaa_off.msaa = 1;
    msaa_off.alpha_to_coverage = false;
    changes.clear();
    CHECK(!ConstrainSettings(msaa_off, no_msaa, changes).smaa && changes.empty());
    // The internal height's range; the upscale is part of the post chain (3D textures), the sharpening part of the
    // upscale: neither makes the post effects non-identical by itself.
    Settings scaled = high;
    scaled.render_height = 100;
    changes.clear();
    CHECK(ConstrainSettings(scaled, all, changes).render_height == 360 && changes.size() == 1);
    scaled.render_height = 1000;
    Settings only_scaled;
    only_scaled.render_height = 1000;
    only_scaled.sharpen = true;
    CHECK(Settings{}.PostIsIdentity() && only_scaled.PostIsIdentity());
    CHECK(RendererStateFor(scaled).render_height == 1000);
    changes.clear();
    CHECK(ConstrainSettings(only_scaled, no_lut, changes).render_height == 0 && !changes.empty());
    const RendererState state = RendererStateFor(high);
    CHECK(state.msaa_samples == 4 && state.alpha_to_coverage && state.max_anisotropy == 8);
    CHECK(RendererStateFor(Settings{}).msaa_samples == 1 && !RendererStateFor(Settings{}).alpha_to_coverage);
    Support no_aniso = all;
    no_aniso.anisotropy = false;
    changes.clear();
    CHECK(ConstrainSettings(high, no_aniso, changes).anisotropy == 1 && changes.size() == 1);

    // No EAC textures: normal maps and gloss maps go, and so does the shader define.
    Support no_rg = all;
    no_rg.normal_map = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_rg, changes);
    // Gloss, the parallax, its self-shadow and the cavity go with them.
    CHECK(changes.size() == 5 && !constrained.gloss && !constrained.parallax_shadow && !constrained.relief_cavity &&
          !(constrained.lighting_flags & kLightingNormalMap) && constrained.parallax == ParallaxQuality::Off);
    CHECK(LightingDefines(constrained).find("NORMALMAP") == std::string::npos);
    CHECK(high.parallax == ParallaxQuality::Occlusion && Settings::FromPreset(QualityPreset::Medium).parallax == ParallaxQuality::Offset &&
          Settings::FromPreset(QualityPreset::Low).parallax == ParallaxQuality::Off);
    // The self-shadow on Высокая only (it needs occlusion mapping); the classes' depths: stone deepest, wood shallowest.
    CHECK(high.parallax_shadow && !Settings::FromPreset(QualityPreset::Medium).parallax_shadow);
    CHECK(high.relief_cavity && Settings::FromPreset(QualityPreset::Medium).relief_cavity &&
          !Settings::FromPreset(QualityPreset::Low).relief_cavity);
    CHECK(ReliefDepthTexels(ReliefClass::Stone) > ReliefDepthTexels(ReliefClass::Building) &&
          ReliefDepthTexels(ReliefClass::Building) > ReliefDepthTexels(ReliefClass::Plaster) &&
          ReliefDepthTexels(ReliefClass::Plaster) > ReliefDepthTexels(ReliefClass::Wood) &&
          ReliefDepthTexels(ReliefClass::Wood) > ReliefDepthTexels(ReliefClass::Prop) &&
          ReliefDepthTexels(ReliefClass::Prop) > ReliefDepthTexels(ReliefClass::Metal) &&
          ReliefDepthTexels(ReliefClass::Metal) > 0.0f && ReliefDepthTexels(ReliefClass::HeightMap) > 0.0f);
    // One relief strength for both presets: the models' maps at full strength (the owner's choice).
    CHECK(Settings::FromPreset(QualityPreset::Medium).normal_map_strength == high.normal_map_strength &&
          high.normal_map_strength == 1.0f);

    // Inconsistent requests are reported as well.
    Settings odd;
    odd.sun_shafts = true; // without bloom
    odd.lighting_flags = kLightingSpecular; // without per-pixel lighting
    odd.shadows = ShadowQuality::Soft;
    CHECK(ValidateSettings(odd, all).size() == 3);
    changes.clear();
    constrained = ConstrainSettings(odd, all, changes);
    CHECK(changes.size() == 3 && constrained == Settings{});
    CHECK(Support{}.Describe().find("NO") != std::string::npos && all.Describe().find("NO") == std::string::npos);
}

void CheckLights()
{
    PointLight torch;
    torch.position = {0.0f, 2.0f, 0.0f};
    torch.color = {1.0f, 0.6f, 0.3f};
    torch.range = 10.0f;
    torch.att0 = 0.5f;
    torch.att1 = 0.1f;
    torch.att2 = 0.05f;
    // D3D9 attenuation inside the range, nothing beyond it, a short fade right before the limit.
    CHECK(Near(LightAttenuation(torch, 0.0f), 2.0f, 1e-5f));
    CHECK(Near(LightAttenuation(torch, 4.0f), 1.0f / (0.5f + 0.4f + 0.8f), 1e-5f));
    CHECK(LightAttenuation(torch, 10.01f) == 0.0f);
    CHECK(Near(LightAttenuation(torch, 9.25f), 0.5f / (0.5f + 0.925f + 0.05f * 9.25f * 9.25f), 1e-4f));
    float previous = 1e9f;
    for (int i = 0; i <= 100; ++i)
    {
        const float a = LightAttenuation(torch, 0.1f * i);
        CHECK(a <= previous && a >= 0.0f);
        previous = a;
    }

    // Selection: strongest at the object first, unreachable lights dropped, the overflow counted.
    std::vector<PointLight> lights;
    for (int i = 0; i < 12; ++i)
    {
        PointLight light = torch;
        light.position = {3.0f * i, 2.0f, 0.0f};
        light.baked = i % 2 == 0;
        lights.push_back(light);
    }
    const LightSelection four = SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 4);
    CHECK(four.count == 4 && four.dropped == 0); // lights at x = 0, 3, 6, 9 reach; x = 12 and beyond do not
    CHECK(four.lights[0].position[0] == 0.0f && four.lights[3].position[0] == 9.0f);
    const LightSelection crowded = SelectLights(lights, {15.0f, 1.0f, 0.0f}, 6.0f, 4);
    CHECK(crowded.count == 4 && crowded.dropped > 0);
    CHECK(SelectLights(lights, {15.0f, 1.0f, 0.0f}, 6.0f, 8).dropped < crowded.dropped);
    CHECK(SelectLights(lights, {500.0f, 0.0f, 0.0f}, 1.0f, 8).count == 0);
    CHECK(SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 100).count <= kMaxPointLights);
    // Geometry with baked vertex colours: baked lights are left out, dynamic ones stay.
    const LightSelection dynamic = SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 8, false);
    CHECK(dynamic.count == 2 && !dynamic.lights[0].baked && !dynamic.lights[1].baked);
    // A brighter light further away can outrank a dim one nearby.
    lights[3].color = {16.0f, 16.0f, 16.0f};
    CHECK(SelectLights(lights, {0.0f, 1.0f, 0.0f}, 1.0f, 1).lights[0].position[0] == 9.0f);

    // A wall of a large baked mesh next to a torch: chosen at the wall's own sphere, the torch lights it, and the
    // choice does not depend on anything else (there is no camera in it: the renderer passes the draw's bounds).
    {
        PointLight wall_torch = torch;
        wall_torch.position = {40.0f, 2.0f, 0.0f};
        std::vector<PointLight> town(6, torch);
        for (int i = 0; i < 6; ++i)
            town[i].position = {-60.0f + 4.0f * i, 2.0f, 0.0f}; // far from the wall
        town.push_back(wall_torch);
        const LightSelection wall = SelectLights(town, {41.0f, 2.0f, 0.0f}, 3.0f, 4, true, true);
        CHECK(wall.count == 1 && wall.lights[0].position[0] == 40.0f && wall.lights[0].color[0] == torch.color[0]);
    }
    // Rank fade: as a left-out light catches up with the weakest kept one, the kept one dims to nothing, so the
    // swap of the two is continuous; with a clear lead it keeps its full colour.
    {
        PointLight a = torch, b = torch;
        a.position = {0.0f, 0.0f, 0.0f};
        b.position = {0.0f, 0.0f, 0.0f};
        a.color = {1.0f, 1.0f, 1.0f};
        float previous_weight = 1.0f;
        for (int step = 0; step <= 10; ++step)
        {
            b.color = {0.1f * step, 0.1f * step, 0.1f * step}; // catches up with a
            const PointLight pair[2] = {a, b};
            const LightSelection one = SelectLights(pair, {0.0f, 0.0f, 0.0f}, 0.0f, 1, true, true);
            CHECK(one.count == 1 && one.lights[0].color[0] <= previous_weight + 1e-6f);
            previous_weight = one.lights[0].color[0];
            if (step <= 4)
                CHECK(Near(one.lights[0].color[0], 1.0f, 1e-6f)); // b at most 40 %: a keeps all of its light
        }
        CHECK(previous_weight < 1e-6f); // equal scores: the kept light is faded out completely
        const PointLight pair[2] = {a, b};
        CHECK(SelectLights(pair, {0.0f, 0.0f, 0.0f}, 0.0f, 1).lights[0].color[0] == 1.0f); // fade off: unchanged
    }

    // Uniform packing: view-space position, D3D terms, spot cone as scale and bias.
    std::array<float, 16> view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, -2, -3, 1};
    LightSelection selection;
    selection.count = 2;
    selection.dropped = 3;
    selection.lights[0] = torch;
    selection.lights[1] = torch;
    selection.lights[1].direction = {0.0f, -1.0f, 0.0f};
    selection.lights[1].cos_inner = 0.9f;
    selection.lights[1].cos_outer = 0.7f;
    selection.lights[1].falloff = 2.0f;
    const PointLightUniforms packed = PackPointLights(selection, view);
    CHECK(packed.u_fxPointCount[0] == 2.0f && packed.dropped == 3);
    CHECK(Near(packed.u_fxPoint[0][0], -1.0f, 1e-6f) && Near(packed.u_fxPoint[0][1], 0.0f, 1e-6f) && packed.u_fxPoint[0][3] == 10.0f);
    CHECK(packed.u_fxPoint[1][3] == 0.5f && packed.u_fxPoint[2][0] == 0.1f && packed.u_fxPoint[2][1] == 0.05f);
    CHECK(packed.u_fxPoint[2][2] == 0.0f && packed.u_fxPoint[2][3] == 1.0f); // point light: cone factor is always 1
    const float scale = packed.u_fxPoint[6][2], bias = packed.u_fxPoint[6][3];
    CHECK(Near(0.9f * scale + bias, 1.0f, 1e-4f) && Near(0.7f * scale + bias, 0.0f, 1e-4f) && packed.u_fxPoint[7][3] == 2.0f);

    // Flashes: bright at once, gone after their duration, never more than the pool holds.
    FlashLights flashes;
    flashes.Add({1.0f, 2.0f, 3.0f}, {3.0f, 2.0f, 1.0f}, 20.0f, 0.2f);
    std::vector<PointLight> out;
    flashes.AppendTo(out);
    CHECK(out.size() == 1 && out[0].color[0] == 3.0f && !out[0].baked && out[0].range == 20.0f);
    CHECK(LightAttenuation(out[0], 0.0f) == 1.0f && LightAttenuation(out[0], 19.0f) < 0.06f);
    flashes.Update(0.1f);
    out.clear();
    flashes.AppendTo(out);
    CHECK(Near(out[0].color[0], 0.75f, 1e-4f)); // half the time left: a quarter of the peak
    flashes.Update(0.11f);
    CHECK(flashes.Count() == 0);
    for (int i = 0; i < 50; ++i)
        flashes.Add({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 5.0f, 1.0f);
    CHECK(flashes.Count() == FlashLights::kCapacity);
    flashes.Add({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 5.0f, 0.0f); // nonsense is ignored
    CHECK(flashes.Count() == FlashLights::kCapacity);
}

void CheckShadows()
{
    // A shadow takes the whole direct sun away (only the ambient stays); it fades as the sun sets.
    CHECK(SunShadowStrength(0.7f) == 1.0f && SunShadowStrength(0.125f) == 1.0f);
    CHECK(Near(SunShadowStrength(0.0625f), 0.5f, 1e-6f) && SunShadowStrength(0.0f) == 0.0f && SunShadowStrength(-0.3f) == 0.0f);
    CHECK(ShadowMapSize(ShadowQuality::Off) == 0 && ShadowMapSize(ShadowQuality::Low) == 1024 && ShadowMapSize(ShadowQuality::Soft) == 2048);
    const Vec3 sun{0.48f, 0.64f, 0.6f};
    const Vec3 focus{100.0f, 5.0f, -40.0f};
    const ShadowSetup setup = ComputeShadowSetup(sun, focus, 50.0f, 120.0f, 1024);
    CHECK(Near(setup.texel_world, 100.0f / 1024.0f, 1e-6f));
    // The focus is at the centre of the map (within a texel), half way through the receiver depth range.
    const auto centre = ShadowCoord(setup, focus);
    CHECK(Near(centre[0], 0.5f, 1.0f / 1024.0f) && Near(centre[1], 0.5f, 1.0f / 1024.0f));
    CHECK(Near(centre[2], 170.0f / 220.0f, 1e-3f));
    // Moving towards the sun: same texel, smaller depth. A caster 100 m up the sun ray is still inside.
    const auto nearer = ShadowCoord(setup, {focus[0] + sun[0] * 100.0f, focus[1] + sun[1] * 100.0f, focus[2] + sun[2] * 100.0f});
    CHECK(Near(nearer[0], centre[0], 1e-3f) && Near(nearer[1], centre[1], 1e-3f) && nearer[2] < centre[2] && nearer[2] > 0.0f);
    // Points on the edge of the sphere stay inside the map.
    for (Vec3 offset : {Vec3{49.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -49.0f}, Vec3{-30.0f, 20.0f, 30.0f}})
    {
        const auto c = ShadowCoord(setup, {focus[0] + offset[0], focus[1] + offset[1], focus[2] + offset[2]});
        CHECK(c[0] > 0.0f && c[0] < 1.0f && c[1] > 0.0f && c[1] < 1.0f && c[2] > 0.0f && c[2] < 1.0f);
    }
    // Texel snapping: a small camera move does not shift the map by a fraction of a texel.
    const ShadowSetup moved = ComputeShadowSetup(sun, {focus[0] + 0.013f, focus[1], focus[2] + 0.021f}, 50.0f, 120.0f, 1024);
    const Vec3 probe{90.0f, 3.0f, -35.0f};
    const float shift_u = (ShadowCoord(moved, probe)[0] - ShadowCoord(setup, probe)[0]) * 1024.0f;
    const float shift_v = (ShadowCoord(moved, probe)[1] - ShadowCoord(setup, probe)[1]) * 1024.0f;
    CHECK(Near(shift_u, std::round(shift_u), 2e-2f) && Near(shift_v, std::round(shift_v), 2e-2f));
    // A sun straight overhead is a valid direction.
    const ShadowSetup zenith = ComputeShadowSetup({0.0f, 1.0f, 0.0f}, focus, 50.0f, 120.0f, 1024);
    CHECK(Near(ShadowCoord(zenith, focus)[0], 0.5f, 1.0f / 1024.0f));
}
// Decodes texel (x, y) of mip `mip` into a unit normal.
std::array<float, 3> NormalAt(const NormalMap &map, int mip, int x, int y)
{
    size_t offset = 0;
    int w = map.width, h = map.height;
    for (int m = 0; m < mip; ++m)
    {
        offset += static_cast<size_t>(w) * h * 2;
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
    }
    const float nx = map.rg[offset + (static_cast<size_t>(y) * w + x) * 2] / 255.0f * 2.0f - 1.0f;
    const float ny = map.rg[offset + (static_cast<size_t>(y) * w + x) * 2 + 1] / 255.0f * 2.0f - 1.0f;
    return {nx, ny, std::sqrt(std::max(1.0f - nx * nx - ny * ny, 0.0f))};
}

// Grey RGBA texels from heights.
std::vector<uint8_t> Grey(const std::vector<uint8_t> &heights)
{
    std::vector<uint8_t> rgba;
    for (uint8_t h : heights)
        rgba.insert(rgba.end(), {h, h, h, 255});
    return rgba;
}

NormalMap Derive(const std::vector<uint8_t> &heights, int width, int height, const NormalMapOptions &options)
{
    return DeriveNormalMap(Grey(heights), width, height, options);
}

void CheckNormalMaps()
{
    const int n = 64;
    NormalMapOptions options;
    options.strength = 4.0f;
    options.pre_blur = false;

    // A flat height field: every normal points straight out, at every mip.
    const std::vector<uint8_t> flat(n * n, 77);
    const NormalMap level = Derive(flat, n, n, options);
    CHECK(level.width == n && level.height == n && level.mip_count == 7);
    size_t texels = 0;
    for (int s = n; s >= 1; s /= 2)
        texels += static_cast<size_t>(s) * s;
    CHECK(level.rg.size() == texels * 2);
    CHECK(std::all_of(level.rg.begin(), level.rg.end(), [](uint8_t v) { return v == 128; }));

    // A ramp along u: slope = strength / width per texel, the normal leans against the slope, y stays 0.
    std::vector<uint8_t> ramp(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            ramp[y * n + x] = static_cast<uint8_t>(x * 255 / (n - 1));
    options.wrap = false;
    const NormalMap slope = Derive(ramp, n, n, options);
    const float expected = 4.0f / (n - 1); // dh/du in texels, times strength
    const auto mid = NormalAt(slope, 0, n / 2, n / 2);
    CHECK(Near(mid[0] / mid[2], -expected, 0.012f) && Near(mid[1], 0.0f, 0.01f));
    // Mips keep the world-space slope: two levels down the lean is the same.
    const auto coarse = NormalAt(slope, 2, n / 8, n / 8);
    CHECK(Near(coarse[0] / coarse[2], -expected, 0.012f) && Near(coarse[1], 0.0f, 0.01f));
    // Rotated ramp: the lean follows v instead.
    std::vector<uint8_t> ramp_v(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            ramp_v[y * n + x] = ramp[x * n + y];
    const auto mid_v = NormalAt(Derive(ramp_v, n, n, options), 0, n / 2, n / 2);
    CHECK(Near(mid_v[1] / mid_v[2], -expected, 0.012f) && Near(mid_v[0], 0.0f, 0.01f));

    // A cone: normals point away from the apex, with equal lean all around.
    std::vector<uint8_t> cone(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            cone[y * n + x] = static_cast<uint8_t>(std::clamp(255.0f - 6.0f * std::hypot(x - 31.5f, y - 31.5f), 0.0f, 255.0f));
    const NormalMap hill = Derive(cone, n, n, options);
    for (int i = 0; i < 16; ++i)
    {
        const float angle = 0.3927f * i;
        const int x = static_cast<int>(std::lround(31.5f + 14.0f * std::cos(angle))), y = static_cast<int>(std::lround(31.5f + 14.0f * std::sin(angle)));
        const auto normal = NormalAt(hill, 0, x, y);
        const float lean = std::hypot(normal[0], normal[1]) / normal[2];
        CHECK(Near(lean, 4.0f * 6.0f / 255.0f, 0.02f));
        CHECK(normal[0] * std::cos(angle) + normal[1] * std::sin(angle) > 0.9f * std::hypot(normal[0], normal[1]));
    }

    // A sine that tiles: analytic slope everywhere, and the wrap seam is as smooth as the inside.
    std::vector<uint8_t> wave(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            wave[y * n + x] = static_cast<uint8_t>(std::lround(127.5f + 100.0f * std::sin(6.2831853f * 2.0f * (x + 0.5f) / n)));
    options.wrap = true;
    const NormalMap tiled = Derive(wave, n, n, options);
    for (int x = 0; x < n; ++x)
    {
        const float slope_here = 4.0f * (100.0f / 255.0f) * (6.2831853f * 2.0f / n) * std::cos(6.2831853f * 2.0f * (x + 0.5f) / n);
        const auto normal = NormalAt(tiled, 0, x, 5);
        CHECK(Near(normal[0] / normal[2], -slope_here, 0.012f));
    }
    CHECK(std::abs(tiled.rg[0] - tiled.rg[(n - 1) * 2]) <= std::abs(tiled.rg[2 * 10] - tiled.rg[2 * 11]) + 1);
    // Without wrapping the same texture has an edge: the border normal differs from the wrapped one.
    options.wrap = false;
    CHECK(Derive(wave, n, n, options).rg[0] != tiled.rg[0]);
    options.wrap = true;

    // The pre-blur softens DXT block noise: the steps between 4x4 blocks give smaller peak slopes.
    std::vector<uint8_t> blocky(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            blocky[y * n + x] = static_cast<uint8_t>(128 + 6 * ((((x / 4) * 7 + (y / 4) * 13) % 5) - 2));
    const auto peak = [&](bool blur) {
        NormalMapOptions o = options;
        o.strength = 8.0f;
        o.pre_blur = blur;
        const NormalMap map = Derive(blocky, n, n, o);
        int worst = 0;
        for (int i = 0; i < n * n * 2; ++i)
            worst = std::max(worst, std::abs(map.rg[i] - 128));
        return worst;
    };
    CHECK(peak(false) > 8 && peak(true) * 10 <= peak(false) * 8);

    // Deterministic and cacheable: same input, same bytes; the key changes with every option.
    CHECK(Derive(cone, n, n, options).rg == Derive(cone, n, n, options).rg);
    NormalMapOptions other = options;
    other.strength = 2.0f;
    CHECK(NormalMapCacheKey(options) == NormalMapCacheKey(options) && NormalMapCacheKey(options) != NormalMapCacheKey(other));
    other = options;
    other.pre_blur = !options.pre_blur;
    CHECK(NormalMapCacheKey(options) != NormalMapCacheKey(other));
    // Non-square textures and bad input.
    const NormalMap strip = Derive(std::vector<uint8_t>(256 * 64, 10), 256, 64, options);
    CHECK(strip.mip_count == 9 && strip.rg.size() == 2u * (256 * 64 + 128 * 32 + 64 * 16 + 32 * 8 + 16 * 4 + 8 * 2 + 4 + 2 + 1));
    CHECK(Derive(flat, n, n + 1, options).rg.empty());

    // The size cap: a 256 x 64 texture with max_size 64 is derived at 64 x 16, and DropTopMips trims a chain.
    NormalMapOptions capped = options;
    capped.max_size = 64;
    int w = 0, h = 0;
    NormalMapSize(256, 64, capped, w, h);
    CHECK(w == 64 && h == 16);
    NormalMap small = Derive(std::vector<uint8_t>(256 * 64, 10), 256, 64, capped);
    CHECK(small.width == 64 && small.height == 16 && small.mip_count == 7 && small.rg.size() == NormalMapBytes(64, 16));
    NormalMap trimmed = strip;
    DropTopMips(trimmed, 32);
    CHECK(trimmed.width == 32 && trimmed.height == 8 && trimmed.mip_count == 6 && trimmed.rg.size() == NormalMapBytes(32, 8));
    CHECK(std::equal(trimmed.rg.begin(), trimmed.rg.end(), strip.rg.end() - static_cast<std::ptrdiff_t>(trimmed.rg.size())));
    // EAC RG11 sizes: 16 bytes per 4x4 block, at least one block per level; DropTopMips keeps the right tail.
    CHECK(NormalMapLevelBytes(NormalMapFormat::EacRg11, 256, 64) == 64u * 16 * 16 && NormalMapLevelBytes(NormalMapFormat::EacRg11, 2, 1) == 16);
    CHECK(NormalMapBytes(4, 4, NormalMapFormat::EacRg11) == 3 * 16);
    NormalMap eac{256, 64, 9, NormalMapFormat::EacRg11, std::vector<uint8_t>(NormalMapBytes(256, 64, NormalMapFormat::EacRg11))};
    for (size_t i = 0; i < eac.rg.size(); ++i)
        eac.rg[i] = static_cast<uint8_t>(i * 7);
    NormalMap eac_trimmed = eac;
    DropTopMips(eac_trimmed, 64);
    CHECK(eac_trimmed.width == 64 && eac_trimmed.mip_count == 7 && eac_trimmed.rg.size() == NormalMapBytes(64, 16, NormalMapFormat::EacRg11));
    CHECK(std::equal(eac_trimmed.rg.begin(), eac_trimmed.rg.end(), eac.rg.end() - static_cast<std::ptrdiff_t>(eac_trimmed.rg.size())));
    other = options;
    other.max_size = 512;
    CHECK(NormalMapCacheKey(options) != NormalMapCacheKey(other));
}

// Height maps at a lower preset's size keep their relief.
void CheckHeightMapSizes()
{
    // A lower preset derives a smaller map with the same relief: a 2048-wide stripe pattern derived at
    // 512 leans like the 1024 map (the strength is given at kNormalMapReferenceSize).
    const int big = 2048;
    std::vector<uint8_t> stripes(static_cast<size_t>(big) * 8 * 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < big; ++x)
        {
            const uint8_t v = static_cast<uint8_t>(std::lround(127.5f + 100.0f * std::sin(6.2831853f * x / 64.0f)));
            uint8_t *t = &stripes[(static_cast<size_t>(y) * big + x) * 4];
            t[0] = t[1] = t[2] = v;
            t[3] = 255;
        }
    NormalMapOptions full = HeightMapOptions(), half = HeightMapOptions();
    full.strength = half.strength = 7.0f;
    full.max_size = 1024;
    half.max_size = 512;
    const NormalMap at_full = DeriveNormalMap(stripes, big, 8, full), at_half = DeriveNormalMap(stripes, big, 8, half);
    CHECK(at_full.width == 1024 && at_half.width == 512);
    float lean_full = 0.0f, lean_half = 0.0f;
    for (int x = 0; x < 1024; ++x)
        lean_full = std::max(lean_full, std::fabs(NormalAt(at_full, 0, x, 1)[0]));
    for (int x = 0; x < 512; ++x)
        lean_half = std::max(lean_half, std::fabs(NormalAt(at_half, 0, x, 1)[0]));
    CHECK(lean_full > 0.1f && Near(lean_half, lean_full, 0.1f * lean_full));
}

// The relief models around their inference (a stand-in `infer`): the wrapped colour input, the crop, the axes, the
// mips; the relief store.
void CheckReliefModel()
{
    // A 4x2 map from an 8x4 texture: the input is the colour box-halved, wrap-padded by kReliefModelPad, three planes.
    std::vector<uint8_t> rgba(8 * 4 * 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
        {
            uint8_t *t = &rgba[(static_cast<size_t>(y) * 8 + x) * 4];
            t[0] = static_cast<uint8_t>(x * 30), t[1] = static_cast<uint8_t>(y * 60), t[2] = 255, t[3] = 255;
        }
    const int mw = 4, mh = 2, pw = mw + 2 * kReliefModelPad, ph = mh + 2 * kReliefModelPad;
    const std::vector<float> input = ReliefModelInput(rgba, 8, 4, mw, mh);
    const size_t plane = static_cast<size_t>(pw) * ph;
    CHECK(input.size() == plane * 3);
    // The map's texel (0, 0) averages the texture's 2x2 block; the padding repeats the map (the texel kReliefModelPad
    // left of the first column is the last one).
    const auto at = [&](int c, int x, int y) { return input[c * plane + static_cast<size_t>(y + kReliefModelPad) * pw + x + kReliefModelPad]; };
    CHECK(Near(at(0, 0, 0), (0 + 30) / 2.0f / 255.0f, 1e-5f) && Near(at(1, 0, 0), (0 + 60) / 2.0f / 255.0f, 1e-5f) &&
          Near(at(2, 0, 0), 1.0f, 1e-6f));
    CHECK(at(0, -1, 0) == at(0, mw - 1, 0) && at(1, 0, -1) == at(1, 0, mh - 1) && at(0, -kReliefModelPad, 0) == at(0, 0, 0));
    // Run: the stand-in returns a function of each padded texel; the crop takes the map's own texels back.
    int calls = 0;
    const std::vector<float> planes = ReliefModelRun(input, mw, mh, [&](const float *image, int iw, int ih, float *out) {
        ++calls;
        CHECK(iw == pw && ih == ph);
        for (size_t i = 0; i < plane * 3; ++i)
            out[i] = 0.5f + 0.5f * image[i];
        return true;
    });
    CHECK(calls == 1 && planes.size() == static_cast<size_t>(mw) * mh * 3 && Near(planes[0], 0.5f + 0.5f * at(0, 0, 0), 1e-6f));
    CHECK(ReliefModelRun(input, mw, mh, [](const float *, int, int, float *) { return false; }).empty());
    // Normals: the planes' 2 v - 1, unit, in our axes; height: the planes' mean.
    std::vector<float> p(static_cast<size_t>(mw) * mh * 3);
    for (size_t t = 0; t < static_cast<size_t>(mw) * mh; ++t)
        p[t] = 0.75f, p[mw * mh + t] = 0.5f, p[2 * mw * mh + t] = 1.0f; // (0.5, 0, 1): tilted to +u
    const std::vector<float> n = ReliefModelNormals(p, mw, mh), h = ReliefModelHeight(p, mw, mh);
    CHECK(Near(n[0], 0.5f / std::sqrt(1.25f), 1e-5f) && Near(n[1], 0.0f, 1e-6f) && Near(n[2], 1.0f / std::sqrt(1.25f), 1e-5f));
    CHECK(Near(h[0], 0.75f, 1e-6f));

    // The map: mips down to 1x1, the top mip the model's own normals (no rescale), tilts never above 0.99.
    const int w = 300, hh = 170;
    std::vector<float> normals(static_cast<size_t>(w) * hh * 3);
    for (size_t t = 0; t < normals.size() / 3; ++t)
    {
        const float x = 0.3f * static_cast<float>(t % 7) / 7.0f, y = -0.2f;
        const float l = std::sqrt(x * x + y * y + 0.25f);
        normals[t * 3] = x / l, normals[t * 3 + 1] = y / l, normals[t * 3 + 2] = 0.5f / l;
    }
    const NormalMap map = NormalMapOfNormals(normals, w, hh);
    CHECK(map.width == w && map.height == hh && map.mip_count == 9 && map.rg.size() == NormalMapBytes(w, hh));
    float off = 0.0f;
    for (size_t t = 0; t < static_cast<size_t>(w) * hh; ++t)
        for (int c = 0; c < 2; ++c)
            off = std::max(off, std::fabs(map.rg[t * 2 + c] / 255.0f * 2 - 1 - normals[t * 3 + c]));
    CHECK(off < 0.01f);
    std::vector<float> flat_out(normals.size());
    for (size_t t = 0; t < flat_out.size(); t += 3)
        flat_out[t] = 1.0f; // lying flat: x = 1, z = 0
    const NormalMap steep = NormalMapOfNormals(flat_out, w, hh);
    for (size_t i = 0; i < steep.rg.size(); i += 2)
    {
        const float x = steep.rg[i] / 255.0f * 2 - 1, y = steep.rg[i + 1] / 255.0f * 2 - 1;
        CHECK(std::sqrt(x * x + y * y) <= 1.0f);
    }

    // Probe levels: the first level no larger than 64 px, none below a 4x4 block.
    CHECK(ReliefProbeLevel(1024, 512, 11) == 4 && ReliefProbeLevel(64, 64, 7) == 0 && ReliefProbeLevel(512, 4, 8) == -1);
    // Few mips (the decks' 512 textures have three): the smallest level is the probe.
    CHECK(ReliefProbeLevel(256, 256, 1) == 0 && ReliefProbeLevel(512, 256, 3) == 2 && ReliefProbeLevel(512, 512, 3) == 2);
    // Keys follow the path, the probe bytes and the method.
    const uint8_t probe[4] = {1, 2, 3, 4};
    const uint64_t stone = ReliefMethod(ReliefClass::Stone);
    const uint64_t key = ReliefKey("a/b", probe, 4, 4, stone, kReliefMapSize);
    const uint8_t other[4] = {1, 2, 3, 5};
    CHECK(key == ReliefKey("a/b", probe, 4, 4, stone, kReliefMapSize));
    CHECK(key != ReliefKey("a/c", probe, 4, 4, stone, kReliefMapSize));
    CHECK(key != ReliefKey("a/b", other, 4, 4, stone, kReliefMapSize));
    CHECK(key != ReliefKey("a/b", probe, 4, 4, ReliefMethod(ReliefClass::HeightMap), kReliefMapSize));
    // The method covers what makes the map: PBRify's maps (one method for every colour class), the derivation of height
    // maps, the models, the material's height and gloss and the version. The maps of the methods before it (DeepBump's,
    // the heuristic gloss) are never found again.
    CHECK(ReliefMethod(ReliefClass::None) == 0);
    CHECK(ReliefMethod(ReliefClass::Metal) == ReliefMethod(ReliefClass::Stone) &&
          ReliefMethod(ReliefClass::Metal) != ReliefMethod(ReliefClass::HeightMap)); // every colour class has a height
    for (const ReliefClass colour : {ReliefClass::Stone, ReliefClass::Wood, ReliefClass::Building, ReliefClass::Plaster})
        CHECK(ReliefMethod(colour) == ReliefMethod(ReliefClass::Stone) &&
              ReliefMethod(colour) != ReliefMethod(ReliefClass::HeightMap));
    CHECK(kReliefMethodVersion == 8);
    CHECK(ReliefMethod(ReliefClass::Stone) != 3 && key != ReliefKey("a/b", probe, 4, 4, 3, kReliefMapSize));
    // The model files' sha256 prefixes (checked by CMake too).
    CHECK(kReliefNormalModelDigest == 0xfc91ce30e48deaf5ull && kReliefHeightModelDigest == 0x01f251dc40e79fa7ull &&
          kReliefRoughnessModelDigest == 0x860845c92142b7d1ull);
    // The gloss maps' method: the version, the roughness network, the class.
    CHECK(GlossMethod(GlossClass::Metal) != GlossMethod(GlossClass::Glass) && kGlossMethod == 0x47000005u);

    // The store: a written map reads back; a damaged file is deleted and reported.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "storm_fx_relief_check";
    std::filesystem::remove_all(dir);
    NormalMap eac{8, 4, 4, NormalMapFormat::EacRg11, std::vector<uint8_t>(NormalMapBytes(8, 4, NormalMapFormat::EacRg11), 7)};
    CHECK(!HasReliefMap(dir, key) && !ReadReliefMap(dir, key));
    CHECK(WriteReliefMap(dir, key, eac) && HasReliefMap(dir, key));
    const std::optional<NormalMap> back = ReadReliefMap(dir, key);
    CHECK(back && back->width == 8 && back->height == 4 && back->mip_count == 4 && back->format == NormalMapFormat::EacRg11 &&
          back->rg == eac.rg);
    // With the material texture: it and its depth read back.
    eac.material.assign(eac.rg.size(), 9);
    eac.depth = 6.5f;
    eac.trust.assign(2, 200);
    eac.trust_coverage = 0.25f;
    CHECK(WriteReliefMap(dir, key, eac));
    const std::optional<NormalMap> with_material = ReadReliefMap(dir, key);
    CHECK(with_material && with_material->rg == eac.rg && with_material->material == eac.material && with_material->depth == 6.5f &&
          with_material->trust == eac.trust && with_material->trust_coverage == 0.25f);
    std::filesystem::resize_file(ReliefFile(dir, key), 40);
    std::string error;
    CHECK(!ReadReliefMap(dir, key, &error) && !error.empty() && !HasReliefMap(dir, key));
    std::filesystem::remove_all(dir);
}

// The relief's material texture (height_map.hpp): the height normalised per region (an atlas of a deep and a shallow
// part: each keeps its own range), its depth the class's, the gloss in G, the mips.
void CheckReliefMaterial()
{
    // Every class with relief has a height (the parallax wherever it can work), the grey height maps their own.
    CHECK(ReliefHasHeight(ReliefClass::Stone) && ReliefHasHeight(ReliefClass::Wood) && ReliefHasHeight(ReliefClass::Plaster) &&
          ReliefHasHeight(ReliefClass::Building) && ReliefHasHeight(ReliefClass::HeightMap) && !ReliefHasHeight(ReliefClass::None) &&
          ReliefHasHeight(ReliefClass::Metal) && ReliefHasHeight(ReliefClass::Prop));
    CHECK(ReliefDepthTexels(ReliefClass::Metal) > 0.0f && ReliefDepthTexels(ReliefClass::Metal) < ReliefDepthTexels(ReliefClass::Prop) &&
          ReliefDepthTexels(ReliefClass::None) == 0.0f);
    // A grey height texture's heights at its map's size.
    {
        std::vector<uint8_t> grey(8 * 8 * 4, 0);
        for (int i = 0; i < 64; ++i)
            grey[i * 4] = static_cast<uint8_t>(i % 8 < 4 ? 0 : 255);
        const std::vector<float> h = GreyHeights(grey, 8, 8, 4, 4);
        CHECK(h.size() == 16 && h[0] == 0.0f && h[3] == 1.0f && GreyHeights(grey, 8, 8, 3, 3).empty());
    }
    CHECK(ReliefMethod(ReliefClass::Stone) != ReliefMethod(ReliefClass::HeightMap));
    const int n = 256;
    NormalMap map;
    map.width = map.height = n;
    map.format = NormalMapFormat::Rg8;
    for (int w = n; w >= 1; w /= 2)
        ++map.mip_count;
    map.rg.assign(NormalMapBytes(n, n), 128);
    // Left half: deep blocks (0..1); right half: shallow planks (0.45..0.55, a seam every 16 texels).
    std::vector<float> height(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            height[static_cast<size_t>(y) * n + x] = x < n / 2 ? (((x / 32 + y / 32) & 1) ? 1.0f : 0.0f)
                                                               : (y % 16 < 2 ? 0.45f : 0.55f);
    std::vector<uint8_t> gloss(static_cast<size_t>(n) * n, 200);
    CHECK(AddReliefMaterial(map, ReliefClass::Stone, height, gloss));
    CHECK(map.material.size() == map.rg.size());
    const auto r = [&](int x, int y) { return map.material[(static_cast<size_t>(y) * n + x) * 2]; };
    // The shallow planks' seams are as deep as the blocks' joints are: the region's own range (not a few percent).
    CHECK(r(3 * n / 4, 64) < 70 && r(3 * n / 4, 72) > 150);
    CHECK(r(16, 16) < 120 && r(48, 16) > 180 && r(48, 16) - r(16, 16) > 90);
    CHECK(map.material[1] == 200); // G: the gloss
    CHECK(Near(map.depth, ReliefDepthTexels(ReliefClass::Stone) * 0.5f, 1e-5f)); // texels of this 256 map
    // DropTopMips keeps the material with its mips and the depth in texels of the new top.
    NormalMap half = map;
    DropTopMips(half, n / 2);
    CHECK(half.width == n / 2 && half.material.size() == half.rg.size() && Near(half.depth, map.depth * 0.5f, 1e-6f));
    CHECK(std::equal(half.material.begin(), half.material.end(), map.material.begin() + static_cast<std::ptrdiff_t>(n) * n * 2));
    // No gloss given: G 0. A flat height, mismatched sizes or a class without height: no material.
    NormalMap plain = map;
    CHECK(AddReliefMaterial(plain, ReliefClass::Wood, height, {}) && plain.material[1] == 0 &&
          Near(plain.depth, ReliefDepthTexels(ReliefClass::Wood) * 0.5f, 1e-5f));
    const std::vector<float> flat(static_cast<size_t>(n) * n, 0.5f);
    CHECK(!AddReliefMaterial(plain, ReliefClass::Stone, flat, {}) && plain.material.empty() && plain.depth == 0.0f);
    CHECK(!AddReliefMaterial(plain, ReliefClass::Stone, std::span(height).first(10), {}));
    CHECK(!AddReliefMaterial(plain, ReliefClass::None, height, {}) && plain.material.empty());
    CHECK(AddReliefMaterial(plain, ReliefClass::HeightMap, height, {}) && !plain.material.empty());
}

void CheckCheapUpgrades()
{
    WeatherInput weather;
    weather.sun_dir = {0.6f, 0.8f, 0.0f};
    weather.fog_density = 0.002f;
    const SkyState noon = ComputeSky(weather);

    // Aerial perspective: the game's fog density, a haze layer that thins with the camera's height, world up
    // taken from the view matrix, haze colour = the horizon (gamma encoded).
    const std::array<float, 16> view = {0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1}; // world y -> view x
    const AerialUniforms low = ComputeAerial(noon, 2.0f, view), high_up = ComputeAerial(noon, 200.0f, view);
    CHECK(low.u_fxAerial[0][3] == 0.002f && Near(low.u_fxAerial[0][1], std::sqrt(noon.horizon[1]), 1e-5f));
    CHECK(low.u_fxAerial[1][0] > 10.0f * high_up.u_fxAerial[1][0] && high_up.u_fxAerial[1][0] > 0.0f);
    CHECK(low.u_fxAerial[2][0] == 1.0f && low.u_fxAerial[2][1] == 0.0f && low.u_fxAerial[1][3] < 1.0f);
    // The emulator's form: a sixth of the haze, relative to the fog density, milder desaturation, no opacity cap.
    const auto fog_low = ComputeAerialFogHaze(2.0f), fog_high = ComputeAerialFogHaze(200.0f);
    CHECK(Near(fog_low[0] * noon.fog_density * 6.0f, low.u_fxAerial[1][0], 1e-7f) && fog_low[0] > 10.0f * fog_high[0]);
    CHECK(fog_low[1] == low.u_fxAerial[1][1] && kAerialFogDesaturation < low.u_fxAerial[1][2]);

    // Wind: stronger weather, larger and faster sway; the direction is the weather's.
    weather.wind = {0.6f, 0.8f};
    weather.wind_speed = 3.0f;
    const WindUniforms breeze = ComputeWind(weather, 5.0f);
    weather.wind_speed = 30.0f;
    const WindUniforms gale = ComputeWind(weather, 5.0f);
    CHECK(breeze.u_fxWind[0][0] == 0.6f && breeze.u_fxWind[0][2] == 0.8f && breeze.u_fxWind[0][3] == 5.0f);
    CHECK(gale.u_fxWind[1][0] > 2.0f * breeze.u_fxWind[1][0] && gale.u_fxWind[1][1] > breeze.u_fxWind[1][1]);
    CHECK(gale.u_fxWind[1][0] <= 0.5f + 1e-6f); // saturates: trees do not fly away in a storm

    // Scene grades: each mood is distinct, the blend follows the sky continuously.
    const auto sky_at = [](float sun_height) {
        WeatherInput w;
        w.sun_dir = {std::sqrt(1.0f - sun_height * sun_height), sun_height, 0.0f};
        return ComputeSky(w);
    };
    const std::array<float, 3> grey{0.5f, 0.5f, 0.5f};
    const std::array<float, 3> gain_identity{1.0f, 1.0f, 1.0f};
    const ColorGrade day = SceneGrade(sky_at(0.8f), 0.0f, 0.0f), dusk = SceneGrade(sky_at(0.08f), 0.0f, 0.0f);
    const ColorGrade night = SceneGrade(sky_at(-0.5f), 0.0f, 0.0f), storm = SceneGrade(sky_at(0.8f), 1.0f, 0.0f);
    const ColorGrade indoors = SceneGrade(sky_at(-0.5f), 0.0f, 1.0f);
    CHECK(day == ColorGrade::ForMood(ColorGrade::Mood::TropicalNoon) && night == ColorGrade::ForMood(ColorGrade::Mood::Night));
    CHECK(indoors == ColorGrade::ForMood(ColorGrade::Mood::Interior));
    CHECK(dusk.Apply(grey)[0] > dusk.Apply(grey)[2] + 0.015f);   // warm
    CHECK(night.Apply(grey)[2] > night.Apply(grey)[0] + 0.015f); // cool
    CHECK(storm.Apply(grey)[2] > storm.Apply(grey)[0]); // cool
    // Restrained: the moods shift hue and warmth only, never contrast, saturation or exposure.
    for (const ColorGrade *mood : {&day, &dusk, &night, &storm, &indoors})
        CHECK(mood->contrast == 1.0f && mood->saturation == 1.0f && mood->vibrance == 0.0f && mood->exposure == 0.0f &&
              mood->gain == gain_identity);
    // A small step of the sun is a small step of the grade (no jump at dusk).
    float worst = 0.0f;
    for (int i = 0; i < 60; ++i)
    {
        const auto a = SceneGrade(sky_at(-0.3f + 0.01f * i), 0.0f, 0.0f).Apply(grey), b = SceneGrade(sky_at(-0.29f + 0.01f * i), 0.0f, 0.0f).Apply(grey);
        worst = std::max({worst, std::fabs(a[0] - b[0]), std::fabs(a[2] - b[2])});
    }
    CHECK(worst < 0.02f);
    // The post chain rebuilds the table only when the drift shows in 8-bit output.
    CHECK(GradeDifference(day, day) == 0.0f && GradeDifference(day, night) > 0.01f);
    const ColorGrade user = ColorGrade::Preset(GradePreset::Caribbean);
    // Built two slices at a time (the post chain's background rebuild), the table is the same.
    std::vector<uint8_t> sliced(8 * 8 * 8 * 4);
    for (int first = 0; first < 8; first += 2)
        BuildLutSlices(user, 8, dusk, first, 2, sliced.data());
    CHECK(sliced == BuildLutRgba8(user, 8, dusk));
    CHECK(GradeDifference(SceneGrade(sky_at(0.5f), 0.0f, 0.0f), SceneGrade(sky_at(0.501f), 0.0f, 0.0f)) < 0.5f / 255.0f);
    // The scene grade is applied before the player's grade.
    const std::vector<uint8_t> both = BuildLutRgba8(user, 8, night);
    const auto expected = user.Apply(night.Apply({1.0f / 7.0f, 3.0f / 7.0f, 5.0f / 7.0f}));
    const uint8_t *texel = &both[((5 * 8 + 3) * 8 + 1) * 4];
    CHECK(std::abs(texel[0] - static_cast<int>(std::lround(expected[0] * 255.0f))) <= 1 &&
          std::abs(texel[2] - static_cast<int>(std::lround(expected[2] * 255.0f))) <= 1);
}

int g_log_lines = 0;
int g_debug_lines = 0;

void CheckReliefRules()
{
    CHECK(NormalizeTexturePath("Locations\\Bump_City.tga.tx") == "locations/bump_city");
    CHECK(NormalizeTexturePath("resource\\textures\\Ships\\Brig1\\Hull1\\bortoutbrig.TGA") == "ships/brig1/hull1/bortoutbrig");
    CHECK(NameSuggestsHeightMap("bump_houses") && NameSuggestsHeightMap("ships/sailbumpu1") && NameSuggestsHeightMap("sph2_bump2"));
    CHECK(!NameSuggestsHeightMap("bumpy_folder/wall") && !NameSuggestsHeightMap("deckplanksu2"));

    // Grey with the channel error of DXT compression is grey; a tinted texture is not.
    std::vector<uint8_t> grey, tinted;
    for (int i = 0; i < 1024; ++i)
    {
        const uint8_t v = static_cast<uint8_t>(i % 251);
        grey.insert(grey.end(), {v, static_cast<uint8_t>(std::min(v + (i % 5), 255)), static_cast<uint8_t>(std::max(v - (i % 7), 0)), 255});
        tinted.insert(tinted.end(), {v, static_cast<uint8_t>(v / 2), static_cast<uint8_t>(v / 3), 255});
    }
    CHECK(IsGrayscale(grey) && !IsGrayscale(tinted) && !IsGrayscale({}));

    // The table on names from TEHO's RESOURCE/Textures.
    const auto relief = [](const char *path) { return ClassifyTextureName(NormalizeTexturePath(path)).relief; };
    const ReliefClass N = ReliefClass::None, S = ReliefClass::Stone, W = ReliefClass::Wood, B = ReliefClass::Building,
                      P = ReliefClass::Plaster;
    const std::pair<const char *, ReliefClass> cases[] = {
        // stone and brick
        {"StoneH2_walls.tga.tx", S}, {"BigBarBrick.tga", S}, {"rockK1a", S}, {"skala_jungle", S}, {"cobbleM1", S},
        {"TOWN_COB", S}, {"Beton_2", S}, {"fort_fra_0", S}, {"TortugaFortMain", S}, {"cavernTileU4", S}, {"tileU8", S},
        {"063pierres", S}, {"pierStone1", S}, {"lighthouseU1", S}, {"incas_hram", S}, {"Crypt_0", S},
        // wood, planks, ships
        {"woodPlanksU1", W}, {"deckPlanksT5", W}, {"Capdeck_2", W}, {"dm_barque", W}, {"Gundeck_1", W}, {"pierWood1", W},
        {"balkU3", W}, {"doorU1", W}, {"Hold_1", W}, {"TOWN_WOOD", W}, {"Ships\\Brig1\\Hull1\\bortoutbrig", W},
        {"Ships\\Barque1\\Hull3\\deckPlanksT3", W}, {"Ships\\deck", W}, {"cabinet1c", W},
        // building atlases
        {"BigHouse01", B}, {"Residence2_walls", B}, {"Tavern1_walls", B}, {"tavern1ins", B}, {"church2_walls", B},
        {"windmill_walls", B}, {"FlH03", B}, {"SpH4_walls", B}, {"Shipyard1", B}, {"objects_tex2", W},
        // plaster, ground
        {"Celling", P}, {"floorU3", P}, {"groundK2", P}, {"1sand1", P}, {"shoreU5", P},
        {"dirtK3", P}, {"tropa256", P}, {"jungleU5", P}, {"1grass1", P}, {"grassgreen1", P},
        // no relief: characters, UI, sky, water, glass, foliage cards, cloth and sails, decals, light, lightmaps
        {"Characters\\Lo_Man_1", N}, {"INTERFACES\\PORTRAITS\\128\\face_1", N}, {"BATTLE_INTERFACE\\cannons", N},
        {"WEATHER\\SKIES\\day", N}, {"lighting\\day1\\Fort_lm", N}, {"Ships\\parus_common", N},
        {"Ships\\Mayfang1\\Hull1\\mayfang1_alpha", N}, {"Ships\\trailShip", N}, {"sky_up", N}, {"water1", N},
        {"sea_1", N}, {"GlassWindow", N}, {"windBigHouse01", N}, {"res_win", N}, {"occlTempleU1", N}, {"reflChurchU2", N},
        {"MontserratMask", N}, {"PalmsSprites", N}, {"leafPalms", N}, {"Ferns", N}, {"tree4", N}, {"cloth1", P},
        {"carpetU4", P}, {"ropeU1", N}, {"FishingNets", N}, {"SignboardsA", N}, {"smoke_dirt", N}, {"bonfire_small", N},
        {"corona01", N}, {"fonar", ReliefClass::Metal}, {"lighthouseFonarU1", ReliefClass::Metal}, {"candle4c", N}, {"WaterFall", N}, {"River1", N},
        {"metalU4", ReliefClass::Metal}, {"cannon_T1", ReliefClass::Metal}, {"skelet3", ReliefClass::Prop}, {"telo", N}, {"Item_Rapier02", ReliefClass::Metal},
        {"ammo/katana", ReliefClass::Metal}, {"items/purse", ReliefClass::Prop}, {"islands/caiman/caiman", ReliefClass::Prop},
        {"islands/nevis/nevismask", N}, {"grass/grass_1", N}, {"Ships/Brig1/Hull1/cannon_brig", ReliefClass::Metal}, {"MenuWoodFlooooor01", N},
        {"HiDeckalpha", N}, {"cap_deck_glass", N}, {"Particles_list", N}, {"shadow2", N}, {"back_jungle", N},
    };
    for (const auto &[path, expected] : cases)
        if (relief(path) != expected)
        {
            std::printf("FAILED relief rule: %s is %s, expected %s\n", path, ReliefClassName(relief(path)), ReliefClassName(expected));
            ++failures;
        }
    // An unknown name is a prop (the last rule, which says so).
    CHECK(ClassifyTextureName("antigua").rule != nullptr && ClassifyTextureName("antigua").relief == ReliefClass::Prop);

    SetLogSink([](LogLevel level, const char *) { ++(level == LogLevel::Debug ? g_debug_lines : g_log_lines); });
    ReliefClassifier classifier;
    const auto verdict = [&](const char *path, const std::vector<uint8_t> &pixels) { return classifier.Classify(path, pixels); };
    CHECK(verdict("bump_city.tga.tx", grey).relief == ReliefClass::HeightMap);
    // A coloured "bump" texture is an ordinary one: here the table's stone.
    CHECK(verdict("bump_rock.tga", tinted).relief == S);
    // The sea's animated bump frames stay without relief (folder rule), grey or not.
    CHECK(verdict("WEATHER\\SEA\\BUMP_G2MX\\seabump0017.tga.tx", grey).relief == N);
    CHECK(verdict("deckPlanksU2.tga", {}).relief == W);
    CHECK(verdict("Antigua.tga.tx", {}).relief == ReliefClass::Prop);
    CHECK(verdict("deckPlanksU2.tga", {}).relief == W); // a repeat: decided again, not logged again
    // One debug line per texture, nothing at the normal levels.
    CHECK(g_debug_lines == 5 && g_log_lines == 0);
    SetLogSink(nullptr);
}
} // namespace

// Every preset on every GPU-family profile: constrained settings are valid, every loss is reported, and the
// low preset never needs anything.
void CheckCapsProfiles()
{
    Support all;
    all.lut_3d = all.shadow_map = all.scene_depth = all.normal_map = true;
    all.msaa_samples = 4;
    all.msaa_depth = all.anisotropy = all.smaa = all.sea_waves_vertex = all.sea_waves_float = true;
    for (int p = 0; p < static_cast<int>(CapsProfile::Count); ++p)
    {
        const auto profile = static_cast<CapsProfile>(p);
        const Support support = RestrictSupport(all, profile);
        CHECK(std::string(CapsProfileName(profile)) != "?");
        for (QualityPreset preset : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High})
        {
            const Settings requested = Settings::FromPreset(preset);
            std::vector<std::string> changes;
            const Settings constrained = ConstrainSettings(requested, support, changes);
            CHECK(ValidateSettings(constrained, support).empty());
            CHECK(changes.empty() == (constrained == requested));
            if (preset == QualityPreset::Low)
                CHECK(changes.empty());
            if (support.msaa_samples < 2)
                CHECK(constrained.msaa == 1 && !constrained.alpha_to_coverage);
            // Every phone without MSAA still gets anti-aliasing on Средняя / Высокая: SMAA in its place.
            if (preset != QualityPreset::Low)
                CHECK(constrained.smaa == (support.msaa_samples < 2));
        }
    }
    // A profile only removes.
    CHECK(RestrictSupport(Support{}, CapsProfile::Xclipse).msaa_samples == 1);
}

// Axis-aligned box as 12 triangles (the AO bake only needs surfaces).
void AddBox(std::vector<Vec3> &p, std::vector<uint32_t> &i, Vec3 lo, Vec3 hi)
{
    const uint32_t base = static_cast<uint32_t>(p.size());
    for (int c = 0; c < 8; ++c)
        p.push_back({c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]});
    const uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto &f : faces)
        for (const uint32_t t : {f[0], f[1], f[2], f[0], f[2], f[3]})
            i.push_back(base + t);
}

void CheckLampShadows()
{
    // Three candles of one candlestick become one lamp; the fireplace stays apart.
    const Lamp candle{{1.0f, 2.0f, 1.0f}, {2.2f, 1.5f, 0.5f}, 15.0f, 0.5f, 0.0f, 1.5f};
    std::vector<Lamp> lamps = {candle, candle, candle, {{-4.0f, 0.8f, 0.0f}, {2.2f, 1.2f, 0.7f}, 40.0f, 1.5f, 0.0f, 0.2f}};
    lamps[1].position[0] += 0.1f;
    lamps[2].position[2] -= 0.1f;
    const std::vector<Lamp> clustered = ClusterLamps(lamps, 0.6f);
    CHECK(clustered.size() == 2 && Near(clustered[0].color[0], 6.6f, 1e-4f));
    CHECK(Near(clustered[0].position[0], 1.0333f, 1e-3f) && Near(clustered[0].position[2], 0.9667f, 1e-3f));

    // A lamp lights as far as its light stays above the threshold: the candle of lights.ini 3.6 m at 0.12, the
    // fireplace farther, never beyond the limit or its own D3D range; three candles reach farther than one.
    const float candle_radius = LampRadius(candle, 0.12f, 12.0f);
    const float light_there = (0.299f * 2.2f + 0.587f * 1.5f + 0.114f * 0.5f) / (0.5f + 1.5f * candle_radius * candle_radius);
    CHECK(Near(light_there, 0.12f, 1e-3f) && candle_radius > 2.5f && candle_radius < 3.5f);
    CHECK(LampRadius(clustered[0], 0.12f, 12.0f) > candle_radius + 1.0f);
    CHECK(LampRadius(clustered[1], 0.12f, 12.0f) > LampRadius(clustered[0], 0.12f, 12.0f));
    CHECK(LampRadius(clustered[1], 0.001f, 12.0f) == 12.0f && LampRadius(candle, 0.0001f, 50.0f) == 15.0f);

    // The lamp grid: a cell holds the lamps whose radius reaches it, the strongest there first; cells no lamp reaches
    // hold none; a cell that more lamps reach than it holds keeps the strongest and is counted.
    {
        std::vector<Lamp> row;
        for (int i = 0; i < 3; ++i)
            row.push_back({{2.5f + 5.0f * static_cast<float>(i), 1.5f, 2.5f}, {2.0f, 2.0f, 2.0f}, 3.0f, 0.5f, 0.0f, 1.5f});
        const LampGrid grid = BuildLampGrid(row, {0.0f, 0.0f, 0.0f}, {15.0f, 3.0f, 5.0f}, 1.0f, 64, 16);
        CHECK(grid.nx == 15 && grid.ny == 3 && grid.nz == 5 && grid.rgba.size() == 2u * 15 * 3 * 5 * 4 && grid.overflow_cells == 0);
        const auto cell = [&](int x, int y, int z, int k) {
            return grid.rgba[((static_cast<size_t>(z) * grid.ny + y) * (2 * grid.nx) + x + (k / 4) * grid.nx) * 4 + k % 4];
        };
        CHECK(cell(2, 1, 2, 0) == 1 && cell(2, 1, 2, 1) == 0);  // the first lamp's own cell
        CHECK(cell(7, 1, 2, 0) == 2 && cell(12, 1, 2, 0) == 3); // the second and the third
        CHECK(cell(5, 1, 2, 0) != 0 && cell(5, 1, 2, 1) != 0 && cell(5, 1, 2, 2) == 0); // between two: both
        CHECK(cell(4, 1, 2, 0) == 1 && cell(4, 1, 2, 1) == 2); // nearer the first: it comes first
        std::vector<Lamp> crowd(12, row[0]);
        crowd[11].color = {9.0f, 9.0f, 9.0f}; // the strongest, last in the list
        const LampGrid full = BuildLampGrid(crowd, {0.0f, 0.0f, 0.0f}, {5.0f, 3.0f, 5.0f}, 1.0f, 64, 16);
        CHECK(full.max_lamps == 12 && full.overflow_cells > 0);
        const size_t texel = (static_cast<size_t>(2) * full.ny + 1) * (2 * full.nx) + 2;
        CHECK(full.rgba[texel * 4] == 12 && full.rgba[(texel + full.nx) * 4 + 3] != 0);
        // A lamp with radius 0 (a free slot) is in no cell.
        row[1].range = 0.0f;
        const LampGrid without = BuildLampGrid(row, {0.0f, 0.0f, 0.0f}, {15.0f, 3.0f, 5.0f}, 1.0f, 64, 16);
        for (size_t i = 0; i < without.rgba.size(); ++i)
            CHECK(without.rgba[i] != 2);
        // A hall larger than the grid holds cells: larger cells, the same coverage.
        const LampGrid hall = BuildLampGrid(row, {0.0f, 0.0f, 0.0f}, {200.0f, 3.0f, 5.0f}, 1.0f, 64, 16);
        CHECK(hall.nx == 64 && Near(hall.cell[0], 200.0f / 64.0f, 1e-4f));
    }

    // The still triangles a lamp can shadow with: those that touch its sphere, found through the triangle grid; the
    // same list as testing every triangle, for a lamp anywhere (a carried lantern asks every frame).
    {
        std::vector<Vec3> p;
        std::vector<uint32_t> idx;
        AddBox(p, idx, {-0.5f, 0.0f, -0.5f}, {0.5f, 1.0f, 0.5f});   // at the lamp
        AddBox(p, idx, {20.0f, 0.0f, -0.5f}, {21.0f, 1.0f, 0.5f});  // far away
        AddBox(p, idx, {-30.0f, -1.0f, -30.0f}, {30.0f, 0.0f, 30.0f}); // a floor far larger than the lamp's reach
        for (int i = 0; i < 40; ++i) // furniture all over the floor
        {
            const float x = -28.0f + 1.4f * static_cast<float>(i), z = 25.0f * std::sin(1.7f * static_cast<float>(i));
            AddBox(p, idx, {x, 0.0f, z}, {x + 0.8f, 0.9f, z + 0.8f});
        }
        const TriangleGrid grid = BuildTriangleGrid(p, idx, 4.0f);
        CHECK(grid.count == idx.size() / 3 && grid.size[0] >= 15 && grid.size[0] <= kTriangleGridCells && grid.size[1] == 1);
        std::vector<uint32_t> near, seen;
        TrianglesInSphere(grid, idx, {0.0f, 2.0f, 0.0f}, 4.0f, near, seen);
        CHECK(near.size() % 3 == 0 && near.size() >= 36 + 6 && near.size() < idx.size());
        bool far_box = false, floor_top = false;
        for (const uint32_t i : near)
        {
            far_box |= i >= 8 && i < 16;
            floor_top |= i >= 16 && i < 24;
        }
        CHECK(!far_box && floor_top);
        bool same = true;
        for (int probe = 0; probe < 60; ++probe)
        {
            const Vec3 lamp{-29.0f + static_cast<float>(probe), 1.5f, 20.0f * std::cos(0.9f * static_cast<float>(probe))};
            const float reach = 1.0f + 0.2f * static_cast<float>(probe % 30);
            std::vector<uint32_t> expected, got;
            for (size_t t = 0; t + 2 < idx.size(); t += 3)
            {
                Vec3 lo = p[idx[t]], hi = p[idx[t]];
                for (int v = 1; v < 3; ++v)
                    for (int k = 0; k < 3; ++k)
                    {
                        lo[k] = std::min(lo[k], p[idx[t + v]][k]);
                        hi[k] = std::max(hi[k], p[idx[t + v]][k]);
                    }
                float d2 = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    const float nearest = std::clamp(lamp[k], lo[k], hi[k]);
                    d2 += (nearest - lamp[k]) * (nearest - lamp[k]);
                }
                if (d2 <= reach * reach)
                    expected.insert(expected.end(), {idx[t], idx[t + 1], idx[t + 2]});
            }
            TrianglesInSphere(grid, idx, lamp, reach, got, seen);
            same &= got == expected;
        }
        CHECK(same);
        // Nothing for a lamp outside the place, and for a place without triangles.
        near.clear();
        TrianglesInSphere(grid, idx, {500.0f, 0.0f, 0.0f}, 5.0f, near, seen);
        TrianglesInSphere(BuildTriangleGrid(p, {}, 4.0f), {}, {0.0f, 0.0f, 0.0f}, 5.0f, near, seen);
        CHECK(near.empty());
    }

    // The room light: the baked colours around a point, mostly those of the level surfaces. A room with a bright
    // floor and dark walls and ceiling, one half lit warm: the light where one stands is close to the floor's (the
    // walls count little), the warm half is warm, the upper storey's cell is darker, and it is the same every time.
    {
        std::vector<Vec3> p;
        std::vector<uint32_t> idx;
        AddBox(p, idx, {0.0f, 0.0f, 0.0f}, {12.0f, 6.0f, 6.0f});
        std::vector<uint32_t> colors(p.size());
        for (size_t i = 0; i < p.size(); ++i)
        {
            const uint32_t level = p[i][1] < 1.0f ? 160u : 60u;
            const uint32_t blue = p[i][0] < 6.0f ? level / 3u : level;
            colors[i] = 0xff000000u | (level << 16) | (level << 8) | blue;
        }
        const RoomLight room = BuildRoomLight(p, colors, idx, {0.0f, -0.5f, 0.0f}, {12.0f, 8.5f, 6.0f}, 1.5f, 3.0f, 48, 12);
        CHECK(room.nx == 8 && room.ny == 3 && room.nz == 4 && room.rgba.size() == 8u * 3 * 4 * 4);
        const auto at = [&](int x, int y, int z, int ch) { return room.rgba[((static_cast<size_t>(z) * room.ny + y) * room.nx + x) * 4 + ch]; };
        CHECK(at(4, 0, 2, 0) > 110 && at(4, 0, 2, 0) <= 160); // where one stands: near the floor's 160, above the mean
        CHECK(at(4, 0, 2, 0) > at(4, 2, 2, 0) + 40);          // under the ceiling it is the ceiling's
        CHECK(at(1, 0, 1, 2) + 20 < at(6, 0, 1, 2));          // the warm half has less blue
        const RoomLight again = BuildRoomLight(p, colors, idx, {0.0f, -0.5f, 0.0f}, {12.0f, 8.5f, 6.0f}, 1.5f, 3.0f, 48, 12);
        CHECK(again.rgba == room.rgba);
        CHECK(BuildRoomLight(p, colors, idx, {50.0f, 0.0f, 0.0f}, {60.0f, 6.0f, 6.0f}, 1.5f, 3.0f, 48, 12).rgba.empty());
    }

    // Caster side (face view-projection + tile, the distance fs_ff_lamp_caster writes) and receiver side
    // (LampFaceCoord, the distance to the lamp) agree on atlas position and distance in every face, and the guard
    // band keeps points of a face inside its tile.
    const Vec3 lamp{0.5f, 2.0f, -1.0f};
    const float far_plane = 12.0f;
    const int columns = 6, rows = 4;
    int faces_seen = 0;
    for (int i = 0; i < 200; ++i)
    {
        const float a = static_cast<float>(i) * 2.399963f, y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 200.0f;
        const float r = std::sqrt(1.0f - y * y), distance = 0.05f + 0.05f * static_cast<float>(i % 100);
        const Vec3 p{lamp[0] + r * std::cos(a) * distance, lamp[1] + y * distance, lamp[2] + r * std::sin(a) * distance};
        for (const int slot : {0, 3})
        {
            const LampFaceSample s = LampFaceCoord(lamp, p, slot, columns, rows);
            faces_seen |= 1 << s.face;
            const auto vp = LampFaceViewProj(lamp, s.face, far_plane);
            const auto tile_clip = TileClip(LampAtlasTile(slot, s.face, columns, rows));
            float clip[4], atlas[4];
            for (int j = 0; j < 4; ++j)
                clip[j] = p[0] * vp[j] + p[1] * vp[4 + j] + p[2] * vp[8 + j] + vp[12 + j];
            for (int j = 0; j < 4; ++j)
                atlas[j] = clip[0] * tile_clip[j] + clip[1] * tile_clip[4 + j] + clip[2] * tile_clip[8 + j] + clip[3] * tile_clip[12 + j];
            const float x = atlas[0] / atlas[3] * 0.5f + 0.5f, yy = 0.5f - atlas[1] / atlas[3] * 0.5f;
            CHECK(Near(x, s.x, 1e-4f) && Near(yy, s.y, 1e-4f));
            CHECK(Near(LampCasterDistance(clip[0], clip[1], clip[3]), distance, 1e-3f));
            // In front of the near plane (a table top 15 cm under its candle, a wall 3 cm behind its sconce).
            CHECK(clip[3] > kLampShadowNear && clip[2] / clip[3] > 0.0f && clip[2] / clip[3] < 1.0f);
            const AtlasTile t = LampAtlasTile(slot, s.face, columns, rows);
            const float margin = 0.5f * (1.0f - 1.0f / kLampFaceGuard) * t.sx * 0.99f;
            CHECK(s.x >= t.x0 + margin && s.x <= t.x0 + t.sx - margin && s.y >= t.y0 && s.y <= t.y0 + t.sy);
        }
    }
    CHECK(faces_seen == 63);
    // A character 2 m in front of the lamp on +Z is in that face only; one straddling the edge in two; one at the
    // lamp in all six.
    int in_faces = 0, edge_faces = 0, at_lamp = 0;
    for (int f = 0; f < 6; ++f)
    {
        in_faces += SphereInLampFace(lamp, f, {lamp[0], lamp[1], lamp[2] + 2.0f}, 0.4f) ? 1 : 0;
        edge_faces += SphereInLampFace(lamp, f, {lamp[0] + 2.0f, lamp[1], lamp[2] + 2.0f}, 0.4f) ? 1 : 0;
        at_lamp += SphereInLampFace(lamp, f, lamp, 0.4f) ? 1 : 0;
    }
    CHECK(in_faces == 1 && edge_faces == 2 && at_lamp == 6);
}

void CheckAoVolume()
{
    // A 6 x 3 x 6 m room, a table (top at 0.8 m), a barrel 0.7 m from the +x wall.
    std::vector<Vec3> p;
    std::vector<uint32_t> idx;
    AddBox(p, idx, {-3.0f, 0.0f, -3.0f}, {3.0f, 3.0f, 3.0f});
    AddBox(p, idx, {-0.6f, 0.76f, -0.6f}, {0.6f, 0.8f, 0.6f});
    AddBox(p, idx, {1.7f, 0.0f, -0.3f}, {2.3f, 0.9f, 0.3f});
    for (float z = -2.5f; z <= 2.5f; z += 1.0f) // ceiling beams across the room, 0.4 m under the ceiling
        AddBox(p, idx, {-3.0f, 2.6f, z - 0.1f}, {3.0f, 2.8f, z + 0.1f});
    AoBakeParams params;
    params.threads = 1;
    const AoGeometry geometry{p, idx};
    const auto start = std::chrono::steady_clock::now();
    const AoVolume volume = BakeAoVolume(geometry, params);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("ao volume: %dx%dx%d cells of %.2f m baked in %.1f ms (1 thread)\n", volume.size[0], volume.size[1],
                volume.size[2], volume.cell, ms);
    CHECK(!volume.Empty());
    CHECK(Near(volume.cell, 0.2f, 1e-6f));
    CHECK(volume.size[0] == 32 && volume.size[1] == 17 && volume.size[2] == 32);
    // Receivers sample half a cell off the surface along the normal (as the shader does).
    const Vec3 up{0.0f, 1.0f, 0.0f}, west{-1.0f, 0.0f, 0.0f};
    const float open = volume.Sample({-1.5f, 0.1f, -1.5f}, up);
    const float under_table = volume.Sample({0.0f, 0.1f, 0.0f}, up);
    const float corner = volume.Sample({2.9f, 0.1f, 2.9f}, up);
    const float behind_barrel = volume.Sample({2.65f, 0.1f, 0.0f}, up);
    const float in_front_of_barrel = volume.Sample({1.0f, 0.1f, 0.0f}, up);
    const float mid_air = volume.Sample({-1.5f, 1.6f, -1.5f}, up);
    const float wall = volume.Sample({2.9f, 1.5f, -1.5f}, west);
    const float wall_under_beams = volume.Sample({2.9f, 2.35f, -1.5f}, west);
    std::printf("ao volume: open floor %.2f, under the table %.2f, corner %.2f, behind / in front of the barrel %.2f / "
                "%.2f, wall %.2f, wall under the beams %.2f, mid-air %.2f\n",
                open, under_table, corner, behind_barrel, in_front_of_barrel, wall, wall_under_beams, mid_air);
    CHECK(Near(open, kAoOpenSurface, 0.04f) && Near(wall, kAoOpenSurface, 0.04f)); // the shader's reference
    CHECK(under_table < 0.85f * open);         // the shader scales the contrast (fx strength)
    CHECK(behind_barrel < 0.75f * open);       // ... and behind the barrels
    CHECK(behind_barrel < in_front_of_barrel); // ... than in front of them
    CHECK(corner < 0.85f * open);
    CHECK(wall > corner && wall < mid_air);
    CHECK(mid_air > 0.9f);
    CHECK(wall_under_beams > 0.65f * wall); // beams 0.25 m above shade a wall less than a table top shades the floor
    CHECK(volume.At({0.0f, 50.0f, 0.0f}, up) == 1.0f); // outside the grid: open
    // Inside the table top the cells take their open neighbours' value, not 0.
    CHECK(volume.At({0.0f, 0.78f, 0.0f}, up) > 0.2f);

    // A cramped place, a corridor 1.2 m wide and 2 m high, with its walk points: its floor is as light as an open
    // floor (its baked colours hold how light the place is), a shelf across it still darkens the floor under it.
    {
        std::vector<Vec3> cp;
        std::vector<uint32_t> cidx;
        AddBox(cp, cidx, {-0.6f, 0.0f, -4.0f}, {0.6f, 2.0f, 4.0f});
        AddBox(cp, cidx, {-0.6f, 0.76f, 2.0f}, {0.6f, 0.8f, 3.0f});
        AoBakeParams walked = params;
        for (float z = -3.5f; z <= 3.5f; z += 0.5f) // one walks along its middle, under the shelf too
            walked.walk.push_back({0.0f, 0.0f, z});
        const AoVolume corridor = BakeAoVolume({cp, cidx}, walked);
        CHECK(AoBakeKey({cp, cidx}, walked) != AoBakeKey({cp, cidx}, params));
        const float floor = corridor.Sample({0.0f, 0.1f, -1.0f}, up);
        const float under_shelf = corridor.Sample({0.0f, 0.1f, 2.5f}, up);
        std::printf("ao volume, corridor: floor %.2f, under the shelf %.2f\n", floor, under_shelf);
        CHECK(floor > 0.9f * kAoOpenSurface);
        CHECK(under_shelf < 0.85f * floor);
    }

    // The thread count does not change a single cell; the key follows geometry and parameters.
    params.threads = 4;
    const AoVolume threaded = BakeAoVolume(geometry, params);
    CHECK(threaded.visibility == volume.visibility);
    const uint64_t key = AoBakeKey(geometry, params);
    CHECK(key == AoBakeKey(geometry, AoBakeParams{}));    // threads are not part of the key
    AoBakeParams wider = params;
    wider.radius = 2.0f;
    CHECK(key != AoBakeKey(geometry, wider));
    std::vector<Vec3> moved = p;
    moved[0][1] += 0.01f;
    CHECK(key != AoBakeKey({moved, idx}, params));

    // Disk cache round trip and explicit refusals.
    const std::vector<uint8_t> bytes = EncodeAoVolume(volume, key);
    std::string error;
    const auto decoded = DecodeAoVolume(bytes, key, error);
    CHECK(decoded && decoded->visibility == volume.visibility && decoded->size == volume.size &&
          decoded->origin == volume.origin && decoded->cell == volume.cell);
    CHECK(!DecodeAoVolume(bytes, key + 1, error) && error.find("other geometry") != std::string::npos);
    CHECK(!DecodeAoVolume(std::span(bytes).first(bytes.size() - 1), key, error) && !error.empty());
    CHECK(!DecodeAoVolume(std::span(bytes).first(10), key, error) && !error.empty());

    // Bounds: a backdrop far outside the room does not grow the grid, yet still occludes inside it.
    std::vector<Vec3> with_backdrop = p;
    std::vector<uint32_t> backdrop_idx = idx;
    AddBox(with_backdrop, backdrop_idx, {-200.0f, -5.0f, -150.0f}, {250.0f, 45.0f, 120.0f});
    AoBakeParams bounded = params;
    bounded.bounds_lo = {-3.5f, -0.5f, -3.5f};
    bounded.bounds_hi = {3.5f, 3.5f, 3.5f};
    const AoVolume clipped = BakeAoVolume({with_backdrop, backdrop_idx}, bounded);
    CHECK(clipped.size[0] == 37 && clipped.size[1] == 22 && clipped.size[2] == 37 && clipped.cell == volume.cell);
    std::printf("ao volume, bounded: under the table %.2f, open floor %.2f\n", clipped.Sample({0.0f, 0.1f, 0.0f}, up),
                clipped.Sample({-1.5f, 0.1f, -1.5f}, up));
    CHECK(Near(clipped.Sample({0.0f, 0.1f, 0.0f}, up), under_table, 0.08f) &&
          Near(clipped.Sample({-1.5f, 0.1f, -1.5f}, up), open, 0.08f)); // another cell grid
    CHECK(AoBakeKey({with_backdrop, backdrop_idx}, bounded) != AoBakeKey({with_backdrop, backdrop_idx}, params));

    // Nothing to bake: an empty volume (the caller then has no AO for the location and says so).
    CHECK(BakeAoVolume({}, params).Empty());
    // A huge box grows the cell to stay within the cell budget.
    std::vector<Vec3> big;
    std::vector<uint32_t> big_idx;
    AddBox(big, big_idx, {0.0f, 0.0f, 0.0f}, {300.0f, 40.0f, 300.0f});
    AoBakeParams coarse;
    coarse.max_cells = 64 * 64 * 64;
    coarse.radius = 0.5f;
    const AoVolume large = BakeAoVolume({big, big_idx}, coarse);
    CHECK(static_cast<size_t>(large.size[0]) * large.size[1] * large.size[2] <= coarse.max_cells && large.cell > 0.2f);
}

// Materials whose texture alpha cuts holes (fx/cutout.hpp): the CPU passes cut what the colour pass's alpha test cuts.
void CheckCutout()
{
    // A checkerboard of 8 x 8 squares, alpha 255 / 0, and an opaque image.
    const int n = 64;
    std::vector<uint8_t> checker(static_cast<size_t>(n) * n * 4, 255), solid(static_cast<size_t>(n) * n * 4, 255);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            checker[(static_cast<size_t>(y) * n + x) * 4 + 3] = ((x / 8 + y / 8) & 1) ? 0 : 255;
    const float reference = 160.0f / 255.0f;
    AlphaMask mask, none;
    CHECK(MakeAlphaMask(checker, n, n, reference, mask) && mask.width == n && mask.alpha.size() == static_cast<size_t>(n) * n);
    CHECK(!MakeAlphaMask(solid, n, n, reference, none) && none.alpha.empty()); // no holes: no mask, the caster is whole
    solid[3] = 0; // one transparent texel in 4096 is not a cut-out
    CHECK(!MakeAlphaMask(solid, n, n, reference, none));
    CHECK(mask.At(0.06f, 0.06f) == 1.0f && mask.At(0.19f, 0.06f) == 0.0f && mask.At(1.06f, -0.94f) == 1.0f); // wraps

    // A 2 x 2 m quad with that texture, cut into pieces of 5 cm: about half of its area stays, every piece lies on an
    // opaque square, none on a hole.
    std::vector<Vec3> positions = {{0.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 2.0f}, {0.0f, 1.0f, 2.0f}};
    const std::vector<std::array<float, 2>> uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    const std::vector<uint32_t> quad = {0, 1, 2, 0, 2, 3};
    std::vector<uint32_t> pieces;
    AppendCutTriangles(positions, pieces, uvs, quad, mask, reference, 0.05f);
    CHECK(!pieces.empty() && pieces.size() % 3 == 0);
    float area = 0.0f;
    bool on_opaque = true;
    for (size_t t = 0; t < pieces.size(); t += 3)
    {
        const Vec3 &a = positions[pieces[t]], &b = positions[pieces[t + 1]], &c = positions[pieces[t + 2]];
        area += 0.5f * std::fabs((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]));
        const float u = (a[0] + b[0] + c[0]) / 6.0f, v = (a[2] + b[2] + c[2]) / 6.0f; // the quad's uv = position / 2
        on_opaque &= mask.At(u, v) > reference && Near(a[1], 1.0f, 1e-5f);
    }
    CHECK(on_opaque && Near(area, 2.0f, 0.1f));

    // The AO bake under a grating (the cut pieces as points of the bake's cell size; its bars and holes are 25 cm)
    // against under a solid plate of the same size, 0.6 m above a floor: the floor under the grating sees clearly more
    // than under the plate, and less than an open floor.
    const auto floor_visibility = [&](bool plate, bool grating) {
        std::vector<Vec3> p;
        std::vector<uint32_t> idx;
        AddBox(p, idx, {-4.0f, -0.1f, -4.0f}, {6.0f, 0.0f, 6.0f});
        if (plate)
            AddBox(p, idx, {0.0f, 0.6f, 0.0f}, {2.0f, 0.62f, 2.0f});
        if (grating)
        {
            const auto base = static_cast<uint32_t>(p.size());
            for (const Vec3 &corner : {Vec3{0.0f, 0.6f, 0.0f}, Vec3{2.0f, 0.6f, 0.0f}, Vec3{2.0f, 0.6f, 2.0f}, Vec3{0.0f, 0.6f, 2.0f}})
                p.push_back(corner);
            std::vector<std::array<float, 2>> grating_uvs(p.size(), std::array<float, 2>{0.0f, 0.0f});
            grating_uvs[base + 1] = {1.0f, 0.0f};
            grating_uvs[base + 2] = {1.0f, 1.0f};
            grating_uvs[base + 3] = {0.0f, 1.0f};
            const std::vector<uint32_t> tris = {base, base + 1, base + 2, base, base + 2, base + 3};
            AppendCutTriangles(p, idx, grating_uvs, tris, mask, reference, 0.1f, 0.02f);
        }
        AoBakeParams params;
        params.threads = 1;
        params.bounds_lo = {-2.0f, -0.1f, -2.0f};
        params.bounds_hi = {4.0f, 2.0f, 4.0f};
        const AoVolume volume = BakeAoVolume({p, idx}, params);
        return volume.Sample({1.0f, 0.1f, 1.0f}, {0.0f, 1.0f, 0.0f});
    };
    const float open = floor_visibility(false, false), under_plate = floor_visibility(true, false),
                under_grating = floor_visibility(false, true);
    std::printf("cut-out: AO on the floor: open %.2f, under a grating %.2f, under a plate %.2f\n", open, under_grating,
                under_plate);
    // (The volume's rays cross the grating's one-cell layer obliquely and its cells are 0.2 m: it tells a 25 cm grating
    // from a plate only by a few per cent. Finer holes are below what it resolves.)
    CHECK(under_plate < open - 0.15f && under_grating > under_plate + 0.03f && under_grating < open - 0.03f);
}

void CheckSunFit()
{
    // A 40 x 40 m square with three houses; its "lightmap" is dark where the houses shade it from a sun 200° round
    // and 60° up, with some noise. The fit finds that sun, not the one of the hour.
    std::vector<Vec3> p;
    std::vector<uint32_t> idx;
    AddBox(p, idx, {-8.0f, 0.0f, -6.0f}, {-2.0f, 6.0f, 0.0f});
    AddBox(p, idx, {4.0f, 0.0f, 3.0f}, {9.0f, 9.0f, 7.0f});
    AddBox(p, idx, {-3.0f, 0.0f, 8.0f}, {1.0f, 4.0f, 12.0f});
    const Occluders occluders(p, idx);
    Occluders::Scratch scratch;
    const float az = 200.0f * 0.017453293f, el = 60.0f * 0.017453293f;
    const Vec3 baked{std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};
    std::vector<SunFitSample> samples;
    uint32_t seed = 7;
    auto random = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 16777216.0f; };
    for (int i = 0; i < 1500; ++i)
    {
        const Vec3 q{-20.0f + 40.0f * random(), 0.0f, -20.0f + 40.0f * random()};
        const bool shade = occluders.Occluded({q[0], 0.05f, q[2]}, baked, 200.0f, scratch);
        samples.push_back({q, (shade ? 0.25f : 0.7f) + 0.15f * (random() - 0.5f)});
    }
    const SunFit fit = FitBakedSun(occluders, samples, 200.0f, 4);
    const float off = std::acos(std::clamp(fit.dir[0] * baked[0] + fit.dir[1] * baked[1] + fit.dir[2] * baked[2], -1.0f, 1.0f)) * 57.29578f;
    std::printf("sun fit: %.1f° off the bake, r %.2f, %.0f %% shaded\n", off, fit.r, 100.0f * fit.shadowed);
    CHECK(fit.valid && off < 3.0f && fit.r > 0.9f);
    CHECK(!FitBakedSun(occluders, {}).valid); // no samples: no direction
}

void CheckTownLight()
{
    // A 20 x 20 m square (its lightmap: u = (x + 10) / 20, v = (z + 10) / 20) and a 4 m cube in the middle. The sun
    // from 45° up in the west (-x): the cube's shadow falls east of it.
    std::vector<Vec3> p{{-10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, 10.0f}, {-10.0f, 0.0f, 10.0f}};
    std::vector<std::array<float, 2>> uv{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    std::vector<uint32_t> idx{0, 2, 1, 0, 3, 2};
    std::vector<uint32_t> map{0, 0};
    const LightmapTexels texels = RasterizeLightmap(p, uv, idx, map, 0, 64, 64);
    CHECK(texels.texel.size() == 64 * 64 && texels.gutter.empty()); // the whole square: no gutter left
    std::vector<Vec3> occ;
    std::vector<uint32_t> occ_idx;
    AddBox(occ, occ_idx, {-2.0f, 0.0f, -2.0f}, {2.0f, 4.0f, 2.0f});
    const Occluders occluders(occ, occ_idx);
    const Vec3 sun{-0.7071f, 0.7071f, 0.0f};
    const std::vector<uint8_t> vis = SunVisibility(occluders, texels, sun, 20.0f / 64.0f, 4, 2);
    const std::vector<uint8_t> sky = SkyVisibility(occluders, texels, 10.0f, 32, 2);
    auto at = [&](const std::vector<uint8_t> &v, float x, float z) {
        const int tx = static_cast<int>((x + 10.0f) / 20.0f * 64.0f), tz = static_cast<int>((z + 10.0f) / 20.0f * 64.0f);
        for (size_t i = 0; i < texels.texel.size(); ++i)
            if (texels.texel[i] == static_cast<uint32_t>(tz * 64 + tx))
                return v[i] / 255.0f;
        return -1.0f;
    };
    std::printf("town light: sun east of the cube %.2f, west %.2f; sky by the cube %.2f, open %.2f\n", at(vis, 4.0f, 0.0f),
                at(vis, -6.0f, 0.0f), at(sky, 2.4f, 0.0f), at(sky, -8.0f, -8.0f));
    CHECK(at(vis, 4.0f, 0.0f) < 0.1f && std::fabs(at(vis, -6.0f, 0.0f) - 0.707f) < 0.01f && at(vis, 8.0f, 6.0f) > 0.69f);
    // A low sun lights the open ground less: 15° up, the cosine.
    const std::vector<uint8_t> low = SunVisibility(occluders, texels, {-0.9659f, 0.2588f, 0.0f}, 20.0f / 64.0f, 4, 2);
    CHECK(std::fabs(at(low, -8.0f, 6.0f) - 0.2588f) < 0.01f);
    CHECK(at(sky, 2.4f, 0.0f) < at(sky, -8.0f, -8.0f) - 0.2f && at(sky, -8.0f, -8.0f) > 0.8f);
    // A "game" lightmap: paint (stripes of grass and sand) under a known light, 0.06 + 0.28 sun + 0.06 sky.
    std::vector<uint8_t> game(64 * 64 * 4, 255);
    for (size_t i = 0; i < texels.texel.size(); ++i)
    {
        const float paint = (texels.texel[i] / 64) % 8 < 4 ? 1.0f : 0.6f;
        const float light = 0.06f + 0.28f * vis[i] / 255.0f + 0.06f * sky[i] / 255.0f;
        for (int ch = 0; ch < 3; ++ch)
            game[texels.texel[i] * 4 + ch] = static_cast<uint8_t>(std::lround(paint * light * 255.0f));
    }
    const TownLightTone tone = CalibrateTownLight(texels, game, vis, sky);
    std::printf("town light tone: %.3f + %.3f sun + %.3f sky (R² %.2f, %u shadow edges)\n", tone.a, tone.b, tone.c, tone.r2, tone.edges);
    // The game's shadow depth under open sky (shadowed over lit, the sun 45° up), taken from its edges: the least
    // squares alone (a, c low, b 0.22) would make it shallower.
    const float depth = (tone.a + tone.c) / (tone.a + tone.c + tone.b * 0.7071f);
    CHECK(tone.valid && tone.edges >= 50 && std::fabs(depth - 0.12f / (0.12f + 0.28f * 0.7071f)) < 0.02f);
    // Relit for the sun it was baked with: the game's lightmap again; for a sun from the east the cube's shadow falls
    // west of it, the paint stays.
    std::vector<uint8_t> same(64 * 64 * 4, 0), east(64 * 64 * 4, 0);
    ComposeTownLight(texels, tone, game, vis, vis, vis, 0.0f, sky, same);
    double error = 0.0;
    for (size_t i = 0; i < texels.texel.size(); ++i)
        error += std::fabs(same[texels.texel[i] * 4 + 1] - game[texels.texel[i] * 4 + 1]);
    error /= static_cast<double>(texels.texel.size());
    const std::vector<uint8_t> morning = SunVisibility(occluders, texels, {0.7071f, 0.7071f, 0.0f}, 20.0f / 64.0f, 4, 2);
    ComposeTownLight(texels, tone, game, vis, morning, morning, 0.0f, sky, east);
    auto pixel = [&](const std::vector<uint8_t> &img, float x, float z) {
        const int tx = static_cast<int>((x + 10.0f) / 20.0f * 64.0f), tz = static_cast<int>((z + 10.0f) / 20.0f * 64.0f);
        return static_cast<float>(img[(tz * 64 + tx) * 4 + 1]);
    };
    std::printf("town light: relit for its own sun, mean error %.1f / 255; for the morning sun east of the cube %.0f "
                "(was %.0f), west %.0f (was %.0f)\n",
                error, pixel(east, 4.0f, 0.0f), pixel(game, 4.0f, 0.0f), pixel(east, -4.0f, 0.0f), pixel(game, -4.0f, 0.0f));
    CHECK(error < 2.0);
    CHECK(pixel(east, 4.0f, 0.0f) > 2.0f * pixel(game, 4.0f, 0.0f) && pixel(east, -4.0f, 0.0f) < 0.5f * pixel(game, -4.0f, 0.0f));
}

void CheckVisibility()
{
    // A 40 x 40 m ground; a closed hall (10 x 10 m, its ceiling 12 m up); an awning 3 m over a 6 x 6 m patch.
    std::vector<Vec3> p;
    std::vector<uint32_t> idx;
    AddBox(p, idx, {-20.0f, -0.5f, -20.0f}, {20.0f, 0.0f, 20.0f});
    AddBox(p, idx, {5.0f, 0.0f, 5.0f}, {5.3f, 12.0f, 15.0f});
    AddBox(p, idx, {14.7f, 0.0f, 5.0f}, {15.0f, 12.0f, 15.0f});
    AddBox(p, idx, {5.0f, 0.0f, 5.0f}, {15.0f, 12.0f, 5.3f});
    AddBox(p, idx, {5.0f, 0.0f, 14.7f}, {15.0f, 12.0f, 15.0f});
    AddBox(p, idx, {5.0f, 12.0f, 5.0f}, {15.0f, 12.3f, 15.0f});
    AddBox(p, idx, {-12.0f, 3.0f, -12.0f}, {-6.0f, 3.1f, -6.0f});
    const Occluders occluders(p, idx);
    VisibilityParams params;
    params.cell = 1.0f;
    params.threads = 1;
    const VisibilityVolume one = BakeVisibility(occluders, p, idx, params);
    params.threads = 4;
    const VisibilityVolume four = BakeVisibility(occluders, p, idx, params);
    const auto open = one.Sample({-15.0f, 1.0f, 12.0f}), hall = one.Sample({10.0f, 1.5f, 10.0f}),
               awning = one.Sample({-9.0f, 1.0f, -9.0f});
    std::printf("visibility: %dx%dx%d cells of %.2f m; open ground sky %.2f open %.2f, hall %.2f %.2f, awning %.2f %.2f\n",
                one.size[0], one.size[1], one.size[2], one.cell, open[0], open[1], hall[0], hall[1], awning[0], awning[1]);
    CHECK(!one.Empty() && one.rgba == four.rgba); // the thread count does not change it
    CHECK(open[0] > 0.9f && open[1] > 0.9f);
    CHECK(hall[0] < 0.05f && hall[1] < 0.05f);  // a ceiling 12 m up closes it
    // A wall read one cell off it (as ffVisibility does) on either side: the hall's side closed, the street's open.
    const float inside_wall = one.Sample({5.3f + one.cell, 1.5f, 10.0f})[0], outside_wall = one.Sample({5.0f - one.cell, 1.5f, 10.0f})[0];
    std::printf("visibility: by the hall's wall inside %.2f, outside %.2f\n", inside_wall, outside_wall);
    CHECK(inside_wall < 0.15f && outside_wall > 0.5f); // outside: the 12 m wall itself hides half the sky
    CHECK(awning[1] < 0.5f && awning[0] < open[0] - 0.3f);
    // The cache entry: back as baked; another key is refused.
    const uint64_t key = VisibilityKey(p, idx, params);
    std::string error;
    const std::optional<VisibilityVolume> back = DecodeVisibility(EncodeVisibility(one, key), key, error);
    CHECK(back && back->rgba == one.rgba && back->size == one.size && back->cell == one.cell);
    CHECK(!DecodeVisibility(EncodeVisibility(one, key), key + 1, error) && !error.empty());
    CHECK(VisibilityVolume{}.Sample({0.0f, 0.0f, 0.0f})[0] == 1.0f); // none: open
}

std::atomic<int> g_hot_calls{0};
const char *HotTwice()
{
    return g_hot_calls.fetch_add(1) < 3 ? "check: hot" : nullptr;
}

void CheckBakeThermal()
{
    // The bakes' pieces wait while the device is hot and then run all; the result is that of one thread.
    std::vector<int> once(5000, 0), hot(5000, 0);
    ParallelRanges(once.size(), 1, [&](size_t first, size_t last) {
        for (size_t i = first; i < last; ++i)
            once[i] = static_cast<int>(i * 7 % 13);
    });
    SetBakeGate(&HotTwice, 10);
    const auto started = std::chrono::steady_clock::now();
    ParallelRanges(hot.size(), 4, [&](size_t first, size_t last) {
        for (size_t i = first; i < last; ++i)
            hot[i] = static_cast<int>(i * 7 % 13);
    });
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    SetBakeGate(nullptr);
    std::printf("bake thermal: %d status calls, %.0f ms with a pause of 10 ms\n", g_hot_calls.load(), ms);
    CHECK(hot == once && g_hot_calls.load() > 3 && ms >= 10.0);
    // Few heavy items (the sun fit's candidates) still spread over the threads.
    std::atomic<int> pieces{0};
    ParallelRanges(8, 4, [&](size_t first, size_t last) { pieces += 1; (void)first; (void)last; }, 1);
    CHECK(pieces.load() == 8);
}

const char *AlwaysHot()
{
    return "check: always hot";
}

template <class Done> bool WaitFor(Done done)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return done();
}

double MsSince(std::chrono::steady_clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

void CheckBakeCancel()
{
    // The bakes run on their own thread: a bake in the thermal pause (the device stays hot, the pause a minute) is
    // woken and stopped when its owner lets go of it, a bake queued behind it and let go of never runs, the next one
    // runs at once; ShutdownBakes stops a paused bake and returns at once; a bake posted after it runs again.
    SetBakeGate(&AlwaysHot, 60000);
    std::atomic<bool> started{false}, finished{false}, queued_ran{false}, marker{false};
    std::atomic<int> pieces{0};
    auto paused_bake = [&] {
        started = true;
        ParallelRanges(1000, 2, [&](size_t, size_t) { ++pieces; }, 1);
        finished = true; // not reached: ParallelRanges throws BakeCancelled
    };
    BakeTicket paused = PostBake("check: paused", paused_bake);
    BakeTicket queued = PostBake("check: queued", [&] { queued_ran = true; });
    CHECK(WaitFor([&] { return started.load(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // in the pause by now
    queued.reset();
    auto t = std::chrono::steady_clock::now();
    paused.reset();
    BakeTicket next = PostBake("check: next", [&] { marker = true; });
    CHECK(WaitFor([&] { return marker.load(); }));
    const double cancel_ms = MsSince(t);
    std::printf("bake cancel: the paused bake let go of, the next one ran after %.0f ms; %d pieces, queued ran %d\n",
                cancel_ms, pieces.load(), queued_ran.load() ? 1 : 0);
    CHECK(!finished && pieces == 0 && !queued_ran && cancel_ms < 2000.0);

    started = false;
    BakeTicket again = PostBake("check: paused at shutdown", paused_bake);
    CHECK(WaitFor([&] { return started.load(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    t = std::chrono::steady_clock::now();
    ShutdownBakes();
    const double shutdown_ms = MsSince(t);
    std::printf("bake shutdown with a bake in the thermal pause: %.0f ms\n", shutdown_ms);
    CHECK(!finished && pieces == 0 && shutdown_ms < 2000.0);
    SetBakeGate(nullptr);

    marker = false;
    next = PostBake("check: after the shutdown", [&] { marker = true; });
    CHECK(WaitFor([&] { return marker.load(); }));
    ShutdownBakes();
}

void CheckOccluderBackdrop()
{
    // Charles_town-like: a 60 x 60 m town with a 10 m wall, and a backdrop island 18.7 km across (Nevis#31) whose
    // nearest edge is 1 km off. All of it: the cap coarsens the grid (logged as an error). Within the town's box grown
    // by the sun's reach: the island is left out, the cells stay 2 m, and the wall's shadow is the same.
    std::vector<Vec3> positions = {{0.0f, 0.0f, 0.0f},  {60.0f, 0.0f, 0.0f},  {60.0f, 0.0f, 60.0f}, {0.0f, 0.0f, 60.0f},
                                   {30.0f, 0.0f, 10.0f}, {30.0f, 0.0f, 50.0f}, {30.0f, 10.0f, 50.0f}, {30.0f, 10.0f, 10.0f}};
    std::vector<uint32_t> indices = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7};
    const std::vector<Vec3> town(positions.begin(), positions.end());
    constexpr int kSide = 40; // the island: a 40 x 40 vertex mesh, 18.7 km across, up to 600 m high
    const uint32_t first = static_cast<uint32_t>(positions.size());
    for (int z = 0; z < kSide; ++z)
        for (int x = 0; x < kSide; ++x)
        {
            const float u = static_cast<float>(x) / (kSide - 1), v = static_cast<float>(z) / (kSide - 1);
            const float height = 600.0f * std::sin(3.14159f * u) * std::sin(3.14159f * v);
            positions.push_back({1060.0f + 18700.0f * u, height, -9000.0f + 18700.0f * v});
        }
    for (int z = 0; z + 1 < kSide; ++z)
        for (int x = 0; x + 1 < kSide; ++x)
        {
            const uint32_t a = first + z * kSide + x;
            indices.insert(indices.end(), {a, a + kSide, a + 1, a + 1, a + kSide, a + kSide + 1});
        }
    static int errors = 0;
    errors = 0;
    SetLogSink([](LogLevel level, const char *) { errors += level == LogLevel::Error ? 1 : 0; });
    const Occluders all(positions, indices);
    const int all_errors = errors;
    errors = 0;
    const Occluders town_only(positions, indices, BoundsOf(town, kSunRayReach));
    const int town_errors = errors;
    SetLogSink(nullptr);
    Occluders::Scratch scratch;
    const Vec3 sun{0.6f, 0.8f, 0.0f}; // from +x, 53° up: the wall at x = 30 shades the ground west of it
    const bool shade_all = all.Occluded({25.0f, 0.05f, 30.0f}, sun, kSunRayReach, scratch);
    const bool shade_town = town_only.Occluded({25.0f, 0.05f, 30.0f}, sun, kSunRayReach, scratch);
    const bool open_town = town_only.Occluded({40.0f, 0.05f, 30.0f}, sun, kSunRayReach, scratch);
    std::printf("occluder backdrop: all of it %d cap error(s); within the town's reach %d error(s), %zu of %zu triangles "
                "left out (x %.0f..%.0f m); the wall's shade %d / %d, open ground %d\n",
                all_errors, town_errors, town_only.Excluded(), town_only.Triangles(), town_only.ExcludedBounds().lo[0],
                town_only.ExcludedBounds().hi[0], shade_all ? 1 : 0, shade_town ? 1 : 0, open_town ? 1 : 0);
    CHECK(all_errors == 1 && town_errors == 0 && town_only.Excluded() == static_cast<size_t>((kSide - 1) * (kSide - 1) * 2) &&
          shade_all && shade_town && !open_town);
}

void CheckBakeLoads()
{
    // A result in the cache (a bake's load stage) does not wait behind a running bake: the same cache read posted as
    // a plain bake waits for the bake running before it, posted with a load stage it is done at once.
    std::atomic<bool> release{false}, running{false};
    auto long_bake = [&] {
        running = true;
        while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        running = false;
    };
    constexpr auto kHold = std::chrono::milliseconds(300);
    std::atomic<bool> loaded{false};
    BakeTicket hold = PostBake("check: long bake", long_bake);
    CHECK(WaitFor([&] { return running.load(); }));
    auto t = std::chrono::steady_clock::now();
    BakeTicket plain = PostBake("check: cache read as a bake", [&] { loaded = true; });
    std::this_thread::sleep_for(kHold);
    release = true;
    CHECK(WaitFor([&] { return loaded.load(); }));
    const double plain_ms = MsSince(t);

    release = false;
    loaded = false;
    std::atomic<int> baked{0};
    hold = PostBake("check: long bake", long_bake);
    CHECK(WaitFor([&] { return running.load(); }));
    t = std::chrono::steady_clock::now();
    BakeTicket staged = PostBake("check: cache read as a load", [&](BakeStage stage) {
        if (stage == BakeStage::Bake)
            ++baked;
        loaded = true;
        return true; // found in the cache
    });
    CHECK(WaitFor([&] { return loaded.load(); }));
    const double load_ms = MsSince(t);
    const bool still_running = running.load();
    // Not in the cache: the bake stage follows on the bake thread, after the running bake.
    std::atomic<bool> missed{false};
    BakeTicket miss = PostBake("check: not in the cache", [&](BakeStage stage) {
        if (stage == BakeStage::Load)
            return false;
        missed = true;
        return true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool missed_early = missed.load();
    release = true;
    CHECK(WaitFor([&] { return missed.load(); }));
    std::printf("bake loads: a cache read behind a running bake waited %.0f ms as a bake, %.1f ms as a load (the "
                "bake still running: %d); a miss is baked after it (%d)\n",
                plain_ms, load_ms, still_running ? 1 : 0, missed_early ? 0 : 1);
    CHECK(plain_ms >= 300.0 && load_ms < 100.0 && still_running && baked == 0 && !missed_early);
    ShutdownBakes();
}

void CheckOccluderCap()
{
    // Two triangles 20 km apart (and 500 m up): 2 m cells would be 2.5e10; the grid is capped at kMaxOccluderCells
    // with larger cells (logged), and the rays hit what they hit with the asked cells.
    const std::vector<Vec3> positions = {{0.0f, 0.0f, 0.0f},         {0.0f, 0.0f, 1.0f},         {1.0f, 0.0f, 0.0f},
                                         {20000.0f, 500.0f, 20000.0f}, {20000.0f, 500.0f, 20001.0f}, {20001.0f, 500.0f, 20000.0f}};
    const std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
    int errors = 0;
    static int *g_errors = nullptr;
    g_errors = &errors;
    SetLogSink([](LogLevel level, const char *) { *g_errors += level == LogLevel::Error ? 1 : 0; });
    const Occluders occluders(positions, indices);
    SetLogSink(nullptr);
    Occluders::Scratch scratch;
    const Vec3 down{0.0f, -1.0f, 0.0f};
    const bool near_hit = occluders.Occluded({0.2f, 5.0f, 0.2f}, down, 10.0f, scratch);
    const bool far_hit = occluders.Occluded({20000.2f, 505.0f, 20000.2f}, down, 10.0f, scratch);
    const bool between = occluders.Occluded({5000.0f, 505.0f, 5000.0f}, down, 600.0f, scratch);
    std::printf("occluder cap: %d error line(s), hits %d %d, between %d\n", errors, near_hit ? 1 : 0, far_hit ? 1 : 0,
                between ? 1 : 0);
    CHECK(errors == 1 && near_hit && far_hit && !between);
}

void CheckOccluderLargeTown()
{
    // Charles_town's box with the sun's reach around it, 814 x 268 x 792 m: larger cells than 2 m (logged), no error.
    const std::vector<Vec3> positions = {{0.0f, 0.0f, 0.0f},     {0.0f, 0.0f, 1.0f},     {1.0f, 0.0f, 0.0f},
                                         {814.0f, 268.0f, 792.0f}, {813.0f, 268.0f, 792.0f}, {814.0f, 268.0f, 791.0f}};
    const std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
    static int errors = 0, infos = 0;
    errors = infos = 0;
    SetLogSink([](LogLevel level, const char *) { ++(level == LogLevel::Error ? errors : infos); });
    const Occluders occluders(positions, indices);
    SetLogSink(nullptr);
    std::printf("occluder large town: %d error(s), %d info line(s)\n", errors, infos);
    CHECK(errors == 0 && infos == 1);
}

void CheckBounce()
{
    // A 40 x 40 m red ground and a grey wall 10 m high along z at x 5..5.3; the sun 60° up from -x lights the ground
    // and the wall's west face, the wall's east face and the ground east of it up to 5.8 m stand in its shadow.
    // The game's winding (D3D: clockwise front faces, left-handed): the cross product of the edges faces out.
    std::vector<Vec3> p;
    std::vector<uint32_t> idx;
    AddBox(p, idx, {-20.0f, -0.5f, -20.0f}, {20.0f, 0.0f, 20.0f});
    const size_t ground_triangles = idx.size() / 3;
    AddBox(p, idx, {5.0f, 0.0f, -20.0f}, {5.3f, 10.0f, 20.0f});
    for (size_t t = 0; t < idx.size(); t += 3)
        std::swap(idx[t + 1], idx[t + 2]); // AddBox winds inwards
    std::vector<uint32_t> texture(idx.size() / 3, 1);
    std::fill(texture.begin(), texture.begin() + ground_triangles, 0u);
    const BounceTexture red{1, 1, {153, 26, 26}}, grey{1, 1, {102, 102, 102}}; // 0.6, 0.1, 0.1 and 0.4
    const std::vector<const BounceTexture *> textures = {&red, &grey};
    const std::vector<std::array<float, 2>> uvs(p.size(), {0.0f, 0.0f});
    const Occluders occluders(p, idx);
    BounceGeometry geometry{p, uvs, idx, texture, textures};
    BounceGrid grid;
    grid.cell = 1.0f;
    grid.origin = {-10.0f, -1.0f, -6.0f};
    grid.size = {30, 14, 12};
    BounceParams params;
    params.surfel = 1.0f;
    params.rays = 64;
    params.threads = 4;
    const auto started = std::chrono::steady_clock::now();
    const BounceScene scene = BuildBounceScene(occluders, geometry, grid, params);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    CHECK(!scene.Empty() && scene.stride == 1 && scene.untextured == 0);
    const float elevation = 60.0f * 0.017453293f;
    const Vec3 sun{-std::cos(elevation), std::sin(elevation), 0.0f};
    const SurfelLight light = SunOnSurfels(occluders, scene, sun, {1.0f, 1.0f, 1.0f}, 4);
    const BounceField field = GatherBounce(scene, light, 4);
    // Read one cell off the surface, as ffBounce does.
    const Rgb shaded_wall = field.Irradiance({5.3f + 1.0f, 3.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    const Rgb lit_ground = field.Irradiance({14.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const Rgb shaded_ground = field.Irradiance({7.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const Rgb lit_wall = field.Irradiance({5.0f - 1.0f, 3.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    std::printf("bounce: %zu surfels, %zu points x %d rays in %.0f ms; shaded wall %.3f %.3f %.3f, lit ground %.3f, shaded "
                "ground %.3f, lit wall %.3f\n",
                scene.position.size(), scene.Points(), scene.rays, ms, shaded_wall[0], shaded_wall[1], shaded_wall[2],
                lit_ground[0], shaded_ground[0], lit_wall[0]);
    // The shaded wall takes the lit red ground's light, reddish: the lit ground (0.6 x sin 60°) fills a view factor of
    // (sin 32° - sin 12.3°) / 2 = 0.16 of it, 0.08 (the L1 lobe gives a little less).
    CHECK(shaded_wall[0] > 0.05f && shaded_wall[0] > 2.0f * shaded_wall[1]);
    // A lit floor does not relight itself from the cell above it: far from the wall it gets almost nothing.
    CHECK(lit_ground[0] < 0.25f * shaded_wall[0]);
    // The shaded ground by the wall sees the lit ground beyond the shadow (and the wall above it is in shade too).
    CHECK(shaded_ground[0] > lit_ground[0]);
    // The lit wall faces the lit red ground too.
    CHECK(lit_wall[0] > 0.08f && lit_wall[0] > 2.0f * lit_wall[2]);
    // No sun, no bounce; the lamps light by their falloff and shadow: a lamp 1 m east of the wall lights the shaded
    // side, its light comes back to the wall from the ground.
    CHECK(GatherBounce(scene, SunOnSurfels(occluders, scene, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 1), 1).Energy() == 0.0);
    const Lamp lamp{{6.3f, 2.0f, 0.0f}, {2.0f, 1.5f, 0.8f}, 6.0f, 1.0f, 0.0f, 0.3f};
    const float holder[1] = {0.2f};
    const BounceField lamps = GatherBounce(scene, LampsOnSurfels(occluders, scene, std::span(&lamp, 1), holder, 4), 4);
    const Rgb by_lamp = lamps.Irradiance({5.3f + 1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    const Rgb behind_wall = lamps.Irradiance({5.0f - 1.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f});
    std::printf("bounce: by the lamp %.3f %.3f %.3f, behind the wall %.4f\n", by_lamp[0], by_lamp[1], by_lamp[2], behind_wall[0]);
    CHECK(by_lamp[0] > 0.02f && behind_wall[0] < 0.01f * by_lamp[0]);
    // The threads do not change it; the cache entry comes back as baked, another key is refused.
    params.threads = 1;
    const BounceScene one = BuildBounceScene(occluders, geometry, grid, params);
    CHECK(one.hit == scene.hit && one.inside == scene.inside && one.link == scene.link);
    const uint64_t key = BounceKey(geometry, grid, params);
    std::string error;
    const std::optional<BounceScene> back = DecodeBounceScene(EncodeBounceScene(one, key), key, error);
    CHECK(back && back->hit == one.hit && back->albedo == one.albedo && back->stride == one.stride);
    CHECK(!DecodeBounceScene(EncodeBounceScene(one, key), key + 1, error) && !error.empty());
    // The texture: every cell of the grid, v (the light's direction) in block 1's B and A and block 2's A.
    std::vector<uint8_t> vis(static_cast<size_t>(grid.size[0]) * grid.size[1] * grid.size[2] * 4, 0), block;
    EncodeBounceBlocks(grid, field, {1.0f, 1.0f, 1.0f}, nullptr, vis, block);
    CHECK(block.size() == vis.size());
    const size_t wall_cell = ((static_cast<size_t>(6) * grid.size[1] + 4) * grid.size[0] + 16) * 4; // x 6.5, y 3.5, z 0.5
    const float c = block[wall_cell] / 255.0f, vx = vis[wall_cell + 2] / 255.0f * 4.0f - 2.0f;
    std::printf("bounce: the shaded wall's cell c %.3f, v.x %.2f\n", c * c * kBounceRange, vx);
    CHECK(c * c * kBounceRange > 0.02f && vx > 0.3f); // red, from the east (the lit ground beyond the shadow)
    // A weather's sun of half the strength, plus the lamp's field: the lamp next to the cell dominates it.
    std::vector<uint8_t> lit_vis = vis, lit_block;
    EncodeBounceBlocks(grid, field, {0.5f, 0.5f, 0.5f}, &lamps, lit_vis, lit_block);
    CHECK(lit_block[wall_cell + 1] > block[wall_cell + 1] && lit_vis[wall_cell] == vis[wall_cell]); // R / G of block 1 kept
}

// fx::SeaWaves' fixed steps (WaveClock): a 60 Hz frame takes one, a slow frame at most kMaxSteps, the rest is dropped.
void CheckSeaWaves()
{
    WaveClock clock;
    CHECK(clock.Due(1.0f / 60.0f) == 1);
    CHECK(clock.Due(0.0f) == 0);
    int total = 0;
    for (int i = 0; i < 120; ++i)
        total += clock.Due(1.0f / 30.0f);
    CHECK(total >= 238 && total <= 241); // two per 30 Hz frame
    CHECK(clock.Due(1.0f) == WaveClock::kMaxSteps);
    CHECK(clock.Due(1.0f / 120.0f) <= 1); // the dropped time is not made up later
}

// Gloss maps (gloss_map.hpp): the class table on TEHO's names, the class order on one texture, the texel cues.
void CheckGlossMaps()
{
    const auto gloss = [](const char *path) { return ClassifyGlossName(NormalizeTexturePath(path)).gloss; };
    CHECK(gloss("Resource\\Textures\\cannon_T1.tga.tx") == GlossClass::Metal);
    CHECK(gloss("Resource\\Textures\\dm_guns.tga.tx") == GlossClass::Metal);
    CHECK(gloss("Resource\\Textures\\Gundeck_1.tga.tx") == GlossClass::DryWood);
    CHECK(gloss("Resource\\Textures\\Ships\\Brig1\\Hull1\\brighull1.tga.tx") == GlossClass::PaintedWood);
    CHECK(gloss("Resource\\Textures\\Ships\\Brig1\\Hull1\\deck_brig.tga.tx") == GlossClass::DryWood);
    CHECK(gloss("Resource\\Textures\\Ships\\parus_common.tga.tx") == GlossClass::Matte);
    CHECK(gloss("Resource\\Textures\\GlassWindow.tga.tx") == GlossClass::Glass);
    CHECK(gloss("Resource\\Textures\\windtavern2ins.tga.tx") == GlossClass::Glass);
    CHECK(gloss("Resource\\Textures\\windmill_walls.tga.tx") == GlossClass::Building);
    CHECK(gloss("Resource\\Textures\\tavern2ins.tga.tx") == GlossClass::Interior);
    CHECK(gloss("Resource\\Textures\\BigHouse01.tga.tx") == GlossClass::Building);
    CHECK(gloss("Resource\\Textures\\furnitureU1.tga.tx") == GlossClass::Lacquered);
    CHECK(gloss("Resource\\Textures\\bag2.tga.tx") == GlossClass::Leather);
    CHECK(gloss("Resource\\Textures\\063pierres.tga.tx") == GlossClass::Stone);
    CHECK(gloss("Resource\\Textures\\Barn2_bump.tga.tx") == GlossClass::None);
    CHECK(gloss("Resource\\Textures\\Characters\\blaze.tga.tx") == GlossClass::None);
    CHECK(gloss("Resource\\Textures\\antigua.tga.tx") == GlossClass::None);

    // The roughness network's output corrected (no model here: fields made up): GGX's roughness as the highlight's power,
    // the grooves of the height, dark non-metal texels, the metal mask, the class caps.
    constexpr int kSize = 64;
    constexpr size_t kTexels = size_t(kSize) * kSize;
    std::vector<uint8_t> grey, rusty;
    for (size_t i = 0; i < kTexels; ++i)
    {
        const uint8_t n = static_cast<uint8_t>((i * 7919) % 23), grain = static_cast<uint8_t>((i * 104729) % 41);
        rusty.insert(rusty.end(), {static_cast<uint8_t>(40 + grain), static_cast<uint8_t>(18 + grain / 2), static_cast<uint8_t>(6), 255});
        grey.insert(grey.end(), {static_cast<uint8_t>(95 + n), static_cast<uint8_t>(100 + n), static_cast<uint8_t>(105 + n), 255});
    }
    const std::vector<float> flat(kTexels, 0.5f);
    const auto mean = [&](const GlossField &f) {
        double sum = 0.0;
        for (const float g : f.gloss)
            sum += g;
        return f.gloss.size() == kTexels ? sum / static_cast<double>(kTexels) : -1.0;
    };
    const auto at = [&](float r, GlossClass c, const std::vector<uint8_t> &rgba = {}) {
        return mean(GlossOfRoughness(std::vector<float>(kTexels, r), flat, rgba.empty() ? grey : rgba, kSize, kSize, c));
    };
    // r 0.3: alpha 0.09, power 2/0.0081 - 2 = 245, g = (log2 245 - 3) / 4.5 = 1.1 -> 1; r 0.5: power 30, 0.42; r 0.67 and
    // up: matte.
    std::printf("gloss of roughness 0.3 %.2f, 0.5 %.2f, 0.7 %.2f\n", at(0.3f, GlossClass::Glass), at(0.5f, GlossClass::Glass),
                at(0.7f, GlossClass::Glass));
    CHECK(at(0.3f, GlossClass::Glass) > 0.99 && std::abs(at(0.5f, GlossClass::Glass) - 0.424) < 0.02 &&
          at(0.7f, GlossClass::Glass) == 0.0);
    // Caps: cloth and plaster 0.1, stone 0.35.
    CHECK(std::abs(at(0.3f, GlossClass::Matte) - 0.1) < 1e-6 && std::abs(at(0.3f, GlossClass::Stone) - 0.35) < 1e-6);
    // Dark non-metal texels lose the gloss; metal keeps it, and polished metal at least half of it whatever the network.
    const std::vector<uint8_t> black(kTexels * 4, 8);
    CHECK(at(0.5f, GlossClass::Glass, black) < 0.01 && at(0.5f, GlossClass::Metal, black) > 0.4);
    const std::vector<float> mask = GlossMetalMask(grey, kSize, kSize), rust_mask = GlossMetalMask(rusty, kSize, kSize);
    std::printf("metal: grey %.2f (mask %.2f), rust %.2f (mask %.2f)\n", at(0.9f, GlossClass::Metal), mask[100],
                at(0.9f, GlossClass::Metal, rusty), rust_mask[100]);
    CHECK(at(0.9f, GlossClass::Metal) > 0.45 && at(0.9f, GlossClass::Metal, rusty) + 0.1 < at(0.9f, GlossClass::Metal));
    CHECK(mask.size() == kTexels && mask[100] > 0.95f && rust_mask[100] < 0.8f);
    CHECK(GlossOfRoughness(flat, flat, grey, kSize, kSize, GlossClass::None).gloss.empty());
    CHECK(GlossOfRoughness(flat, {}, grey, kSize, kSize, GlossClass::Stone).gloss.empty());
    // A groove in the height keeps a quarter of the gloss.
    std::vector<float> groove = flat;
    for (int y = 0; y < kSize; ++y)
        for (int x = 30; x < 33; ++x)
            groove[size_t(y) * kSize + x] = 0.1f;
    const GlossField grooved = GlossOfRoughness(std::vector<float>(kTexels, 0.4f), groove, grey, kSize, kSize, GlossClass::Glass);
    CHECK(grooved.gloss[31] < 0.4f * grooved.gloss[10]);
    // The gloss map file's texture: RG8, all mips, R the metal mask, G the gloss.
    const GlossField metal = GlossOfRoughness(flat, flat, grey, kSize, kSize, GlossClass::Metal);
    const NormalMap material = GlossMaterial(metal);
    CHECK(material.format == NormalMapFormat::Rg8 && material.width == kSize && material.mip_count == 7 &&
          material.rg.size() == NormalMapBytes(kSize, kSize, NormalMapFormat::Rg8) && material.rg[200] > 240 &&
          std::abs(material.rg[201] - std::lround(metal.gloss[100] * 255.0f)) <= 1);
}

// The parallax trust (parallax_trust.hpp): plain relief keeps it, a dark pane set into a wall loses it, a texture of
// nothing but panes is below the coverage a texture needs; the exclusion lists (parallax_exclusions.hpp).
// The parallax probe's CPU side (fx_pom_probe): the centre-ray candidates, the pick from the occlusion queries, the view
// order that runs the probe view after the scene, the toggle's edge.
void CheckParallaxProbe()
{
    // Camera at the origin looking down +z (identity view), symmetric perspective.
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float proj[16] = {1.5f, 0, 0, 0, 0, 2.0f, 0, 0, 0, 0, 1.001f, 1, 0, 0, -0.1f, 0};
    const float ahead[3] = {0.3f, -0.2f, 10.0f}, aside[3] = {3.0f, 0.0f, 10.0f}, behind[3] = {0.0f, 0.0f, -5.0f};
    CHECK(OnCentreRay(identity, proj, ahead, 0.5f));
    CHECK(!OnCentreRay(identity, proj, aside, 0.5f));
    CHECK(OnCentreRay(identity, proj, aside, 3.5f));
    CHECK(!OnCentreRay(identity, proj, behind, 1.0f));
    CHECK(OnCentreRay(identity, proj, behind, 6.0f)); // the camera inside the sphere
    // A view that moves the world 10 units back along z: a sphere at the origin is then ahead.
    const float moved[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 10, 1};
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    CHECK(OnCentreRay(moved, proj, origin, 0.1f));
    // An off-centre projection: its centre ray leans by -_31 / _11.
    const float skewed[16] = {1.0f, 0, 0, 0, 0, 1.0f, 0, 0, -0.5f, 0, 1.001f, 1, 0, 0, -0.1f, 0};
    const float leaning[3] = {5.0f, 0.0f, 10.0f};
    CHECK(OnCentreRay(identity, skewed, leaning, 0.2f));
    CHECK(!OnCentreRay(identity, proj, leaning, 0.2f));

    const int32_t none[] = {0, 0};
    const int32_t wall_and_decal[] = {4, 0, 2};
    const int32_t pending[] = {4, kProbeNoResult};
    CHECK(PickProbeTarget(none).conclusive && PickProbeTarget(none).index == -1);
    CHECK(PickProbeTarget(wall_and_decal).conclusive && PickProbeTarget(wall_and_decal).index == 2);
    CHECK(!PickProbeTarget(pending).conclusive);
    CHECK(PickProbeTarget({}).conclusive && PickProbeTarget({}).index == -1);

    // bgfx::setViewOrder's swaps (bgfx_p.h), from identity: the probe view runs right after `last`, the rest keep
    // their order; a reset (null order) restores identity.
    constexpr int kViews = 40;
    uint16_t remap[kViews], position[kViews];
    const auto set_order = [&](uint16_t first, uint16_t count, const uint16_t *order) {
        for (uint16_t i = 0; i < count; ++i)
        {
            const uint16_t pos = first + i, id = order ? order[i] : pos;
            const uint16_t prev_pos = position[id], prev_id = remap[pos];
            remap[pos] = id;
            position[id] = pos;
            remap[prev_pos] = prev_id;
            position[prev_id] = prev_pos;
        }
    };
    for (uint16_t i = 0; i < kViews; ++i)
        remap[i] = position[i] = i;
    for (const uint16_t last : {uint16_t(12), uint16_t(30)})
    {
        set_order(0, kViews, nullptr);
        const std::vector<uint16_t> order = ProbeViewOrder(7, last);
        CHECK(order.size() == static_cast<size_t>(last - 7 + 1));
        set_order(7, static_cast<uint16_t>(order.size()), order.data());
        bool ok = position[7] == position[last] + 1 && position[last + 1] == position[7] + 1;
        for (uint16_t id = 0; id + 1 < kViews; ++id)
            if (id != 7 && id + 1 != 7)
                ok = ok && position[id] < position[id + 1];
        CHECK(ok);
    }
    set_order(0, kViews, nullptr);
    bool identity_again = true;
    for (uint16_t i = 0; i < kViews; ++i)
        identity_again = identity_again && remap[i] == i;
    CHECK(identity_again);
    CHECK(ProbeViewOrder(7, 7).empty());

    EdgeTrigger toggle;
    CHECK(!toggle.Apply(-1)); // the stored value at the start
    CHECK(!toggle.Apply(-1)); // re-applied unchanged
    CHECK(toggle.Apply(1));
    CHECK(!toggle.Apply(1));
    CHECK(toggle.Apply(-1));
}

void CheckParallaxTrust()
{
    const int n = 256;
    const size_t texels = static_cast<size_t>(n) * n;
    std::vector<uint8_t> rgba(texels * 4, 255);
    std::vector<float> height(texels);
    uint32_t seed = 12345;
    const auto noise = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    // Plain stone: mid-grey colour with grain, a height of grain with thin joints.
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const size_t t = static_cast<size_t>(y) * n + x;
            const uint8_t grey = static_cast<uint8_t>(120 + noise() * 60.0f);
            rgba[t * 4] = rgba[t * 4 + 1] = rgba[t * 4 + 2] = grey;
            height[t] = 0.6f + noise() * 0.1f - ((x % 32 < 2 || y % 32 < 2) ? 0.3f : 0.0f);
        }
    const std::vector<float> plain = ParallaxTrust(rgba, height, n, n);
    CHECK(plain.size() == texels && TrustCoverage(plain) > 0.95f);
    // A window: a 40 x 56 texel pane, dark and flat, its height deep.
    std::vector<uint8_t> walled = rgba;
    std::vector<float> recessed = height;
    for (int y = 100; y < 156; ++y)
        for (int x = 100; x < 140; ++x)
        {
            const size_t t = static_cast<size_t>(y) * n + x;
            walled[t * 4] = walled[t * 4 + 1] = walled[t * 4 + 2] = 20;
            recessed[t] = 0.05f;
        }
    const std::vector<float> with_window = ParallaxTrust(walled, recessed, n, n);
    CHECK(with_window[128 * n + 120] < 0.05f);                      // the pane: no parallax
    CHECK(with_window[20 * n + 20] > 0.95f && with_window[200 * n + 200] > 0.95f); // the wall far from it keeps it
    CHECK(TrustCoverage(with_window) > 0.85f && TrustCoverage(with_window) < 0.97f);
    CHECK(TrustThumbnail(with_window, n, n).size() == static_cast<size_t>(TrustThumbnailSize(n)) * TrustThumbnailSize(n));
    // Nothing but dark flat glass: no trusted texel to speak of.
    std::vector<uint8_t> glass(texels * 4, 15);
    CHECK(TrustCoverage(ParallaxTrust(glass, std::vector<float>(texels, 0.5f), n, n)) < kParallaxMinCoverage);
    CHECK(ParallaxTrust(rgba, std::vector<float>(10), n, n).empty());

    // In the material: the pane's height goes to the top (1), the coverage and the mask are kept; a texture below the
    // coverage keeps its height whole.
    NormalMap map;
    map.width = map.height = n;
    map.format = NormalMapFormat::Rg8;
    for (int w = n; w >= 1; w /= 2)
        ++map.mip_count;
    map.rg.assign(NormalMapBytes(n, n), 128);
    std::vector<float> varied = recessed;
    for (size_t t = 0; t < texels; ++t)
        varied[t] += 0.3f * static_cast<float>((t / n / 8 + t % n / 8) & 1);
    CHECK(AddReliefMaterial(map, ReliefClass::Stone, varied, {}, walled));
    CHECK(map.trust_coverage > 0.85f && map.trust.size() == static_cast<size_t>(TrustThumbnailSize(n)) * TrustThumbnailSize(n));
    CHECK(map.material[(128 * n + 120) * 2] > 250); // the pane: the top
    NormalMap dark = map;
    dark.material.clear();
    CHECK(AddReliefMaterial(dark, ReliefClass::Stone, varied, {}, glass));
    CHECK(dark.trust_coverage < kParallaxMinCoverage);

    // The lists.
    ParallaxExclusions lists;
    std::vector<std::string> problems;
    lists.LoadShipped("# comment\nexclude signboardsa  # a sign\n\nbad name\ninclude locations/wall\n", &problems);
    CHECK(lists.shipped_count() == 2 && problems.size() == 1);
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "storm_fx_pom_user_check" / "user.txt";
    std::filesystem::remove_all(file.parent_path());
    CHECK(lists.LoadUser(file) && lists.user_count() == 0);
    CHECK(lists.Of("signboardsa") == ParallaxExclusions::Verdict::Excluded && lists.Of("locations/wall") == ParallaxExclusions::Verdict::Included &&
          lists.Of("tavern2") == ParallaxExclusions::Verdict::Default);
    // Masonry takes the parallax by default, nothing else does.
    CHECK(ReliefTakesParallax(ReliefClass::Stone) && ReliefTakesParallax(ReliefClass::Building) && !ReliefTakesParallax(ReliefClass::Wood) &&
          !ReliefTakesParallax(ReliefClass::Prop) && !ReliefTakesParallax(ReliefClass::Plaster) && !ReliefTakesParallax(ReliefClass::Metal) &&
          !ReliefTakesParallax(ReliefClass::HeightMap) && !ReliefTakesParallax(ReliefClass::None));
    CHECK(lists.SetUser("signboardsa", true) && lists.SetUser("tavern2", false) &&
          lists.Of("signboardsa") == ParallaxExclusions::Verdict::Included && lists.Of("tavern2") == ParallaxExclusions::Verdict::Excluded);
    ParallaxExclusions again;
    again.LoadShipped(ParallaxExclusions::ShippedText());
    CHECK(again.shipped_count() > 0 && again.Of("pierwood1") == ParallaxExclusions::Verdict::Included);
    CHECK(again.LoadUser(file) && again.user_count() == 2 && again.shipped_count() > 0 &&
          again.Of("signboardsa") == ParallaxExclusions::Verdict::Included && again.Of("tavern2") == ParallaxExclusions::Verdict::Excluded);
    std::filesystem::remove_all(file.parent_path());
}

int main()
{
    CheckSettings();
    CheckLut();
    CheckToneCurve();
    CheckNoise();
    CheckSky();
    CheckLighting();
    CheckSupport();
    CheckCapsProfiles();
    CheckLights();
    CheckShadows();
    CheckNormalMaps();
    CheckCheapUpgrades();
    CheckHeightMapSizes();
    CheckReliefModel();
    CheckReliefMaterial();
    CheckReliefRules();
    CheckAoVolume();
    CheckLampShadows();
    CheckCutout();
    CheckSunFit();
    CheckTownLight();
    CheckVisibility();
    CheckBakeThermal();
    CheckBakeCancel();
    CheckBakeLoads();
    CheckOccluderCap();
    CheckOccluderLargeTown();
    CheckOccluderBackdrop();
    CheckBounce();
    CheckParallaxTrust();
    CheckParallaxProbe();
    CheckSeaWaves();
    CheckGlossMaps();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fx core checks passed\n");
    return 0;
}
