// CPU side of the sun shadow map: one orthographic cascade around the part of
// the scene the player looks at.
#pragma once

#include "storm/fx/settings.hpp"
#include "storm/fx/sky_model.hpp"

#include <array>

namespace storm::fx
{

struct ShadowSetup
{
    std::array<float, 16> view{};       // world -> light space (row-vector convention, as bx)
    std::array<float, 16> projection{}; // light space -> clip, orthographic, depth 0..1 over [near, far]
    float texel_world = 0.0f;           // world size of one shadow map texel
    float radius = 0.0f;
};

// Shadow map size for a quality level (0 = no shadows).
int ShadowMapSize(ShadowQuality quality);

// Cascade covering the sphere (`focus`, `radius`) as seen from the sun. `caster_reach` extends the depth range
// towards the sun for casters outside the sphere (masts, sails). The cascade moves in whole texels, so
// shadow edges do not crawl when the camera moves.
ShadowSetup ComputeShadowSetup(Vec3 sun_dir, Vec3 focus, float radius, float caster_reach, int map_size);

// Shadow map uv (x, y in 0..1, top-left origin) and depth (0..1) of a world position; for tests and tools.
std::array<float, 3> ShadowCoord(const ShadowSetup &setup, Vec3 world);

} // namespace storm::fx
