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
//   * sun shadows (Settings::shadows, effects/shadows.hpp): lit draws into the game picture cast (the shim submits
//     them again into the shadow view: FfDraw::caster) and receive (FX_LIGHT_SHADOW[_SOFT] variants).
//   * alpha to coverage (RendererState::alpha_to_coverage): alpha-tested (GREATER / GREATEREQUAL), non-blended draws
//     into a multisampled target: BGFX_STATE_BLEND_ALPHA_TO_COVERAGE and fxCoverageAlpha() instead of the discard.
//   * aerial perspective (kLightingAerial): every draw through the fixed-function vertex pipeline whose fog is the
//     game's exp fog takes fog mode 5 (ff_finish.sh, fx_aerial.sh fxAerialFog): the same fog colour and density with
//     height haze and desaturation, for lit, baked and unlit draws and ported pixel shaders alike. Draws with a ported
//     vertex shader keep the fixed-function fog (reported once); without weather (interiors) the game's fog stays.
// Cube-mapped draws (fs_ff_cube*) have no lighting or coverage variant: they keep the legacy path, reported once.
#pragma once

#include "effects/ao_volume.hpp"
#include "effects/normal_maps.hpp"
#include "effects/shadows.hpp"
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
// World-space bounding sphere of the vertices a draw covers (shim::Device::draw_bounds): the lights are chosen
// for it, so the choice depends on where the geometry is, never on the camera.
struct DrawSphere
{
    fx::Vec3 center{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    bool valid = false;
};

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
    bool shadow = false;           // FX_LIGHT_SHADOW variant: submit() binds the map for `view`
    bool caster = false;           // the shim also submits this draw into the shadow view (submit_caster)
    std::array<float, 16> view{};  // the draw's world -> view (shadow and AO-volume receivers)
    bool ao = false;               // samples the interior's AO volume (submit() binds it for `view`)
};

class Lighting
{
  public:
    void configure(const fx::Settings &settings, const fx::RendererState &state);
    // Creates the fixed-function programs the draws of `settings` can pick (lit / baked variants with and without
    // relief and shadow, their skinned twins, coverage, shadow casters), at most `budget` of them (reduced by the
    // number created; each is a shader link on the GPU driver). True once all exist. A settings switch calls it
    // while the previous settings still draw (Host::advance_switch).
    bool prepare_programs(shim::Device &device, const fx::Settings &settings, int &budget);
    // Nothing to decide per draw (preset «Низкая»): the shim skips prepare().
    bool idle() const { return !lighting_ && !coverage_; }

    // Fixed-function draw (no shader ports). `cube_variant` = the fs_ff_cube* variant the shim picked (0 = none).
    void prepare(const shim::Device &device, const shim::LayoutInfo &layout, uint32_t cube_variant, const DrawSphere &sphere,
                 FfDraw &draw);
    void submit(const FfDraw &draw);
    // Fog of a draw with fog enabled in D3DFOG_EXP mode (the game's): true when it takes the aerial perspective (fog
    // mode 5); u_fxAerialUp is then set for that draw and `haze` goes where fog mode 5 reads it (the fog start and end
    // slots of u_ffFs, unused by exp fog). `view`: the draw's world -> view.
    bool aerial_fog(const D3DMATRIX &view, bool vs_port, std::array<float, 2> &haze);

    void set_technique(const char *name);
    // The frame's dynamic point lights (not in any baked colours); kept until a frame passes without them.
    void set_dynamic_lights(std::span<const fx::PointLight> lights)
    {
        dynamic_lights_.assign(lights.begin(), lights.end());
        dynamic_lights_this_frame_ = true;
    }
    void set_sky(const fx::SkyState *sky) { sky_ = sky; }
    // The interior's AO volume for this frame's draws into the game picture (null: none).
    void set_ao_volume(const AoVolumes::Bound *volume) { ao_volume_ = volume; }
    NormalMaps &normal_maps() { return normal_maps_; }
    SunShadows &shadows() { return shadows_; }
    // After the draw was submitted with its buffers kept (BGFX_DISCARD_NONE for buffers): the depth-only copy.
    void submit_caster(shim::Device &device, const D3DMATRIX &world, bool skinned)
    {
        shadows_.submit_caster(device, &world.m[0][0], skinned);
        ++frame_.casters;
    }
    void end_frame();
    // Draws of the last presented frame that used a lit / baked / coverage variant (effects profile).
    struct Counts
    {
        uint32_t lit = 0, baked = 0, coverage = 0, relief = 0, shadowed = 0, casters = 0, aerial = 0, ao = 0;
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
    void find_shaders();

    uint32_t vs_ff_ = 0, vs_lit_ = 0, vs_lit_skin_ = 0, fs_coverage_ = 0;
    uint32_t fs_lit_[2][2][2][3] = {}; // [specular][normal map][linear][shadow: none, one tap, soft]
    uint32_t fs_baked_[2][2] = {};  // [normal map][linear]
    NormalMaps normal_maps_;
    SunShadows shadows_;
    bool cube_reported_ = false;
    bool bounds_reported_ = false;
    bool aerial_port_reported_ = false;
    bgfx::UniformHandle u_aerial_up_ = BGFX_INVALID_HANDLE;
    fx::Lighting fx_;
    bgfx::UniformHandle u_ff_ = BGFX_INVALID_HANDLE;
    // AO volume (ff_fragment.sh ffAoVolume): sampler stage kAoVolumeStage, view -> volume uvw, parameters
    void bind_ao_volume(const FfDraw &draw);
    const AoVolumes::Bound *ao_volume_ = nullptr;
    bgfx::UniformHandle s_ao_volume_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ao_matrix_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ao_params_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ao_open_ = BGFX_INVALID_HANDLE; // 1x1x1 R8 = 255 for draws without the volume
    std::array<float, 16> ao_view_{};     // the camera ao_view_to_uvw_ was made for
    std::array<float, 16> ao_view_to_uvw_{};
    bgfx::UniformHandle u_coverage_ = BGFX_INVALID_HANDLE;
    Counts frame_{}, last_frame_{};
};

} // namespace storm::effects
