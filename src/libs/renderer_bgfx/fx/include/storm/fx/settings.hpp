// Runtime switches of the graphics effects package.
//
// Every field may be changed between frames; the passes read the struct each
// frame and (re)create GPU resources lazily. `QualityPreset::Original` turns
// every effect off: the post chain degenerates to a plain copy and the legacy
// sea / sky / fixed-function lighting paths must be used unchanged.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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
    kLightingPoint = 1u << 4,       // FX_LIGHT_POINT: point and spot lights evaluated per pixel
    kLightingNormalMap = 1u << 5,   // FX_LIGHT_NORMALMAP: relief from normal maps derived from the game's bump textures
    kLightingAll = 0x3f,
};

enum class ShadowQuality : uint8_t
{
    Off,
    Low,  // 1024² map, one hardware-filtered tap
    Soft, // 2048² map, four taps
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
    bool ssao = false;            // high end only; needs the scene depth texture
    float ssao_radius = 0.6f;     // metres
    float ssao_intensity = 0.8f;

    // ---- world passes ----
    WaterQuality water = WaterQuality::Legacy;
    bool water_refraction = false; // needs a copy of the opaque scene colour
    bool sky_atmosphere = false;   // analytic sky instead of the textured sky box
    bool sky_clouds = false;
    uint32_t lighting_flags = kLightingNone;
    uint8_t point_lights = 4;      // per draw call with kLightingPoint: 4 or 8
    float normal_map_strength = 1.0f; // with kLightingNormalMap: scales every material's relief (0..1)
    ShadowQuality shadows = ShadowQuality::Off; // sun shadows on per-pixel lit surfaces (needs kLightingPerPixel)

    static Settings FromPreset(QualityPreset preset);

    // True when the post chain would not change a single pixel (given equal
    // input and output size). Callers may then skip PostChain entirely.
    bool PostIsIdentity() const;

    bool operator==(const Settings &) const = default;
};

// What the device and the renderer can do, decided once at start-up (fx::DetectSupport() in capabilities.hpp
// fills the GPU part; the renderer fills `scene_depth`). Show it on the settings screen: an option whose
// requirement is missing must be disabled there, not quietly ignored.
struct Support
{
    bool lut_3d = false;        // 3D RGBA8 texture: colour grade (any post effect but FXAA / sharpening needs it)
    bool shadow_map = false;    // D16 texture as a render target: Settings::shadows
    bool scene_snapshot = false; // two colour attachments: WaterQuality::High
    bool wave_field = false;    // RGBA16F texture readable in the vertex shader: Water::SubmitField
    bool scene_depth = false;   // the scene's depth attachment can be sampled: Settings::ssao, WaterQuality::High
    bool normal_map = false;    // RG8 textures: kLightingNormalMap

    std::string Describe() const;
};

// Inconsistent or unsupported requests in `settings`, one message each; empty = every effect will run exactly
// as asked. PostChain, Water and Shadows refuse (and log) what this reports.
std::vector<std::string> ValidateSettings(const Settings &settings, const Support &support);

// The explicit, one-time alternative to a silent fallback: returns `requested` with every option that
// ValidateSettings() objects to switched off or lowered, and appends one line per change to `changes`.
// Call it when settings are loaded or changed, log the lines, and store / display the result.
Settings ConstrainSettings(const Settings &requested, const Support &support, std::vector<std::string> &changes);

// Names for the settings screen (UTF-8, Russian).
std::string_view NameRu(QualityPreset preset);
std::string_view NameRu(GradePreset preset);

} // namespace storm::fx
