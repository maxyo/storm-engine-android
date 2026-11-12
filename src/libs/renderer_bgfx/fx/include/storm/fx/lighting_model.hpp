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
    float relief = 0.0f;            // scale of the derived normal map (whose texture class sets the strength): 1, canvas 0.35
    float rim = 0.0f;               // rim light against the sun (kLightingRim): characters 0.2, hulls and sails 0.3

    static LightingMaterial Stone();     // walls, rock, ground
    static LightingMaterial Hull();      // wet painted wood
    static LightingMaterial Sail();      // canvas
    static LightingMaterial Character(); // skin and cloth
};

struct LightingUniforms
{
    std::array<std::array<float, 4>, 6> u_fxLight; // layout documented in fx_lighting.sh
};

// `sky` may be null: the hemisphere then has no colour tint.
LightingUniforms ComputeLighting(const LightingInput &input, const LightingMaterial &material, const SkyState *sky,
                                 const Settings &settings);

// shaderc --define value for the lighting variant these settings ask for, e.g.
// "FX_LIGHT_PER_PIXEL;FX_LIGHT_HEMISPHERE;FX_LIGHT_POINT;FX_LIGHT_POINT_MAX=4". Empty = legacy path.
// Flags that need kLightingPerPixel are dropped when it is not set; kLightingPoint works on its own
// (geometry with baked vertex colours, see fx_lighting.sh).
std::string LightingDefines(const Settings &settings);

// Number of point / spot lights to select per draw call (0 when kLightingPoint is off).
int PointLightBudget(const Settings &settings);

} // namespace storm::fx
