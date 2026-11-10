#include "storm/fx/settings.hpp"

namespace storm::fx
{

Settings Settings::FromPreset(QualityPreset preset)
{
    Settings s;
    if (preset == QualityPreset::Original)
        return s;

    s.tonemap = true;
    s.bloom = true;
    s.fxaa = true;
    s.sky_atmosphere = true;
    s.sky_clouds = true;
    if (preset == QualityPreset::Medium)
    {
        s.bloom_downscale = 4;
        s.bloom_levels = 3;
        s.bloom_intensity = 0.30f;
        s.water = WaterQuality::Medium;
        s.lighting_flags = kLightingPerPixel | kLightingHemisphere | kLightingPoint;
        s.point_lights = 4;
        s.shadows = ShadowQuality::Low;
        return s;
    }
    s.bloom_downscale = 2;
    s.bloom_levels = 4;
    s.bloom_intensity = 0.35f;
    s.sharpen = true;
    s.sun_shafts = true;
    s.water = WaterQuality::High;
    s.water_refraction = true;
    s.lighting_flags = kLightingAll;
    s.point_lights = 8;
    s.shadows = ShadowQuality::Soft;
    s.ssao = true;
    return s;
}

bool Settings::PostIsIdentity() const
{
    const bool graded = grade != GradePreset::Original && grade_strength > 0.0f;
    return !tonemap && !graded && !bloom && !fxaa && !sharpen && !sun_shafts && !ssao;
}

std::string Support::Describe() const
{
    const auto word = [](bool ok) { return ok ? "yes" : "NO"; };
    std::string text = "3D colour table: ";
    text += word(lut_3d);
    text += ", shadow map: ";
    text += word(shadow_map);
    text += ", scene snapshot (2 colour targets): ";
    text += word(scene_snapshot);
    text += ", wave field in the vertex shader: ";
    text += word(wave_field);
    text += ", sampleable scene depth: ";
    text += word(scene_depth);
    return text;
}

namespace
{
// One rule = one way a request cannot be honoured, and the change that makes the settings honest again.
struct Rule
{
    bool (*violated)(const Settings &, const Support &);
    void (*fix)(Settings &);
    const char *message;
};

constexpr Rule kRules[] = {
    {[](const Settings &s, const Support &c) {
         return !c.lut_3d && (s.tonemap || s.bloom || s.ssao || s.sun_shafts || (s.grade != GradePreset::Original && s.grade_strength > 0.0f));
     },
     [](Settings &s) {
         s.tonemap = s.bloom = s.ssao = s.sun_shafts = false;
         s.grade = GradePreset::Original;
     },
     "tone curve, colour grade, bloom, sun shafts and ambient occlusion need 3D textures: switched off"},
    {[](const Settings &s, const Support &) { return s.sun_shafts && !s.bloom; }, [](Settings &s) { s.sun_shafts = false; },
     "sun shafts are part of the bloom chain and bloom is off: sun shafts switched off"},
    {[](const Settings &s, const Support &c) { return s.ssao && !c.scene_depth; }, [](Settings &s) { s.ssao = false; },
     "ambient occlusion needs a sampleable scene depth texture: switched off"},
    {[](const Settings &s, const Support &c) { return s.water == WaterQuality::High && !(c.scene_snapshot && c.scene_depth); },
     [](Settings &s) {
         s.water = WaterQuality::Medium;
         s.water_refraction = false;
     },
     "high water quality needs a scene snapshot (two colour targets and sampleable depth): lowered to medium"},
    {[](const Settings &s, const Support &) { return s.water_refraction && s.water != WaterQuality::High; },
     [](Settings &s) { s.water_refraction = false; }, "water refraction exists only at high water quality: switched off"},
    {[](const Settings &s, const Support &) {
         return !(s.lighting_flags & kLightingPerPixel) && (s.lighting_flags & (kLightingHemisphere | kLightingSpecular | kLightingTranslucent));
     },
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingHemisphere | kLightingSpecular | kLightingTranslucent); },
     "hemisphere ambient, specular and translucency need per-pixel lighting: switched off"},
    {[](const Settings &s, const Support &) { return s.shadows != ShadowQuality::Off && !(s.lighting_flags & kLightingPerPixel); },
     [](Settings &s) { s.shadows = ShadowQuality::Off; }, "sun shadows need per-pixel lighting: switched off"},
    {[](const Settings &s, const Support &c) { return s.shadows != ShadowQuality::Off && !c.shadow_map; },
     [](Settings &s) { s.shadows = ShadowQuality::Off; }, "sun shadows need a D16 depth render target: switched off"},
    {[](const Settings &s, const Support &) { return (s.lighting_flags & kLightingPoint) && s.point_lights != 4 && s.point_lights != 8; },
     [](Settings &s) { s.point_lights = s.point_lights > 4 ? 8 : 4; }, "point lights per draw call must be 4 or 8: rounded"},
};
} // namespace

std::vector<std::string> ValidateSettings(const Settings &settings, const Support &support)
{
    std::vector<std::string> problems;
    for (const Rule &rule : kRules)
        if (rule.violated(settings, support))
            problems.emplace_back(rule.message);
    return problems;
}

Settings ConstrainSettings(const Settings &requested, const Support &support, std::vector<std::string> &changes)
{
    Settings settings = requested;
    // A fix can expose another violation (lower water quality -> refraction), so run until stable.
    for (bool changed = true; changed;)
    {
        changed = false;
        for (const Rule &rule : kRules)
            if (rule.violated(settings, support))
            {
                rule.fix(settings);
                changes.emplace_back(rule.message);
                changed = true;
            }
    }
    return settings;
}

std::string_view NameRu(QualityPreset preset)
{
    switch (preset)
    {
    case QualityPreset::Medium:
        return "Среднее";
    case QualityPreset::High:
        return "Высокое";
    default:
        return "Оригинал";
    }
}

std::string_view NameRu(GradePreset preset)
{
    switch (preset)
    {
    case GradePreset::Caribbean:
        return "Карибы";
    case GradePreset::Overcast:
        return "Пасмурно";
    default:
        return "Оригинал";
    }
}

} // namespace storm::fx
