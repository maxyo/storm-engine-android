// Runtime switches of the graphics effects package.
//
// Every field may be changed between frames; the passes read the struct each
// frame and (re)create GPU resources lazily. `QualityPreset::Original` turns
// every effect off: the post chain degenerates to a plain copy and the legacy
// sea / sky / fixed-function lighting paths must be used unchanged.
#pragma once

#include <cstdint>
#include <string_view>

namespace storm::fx
{

enum class QualityPreset : uint8_t
{
    Original, // «Оригинал»: parity with the legacy renderer
    Medium,   // «Среднее»: Mali-G52 / Adreno 610 class at 30 fps
    High,     // «Высокое»: Adreno 640+ / Mali-G76+ class
};

// Colour grade, independent of the quality preset.
enum class GradePreset : uint8_t
{
    Original,  // «Оригинал»: identity look-up table
    Caribbean, // «Карибы»: warm, saturated
    Overcast,  // «Пасмурно»: cool, low saturation, softer contrast
};

enum class WaterQuality : uint8_t
{
    Legacy, // fx::Water is not used; the ported Sea2/Sea3 techniques draw the sea
    Low,    // analytic sky reflection, two detail normals, sun specular
    Medium, // + crest foam, reflected clouds / environment cube
    High,   // + scene depth: depth colour, shore foam, soft edges, refraction
};

// Bits understood by shaders/include/fx_lighting.sh (see lighting.hpp).
enum LightingFlags : uint32_t
{
    kLightingNone = 0,
    kLightingPerPixel = 1u << 0,   // FX_LIGHT_PER_PIXEL: sun N·L evaluated per pixel
    kLightingHemisphere = 1u << 1, // FX_LIGHT_HEMISPHERE: sky/ground ambient instead of a flat one
    kLightingSpecular = 1u << 2,   // FX_LIGHT_SPECULAR: Blinn-Phong highlight
    kLightingTranslucent = 1u << 3, // FX_LIGHT_TRANSLUCENT: back-lit thin surfaces (sails, flags)
    kLightingAll = 0xf,
};

struct Settings
{
    // ---- post chain ----
    bool tonemap = false;       // filmic (ACES fit) tone curve
    float exposure = 1.0f;      // linear multiplier before the tone curve
    float tonemap_strength = 0.6f; // 0 = untouched .. 1 = full filmic contrast (crushes the darkest shadows)
    GradePreset grade = GradePreset::Original;
    float grade_strength = 1.0f; // 0 = identity .. 1 = full preset
    bool bloom = false;
    uint8_t bloom_downscale = 4;  // 2 = bloom chain starts at half, 4 = at quarter resolution
    uint8_t bloom_levels = 4;     // dual-filter down/up steps (1..6)
    float bloom_threshold = 0.85f; // linear value of the brightest channel above which a pixel glows
    float bloom_knee = 0.12f;     // glow fades in from threshold * (1 - knee) and is full at white
    float bloom_intensity = 0.35f;
    bool fxaa = false;
    bool sharpen = false;         // contrast adaptive sharpening, also the upscale filter
    float sharpness = 0.5f;       // 0..1
    bool sun_shafts = false;      // high end only
    float sun_shafts_intensity = 0.35f;

    // ---- world passes ----
    WaterQuality water = WaterQuality::Legacy;
    bool water_refraction = false; // needs a copy of the opaque scene colour
    bool sky_atmosphere = false;   // analytic sky instead of the textured sky box
    bool sky_clouds = false;
    uint32_t lighting_flags = kLightingNone;

    static Settings FromPreset(QualityPreset preset);

    // True when the post chain would not change a single pixel (given equal
    // input and output size). Callers may then skip PostChain entirely.
    bool PostIsIdentity() const;

    bool operator==(const Settings &) const = default;
};

// Names for the settings screen (UTF-8, Russian).
std::string_view NameRu(QualityPreset preset);
std::string_view NameRu(GradePreset preset);

} // namespace storm::fx
