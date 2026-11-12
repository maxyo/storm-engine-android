// Unit checks for the CPU side of the fx package: grading LUTs, tone curve,
// baked noise, the sky model, lighting and point-light selection, the shadow cascade and the
// explicit handling of unsupported settings. No GPU is needed.
#include "storm/fx/ao_volume.hpp"
#include "storm/fx/capabilities.hpp"
#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/color_lut.hpp"
#include "storm/fx/light_set.hpp"
#include "storm/fx/lighting_model.hpp"
#include "storm/fx/log.hpp"
#include "storm/fx/noise_bake.hpp"
#include "storm/fx/normal_map.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/shadow_model.hpp"
#include "storm/fx/sky_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
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
    CHECK(!original.sky_atmosphere);
    CHECK(original.lighting_flags == kLightingNone);
    const Settings medium = Settings::FromPreset(QualityPreset::Medium);
    const Settings high = Settings::FromPreset(QualityPreset::High);
    CHECK(!medium.PostIsIdentity() && !high.PostIsIdentity());
    CHECK(medium.bloom_downscale == 4 && high.bloom_downscale == 2);
    // FSR 1 on both: Средняя renders the scene smaller than Высокая; «Низкая» stays native without any post pass.
    CHECK(medium.sharpen && high.sharpen && medium.render_scale < high.render_scale && high.render_scale < 1.0f);
    CHECK(medium.render_scale >= 0.5f && original.render_scale == 1.0f && !original.sharpen);
    // MSAA is the primary anti-aliasing of the presets; SMAA is left to the player. Optional looks stay off.
    CHECK(medium.msaa == 4 && high.msaa == 4 && !medium.smaa && !high.smaa && original.msaa == 1);
    CHECK(medium.anisotropy == 4 && high.anisotropy == 8 && original.anisotropy == 1);
    CHECK(medium.dither && !original.dither);
    // The game's own sea everywhere: the band limit from «Средняя», soft edges at «Высокая», the specular optional.
    CHECK(medium.sea_band_limit && !medium.sea_soft_edges && high.sea_band_limit && high.sea_soft_edges);
    CHECK(!medium.sea_specular && !high.sea_specular);
    CHECK((medium.lighting_flags & kLightingPerPixel) && !(medium.lighting_flags & kLightingSpecular));
    CHECK(medium.shadows == ShadowQuality::Low && high.shadows == ShadowQuality::Soft && original.shadows == ShadowQuality::Off);
    CHECK(!medium.ssao && high.ssao && !original.ssao);
    CHECK(original.ao_volume == 0 && medium.ao_volume == 0 && high.ao_volume == 0); // off until interiors are reworked
    CHECK(original.lamp_shadows == 0 && medium.lamp_shadows == 0 && high.lamp_shadows == 0); // off until fixed
    // The presets keep the legacy look: no tone curve, grade, flare, shafts, fx sky, hemisphere or linear sums.
    for (const Settings *preset : {&medium, &high})
        CHECK(!preset->tonemap && !preset->scene_grading && !preset->lens_flare && !preset->sun_shafts &&
              !preset->sky_atmosphere && preset->grade == GradePreset::Caribbean && preset->grade_strength <= 0.4f &&
              !(preset->lighting_flags & (kLightingHemisphere | kLightingLinear | kLightingRim | kLightingAerial)));
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
    // Filmic shape: darker shadows, white not clipped.
    CHECK(TonemapTransfer(0.2f) < 0.2f);
    CHECK(TonemapTransfer(1.0f) > 0.9f && TonemapTransfer(1.0f) < 1.0f);
    // At the default strength shadows are only mildly deepened.
    const Settings defaults;
    CHECK(TonemapTransfer(0.1f, 1.0f, defaults.tonemap_strength) > 0.055f);
    CHECK(Near(TonemapTransfer(0.37f, 1.0f, 0.0f), 0.37f, 1e-5f));
    CHECK(Near(ExpandLdr(1.0f), 3.0f, 1e-4f) && Near(ExpandLdr(0.0f), 0.0f, 1e-6f));
}

void CheckNoise()
{
    const BakedTexture clouds = BakeCloudNoise(64, 3u);
    CHECK(clouds.size == 64 && clouds.mip_count == 7);
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

    // Overcast: grey, darker dome, more cloud, weaker sun.
    const SkyState storm = sky_for(0.85f, 1.0f);
    const Vec3 storm_zenith = SkyColor(storm, {0.0f, 1.0f, 0.0f});
    CHECK(storm_zenith[2] / std::max(storm_zenith[0], 1e-4f) < 1.6f);
    CHECK(storm.cloud_cover > noon.cloud_cover + 0.3f);
    CHECK(storm.sun_light[1] < noon.sun_light[1] * 0.5f);

    // Fog colour is the horizon in the viewing direction, and its average is `horizon`.
    const Vec3 fog = FogColor(noon, {-1.0f, 0.0f});
    CHECK(Near(fog[0], horizon[0], 1e-4f) && Near(fog[2], horizon[2], 1e-4f));
    CHECK(noon.horizon[1] > 0.5f * fog[1] && noon.horizon[1] < 2.0f * fog[1]);
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
          "FX_LIGHT_PER_PIXEL;FX_LIGHT_SHADOW;FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=4;FX_LIGHT_NORMALMAP");
    CHECK(LightingDefines(Settings::FromPreset(QualityPreset::High)) ==
          "FX_LIGHT_PER_PIXEL;FX_LIGHT_SPECULAR;FX_LIGHT_TRANSLUCENT;FX_LIGHT_SHADOW;FX_LIGHT_SHADOW_SOFT;"
          "FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=8;FX_LIGHT_NORMALMAP");
    CHECK(PointLightBudget(Settings::FromPreset(QualityPreset::High)) == 8);
}

// Nothing is downgraded silently: every unsupported request is reported and fixed explicitly.
void CheckSupport()
{
    Support all;
    all.lut_3d = all.shadow_map = all.scene_depth = all.normal_map = true;
    all.msaa = all.msaa_depth = all.anisotropy = all.ao_volume = all.smaa = true;
    for (QualityPreset preset : {QualityPreset::Low, QualityPreset::Medium, QualityPreset::High})
    {
        const Settings settings = Settings::FromPreset(preset);
        CHECK(ValidateSettings(settings, all).empty());
        std::vector<std::string> changes;
        CHECK(ConstrainSettings(settings, all, changes) == settings && changes.empty());
        // The original look never depends on a capability.
        if (preset == QualityPreset::Low)
            CHECK(ValidateSettings(settings, Support{}).empty());
    }
    const Settings high = Settings::FromPreset(QualityPreset::High);

    // No sampleable scene depth: occlusion and the sea's soft edges off.
    Support no_depth = all;
    no_depth.scene_depth = false;
    CHECK(ValidateSettings(high, no_depth).size() == 2);
    std::vector<std::string> changes;
    Settings constrained = ConstrainSettings(high, no_depth, changes);
    CHECK(!constrained.ssao && !constrained.sea_soft_edges && constrained.sea_band_limit);
    CHECK(changes.size() == 2);
    CHECK(constrained.shadows == high.shadows && constrained.bloom && constrained.lighting_flags == high.lighting_flags);
    CHECK(ValidateSettings(constrained, no_depth).empty());

    // No shadow maps.
    Support no_shadow = all;
    no_shadow.shadow_map = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_shadow, changes);
    CHECK(constrained.shadows == ShadowQuality::Off && constrained.lamp_shadows == 0 && changes.size() == 1);
    CHECK(LightingDefines(constrained).find("FX_LIGHT_SHADOW") == std::string::npos);

    // No R8 3D textures: no AO volume, said once; the rest stays.
    Support no_ao = all;
    no_ao.ao_volume = false;
    changes.clear();
    Settings with_ao = high;
    with_ao.ao_volume = 3; // fx_ao_volume 3
    constrained = ConstrainSettings(with_ao, no_ao, changes);
    CHECK(constrained.ao_volume == 0 && changes.size() == 1 && constrained.shadows == high.shadows);
    // An AO volume without the fx lighting path has no shader to apply it: switched off, said.
    Settings unlit = with_ao;
    unlit.lighting_flags = kLightingNone;
    unlit.shadows = ShadowQuality::Off;
    unlit.lamp_shadows = 0;
    changes.clear();
    CHECK(ConstrainSettings(unlit, all, changes).ao_volume == 0 && changes.size() == 1);
    Settings too_strong = with_ao;
    too_strong.ao_volume = 9;
    changes.clear();
    CHECK(ConstrainSettings(too_strong, all, changes).ao_volume == kAoVolumeLevels && changes.size() == 1);

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
    CHECK(constrained.msaa == 4 && !constrained.ssao && !constrained.sea_soft_edges && changes.size() == 2);
    // No MSAA at all: alpha to coverage goes with it, and then depth-based effects are fine again.
    Support no_msaa = no_msaa_depth;
    no_msaa.msaa = false;
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
    CHECK(constrained.msaa == 1 && !constrained.smaa && constrained.PostIsIdentity() && constrained.render_scale == 1.0f);
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
    // The FSR 1 range of the render scale; the upscale is part of the post chain.
    Settings scaled = high;
    scaled.render_scale = 0.3f;
    changes.clear();
    CHECK(ConstrainSettings(scaled, all, changes).render_scale == 0.5f && changes.size() == 1);
    scaled.render_scale = 0.7f;
    Settings only_scaled;
    only_scaled.render_scale = 0.7f;
    CHECK(Settings{}.PostIsIdentity() && !only_scaled.PostIsIdentity());
    CHECK(RendererStateFor(scaled).render_scale == 0.7f);
    changes.clear();
    CHECK(ConstrainSettings(scaled, no_lut, changes).render_scale == 1.0f && !changes.empty());
    const RendererState state = RendererStateFor(high);
    CHECK(state.msaa_samples == 4 && state.alpha_to_coverage && state.max_anisotropy == 8);
    CHECK(RendererStateFor(Settings{}).msaa_samples == 1 && !RendererStateFor(Settings{}).alpha_to_coverage);
    Support no_aniso = all;
    no_aniso.anisotropy = false;
    changes.clear();
    CHECK(ConstrainSettings(high, no_aniso, changes).anisotropy == 1 && changes.size() == 1);

    // No RG8 textures: normal maps go, and so does the shader define.
    Support no_rg = all;
    no_rg.normal_map = false;
    changes.clear();
    constrained = ConstrainSettings(high, no_rg, changes);
    CHECK(changes.size() == 1 && !(constrained.lighting_flags & kLightingNormalMap));
    CHECK(LightingDefines(constrained).find("NORMALMAP") == std::string::npos);
    // One relief strength for both presets: half of the APK #10 look.
    CHECK(Settings::FromPreset(QualityPreset::Medium).normal_map_strength == high.normal_map_strength &&
          high.normal_map_strength == 0.5f);

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
    other = options;
    other.max_size = 512;
    CHECK(NormalMapCacheKey(options) != NormalMapCacheKey(other));
    other = options;
    other.source = NormalMapSource::Luminance;
    CHECK(NormalMapCacheKey(options) != NormalMapCacheKey(other));
    NormalMapOptions wider = other;
    wider.band_sigma = other.band_sigma + 1.0f;
    CHECK(NormalMapCacheKey(other) != NormalMapCacheKey(wider));
}

// Max lean |n.xy| over a square of the top mip.
float MaxLean(const NormalMap &map, int x0, int y0, int x1, int y1)
{
    float lean = 0.0f;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const auto normal = NormalAt(map, 0, x, y);
            lean = std::max(lean, std::hypot(normal[0], normal[1]));
        }
    return lean;
}

// Colour textures: luminance, band-passed, as height.
void CheckLuminanceRelief()
{
    const int n = 128;
    const NormalMapOptions stone = ReliefOptions(ReliefClass::Stone);
    CHECK(stone.source == NormalMapSource::Luminance && stone.strength > ReliefOptions(ReliefClass::Wood).strength &&
          ReliefOptions(ReliefClass::Wood).strength > ReliefOptions(ReliefClass::Plaster).strength &&
          ReliefOptions(ReliefClass::Building).strength > ReliefOptions(ReliefClass::Plaster).strength);
    CHECK(ReliefOptions(ReliefClass::HeightMap).source == NormalMapSource::Height);
    const auto texture = [&](auto colour) {
        std::vector<uint8_t> rgba;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const std::array<uint8_t, 3> c = colour(x, y);
                rgba.insert(rgba.end(), {c[0], c[1], c[2], 255});
            }
        return rgba;
    };

    // A flat colour (any colour) has no relief at all.
    const NormalMap flat = DeriveNormalMap(texture([](int, int) { return std::array<uint8_t, 3>{180, 90, 40}; }), n, n, stone);
    CHECK(flat.width == n && std::all_of(flat.rg.begin(), flat.rg.end(), [](uint8_t v) { return v == 128; }));

    // A big dark patch (a stain, a painted panel) on a plain wall: the inside of the patch is as flat as the wall around
    // it (no pit); only its rim leans, and only over a few texels.
    const NormalMap patch = DeriveNormalMap(texture([](int x, int y) {
                                                const bool dark = std::abs(x - 64) < 32 && std::abs(y - 64) < 32;
                                                return dark ? std::array<uint8_t, 3>{40, 40, 50} : std::array<uint8_t, 3>{200, 190, 170};
                                            }),
                                            n, n, stone);
    CHECK(MaxLean(patch, 48, 48, 80, 80) < 0.02f); // centre of the patch
    CHECK(MaxLean(patch, 0, 0, 16, 16) < 0.02f);   // the wall far from it
    CHECK(MaxLean(patch, 28, 60, 36, 68) > 0.3f);  // the rim
    // The same patch at twice the size: still flat inside, so the depth does not grow with the patch (a height field
    // would make a pit whose walls get no flatter).
    const NormalMap wide = DeriveNormalMap(texture([](int x, int y) {
                                               const bool dark = std::abs(x - 64) < 56 && std::abs(y - 64) < 56;
                                               return dark ? std::array<uint8_t, 3>{40, 40, 50} : std::array<uint8_t, 3>{200, 190, 170};
                                           }),
                                           n, n, stone);
    CHECK(MaxLean(wide, 40, 40, 88, 88) < 0.02f);

    // Bricks: light bricks with dark mortar lines every 16 texels (rows) and 32 texels (columns). The brick faces are
    // higher than the mortar: on both sides of a horizontal line the normal leans down into it, in v only.
    const NormalMap bricks = DeriveNormalMap(texture([](int x, int y) {
                                                 const bool mortar = y % 16 < 2 || (x + (y / 16 % 2) * 16) % 32 < 2;
                                                 const uint8_t v = mortar ? 60 : 190;
                                                 return std::array<uint8_t, 3>{v, static_cast<uint8_t>(v * 0.8f), static_cast<uint8_t>(v * 0.6f)};
                                             }),
                                             n, n, stone);
    const auto above = NormalAt(bricks, 0, 8, 16 - 2); // the brick above the line at y = 16..17: leans to +v
    const auto below = NormalAt(bricks, 0, 8, 17 + 2); // the brick below it: leans to -v
    CHECK(above[1] > 0.3f && below[1] < -0.3f && std::fabs(above[0]) < 0.05f && std::fabs(below[0]) < 0.05f);
    // ... and next to a vertical line (x = 0..1 in this row) the lean is in u.
    const auto right = NormalAt(bricks, 0, 2, 8);
    CHECK(right[0] < -0.3f && std::fabs(right[1]) < 0.05f);
    // The middle of a brick is nearly flat (only the faint halo of the band pass).
    CHECK(MaxLean(bricks, 14, 7, 18, 10) < 0.1f);
    // Stronger classes lean more on the same texture.
    const auto plaster = NormalAt(DeriveNormalMap(texture([](int x, int y) {
                                                      const bool mortar = y % 16 < 2 || (x + (y / 16 % 2) * 16) % 32 < 2;
                                                      const uint8_t v = mortar ? 60 : 190;
                                                      return std::array<uint8_t, 3>{v, v, v};
                                                  }),
                                                  n, n, ReliefOptions(ReliefClass::Plaster)),
                                  0, 8, 17 + 2);
    CHECK(plaster[1] < 0.0f && plaster[1] > below[1]);

    // A lower preset derives a smaller map with the same relief: a 2048-wide stripe pattern derived at
    // 512 leans like the 1024 map (strength and band are given at kNormalMapReferenceSize).
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
    NormalMapOptions full = stone, half = stone;
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
    // The sea under the haze: denser than the plain fog, less so from the masthead than from the deck.
    CHECK(SeaFogDensity(noon, 2.0f) > SeaFogDensity(noon, 30.0f) && SeaFogDensity(noon, 30.0f) > noon.fog_density);
    CHECK(SeaFogDensity(noon, 2.0f) < noon.fog_density * (1.0f + kHazeDensityFactor) + 1e-6f);

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
    const ColorGrade day = SceneGrade(sky_at(0.8f), 0.0f, 0.0f), dusk = SceneGrade(sky_at(0.08f), 0.0f, 0.0f);
    const ColorGrade night = SceneGrade(sky_at(-0.5f), 0.0f, 0.0f), storm = SceneGrade(sky_at(0.8f), 1.0f, 0.0f);
    const ColorGrade indoors = SceneGrade(sky_at(-0.5f), 0.0f, 1.0f);
    CHECK(day == ColorGrade::ForMood(ColorGrade::Mood::TropicalNoon) && night == ColorGrade::ForMood(ColorGrade::Mood::Night));
    CHECK(indoors == ColorGrade::ForMood(ColorGrade::Mood::Interior));
    CHECK(dusk.Apply(grey)[0] > dusk.Apply(grey)[2] + 0.015f);   // warm
    CHECK(night.Apply(grey)[2] > night.Apply(grey)[0] + 0.015f); // cool
    CHECK(storm.saturation < day.saturation && night.saturation < day.saturation);
    // A small step of the sun is a small step of the grade (no jump at dusk).
    float worst = 0.0f;
    for (int i = 0; i < 60; ++i)
    {
        const auto a = SceneGrade(sky_at(-0.3f + 0.01f * i), 0.0f, 0.0f).Apply(grey), b = SceneGrade(sky_at(-0.29f + 0.01f * i), 0.0f, 0.0f).Apply(grey);
        worst = std::max({worst, std::fabs(a[0] - b[0]), std::fabs(a[2] - b[2])});
    }
    CHECK(worst < 0.02f);
    // The post chain rebuilds the table only when the drift shows in 8-bit output.
    CHECK(GradeDifference(day, day) == 0.0f && GradeDifference(day, night) > 0.02f);
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
        {"dirtK3", P}, {"tropa256", P}, {"jungleU5", P}, {"1grass1", N}, {"grassgreen1", N},
        // no relief: characters, UI, sky, water, glass, foliage cards, cloth and sails, decals, light, lightmaps
        {"Characters\\Lo_Man_1", N}, {"INTERFACES\\PORTRAITS\\128\\face_1", N}, {"BATTLE_INTERFACE\\cannons", N},
        {"WEATHER\\SKIES\\day", N}, {"lighting\\day1\\Fort_lm", N}, {"Ships\\parus_common", N},
        {"Ships\\Mayfang1\\Hull1\\mayfang1_alpha", N}, {"Ships\\trailShip", N}, {"sky_up", N}, {"water1", N},
        {"sea_1", N}, {"GlassWindow", N}, {"windBigHouse01", N}, {"res_win", N}, {"occlTempleU1", N}, {"reflChurchU2", N},
        {"MontserratMask", N}, {"PalmsSprites", N}, {"leafPalms", N}, {"Ferns", N}, {"tree4", N}, {"cloth1", N},
        {"carpetU4", N}, {"ropeU1", N}, {"FishingNets", N}, {"SignboardsA", N}, {"smoke_dirt", N}, {"bonfire_small", N},
        {"corona01", N}, {"fonar", N}, {"lighthouseFonarU1", N}, {"candle4c", N}, {"WaterFall", N}, {"River1", N},
        {"metalU4", N}, {"cannon_T1", N}, {"skelet3", N}, {"telo", N}, {"Item_Rapier02", N}, {"MenuWoodFlooooor01", N},
        {"HiDeckalpha", N}, {"cap_deck_glass", N}, {"Particles_list", N}, {"shadow2", N}, {"back_jungle", N},
    };
    for (const auto &[path, expected] : cases)
        if (relief(path) != expected)
        {
            std::printf("FAILED relief rule: %s is %s, expected %s\n", path, ReliefClassName(relief(path)), ReliefClassName(expected));
            ++failures;
        }
    // An unknown name has no rule: no relief.
    CHECK(ClassifyTextureName("antigua").rule == nullptr && ClassifyTextureName("antigua").relief == N);

    SetLogSink([](LogLevel level, const char *) { ++(level == LogLevel::Debug ? g_debug_lines : g_log_lines); });
    ReliefClassifier classifier;
    const auto verdict = [&](const char *path, const std::vector<uint8_t> &pixels) { return classifier.Classify(path, pixels); };
    CHECK(verdict("bump_city.tga.tx", grey).relief == ReliefClass::HeightMap);
    // A coloured "bump" texture is an ordinary one: here the table's stone.
    CHECK(verdict("bump_rock.tga", tinted).relief == S);
    // The sea's animated bump frames stay without relief (folder rule), grey or not.
    CHECK(verdict("WEATHER\\SEA\\BUMP_G2MX\\seabump0017.tga.tx", grey).relief == N);
    CHECK(verdict("deckPlanksU2.tga", {}).relief == W);
    CHECK(verdict("Antigua.tga.tx", {}).relief == N);
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
    all.msaa = all.msaa_depth = all.anisotropy = all.smaa = true;
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
            if (!support.msaa)
                CHECK(constrained.msaa == 1 && !constrained.alpha_to_coverage);
            // Every phone without MSAA still gets anti-aliasing on Средняя / Высокая: SMAA in its place.
            if (preset != QualityPreset::Low)
                CHECK(constrained.smaa == !support.msaa);
        }
    }
    // A profile only removes.
    CHECK(RestrictSupport(Support{}, CapsProfile::Xclipse).msaa == false);
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

    // Selection: the nearest strong lamp first; out of range lamps never; hysteresis keeps a slot.
    std::vector<Lamp> room = {{{0.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 10.0f, 0.5f, 0.0f, 1.0f},
                              {{6.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 10.0f, 0.5f, 0.0f, 1.0f},
                              {{50.0f, 2.0f, 0.0f}, {9.0f, 9.0f, 9.0f}, 10.0f, 0.5f, 0.0f, 1.0f}};
    std::vector<int> pick = SelectShadowLamps(room, {1.0f, 1.7f, 0.0f}, 2, {});
    CHECK(pick.size() == 2 && pick[0] == 0 && pick[1] == 1);
    pick = SelectShadowLamps(room, {1.0f, 1.7f, 0.0f}, 1, {});
    CHECK(pick.size() == 1 && pick[0] == 0);
    // At the middle the right lamp is marginally stronger; the left one keeps its slot.
    pick = SelectShadowLamps(room, {3.1f, 1.7f, 0.0f}, 1, std::vector<int>{0});
    CHECK(pick.size() == 1 && pick[0] == 0);
    pick = SelectShadowLamps(room, {5.5f, 1.7f, 0.0f}, 1, std::vector<int>{0});
    CHECK(pick.size() == 1 && pick[0] == 1);

    // Caster side (face view-projection + tile) and receiver side (LampShadowCoord) agree on atlas position and depth
    // in every face, and the guard band keeps points of a face inside its tile.
    const Vec3 lamp{0.5f, 2.0f, -1.0f};
    const float far_plane = 12.0f;
    const int columns = 6, rows = 4;
    int faces_seen = 0;
    for (int i = 0; i < 200; ++i)
    {
        const float a = static_cast<float>(i) * 2.399963f, y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 200.0f;
        const float r = std::sqrt(1.0f - y * y), distance = 0.6f + 0.05f * static_cast<float>(i % 100);
        const Vec3 p{lamp[0] + r * std::cos(a) * distance, lamp[1] + y * distance, lamp[2] + r * std::sin(a) * distance};
        for (const int slot : {0, 3})
        {
            const LampShadowSample s = LampShadowCoord(lamp, p, far_plane, slot, columns, rows);
            faces_seen |= 1 << s.face;
            const auto vp = LampFaceViewProj(lamp, s.face, far_plane);
            const auto tile_clip = TileClip(LampAtlasTile(slot, s.face, columns, rows));
            float clip[4], atlas[4];
            for (int j = 0; j < 4; ++j)
                clip[j] = p[0] * vp[j] + p[1] * vp[4 + j] + p[2] * vp[8 + j] + vp[12 + j];
            for (int j = 0; j < 4; ++j)
                atlas[j] = clip[0] * tile_clip[j] + clip[1] * tile_clip[4 + j] + clip[2] * tile_clip[8 + j] + clip[3] * tile_clip[12 + j];
            const float x = atlas[0] / atlas[3] * 0.5f + 0.5f, yy = 0.5f - atlas[1] / atlas[3] * 0.5f;
            CHECK(Near(x, s.x, 1e-4f) && Near(yy, s.y, 1e-4f) && Near(clip[2] / clip[3], s.depth, 1e-4f));
            const AtlasTile t = LampAtlasTile(slot, s.face, columns, rows);
            const float margin = 0.5f * (1.0f - 1.0f / kLampFaceGuard) * t.sx * 0.99f;
            CHECK(s.x >= t.x0 + margin && s.x <= t.x0 + t.sx - margin && s.y >= t.y0 && s.y <= t.y0 + t.sy);
            CHECK(s.depth > 0.0f && s.depth < 1.0f);
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
    CheckLuminanceRelief();
    CheckReliefRules();
    CheckAoVolume();
    CheckLampShadows();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fx core checks passed\n");
    return 0;
}
