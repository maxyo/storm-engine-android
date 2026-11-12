#include "storm/fx/ground_light.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace storm::fx
{

namespace
{

Vec3 Sub(const Vec3 &a, const Vec3 &b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 Cross(const Vec3 &a, const Vec3 &b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// Ground slopes up to 60°: n.y / |n| >= cos 60°.
constexpr float kGroundMinUp = 0.5f;

} // namespace

void GroundLight::Clear()
{
    triangles_.clear();
    lightmaps_.clear();
    levels_.clear();
    cell_start_.clear();
    cell_items_.clear();
    cells_x_ = cells_z_ = 0;
}

void GroundLight::Build(std::span<const Vec3> positions, std::span<const std::array<float, 2>> uvs,
                        std::span<const uint32_t> indices, std::span<const uint32_t> lightmap,
                        std::vector<GroundLightmap> lightmaps)
{
    Clear();
    lightmaps_ = std::move(lightmaps);
    min_x_ = min_z_ = 1e30f;
    float max_x = -1e30f, max_z = -1e30f;
    for (size_t t = 0; t + 2 < indices.size() && t / 3 < lightmap.size(); t += 3)
    {
        const uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size() || i0 >= uvs.size() ||
            i1 >= uvs.size() || i2 >= uvs.size() || lightmap[t / 3] >= lightmaps_.size())
            continue;
        const Vec3 n = Cross(Sub(positions[i1], positions[i0]), Sub(positions[i2], positions[i0]));
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        // Either winding: the game's ground faces up in either order; what matters is how level it is.
        if (length <= 1e-8f || std::fabs(n[1]) < kGroundMinUp * length)
            continue;
        triangles_.push_back({positions[i0], positions[i1], positions[i2], uvs[i0], uvs[i1], uvs[i2], lightmap[t / 3]});
        const Triangle &tri = triangles_.back();
        for (const Vec3 *v : {&tri.a, &tri.b, &tri.c})
        {
            min_x_ = std::min(min_x_, (*v)[0]);
            max_x = std::max(max_x, (*v)[0]);
            min_z_ = std::min(min_z_, (*v)[2]);
            max_z = std::max(max_z, (*v)[2]);
        }
    }
    if (triangles_.empty())
        return;
    cells_x_ = std::max(1, static_cast<int>(std::ceil((max_x - min_x_) / kGroundCell)) + 1);
    cells_z_ = std::max(1, static_cast<int>(std::ceil((max_z - min_z_) / kGroundCell)) + 1);
    // Two passes over the triangles' xz boxes: counts, then items (compressed rows).
    std::vector<uint32_t> counts(static_cast<size_t>(cells_x_) * cells_z_ + 1, 0);
    auto for_cells = [&](const Triangle &tri, auto &&visit) {
        const float x0 = std::min({tri.a[0], tri.b[0], tri.c[0]}), x1 = std::max({tri.a[0], tri.b[0], tri.c[0]});
        const float z0 = std::min({tri.a[2], tri.b[2], tri.c[2]}), z1 = std::max({tri.a[2], tri.b[2], tri.c[2]});
        const int cx0 = std::clamp(static_cast<int>((x0 - min_x_) / kGroundCell), 0, cells_x_ - 1);
        const int cx1 = std::clamp(static_cast<int>((x1 - min_x_) / kGroundCell), 0, cells_x_ - 1);
        const int cz0 = std::clamp(static_cast<int>((z0 - min_z_) / kGroundCell), 0, cells_z_ - 1);
        const int cz1 = std::clamp(static_cast<int>((z1 - min_z_) / kGroundCell), 0, cells_z_ - 1);
        for (int z = cz0; z <= cz1; ++z)
            for (int x = cx0; x <= cx1; ++x)
                visit(static_cast<size_t>(z) * cells_x_ + x);
    };
    for (const Triangle &tri : triangles_)
        for_cells(tri, [&](size_t cell) { ++counts[cell + 1]; });
    for (size_t i = 1; i < counts.size(); ++i)
        counts[i] += counts[i - 1];
    cell_start_ = counts;
    cell_items_.resize(counts.back());
    std::vector<uint32_t> fill(counts.begin(), counts.end() - 1);
    for (uint32_t t = 0; t < triangles_.size(); ++t)
        for_cells(triangles_[t], [&](size_t cell) { cell_items_[fill[cell]++] = t; });

    // Each lightmap's shade and sun: percentiles of what its ground shows (four points per triangle, by area).
    levels_.assign(lightmaps_.size(), {0.0f, 1.0f});
    std::vector<std::vector<std::pair<float, float>>> samples(lightmaps_.size());
    for (const Triangle &tri : triangles_)
    {
        const Vec3 n = Cross(Sub(tri.b, tri.a), Sub(tri.c, tri.a));
        const float area = 0.5f * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (const auto &w : {std::array<float, 2>{1.0f / 3.0f, 1.0f / 3.0f}, std::array<float, 2>{0.1f, 0.1f},
                              std::array<float, 2>{0.8f, 0.1f}, std::array<float, 2>{0.1f, 0.8f}})
            samples[tri.lightmap].push_back({Luminance(tri, w[0], w[1]), 0.25f * area});
    }
    for (size_t m = 0; m < samples.size(); ++m)
    {
        auto &s = samples[m];
        if (s.empty())
            continue;
        std::sort(s.begin(), s.end());
        float total = 0.0f;
        for (const auto &e : s)
            total += e.second;
        float acc = 0.0f, p10 = s.front().first, p90 = s.back().first;
        bool have10 = false;
        for (const auto &e : s)
        {
            acc += e.second;
            if (!have10 && acc >= 0.1f * total)
            {
                p10 = e.first;
                have10 = true;
            }
            if (acc >= 0.9f * total)
            {
                p90 = e.first;
                break;
            }
        }
        levels_[m] = {p10, std::max(p90, p10 + 1e-3f)};
    }
}

float GroundLight::Luminance(const Triangle &t, float u, float v) const
{
    const GroundLightmap &map = lightmaps_[t.lightmap];
    if (map.width <= 0 || map.height <= 0)
        return 0.0f;
    const float w = 1.0f - u - v;
    float s = w * t.ta[0] + u * t.tb[0] + v * t.tc[0];
    float r = w * t.ta[1] + u * t.tb[1] + v * t.tc[1];
    // Wrap like the sampler, then bilinear between texel centres.
    s = (s - std::floor(s)) * static_cast<float>(map.width) - 0.5f;
    r = (r - std::floor(r)) * static_cast<float>(map.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(s)), y0 = static_cast<int>(std::floor(r));
    const float fx = s - static_cast<float>(x0), fy = r - static_cast<float>(y0);
    auto at = [&](int x, int y) {
        x = ((x % map.width) + map.width) % map.width;
        y = ((y % map.height) + map.height) % map.height;
        return static_cast<float>(map.luminance[static_cast<size_t>(y) * map.width + x]) / 255.0f;
    };
    return (1.0f - fy) * ((1.0f - fx) * at(x0, y0) + fx * at(x0 + 1, y0)) +
           fy * ((1.0f - fx) * at(x0, y0 + 1) + fx * at(x0 + 1, y0 + 1));
}

std::optional<float> GroundLight::SunAt(const Vec3 &p) const
{
    if (triangles_.empty())
        return std::nullopt;
    const int cx = static_cast<int>(std::floor((p[0] - min_x_) / kGroundCell));
    const int cz = static_cast<int>(std::floor((p[2] - min_z_) / kGroundCell));
    if (cx < 0 || cz < 0 || cx >= cells_x_ || cz >= cells_z_)
        return std::nullopt;
    const size_t cell = static_cast<size_t>(cz) * cells_x_ + cx;
    float best_y = p[1] - kGroundReach;
    const Triangle *best = nullptr;
    float best_u = 0.0f, best_v = 0.0f;
    for (uint32_t k = cell_start_[cell]; k < cell_start_[cell + 1]; ++k)
    {
        const Triangle &t = triangles_[cell_items_[k]];
        // Barycentric weights of b and c in the xz projection.
        const float e1x = t.b[0] - t.a[0], e1z = t.b[2] - t.a[2], e2x = t.c[0] - t.a[0], e2z = t.c[2] - t.a[2];
        const float det = e1x * e2z - e2x * e1z;
        if (std::fabs(det) < 1e-10f)
            continue;
        const float px = p[0] - t.a[0], pz = p[2] - t.a[2];
        const float u = (px * e2z - e2x * pz) / det, v = (e1x * pz - px * e1z) / det;
        if (u < 0.0f || v < 0.0f || u + v > 1.0f)
            continue;
        const float y = t.a[1] + u * (t.b[1] - t.a[1]) + v * (t.c[1] - t.a[1]);
        // The ground under p: the highest surface not above it (a little tolerance for feet sunk into the ground).
        if (y <= p[1] + 0.1f && y > best_y)
        {
            best_y = y;
            best = &t;
            best_u = u;
            best_v = v;
        }
    }
    if (!best)
        return std::nullopt;
    const std::array<float, 2> &level = levels_[best->lightmap];
    return std::clamp((Luminance(*best, best_u, best_v) - level[0]) / (level[1] - level[0]), 0.0f, 1.0f);
}

std::optional<float> GroundLight::SunVisibility(const Vec3 &p) const
{
    // The centre and eight points on the disk's rim.
    float sum = 0.0f;
    int n = 0;
    for (int i = -1; i < 8; ++i)
    {
        Vec3 q = p;
        if (i >= 0)
        {
            const float a = 0.7853982f * static_cast<float>(i);
            q[0] += kGroundSoftRadius * std::cos(a);
            q[2] += kGroundSoftRadius * std::sin(a);
        }
        if (const auto s = SunAt(q))
        {
            sum += *s;
            ++n;
        }
    }
    if (n == 0)
        return std::nullopt;
    return sum / static_cast<float>(n);
}

} // namespace storm::fx
