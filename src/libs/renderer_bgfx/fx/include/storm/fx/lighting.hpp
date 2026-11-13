// bgfx side of the per-pixel lighting upgrade: owns the u_fxLight and u_fxPoint
// uniforms that shaders/include/fx_lighting.sh declares. The fixed-function
// emulator compiles its shader variants with LightingDefines(settings) and
// calls Apply() before submitting a lit draw call.
#pragma once

#include "storm/fx/light_set.hpp"
#include "storm/fx/lighting_model.hpp"
#include "storm/fx/normal_map.hpp"

#include <bgfx/bgfx.h>

#include <array>

namespace storm::fx
{

class Lighting
{
  public:
    Lighting() = default;
    ~Lighting();
    Lighting(const Lighting &) = delete;
    Lighting &operator=(const Lighting &) = delete;

    // Sets u_fxLight for the next draw call.
    void Apply(const LightingUniforms &uniforms);
    // Sets u_fxPoint / u_fxPointCount for the next draw call of a FX_LIGHT_POINT variant.
    void Apply(const PointLightUniforms &uniforms);

    // FX_LIGHT_NORMALMAP variants. CreateNormalMap() uploads a derived map (RG8 or EAC RG11, NormalMap::format, with
    // mips; the caller owns and destroys the texture). BindNormalMap() binds it to sampler stage kNormalMapStage for the
    // next draw call with the sampler flags of the map's texture stage (wrap mode, anisotropy). A draw call without a
    // map must use a variant without FX_LIGHT_NORMALMAP.
    bgfx::TextureHandle CreateNormalMap(const NormalMap &map);
    void BindNormalMap(bgfx::TextureHandle normal_map, uint32_t sampler_flags = BGFX_SAMPLER_NONE);
    // The material texture (EAC RG11 with mips, uploaded by CreateNormalMap: R the relief's height, G the gloss) on
    // sampler stage kMaterialStage for the next draw call: the FX_LIGHT_SPECULAR variants read its gloss (u_fxGloss.x
    // says whether one is bound), the FX_LIGHT_PARALLAX variants its height with SetParallax's u_fxParallax[2]
    // (fx_lighting.sh fxParallax, fxParallaxShadow, fxReliefCavity).
    void BindMaterial(bgfx::TextureHandle material, uint32_t sampler_flags = BGFX_SAMPLER_NONE);
    void SetParallax(const std::array<float, 8> &parallax);

    // Most lights any single draw call had to leave out since the last TakeDroppedLights() because they did
    // not fit into the per-draw budget (Settings::point_lights). The first occurrence is also logged. Show
    // it in the debug overlay: a steady non-zero value means the budget is too small for the scene.
    int TakeDroppedLights();

    void Shutdown();
    void OnDeviceLost();

  private:
    bgfx::UniformHandle u_light_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_point_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_point_count_ = BGFX_INVALID_HANDLE;
    int dropped_ = 0;
    bool dropped_reported_ = false;
    bgfx::UniformHandle s_normal_map_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_material_ = BGFX_INVALID_HANDLE;
    bool normal_map_reported_ = false;
    bgfx::UniformHandle u_parallax_ = BGFX_INVALID_HANDLE;
};

} // namespace storm::fx
