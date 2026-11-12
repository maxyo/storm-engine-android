// Runtime switches of the graphics effects package.
//
// Every field may be changed between frames; the passes read the struct each
// frame and (re)create GPU resources lazily. `QualityPreset::Low` turns
// every effect off: the post chain degenerates to a plain copy and the legacy
// sea / sky / fixed-function lighting paths are used unchanged.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace storm::fx
{

// Sampler stages fx binds into the fixed-function emulator's programs (FX_LIGHT_SHADOW, FX_LIGHT_NORMALMAP).
// They are above the emulator's own units (stages 0..4, cube maps 5..7 and 11, volume textures 8..10 and 12),
// so every combination fits in the 16 units of GLES 3.0.
inline constexpr uint8_t kShadowMapStage = 15; // 13 is the renderer's bone palette (vertex shader)
inline constexpr uint8_t kAoVolumeStage = 12;  // the interior's AO volume (R8 3D) in the lit / baked FF variants
inline constexpr uint8_t kNormalMapStage = 14;

enum class QualityPreset : uint8_t
{
    Low,    // «Низкая»: the legacy look, no effect runs (parity with the original renderer); weak phones
    Medium, // «Средняя»: Mali-G52 / Adreno 610 class at 30 fps
    High,   // «Высокая»: Adreno 640+ / Mali-G76+ class
};

// Colour grade, independent of the quality preset.
enum class GradePreset : uint8_t
{
    Original,  // «Оригинал»: identity look-up table
    Caribbean, // «Карибы»: warm, saturated
    Overcast,  // «Пасмурно»: cool, low saturation, softer contrast
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
    kLightingRim = 1u << 6,         // FX_LIGHT_RIM: rim light on characters and ships seen against the sun
    kLightingLinear = 1u << 7,      // FX_LIGHT_LINEAR: lights are added in linear light instead of gamma space
    kLightingAerial = 1u << 8,      // FX_AERIAL: aerial perspective and height fog instead of the plain fog
    kLightingWind = 1u << 9,        // FX_WIND: wind sway in the vertex shader (foliage, grass, sails)
    kLightingAll = 0x3ff,
};

enum class ShadowQuality : uint8_t
{
    Off,
    Low,  // 1024² map, one hardware-filtered tap
    Soft, // 2048² map, four taps
};

// Strengths of the interior ambient-occlusion volume (Settings::ao_volume, option fx_ao_volume), for the owner's
// choice of the interior contrast. The shader darkens by mix(1, pow(ao, contrast), strength), where ao is the baked
// visibility relative to an open floor or wall (1 there, lower under tables, behind barrels, in corners).
inline constexpr uint8_t kAoVolumeLevels = 3;
struct AoVolumeLevel
{
    const char *name;
    float strength;
    float contrast;
};
inline constexpr AoVolumeLevel kAoVolumeLevel[kAoVolumeLevels + 1] = {
    {"off", 0.0f, 1.0f},
    {"as the game's data", 0.6f, 1.0f},
    {"moderate contrast", 0.85f, 1.4f},
    {"strong stylised contrast", 1.0f, 2.2f},
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
    bool dither = false;          // +-0.5/255 ordered noise on the sky and the final image: no banding in gradients
    bool lens_flare = false;      // procedural sun glare and flare ghosts (switch the legacy flare sprites off)
    float lens_flare_intensity = 0.5f;
    bool scene_grading = false;   // PostChain::Frame::scene_grade (mood of the place and hour) is applied
    bool ssao = false;            // high end only; needs the scene depth texture
    float ssao_radius = 0.6f;     // metres
    float ssao_intensity = 0.8f;

    // ---- renderer state (the renderer applies these; see RendererState()) ----
    uint8_t msaa = 1;             // samples of the scene target: 1 (off), 2 or 4. The primary anti-aliasing on tile GPUs
    bool alpha_to_coverage = false; // alpha-tested geometry (grass, foliage, rigging) gets smooth edges; needs msaa > 1
    uint8_t anisotropy = 1;       // 1 (off), 4 or 8: anisotropic filtering of world textures

    // ---- world passes ----
    bool contact_shadows = false; // soft blobs under characters and objects (ContactShadows)
    // The game's own sea (the ported Sea2 technique, shaders/ports/fs_port_sea2.sc) with improvements on top; the
    // reflected ray is clamped to the horizon always, these are optional:
    bool sea_band_limit = false;  // the bump detail fades with the pixel footprint: no shimmer towards the horizon
    bool sea_soft_edges = false;  // the sea fades out where shallow in front of the scene (needs the scene depth)
    bool sea_specular = false;    // a clean sun highlight from the bumped normal over the game's own sun road
    float sea_edge_depth = 1.5f;  // metres of water over which a soft edge fades in
    float sea_specular_power = 400.0f;
    float sea_specular_strength = 0.6f;
    bool sky_atmosphere = false;   // analytic sky instead of the textured sky box
    bool sky_clouds = false;
    uint32_t lighting_flags = kLightingNone;
    uint8_t point_lights = 4;      // per draw call with kLightingPoint: 4 or 8
    float normal_map_strength = 1.0f; // with kLightingNormalMap: scales every material's relief (0..1)
    ShadowQuality shadows = ShadowQuality::Off; // sun shadows on per-pixel lit surfaces (needs kLightingPerPixel)
    // Ambient-occlusion volume of interiors (ao_volume.hpp): 0 off, else one of the strengths AoVolumeLevel names.
    uint8_t ao_volume = 0;

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
    bool lut_3d = false;        // 3D RGBA8 texture: the grading table every post effect passes through
    bool shadow_map = false;    // D16 texture as a render target: Settings::shadows
    bool scene_depth = false;   // the scene's depth attachment can be sampled: Settings::ssao, Settings::sea_soft_edges
    bool normal_map = false;    // RG8 textures: kLightingNormalMap
    bool msaa = false;          // multisampled RGBA8 render targets: Settings::msaa
    bool msaa_depth = false;    // the scene depth can still be sampled when the scene target is multisampled
                                // (renderer knowledge, like scene_depth): msaa together with ssao / sea soft edges
    bool anisotropy = false;    // anisotropic texture filtering
    bool ao_volume = false;     // R8 3D textures with linear filtering: Settings::ao_volume

    std::string Describe() const;
};

// Inconsistent or unsupported requests in `settings`, one message each; empty = every effect will run exactly
// as asked. PostChain, Shadows and the sea's soft edges refuse (and log) what this reports.
std::vector<std::string> ValidateSettings(const Settings &settings, const Support &support);

// The explicit, one-time alternative to a silent fallback: returns `requested` with every option that
// ValidateSettings() objects to switched off or lowered, and appends one line per change to `changes`.
// Call it when settings are loaded or changed, log the lines, and store / display the result.
Settings ConstrainSettings(const Settings &requested, const Support &support, std::vector<std::string> &changes);

// What the renderer has to set up for these settings; fx cannot do it itself because the scene target and
// the samplers of the game's textures belong to the renderer.
struct RendererState
{
    uint8_t msaa_samples = 1;         // scene colour + depth target; resolve before PostChain reads it
    bool alpha_to_coverage = false;   // add BGFX_STATE_BLEND_ALPHA_TO_COVERAGE to alpha-tested draw calls and use
                                      // fxCoverageAlpha() (fx_coverage.sh) instead of the alpha test
    uint8_t max_anisotropy = 1;       // > 1: BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC on mip-mapped world
                                      // textures and BGFX_RESET_MAXANISOTROPY; cap the level at this value
};
RendererState RendererStateFor(const Settings &settings);

// Names for the settings screen (UTF-8, Russian).
std::string_view NameRu(QualityPreset preset);
std::string_view NameRu(GradePreset preset);

} // namespace storm::fx
