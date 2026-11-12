#include "storm/fx/shadow_model.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 Normalize(Vec3 a)
{
    const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return {a[0] / len, a[1] / len, a[2] / len};
}
float Dot(Vec3 a, Vec3 b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
} // namespace

int ShadowMapSize(ShadowQuality quality)
{
    switch (quality)
    {
    case ShadowQuality::Low:
        return 1024;
    case ShadowQuality::Soft:
        return 2048;
    default:
        return 0;
    }
}

float SunShadowStrength(float sun_dir_y)
{
    return std::clamp(sun_dir_y * 8.0f, 0.0f, 1.0f);
}

ShadowSetup ComputeShadowSetup(Vec3 sun_dir, Vec3 focus, float radius, float caster_reach, int map_size)
{
    ShadowSetup setup;
    setup.radius = radius;
    setup.texel_world = 2.0f * radius / static_cast<float>(std::max(map_size, 1));
    // Light space: z points away from the sun (the direction light travels), x and y span the map.
    const Vec3 forward = Normalize(Vec3{-sun_dir[0], -sun_dir[1], -sun_dir[2]});
    const Vec3 right = Normalize(Cross(std::fabs(forward[1]) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f}, forward));
    const Vec3 up = Cross(forward, right);
    // Snap the centre to the texel grid of the map.
    const float cx = std::floor(Dot(focus, right) / setup.texel_world + 0.5f) * setup.texel_world;
    const float cy = std::floor(Dot(focus, up) / setup.texel_world + 0.5f) * setup.texel_world;
    const float cz = Dot(focus, forward);
    setup.view = {right[0], up[0], forward[0], 0.0f, right[1], up[1], forward[1], 0.0f,
                  right[2], up[2], forward[2], 0.0f, -cx,      -cy,   -cz,        1.0f};
    const float near_plane = -(radius + caster_reach), far_plane = radius;
    const float depth = far_plane - near_plane;
    setup.projection = {1.0f / radius, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f / radius, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f / depth, 0.0f, 0.0f, 0.0f, -near_plane / depth, 1.0f};
    return setup;
}

std::array<float, 3> ShadowCoord(const ShadowSetup &setup, Vec3 world)
{
    float light[3], clip[3];
    for (int j = 0; j < 3; ++j)
        light[j] = world[0] * setup.view[j] + world[1] * setup.view[4 + j] + world[2] * setup.view[8 + j] + setup.view[12 + j];
    for (int j = 0; j < 3; ++j)
        clip[j] = light[0] * setup.projection[j] + light[1] * setup.projection[4 + j] + light[2] * setup.projection[8 + j] +
                  setup.projection[12 + j];
    return {0.5f + 0.5f * clip[0], 0.5f - 0.5f * clip[1], clip[2]};
}

} // namespace storm::fx
