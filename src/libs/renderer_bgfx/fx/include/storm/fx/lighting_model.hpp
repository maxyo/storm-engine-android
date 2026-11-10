// CPU side of the per-pixel lighting upgrade: turns the fixed-function light
// state into the uniform block read by shaders/include/fx_lighting.sh.
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/sky_model.hpp"

#include <array>
#include <string>

namespace storm::fx
{

// The directional light and ambient the fixed-function pipeline would use
// (D3DLIGHT9 / D3DRS_AMBIENT), colours as 0..1 floats.
struct LightingInput
{
    Vec3 sun_dir{0.0f, 1.0f, 0.0f};     // world space, unit, towards the light (-D3DLIGHT9.Direction)
    Vec3 sun_diffuse{1.0f, 1.0f, 1.0f}; // D3DLIGHT9.Diffuse
    Vec3 ambient{0.4f, 0.4f, 0.4f};     // global ambient that the vertex path no longer adds
    std::array<float, 16> view{};       // world -> view, row-vector convention (D3DTS_VIEW / bx)
};

// Per material class; the fixed-function state has no equivalent.
struct LightingMaterial
{
    float specular_power = 24.0f;
    float specular_strength = 0.0f; // 0 disables the highlight
    float translucency = 0.0f;      // sails, flags: 0.4 .. 0.7
    float wrap = 0.0f;              // 0 = hard N·L like the legacy pipeline; skin / cloth: 0.15 .. 0.3

    static LightingMaterial Hull();      // wet painted wood
    static LightingMaterial Sail();      // canvas
    static LightingMaterial Character(); // skin and cloth
};

struct LightingUniforms
{
    std::array<std::array<float, 4>, 5> u_fxLight; // layout documented in fx_lighting.sh
};

// `sky` may be null: the hemisphere then has no colour tint.
LightingUniforms ComputeLighting(const LightingInput &input, const LightingMaterial &material, const SkyState *sky);

// shaderc --define value for a flag set, e.g. "FX_LIGHT_PER_PIXEL;FX_LIGHT_HEMISPHERE".
// Flags that need kLightingPerPixel are dropped when it is not set.
std::string LightingDefines(uint32_t lighting_flags);

} // namespace storm::fx
