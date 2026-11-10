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
        s.lighting_flags = kLightingPerPixel | kLightingHemisphere;
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
    return s;
}

bool Settings::PostIsIdentity() const
{
    const bool graded = grade != GradePreset::Original && grade_strength > 0.0f;
    return !tonemap && !graded && !bloom && !fxaa && !sharpen && !sun_shafts;
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
