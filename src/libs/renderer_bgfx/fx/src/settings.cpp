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

    // «Средняя» and «Высокая» keep the legacy palette and exposure under a restrained colour grade; the effects add
    // depth and atmosphere. «Средняя» takes the cheap ones (a tone curve, the grade of the place and hour, aerial
    // perspective, rim light), «Высокая» all of them (+ the sun glare and sun shafts). A faint bloom on the
    // brightest highlights, MSAA, sharper textures, per-pixel lighting.
    s.bloom = true;
    s.bloom_threshold = 0.92f;
    s.bloom_intensity = 0.12f;
    s.msaa = 4; // the primary anti-aliasing; SMAA replaces it where MSAA is unavailable (ConstrainSettings)
    s.alpha_to_coverage = true;
    s.dither = true; // ±0.5/255: invisible, removes banding of the bloom
    s.contact_shadows = true;
    s.tonemap = true;
    // The grade of the place and hour shifts hue and warmth only (color_lut.cpp SceneGrade), no saturation.
    s.scene_grading = true;
    // A gentle Caribbean grade: warm sunlight, clearer turquoise water, lush greens, restrained (a third of the
    // full «Карибы» grade: no visible saturation boost).
    s.grade = GradePreset::Caribbean;
    s.grade_strength = 0.35f;
    // FSR 1: the 3D scene at a lower internal resolution, EASU up to the display, RCAS there; the interfaces stay at
    // the display resolution. MSAA (and SMAA where it replaces it) antialiases the internal picture before EASU.
    s.sharpen = true;
    s.sharpness = 0.75f; // 0.5 stops below the reference's maximum

    if (preset == QualityPreset::Medium)
    {
        s.render_scale = 0.67f; // FSR 1 "Quality": 2144x965 of a 3200x1440 display
        s.bloom_downscale = 4;
        s.bloom_levels = 3;
        s.sea_band_limit = true;
        s.anisotropy = 4;
        s.lighting_flags = kLightingPerPixel | kLightingPoint | kLightingNormalMap | kLightingRim | kLightingAerial;
        s.point_lights = 4;
        s.normal_map_size = 512; // a quarter of High's memory
        s.shadows = ShadowQuality::Low;
        // The AO volume stays off: its shade lies as blotches on the walls instead of falling from objects (owner,
        // APK #10 / #11); option fx_ao_volume 1..3 for debugging while interior shadows are reworked.
        s.ao_volume = 0;
        // Lamp shadows stay off until their darkening of baked surfaces matches the bake (option fx_lamp_shadows).
        s.lamp_shadows = 0;
        return s;
    }
    s.render_scale = 0.77f; // FSR 1 "Ultra Quality": 2464x1109 of a 3200x1440 display
    s.bloom_downscale = 2;
    s.bloom_levels = 4;
    s.anisotropy = 8;
    s.lens_flare = true; // the one sun glare: SUNGLOW leaves its glow, overflow and flare sprites out
    s.sun_shafts = true;
    s.sea_band_limit = true;
    s.sea_soft_edges = true;
    // Wind sway is not in the presets: the game already sways sails, flags and grass itself, and the static
    // location models carry no anchor weights for foliage (option fx_wind, reported as not wired).
    s.lighting_flags = kLightingPerPixel | kLightingSpecular | kLightingTranslucent | kLightingPoint | kLightingNormalMap |
                       kLightingRim | kLightingAerial;
    s.point_lights = 8;
    s.shadows = ShadowQuality::Soft;
    s.ao_volume = 0;    // see Средняя
    s.lamp_shadows = 0; // see Средняя
    s.ssao = true;
    s.ssao_intensity = 0.35f;
    return s;
}

bool Settings::PostIsIdentity() const
{
    const bool graded = grade != GradePreset::Original && grade_strength > 0.0f;
    return !tonemap && !graded && !bloom && !smaa && !sharpen && render_scale >= 1.0f && !sun_shafts && !ssao && !dither && !lens_flare && !scene_grading;
}

std::string Support::Describe() const
{
    const auto word = [](bool ok) { return ok ? "yes" : "NO"; };
    std::string text = "3D colour table: ";
    text += word(lut_3d);
    text += ", shadow map: ";
    text += word(shadow_map);
    text += ", sampleable scene depth: ";
    text += word(scene_depth);
    text += ", EAC RG11 normal maps: ";
    text += word(normal_map);
    text += ", MSAA targets: ";
    text += word(msaa);
    text += ", R8/RG8 textures (SMAA): ";
    text += word(smaa);
    text += ", sampleable depth with MSAA: ";
    text += word(msaa_depth);
    text += ", anisotropic filtering: ";
    text += word(anisotropy);
    text += ", RGBA8 3D textures (AO volume): ";
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
         s.tonemap = s.bloom = s.ssao = s.sun_shafts = s.scene_grading = s.dither = s.lens_flare = s.smaa = s.sharpen = false;
         s.grade = GradePreset::Original;
         s.render_scale = 1.0f;
     },
     "the post-processing chain needs 3D textures: every post effect switched off, the scene at native resolution (no FSR)"},
    {[](const Settings &s, const Support &) { return !(s.render_scale >= 0.5f && s.render_scale <= 1.0f); },
     [](Settings &s) { s.render_scale = s.render_scale > 1.0f ? 1.0f : 0.5f; }, "render scale must be 0.5..1 (the FSR 1 range): clamped"},
    {[](const Settings &s, const Support &) { return s.msaa != 1 && s.msaa != 2 && s.msaa != 4; }, [](Settings &s) { s.msaa = s.msaa > 2 ? 4 : 1; },
     "MSAA sample count must be 1, 2 or 4: rounded"},
    // A requested MSAA (Средняя / Высокая) that the device cannot give (no multisampled targets: caps, a GPU-family
    // profile, or the self-test found the multisampled scene broken) is replaced by SMAA, so these phones keep an
    // anti-aliasing. SMAA is a post effect: without the post chain (3D textures) or its tables (R8/RG8) there is none.
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa && c.lut_3d && c.smaa; },
     [](Settings &s) {
         s.msaa = 1;
         s.smaa = true;
     },
     "MSAA unavailable: SMAA instead"},
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa && !(c.lut_3d && c.smaa); }, [](Settings &s) { s.msaa = 1; },
     "MSAA unavailable and SMAA needs the post-processing chain (3D textures) and R8/RG8 textures: no anti-aliasing"},
    {[](const Settings &s, const Support &c) { return s.smaa && !c.smaa; }, [](Settings &s) { s.smaa = false; },
     "SMAA needs R8 and RG8 textures: switched off"},
    {[](const Settings &s, const Support &) { return s.alpha_to_coverage && s.msaa <= 1; }, [](Settings &s) { s.alpha_to_coverage = false; },
     "alpha to coverage works only with MSAA: switched off"},
    {[](const Settings &s, const Support &c) { return s.anisotropy > 1 && !c.anisotropy; }, [](Settings &s) { s.anisotropy = 1; },
     "anisotropic filtering is not available: switched off"},
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa_depth && s.ssao; }, [](Settings &s) { s.ssao = false; },
     "ambient occlusion cannot read the depth of a multisampled scene on this renderer: switched off (or turn MSAA off)"},
    {[](const Settings &s, const Support &c) { return s.msaa > 1 && !c.msaa_depth && s.sea_soft_edges; },
     [](Settings &s) { s.sea_soft_edges = false; },
     "soft sea edges cannot read the depth of a multisampled scene on this renderer: switched off (or turn MSAA off)"},
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
    {[](const Settings &s, const Support &c) { return s.sea_soft_edges && !c.scene_depth; }, [](Settings &s) { s.sea_soft_edges = false; },
     "soft sea edges need a sampleable scene depth texture: switched off"},
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
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingNormalMap); }, "normal maps need EAC RG11 textures: switched off"},
    {[](const Settings &s, const Support &) {
         return (s.lighting_flags & kLightingNormalMap) && !(s.lighting_flags & (kLightingPerPixel | kLightingPoint));
     },
     [](Settings &s) { s.lighting_flags &= ~static_cast<uint32_t>(kLightingNormalMap); },
     "normal maps need per-pixel lighting or per-pixel point lights: switched off"},
    {[](const Settings &s, const Support &) { return s.ao_volume > kAoVolumeLevels; },
     [](Settings &s) { s.ao_volume = kAoVolumeLevels; }, "ambient occlusion volume strength must be 0..3: lowered to 3"},
    {[](const Settings &s, const Support &c) { return s.ao_volume && !c.ao_volume; }, [](Settings &s) { s.ao_volume = 0; },
     "the ambient occlusion volume of interiors needs RGBA8 3D textures: switched off"},
    {[](const Settings &s, const Support &) {
         return s.ao_volume && !(s.lighting_flags & (kLightingPerPixel | kLightingPoint));
     },
     [](Settings &s) { s.ao_volume = 0; },
     "the ambient occlusion volume of interiors needs per-pixel lighting or per-pixel point lights: switched off"},
    {[](const Settings &s, const Support &) { return s.lamp_shadows != 0 && s.lamp_shadows != 2 && s.lamp_shadows != 4; },
     [](Settings &s) { s.lamp_shadows = s.lamp_shadows > 2 ? 4 : 2; }, "lamp shadows are cast by 2 or 4 lamps: rounded"},
    {[](const Settings &s, const Support &c) { return s.lamp_shadows && !c.shadow_map; }, [](Settings &s) { s.lamp_shadows = 0; },
     "lamp shadows need a D16 depth render target: switched off"},
    {[](const Settings &s, const Support &) { return s.lamp_shadows && !(s.lighting_flags & kLightingPerPixel); },
     [](Settings &s) { s.lamp_shadows = 0; }, "lamp shadows need per-pixel lighting: switched off"},
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
    // A fix can expose another violation (per-pixel lighting off -> its shadows), so run until stable.
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
    state.render_scale = settings.render_scale;
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
    case CapsProfile::Gles30Minimal: // R8/RG8 textures are core GLES 3.0: SMAA stays
        s.msaa = s.msaa_depth = s.anisotropy = false;
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
