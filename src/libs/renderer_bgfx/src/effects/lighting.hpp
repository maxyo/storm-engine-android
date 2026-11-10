// Per-pixel lighting, point / spot lights and alpha to coverage for the fixed-function emulator (fx_lighting.sh,
// fx_coverage.sh). The shim asks prepare() which program variant a fixed-function draw uses and which of its
// vertex-lighting terms move to the fragment shader, then calls submit() before bgfx::submit.
//
// What the shim sees and what it decides (documented in FX.md, "Lighting"):
//   * lit draw (D3DRS_LIGHTING on, vertex normals, not pre-transformed): vs_ff_lit + fs_ff_lit*. The first enabled
//     directional light is the sun (per pixel with kLightingPerPixel), the global ambient becomes the hemisphere
//     (kLightingHemisphere), and the strongest point / spot lights up to the budget (4 / 8) are evaluated per
//     pixel (kLightingPoint); the rest stay in the vertex sum, so no light is lost.
//   * baked draw (lighting off, vertex normals): the location's static models with baked colours. They get only
//     the dynamic lights the engine reports (SetEffectsDynamicLights: carried lanterns), fs_ff_baked*; without
//     any the legacy program draws them.
//   * material: from the technique the renderer executes (SetEffectsTechnique): Animation* = character,
//     ShipSail* / ShipFlag = sail (two-sided, translucent), DLightModel = stone; lit draws without a technique
//     are the ship hulls, masts and deck objects = hull.
//   * relief (kLightingNormalMap): a lit or baked draw whose stage 1 holds a height texture with a derived normal map
//     (effects/normal_maps.hpp) uses the FX_LIGHT_NORMALMAP variant with the map at stage 14 and stage 1's sampler
//     flags; lit draws perturb the normal, baked ones scale their colour by fxBakedRelief. Characters have none.
//   * alpha to coverage (RendererState::alpha_to_coverage): alpha-tested (GREATER / GREATEREQUAL), non-blended draws
//     into a multisampled target: BGFX_STATE_BLEND_ALPHA_TO_COVERAGE and fxCoverageAlpha() instead of the discard.
// Cube-mapped draws (fs_ff_cube*) have no lighting or coverage variant: they keep the legacy path, reported once.
#pragma once

#include "effects/normal_maps.hpp"
#include "storm/fx/lighting.hpp"
#include "storm/fx/settings.hpp"
#include "storm/fx/sky_model.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace storm::shim
{
struct Device;
struct LayoutInfo;
} // namespace storm::shim

namespace storm::effects
{

// What one fixed-function draw does differently under the effects.
struct FfDraw
{
    bool active = false;      // a variant below replaces the legacy program
    uint32_t vs_id = 0, fs_id = 0;
    uint8_t vertex_diffuse_off = 0;  // enabled-light slots (order of apply_ff_uniforms) lit per pixel: no diffuse
    uint8_t vertex_specular_off = 0; // ... and no specular in the vertex sum
    bool vertex_ambient_off = false; // the global ambient is the per-pixel hemisphere
    uint64_t state = 0;              // extra bgfx state bits (alpha to coverage)
    // fragment uniforms, set by submit()
    bool lit = false; // FX_FF_LIT / FX_FF_BAKED program: u_fxLight, u_fxPoint, u_fxFf
    fx::LightingUniforms light{};
    fx::PointLightUniforms points{};
    std::array<float, 4> ff{};
    std::array<float, 4> coverage{};
    bgfx::TextureHandle normal_map = BGFX_INVALID_HANDLE; // FX_LIGHT_NORMALMAP variant: bound at stage 14
    uint32_t normal_map_flags = 0;
};

class Lighting
{
  public:
    void configure(const fx::Settings &settings, const fx::RendererState &state);
    // Nothing to decide per draw (preset «Оригинал»): the shim skips prepare().
    bool idle() const { return !lighting_ && !coverage_; }

    // Fixed-function draw (no shader ports). `cube_variant` = the fs_ff_cube* variant the shim picked (0 = none).
    void prepare(const shim::Device &device, const shim::LayoutInfo &layout, uint32_t cube_variant, FfDraw &draw);
    void submit(const FfDraw &draw);

    void set_technique(const char *name);
    // The frame's dynamic point lights (not in any baked colours); kept until a frame passes without them.
    void set_dynamic_lights(std::span<const fx::PointLight> lights)
    {
        dynamic_lights_.assign(lights.begin(), lights.end());
        dynamic_lights_this_frame_ = true;
    }
    void set_sky(const fx::SkyState *sky) { sky_ = sky; }
    NormalMaps &normal_maps() { return normal_maps_; }
    void end_frame();
    // Draws of the last presented frame that used a lit / baked / coverage variant (effects profile).
    struct Counts
    {
        uint32_t lit = 0, baked = 0, coverage = 0, relief = 0;
    };
    Counts last_frame() const { return last_frame_; }

    void shutdown();
    void on_device_lost();

  private:
    enum class Material : uint8_t
    {
        Hull,
        Stone,
        Sail,
        Character,
    };

    fx::Settings settings_{};
    uint32_t flags_ = 0;
    bool lighting_ = false; // per-pixel sun or point lights
    bool coverage_ = false;
    int budget_ = 0;
    Material material_ = Material::Hull;
    bool technique_set_ = false;
    std::vector<fx::PointLight> dynamic_lights_;
    bool dynamic_lights_this_frame_ = false;
    const fx::SkyState *sky_ = nullptr;
    uint32_t vs_ff_ = 0, vs_lit_ = 0, fs_coverage_ = 0;
    uint32_t fs_lit_[2][2][2] = {}; // [specular][normal map][linear]
    uint32_t fs_baked_[2][2] = {};  // [normal map][linear]
    NormalMaps normal_maps_;
    bool cube_reported_ = false;
    fx::Lighting fx_;
    bgfx::UniformHandle u_ff_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_coverage_ = BGFX_INVALID_HANDLE;
    Counts frame_{}, last_frame_{};
};

} // namespace storm::effects
