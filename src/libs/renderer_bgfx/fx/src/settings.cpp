#include "storm/fx/settings.hpp"
#include "storm/fx/capabilities.hpp"

#include <iterator>

namespace storm::fx
{

Settings Settings::FromPreset(QualityPreset preset)
{
    Settings s;
    if (preset == QualityPreset::Low)
        return s;

    // «Средняя» and «Высокая» are the same game, cleaner: the legacy palette and exposure stay. No tone
    // curve, a gentle Caribbean grade only, no lens flare or sun shafts (all options); a faint bloom on the brightest highlights,
    // MSAA, sharper textures, per-pixel lighting that keeps the legacy brightness (the global ambient stays the
    // flat one of the fixed-function pipeline, lights add in gamma space as before), the game's own sky.
    s.bloom = true;
    s.bloom_threshold = 0.92f;
    s.bloom_intensity = 0.12f;
    s.msaa = 4; // the primary anti-aliasing; FXAA replaces it where MSAA is unavailable (ConstrainSettings)
    s.alpha_to_coverage = true;
    s.dither = true; // ±0.5/255: invisible, removes banding of the bloom
    s.contact_shadows = true;
    // A gentle Caribbean grade: warm sunlight, clearer turquoise water, lush greens, restrained (a third of the
    // full «Карибы» grade: no visible saturation boost).
    s.grade = GradePreset::Caribbean;
    s.grade_strength = 0.35f;

    if (preset == QualityPreset::Medium)
    {
        s.bloom_downscale = 4;
        s.bloom_levels = 3;
        s.water = WaterQuality::Medium;
        s.anisotropy = 4;
        s.lighting_flags = kLightingPerPixel | kLightingPoint | kLightingNormalMap;
        s.point_lights = 4;
        s.normal_map_strength = 0.6f;
        s.shadows = ShadowQuality::Low;
        s.ao_volume = 2;
        return s;
    }
    s.bloom_downscale = 2;
    s.bloom_levels = 4;
    s.anisotropy = 8;
    // No sharpening by default: CAS turns the sub-pixel detail of distant foliage into a visible mosaic even at
    // low strength; it stays an option.
    s.water = WaterQuality::High;
    s.water_refraction = true;
    // Wind sway is not in the presets: the game already sways sails, flags and grass itself, and the static
    // location models carry no anchor weights for foliage (option fx_wind, reported as not wired).
    s.lighting_flags = kLightingPerPixel | kLightingSpecular | kLightingTranslucent | kLightingPoint | kLightingNormalMap;
    s.point_lights = 8;
    s.shadows = ShadowQuality::Soft;
    s.ao_volume = 2;
    s.ssao = true;
    s.ssao_intensity = 0.35f;
    return s;
}

bool Settings::PostIsIdentity() const
{
    const bool graded = grade != GradePreset::Original && grade_strength > 0.0f;
    return !tonemap && !graded && !bloom && !fxaa && !sharpen && !sun_shafts && !ssao && !dither && !lens_flare && !scene_grading;
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
    // Water::SubmitField (the GPU-displaced grid) is used by tools/fx-preview only; the game's SEA displaces its
    // mesh on the CPU, so the capability is reported, not used.
    text += ", wave field in the vertex shader (device-capable, not used by SEA): ";
    text += word(wave_field);
    text += ", sampleable scene depth: ";
    text += word(scene_depth);
    text += ", RG8 normal maps: ";
    text += word(normal_map);
    text += ", MSAA targets: ";
    text += word(msaa);
    text += ", sampleable depth with MSAA: ";
    text += word(msaa_depth);
    text += ", anisotropic filtering: ";
    text += word(anisotropy);
    text += ", R8 3D textures (AO volume): ";
    text += word(ao_volume);
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
    {[](const Settings &s, const Support &c) { return !c.lut_3d && !s.PostIsIdentity(); },
     [](Settings &s) {
         s.tonemap = s.bloom = s.ssao = s.sun_shafts = s.scene_grading = s.dither = s.lens_flare = s.fxaa = s.sharpen = false;
         s.grade = GradePreset::Original;
     },
     "the post-processing chain needs 3D textures: every post effect switched off"},
    {[](const Settings &s, const Support &) { return s.msaa != 1 && s.msaa != 2 && s.msaa != 4; }, [](Settings &s) { s.msaa = s.msaa > 2 ? 4 : 1; },
     "MSAA sample count must be 1, 2 or 4: rounded"},
    // A requested MSAA (Средняя / Высокая) that the device cannot give (no multisampled targets: caps, a GPU-family
    // profile, or the self-test found the multisampled scene broken) is replaced by FXAA, so these phones keep an
    // anti-aliasing. FXAA is a post effect: without the post chain (3D textures) there is none to give.
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa && c.lut_3d; },
     [](Settings &s) {
         s.msaa = 1;
         s.fxaa = true;
     },
     "MSAA unavailable: FXAA instead"},
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa && !c.lut_3d; }, [](Settings &s) { s.msaa = 1; },
     "MSAA unavailable and FXAA needs the post-processing chain (3D textures): no anti-aliasing"},
    {[](const Settings &s, const Support &) { return s.alpha_to_coverage && s.msaa <= 1; }, [](Settings &s) { s.alpha_to_coverage = false; },
     "alpha to coverage works only with MSAA: switched off"},
    {[](const Settings &s, const Support &c) { return s.anisotropy > 1 && !c.anisotropy; }, [](Settings &s) { s.anisotropy = 1; },
     "anisotropic filtering is not available: switched off"},
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa_depth && s.ssao; }, [](Settings &s) { s.ssao = false; },
     "ambient occlusion cannot read the depth of a multisampled scene on this renderer: switched off (or turn MSAA off)"},
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa_depth && s.water == WaterQuality::High; },
     [](Settings &s) {
         s.water = WaterQuality::Medium;
         s.water_refraction = false;
     },
     "high water quality cannot read the depth of a multisampled scene on this renderer: lowered to medium (or turn MSAA off)"},
    {[](const Settings &s, const Support &) { return (s.lighting_flags & kLightingRim) && !(s.lighting_flags & kLightingPerPixel); },
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingRim); }, "rim light needs per-pixel lighting: switched off"},
    {[](const Settings &s, const Support &) {
         return (s.lighting_flags & kLightingLinear) && !(s.lighting_flags & (kLightingPerPixel | kLightingPoint));
     },
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingLinear); },
     "linear-light lighting needs per-pixel lighting or per-pixel point lights: switched off"},
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
    {[](const Settings &s, const Support &c) { return (s.lighting_flags & kLightingNormalMap) && !c.normal_map; },
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingNormalMap); }, "normal maps need RG8 textures: switched off"},
    {[](const Settings &s, const Support &) {
         return (s.lighting_flags & kLightingNormalMap) && !(s.lighting_flags & (kLightingPerPixel | kLightingPoint));
     },
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingNormalMap); },
     "normal maps need per-pixel lighting or per-pixel point lights: switched off"},
    {[](const Settings &s, const Support &) { return s.ao_volume > kAoVolumeLevels; },
     [](Settings &s) { s.ao_volume = kAoVolumeLevels; }, "ambient occlusion volume strength must be 0..3: lowered to 3"},
    {[](const Settings &s, const Support &c) { return s.ao_volume && !c.ao_volume; }, [](Settings &s) { s.ao_volume = 0; },
     "the ambient occlusion volume of interiors needs R8 3D textures: switched off"},
    {[](const Settings &s, const Support &) {
         return s.ao_volume && !(s.lighting_flags & (kLightingPerPixel | kLightingPoint));
     },
     [](Settings &s) { s.ao_volume = 0; },
     "the ambient occlusion volume of interiors needs per-pixel lighting or per-pixel point lights: switched off"},
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

RendererState RendererStateFor(const Settings &settings)
{
    RendererState state;
    state.msaa_samples = settings.msaa;
    state.alpha_to_coverage = settings.alpha_to_coverage && settings.msaa > 1;
    state.max_anisotropy = settings.anisotropy;
    return state;
}

std::string_view NameRu(QualityPreset preset)
{
    switch (preset)
    {
    case QualityPreset::Medium:
        return "Средняя";
    case QualityPreset::High:
        return "Высокая";
    default:
        return "Низкая";
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

Support RestrictSupport(const Support &device, CapsProfile profile)
{
    Support s = device;
    switch (profile)
    {
    case CapsProfile::Gles30Minimal:
        s.msaa = s.msaa_depth = s.anisotropy = s.wave_field = false;
        break;
    case CapsProfile::MaliG5x:
    case CapsProfile::Adreno6xx:
        s.msaa_depth = false;
        break;
    case CapsProfile::PowerVR:
        s.msaa = s.msaa_depth = s.anisotropy = false;
        break;
    case CapsProfile::Device:
    case CapsProfile::MaliG7x:
    case CapsProfile::Adreno7xx8xx:
    case CapsProfile::Xclipse:
    case CapsProfile::Count:
        break;
    }
    return s;
}

const char *CapsProfileName(CapsProfile profile)
{
    static const char *const names[] = {"device", "gles30-minimal", "mali-g5x", "mali-g7x", "adreno-6xx",
                                        "adreno-7xx-8xx", "powervr", "xclipse"};
    const auto index = static_cast<size_t>(profile);
    return index < std::size(names) ? names[index] : "?";
}

} // namespace storm::fx
