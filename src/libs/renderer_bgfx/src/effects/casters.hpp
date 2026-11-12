// How a material is drawn in a pass that renders depth or coverage instead of the material's colour: the sun cascade,
// the lamp atlases (characters and the still location geometry), the AO volume's bake. One rule for all of them:
//   * what the colour pass's alpha test (or alpha to coverage) discards is cut out of the caster too: the product of
//     the alphas of the texture stages the material's alpha chain multiplies, against the draw's own alpha
//     reference (foliage, nets, gratings, rigging; a sail's cloth and its tears). Vertex, material and texture-factor
//     alpha are not part of the cut: a character that fades in or out casts until it is gone;
//   * a blended material that does not write depth (glass, water, particles, flares) is light, not a surface: it
//     casts nothing;
//   * a blended material that writes depth (sails, fading characters) is a surface: it casts like any other.
// The passes that read the scene's own depth (the sea's soft edges, SSAO) need none of this: that depth was written
// by the colour pass itself, with its alpha test. There is no depth prepass.
//
// The caster programs live here as well, so no pass keeps a copy: vs_ff_caster[_cut][_skin] with fs_ff_caster[_cut]
// (the sun cascade's depth) or fs_ff_lamp_caster[_cut] (the lamp atlases' distance).
#pragma once

#include "shim/shim.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace storm::shim
{
struct Device;
struct LayoutInfo;
} // namespace storm::shim

namespace storm::effects
{

struct CasterMaterial
{
    bool casts = true;
    float cut = 0.0f;                // > 0: the alpha reference (0..1) the stages below are tested against
    std::array<float, 3> stage{};    // 1 = the alpha of this stage's texture takes part
    std::array<float, 3> uv{};       // the texture coordinate set of the stage
};

// The material of the draw the device is about to make.
CasterMaterial caster_material(const shim::Device &device, const shim::LayoutInfo &layout);
// The material of the still location geometry drawn with `texture` at stage 0 under the game's default states
// (alpha test on at kGameAlphaReference, no blending: GEOM_SERVICE_R::SetMaterial, Renderer::SetCommonStates).
CasterMaterial location_caster_material(const shim::TexStore *texture);

class Casters
{
  public:
    enum class Pass
    {
        Sun,  // depth of the sun cascade
        Lamp, // distance to a lamp
    };
    // The program of a caster (logs a failure and returns an invalid handle), with its cut uniforms set when the
    // material has one. The material's textures must be bound at their stages (a draw's own still are).
    bgfx::ProgramHandle program(shim::Device &device, Pass pass, bool skinned, const CasterMaterial &material);
    // Creates the programs of `pass` (Lighting::prepare_programs), at most `budget` of them.
    bool prepare_programs(shim::Device &device, Pass pass, int &budget);
    // Stage 0's sampler, for a pass that binds a location texture itself.
    bgfx::UniformHandle sampler();

    void shutdown();
    void on_device_lost();

  private:
    void find_shaders();
    void ensure_uniforms();

    uint32_t vs_[2][2] = {};  // [cut][skinned]
    uint32_t fs_[2][2] = {};  // [pass][cut]
    bgfx::UniformHandle u_cut_ = BGFX_INVALID_HANDLE, u_uv_ = BGFX_INVALID_HANDLE, s_tex0_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::effects
