#include "storm/fx/lamp_shadows.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
float Dot(Vec3 a, Vec3 b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
float Luminance(Vec3 c)
{
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}
} // namespace

std::vector<Lamp> ClusterLamps(std::span<const Lamp> lamps, float radius)
{
    std::vector<Lamp> out;
    std::vector<float> weight;
    for (const Lamp &lamp : lamps)
    {
        const float w = std::max(Luminance(lamp.color), 1e-4f);
        bool merged = false;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const Vec3 d{lamp.position[0] - out[i].position[0], lamp.position[1] - out[i].position[1],
                         lamp.position[2] - out[i].position[2]};
            if (Dot(d, d) > radius * radius)
                continue;
            const float total = weight[i] + w;
            for (int k = 0; k < 3; ++k)
            {
                out[i].position[k] = (out[i].position[k] * weight[i] + lamp.position[k] * w) / total;
                out[i].color[k] += lamp.color[k];
            }
            out[i].range = std::max(out[i].range, lamp.range);
            if (w > weight[i])
            {
                out[i].att0 = lamp.att0;
                out[i].att1 = lamp.att1;
                out[i].att2 = lamp.att2;
            }
            weight[i] = total;
            merged = true;
            break;
        }
        if (!merged)
        {
            out.push_back(lamp);
            weight.push_back(w);
        }
    }
    return out;
}

float LampReach(const Lamp &lamp, Vec3 point)
{
    const Vec3 d{point[0] - lamp.position[0], point[1] - lamp.position[1], point[2] - lamp.position[2]};
    const float distance = std::sqrt(Dot(d, d));
    if (distance > lamp.range)
        return 0.0f;
    const float att = lamp.att0 + lamp.att1 * distance + lamp.att2 * distance * distance;
    return Luminance(lamp.color) / std::max(att, 1e-3f);
}

std::vector<int> SelectShadowLamps(std::span<const Lamp> lamps, Vec3 eye, int count, std::span<const int> previous)
{
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(lamps.size()); ++i)
        if (LampReach(lamps[i], eye) > 0.0f)
            order.push_back(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return LampReach(lamps[a], eye) > LampReach(lamps[b], eye); });
    count = std::min<int>(count, static_cast<int>(order.size()));
    std::vector<int> chosen(order.begin(), order.begin() + count);
    if (count == 0)
        return chosen;
    // Hysteresis: a lamp chosen before stays while it is at least 2/3 as strong as the weakest one it would give way to.
    const float weakest = LampReach(lamps[chosen.back()], eye);
    for (const int keep : previous)
    {
        if (keep < 0 || keep >= static_cast<int>(lamps.size()) ||
            std::find(chosen.begin(), chosen.end(), keep) != chosen.end())
            continue;
        if (LampReach(lamps[keep], eye) * 1.5f < weakest)
            continue;
        chosen.back() = keep; // replaces the weakest newcomer
        std::stable_sort(chosen.begin(), chosen.end(),
                         [&](int a, int b) { return LampReach(lamps[a], eye) > LampReach(lamps[b], eye); });
        break;
    }
    return chosen;
}

const std::array<CubeFace, 6> &CubeFaces()
{
    static const std::array<CubeFace, 6> faces = {{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};
    return faces;
}

std::array<float, 16> LampFaceViewProj(Vec3 position, int face, float far_plane)
{
    const CubeFace &f = CubeFaces()[face];
    const float n = kLampShadowNear, far = std::max(far_plane, n * 2.0f);
    const float s = 1.0f / kLampFaceGuard;
    const float a = far / (far - n), b = -far * n / (far - n);
    // view (row-vector): v = (dot(p - pos, right), dot(p - pos, up), dot(p - pos, forward)); projection:
    // x = v.x * s, y = v.y * s, z = v.z * a + b, w = v.z
    std::array<float, 16> m{};
    const Vec3 rows[3] = {f.right, f.up, f.forward};
    for (int i = 0; i < 3; ++i) // world axis i
    {
        m[i * 4 + 0] = rows[0][i] * s;
        m[i * 4 + 1] = rows[1][i] * s;
        m[i * 4 + 2] = rows[2][i] * a;
        m[i * 4 + 3] = rows[2][i];
    }
    const float tx = -Dot(position, f.right), ty = -Dot(position, f.up), tz = -Dot(position, f.forward);
    m[12] = tx * s;
    m[13] = ty * s;
    m[14] = tz * a + b;
    m[15] = tz;
    return m;
}

bool SphereInLampFace(Vec3 lamp, int face, Vec3 center, float radius)
{
    const CubeFace &f = CubeFaces()[face];
    const Vec3 c{center[0] - lamp[0], center[1] - lamp[1], center[2] - lamp[2]};
    const float z = Dot(c, f.forward);
    if (z < -radius)
        return false;
    // Side planes of the pyramid |x| <= g z, |y| <= g z (g: the guard band), normals (±1, 0, -g) / sqrt(1 + g²).
    const float g = kLampFaceGuard, norm = std::sqrt(1.0f + g * g);
    const float x = Dot(c, f.right), y = Dot(c, f.up);
    return (x - g * z) / norm <= radius && (-x - g * z) / norm <= radius && (y - g * z) / norm <= radius &&
           (-y - g * z) / norm <= radius;
}

AtlasTile LampAtlasTile(int slot, int face, int columns, int rows)
{
    const int tile = slot * 6 + face;
    const float sx = 1.0f / static_cast<float>(columns), sy = 1.0f / static_cast<float>(rows);
    return {static_cast<float>(tile % columns) * sx, static_cast<float>(tile / columns) * sy, sx, sy};
}

std::array<float, 16> TileClip(const AtlasTile &t)
{
    std::array<float, 16> m{};
    m[0] = t.sx;
    m[5] = t.sy;
    m[10] = 1.0f;
    m[12] = 2.0f * t.x0 + t.sx - 1.0f;
    m[13] = 1.0f - 2.0f * t.y0 - t.sy;
    m[15] = 1.0f;
    return m;
}

LampShadowSample LampShadowCoord(Vec3 lamp, Vec3 point, float far_plane, int slot, int columns, int rows)
{
    const Vec3 l{point[0] - lamp[0], point[1] - lamp[1], point[2] - lamp[2]};
    const float ax = std::fabs(l[0]), ay = std::fabs(l[1]), az = std::fabs(l[2]);
    int face;
    if (ax >= ay && ax >= az)
        face = l[0] >= 0.0f ? 0 : 1;
    else if (ay >= az)
        face = l[1] >= 0.0f ? 2 : 3;
    else
        face = l[2] >= 0.0f ? 4 : 5;
    const CubeFace &f = CubeFaces()[face];
    const float major = Dot(l, f.forward);
    const float s = 1.0f / kLampFaceGuard;
    const float u = Dot(l, f.right) / major * s, v = Dot(l, f.up) / major * s;
    const AtlasTile tile = LampAtlasTile(slot, face, columns, rows);
    const float n = kLampShadowNear, far = std::max(far_plane, n * 2.0f);
    return {face, tile.x0 + (u * 0.5f + 0.5f) * tile.sx, tile.y0 + (0.5f - v * 0.5f) * tile.sy,
            far / (far - n) * (1.0f - n / major)};
}

} // namespace storm::fx
