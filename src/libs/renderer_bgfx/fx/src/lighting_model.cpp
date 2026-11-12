#include "storm/fx/lighting_model.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{

LightingMaterial LightingMaterial::Stone()
{
    return {16.0f, 0.05f, 0.0f, 0.0f, 1.0f, 0.0f};
}

LightingMaterial LightingMaterial::Hull()
{
    return {32.0f, 0.22f, 0.0f, 0.0f, 1.0f, 0.3f};
}

LightingMaterial LightingMaterial::Sail()
{
    return {8.0f, 0.04f, 0.55f, 0.2f, 0.35f, 0.3f};
}

LightingMaterial LightingMaterial::Character()
{
    return {20.0f, 0.10f, 0.0f, 0.2f, 0.0f, 0.5f};
}

LightingUniforms ComputeLighting(const LightingInput &input, const LightingMaterial &material, const SkyState *sky,
                                 const Settings &settings)
{
    const auto to_view = [&](Vec3 v) {
        Vec3 r;
        for (int j = 0; j < 3; ++j)
            r[j] = v[0] * input.view[j] + v[1] * input.view[4 + j] + v[2] * input.view[8 + j];
        const float len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        return len > 1e-6f ? Vec3{r[0] / len, r[1] / len, r[2] / len} : Vec3{0.0f, 1.0f, 0.0f};
    };
    const Vec3 sun = to_view(input.sun_dir);
    const Vec3 up = to_view(Vec3{0.0f, 1.0f, 0.0f});

    // Hemisphere: brighter, sky-tinted light from above and dimmer, warmer light from below. The two average
    // to the flat ambient, so the overall brightness of a scene does not change with the flag.
    constexpr float kContrast = 0.3f;
    Vec3 hue{1.0f, 1.0f, 1.0f};
    if (sky)
    {
        const float luma = std::max(0.2126f * sky->dome[0] + 0.7152f * sky->dome[1] + 0.0722f * sky->dome[2], 1e-4f);
        for (int i = 0; i < 3; ++i) // gamma-space hue of the dome, half strength
            hue[i] = 0.5f + 0.5f * std::sqrt(sky->dome[i] / luma);
    }
    LightingUniforms u{};
    u.u_fxLight[0] = {sun[0], sun[1], sun[2], material.specular_power};
    u.u_fxLight[1] = {input.sun_diffuse[0], input.sun_diffuse[1], input.sun_diffuse[2], material.specular_strength};
    u.u_fxLight[2] = {up[0], up[1], up[2], material.translucency};
    for (int i = 0; i < 3; ++i)
    {
        u.u_fxLight[3][i] = input.ambient[i] * (1.0f + kContrast) * hue[i];
        u.u_fxLight[4][i] = input.ambient[i] * (1.0f - kContrast) * (2.0f - hue[i]);
    }
    u.u_fxLight[3][3] = material.wrap;
    u.u_fxLight[5] = {material.relief * std::clamp(settings.normal_map_strength, 0.0f, 1.0f), material.rim, 0.0f, 0.0f};
    return u;
}

int PointLightBudget(const Settings &settings)
{
    if (!(settings.lighting_flags & kLightingPoint))
        return 0;
    return settings.point_lights > 4 ? 8 : 4;
}

std::string LightingDefines(const Settings &settings)
{
    const uint32_t flags = settings.lighting_flags;
    std::string defines;
    const auto add = [&defines](const char *define) {
        if (!defines.empty())
            defines += ';';
        defines += define;
    };
    if (flags & kLightingPerPixel)
    {
        add("FX_LIGHT_PER_PIXEL");
        if (flags & kLightingHemisphere)
            add("FX_LIGHT_HEMISPHERE");
        if (flags & kLightingSpecular)
            add("FX_LIGHT_SPECULAR");
        if (flags & kLightingTranslucent)
            add("FX_LIGHT_TRANSLUCENT");
        if (settings.shadows != ShadowQuality::Off)
            add("FX_LIGHT_SHADOW");
        if (settings.shadows == ShadowQuality::Soft)
            add("FX_LIGHT_SHADOW_SOFT");
        if (flags & kLightingRim)
            add("FX_LIGHT_RIM");
    }
    if (flags & kLightingPoint)
    {
        add("FX_LIGHT_POINT");
        add(PointLightBudget(settings) > 4 ? "FX_LIGHT_POINT_MAX=8" : "FX_LIGHT_POINT_MAX=4");
    }
    if ((flags & kLightingNormalMap) && (flags & (kLightingPerPixel | kLightingPoint)))
        add("FX_LIGHT_NORMALMAP");
    if ((flags & kLightingLinear) && (flags & (kLightingPerPixel | kLightingPoint)))
        add("FX_LIGHT_LINEAR");
    if (flags & kLightingAerial)
        add("FX_AERIAL");
    if (flags & kLightingWind)
        add("FX_WIND");
    return defines;
}

} // namespace storm::fx
