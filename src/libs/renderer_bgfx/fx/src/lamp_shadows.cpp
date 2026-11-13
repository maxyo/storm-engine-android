#include "storm/fx/lamp_shadows.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

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

float LampRadius(const Lamp &lamp, float threshold, float limit)
{
    const float reach = std::min(lamp.range, limit);
    const float luminance = Luminance(lamp.color);
    const auto light = [&](float d) { return luminance / std::max(lamp.att0 + lamp.att1 * d + lamp.att2 * d * d, 1e-3f); };
    if (light(reach) >= threshold)
        return reach;
    float lo = 0.0f, hi = reach; // the attenuation only grows with the distance
    for (int i = 0; i < 24; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (light(mid) >= threshold ? lo : hi) = mid;
    }
    return hi;
}

LampGrid BuildLampGrid(std::span<const Lamp> lamps, Vec3 lo, Vec3 hi, float cell, int max_cells_xz, int max_cells_y)
{
    LampGrid grid;
    grid.lo = lo;
    int n[3];
    for (int k = 0; k < 3; ++k)
    {
        const float extent = std::max(hi[k] - lo[k], cell);
        const int limit = k == 1 ? max_cells_y : max_cells_xz;
        n[k] = std::clamp(static_cast<int>(std::ceil(extent / cell)), 1, limit);
        grid.cell[k] = extent / static_cast<float>(n[k]);
    }
    grid.nx = n[0];
    grid.ny = n[1];
    grid.nz = n[2];
    grid.rgba.assign(static_cast<size_t>(2 * grid.nx) * grid.ny * grid.nz * 4, 0);
    std::vector<std::pair<float, int>> reach;
    for (int z = 0; z < grid.nz; ++z)
        for (int y = 0; y < grid.ny; ++y)
            for (int x = 0; x < grid.nx; ++x)
            {
                const Vec3 a{lo[0] + grid.cell[0] * static_cast<float>(x), lo[1] + grid.cell[1] * static_cast<float>(y),
                             lo[2] + grid.cell[2] * static_cast<float>(z)};
                reach.clear();
                for (int i = 0; i < static_cast<int>(lamps.size()) && i < 255; ++i)
                {
                    // The point of the cell nearest to the lamp: inside its radius = the lamp reaches the cell.
                    Vec3 nearest;
                    for (int k = 0; k < 3; ++k)
                        nearest[k] = std::clamp(lamps[i].position[k], a[k], a[k] + grid.cell[k]);
                    const Vec3 d{nearest[0] - lamps[i].position[0], nearest[1] - lamps[i].position[1],
                                 nearest[2] - lamps[i].position[2]};
                    const float distance = std::sqrt(Dot(d, d));
                    if (distance >= lamps[i].range)
                        continue;
                    const float window = 1.0f - std::pow(distance / lamps[i].range, 4.0f);
                    const float att = lamps[i].att0 + lamps[i].att1 * distance + lamps[i].att2 * distance * distance;
                    reach.emplace_back(-Luminance(lamps[i].color) * window * window / std::max(att, 1e-3f), i);
                }
                std::sort(reach.begin(), reach.end());
                grid.max_lamps = std::max(grid.max_lamps, static_cast<int>(reach.size()));
                if (static_cast<int>(reach.size()) > kLampGridCapacity)
                    ++grid.overflow_cells;
                for (int j = 0; j < kLampGridCapacity && j < static_cast<int>(reach.size()); ++j)
                {
                    const size_t texel = (static_cast<size_t>(z) * grid.ny + y) * (2 * grid.nx) + x + (j / 4) * grid.nx;
                    grid.rgba[texel * 4 + j % 4] = static_cast<uint8_t>(reach[j].second + 1);
                }
            }
    return grid;
}

RoomLight BuildRoomLight(std::span<const Vec3> positions, std::span<const uint32_t> colors, std::span<const uint32_t> indices,
                         Vec3 lo, Vec3 hi, float cell, float cell_y, int max_cells_xz, int max_cells_y)
{
    RoomLight room;
    room.lo = lo;
    int n[3];
    for (int k = 0; k < 3; ++k)
    {
        const float size = k == 1 ? cell_y : cell;
        const float extent = std::max(hi[k] - lo[k], size);
        n[k] = std::clamp(static_cast<int>(std::ceil(extent / size)), 1, k == 1 ? max_cells_y : max_cells_xz);
        room.cell[k] = extent / static_cast<float>(n[k]);
    }
    room.nx = n[0];
    room.ny = n[1];
    room.nz = n[2];
    const size_t cells = static_cast<size_t>(n[0]) * n[1] * n[2];
    std::vector<std::array<float, 4>> sum(cells, {0.0f, 0.0f, 0.0f, 0.0f}); // rgb * area, area
    const auto channel = [](uint32_t c, int shift) { return static_cast<float>((c >> shift) & 255u); };
    for (size_t t = 0; t + 2 < indices.size(); t += 3)
    {
        const uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size() || std::max({i0, i1, i2}) >= colors.size())
            continue;
        const Vec3 &a = positions[i0], &b = positions[i1], &c = positions[i2];
        const Vec3 e1{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const Vec3 cr{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float area = 0.5f * std::sqrt(Dot(cr, cr));
        if (area <= 0.0f)
            continue;
        // Level surfaces at full weight, the others at a fifth (whichever way the triangle is wound).
        const float level = std::fabs(cr[1]) > 0.7f * 2.0f * area ? 1.0f : 0.2f;
        // A point every quarter of a square metre (the floors are a few huge triangles), spread evenly over the triangle.
        const int points = std::clamp(static_cast<int>(std::ceil(area * 4.0f)), 1, 1024);
        for (int s = 0; s < points; ++s)
        {
            // The R2 low-discrepancy sequence, folded into the triangle.
            float u = std::fmod(0.5f + 0.7548776662f * static_cast<float>(s), 1.0f);
            float v = std::fmod(0.5f + 0.5698402910f * static_cast<float>(s), 1.0f);
            if (u + v > 1.0f)
            {
                u = 1.0f - u;
                v = 1.0f - v;
            }
            const float w = 1.0f - u - v;
            int index[3];
            bool inside = true;
            for (int k = 0; k < 3; ++k)
            {
                const float p = a[k] * w + b[k] * u + c[k] * v;
                index[k] = static_cast<int>(std::floor((p - lo[k]) / room.cell[k]));
                inside &= index[k] >= 0 && index[k] < n[k];
            }
            if (!inside)
                continue;
            std::array<float, 4> &into = sum[(static_cast<size_t>(index[2]) * n[1] + index[1]) * n[0] + index[0]];
            const float share = level * area / static_cast<float>(points);
            for (int ch = 0; ch < 3; ++ch) // r, g, b
            {
                const int shift = 16 - 8 * ch;
                into[ch] += share * (channel(colors[i0], shift) * w + channel(colors[i1], shift) * u + channel(colors[i2], shift) * v);
            }
            into[3] += share;
        }
    }
    // Cells without a surface take the mean of the neighbours that have one, layer by layer outwards; then every cell
    // is averaged with its neighbours on its own storey, so the light changes over metres, not at a cell's edge.
    std::vector<std::array<float, 3>> light(cells, {0.0f, 0.0f, 0.0f});
    std::vector<uint8_t> known(cells, 0);
    size_t unknown = 0;
    for (size_t i = 0; i < cells; ++i)
        if (sum[i][3] > 0.0f)
        {
            light[i] = {sum[i][0] / sum[i][3], sum[i][1] / sum[i][3], sum[i][2] / sum[i][3]};
            known[i] = 1;
        }
        else
            ++unknown;
    if (unknown == cells)
        return {}; // no surface in the box: no room light
    const auto at = [&](int x, int y, int z) { return (static_cast<size_t>(z) * n[1] + y) * n[0] + x; };
    const auto neighbours = [&](int x, int y, int z, const std::vector<uint8_t> &mask, bool self, std::array<float, 3> &mean) {
        int count = 0;
        mean = {0.0f, 0.0f, 0.0f};
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int qx = x + dx, qy = y + dy, qz = z + dz;
                    if ((!self && dx == 0 && dy == 0 && dz == 0) || (self && dy != 0) || qx < 0 || qy < 0 || qz < 0 || qx >= n[0] || qy >= n[1] || qz >= n[2] ||
                        !mask[at(qx, qy, qz)])
                        continue;
                    for (int ch = 0; ch < 3; ++ch)
                        mean[ch] += light[at(qx, qy, qz)][ch];
                    ++count;
                }
        for (int ch = 0; ch < 3 && count; ++ch)
            mean[ch] /= static_cast<float>(count);
        return count;
    };
    while (unknown > 0)
    {
        std::vector<uint8_t> next = known;
        std::vector<std::array<float, 3>> filled = light;
        for (int z = 0; z < n[2]; ++z)
            for (int y = 0; y < n[1]; ++y)
                for (int x = 0; x < n[0]; ++x)
                    if (std::array<float, 3> mean; !known[at(x, y, z)] && neighbours(x, y, z, known, false, mean) > 0)
                    {
                        filled[at(x, y, z)] = mean;
                        next[at(x, y, z)] = 1;
                        --unknown;
                    }
        known = std::move(next);
        light = std::move(filled);
    }
    room.rgba.assign(cells * 4, 255);
    for (int z = 0; z < n[2]; ++z)
        for (int y = 0; y < n[1]; ++y)
            for (int x = 0; x < n[0]; ++x)
            {
                std::array<float, 3> mean;
                neighbours(x, y, z, known, true, mean);
                for (int ch = 0; ch < 3; ++ch)
                    room.rgba[at(x, y, z) * 4 + ch] = static_cast<uint8_t>(std::clamp(mean[ch] + 0.5f, 0.0f, 255.0f));
            }
    return room;
}

TriangleGrid BuildTriangleGrid(std::span<const Vec3> positions, std::span<const uint32_t> indices, float cell)
{
    TriangleGrid grid;
    grid.count = indices.size() / 3;
    grid.box.resize(grid.count);
    Vec3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f};
    bool any = false;
    std::vector<std::array<Vec3, 2>> &box = grid.box;
    for (size_t t = 0; t < grid.count; ++t)
    {
        const uint32_t i0 = indices[3 * t], i1 = indices[3 * t + 1], i2 = indices[3 * t + 2];
        if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size())
        {
            box[t] = {Vec3{1.0f, 1.0f, 1.0f}, Vec3{-1.0f, -1.0f, -1.0f}}; // never found
            continue;
        }
        const Vec3 &a = positions[i0], &b = positions[i1], &c = positions[i2];
        for (int k = 0; k < 3; ++k)
        {
            box[t][0][k] = std::min({a[k], b[k], c[k]});
            box[t][1][k] = std::max({a[k], b[k], c[k]});
            lo[k] = any ? std::min(lo[k], box[t][0][k]) : box[t][0][k];
            hi[k] = any ? std::max(hi[k], box[t][1][k]) : box[t][1][k];
        }
        any = true;
    }
    grid.lo = lo;
    for (int k = 0; k < 3; ++k)
    {
        const float extent = std::max(hi[k] - lo[k], cell);
        grid.size[k] = std::clamp(static_cast<int>(std::ceil(extent / cell)), 1, kTriangleGridCells);
        grid.cell[k] = extent / static_cast<float>(grid.size[k]);
    }
    const size_t cells = static_cast<size_t>(grid.size[0]) * grid.size[1] * grid.size[2];
    const auto range = [&](size_t t, int k, int &from, int &to) {
        from = std::clamp(static_cast<int>(std::floor((box[t][0][k] - lo[k]) / grid.cell[k])), 0, grid.size[k] - 1);
        to = std::clamp(static_cast<int>(std::floor((box[t][1][k] - lo[k]) / grid.cell[k])), 0, grid.size[k] - 1);
    };
    // Two passes: count the triangles of every cell, then fill the lists.
    grid.first.assign(cells + 1, 0);
    for (int pass = 0; pass < 2; ++pass)
    {
        for (size_t t = 0; t < grid.count; ++t)
        {
            if (box[t][0][0] > box[t][1][0])
                continue;
            int x0, x1, y0, y1, z0, z1;
            range(t, 0, x0, x1);
            range(t, 1, y0, y1);
            range(t, 2, z0, z1);
            for (int z = z0; z <= z1; ++z)
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x)
                    {
                        const size_t c = (static_cast<size_t>(z) * grid.size[1] + y) * grid.size[0] + x;
                        if (pass == 0)
                            ++grid.first[c + 1];
                        else
                            grid.triangles[grid.first[c]++] = static_cast<uint32_t>(t);
                    }
        }
        if (pass == 0)
        {
            for (size_t c = 0; c < cells; ++c)
                grid.first[c + 1] += grid.first[c];
            grid.triangles.resize(grid.first[cells]);
        }
        else
        {
            // The fill moved every start to its cell's end: shift them back.
            for (size_t c = cells; c > 0; --c)
                grid.first[c] = grid.first[c - 1];
            grid.first[0] = 0;
        }
    }
    return grid;
}

void TrianglesInSphere(const TriangleGrid &grid, std::span<const uint32_t> indices, Vec3 center, float radius,
                       std::vector<uint32_t> &out, std::vector<uint32_t> &seen)
{
    if (grid.count == 0)
        return;
    int from[3], to[3];
    for (int k = 0; k < 3; ++k)
    {
        if (center[k] + radius < grid.lo[k] || center[k] - radius > grid.lo[k] + grid.cell[k] * static_cast<float>(grid.size[k]))
            return;
        from[k] = std::clamp(static_cast<int>(std::floor((center[k] - radius - grid.lo[k]) / grid.cell[k])), 0, grid.size[k] - 1);
        to[k] = std::clamp(static_cast<int>(std::floor((center[k] + radius - grid.lo[k]) / grid.cell[k])), 0, grid.size[k] - 1);
    }
    // A triangle is listed in every cell its box touches: `seen` marks the ones already taken (one bit each).
    seen.assign((grid.count + 31) / 32, 0);
    std::vector<uint32_t> found;
    for (int z = from[2]; z <= to[2]; ++z)
        for (int y = from[1]; y <= to[1]; ++y)
            for (int x = from[0]; x <= to[0]; ++x)
            {
                const size_t c = (static_cast<size_t>(z) * grid.size[1] + y) * grid.size[0] + x;
                for (uint32_t i = grid.first[c]; i < grid.first[c + 1]; ++i)
                {
                    const uint32_t t = grid.triangles[i];
                    if (seen[t / 32] & (1u << (t % 32)))
                        continue;
                    seen[t / 32] |= 1u << (t % 32);
                    // The point of the triangle's box nearest to the lamp: inside the lamp's radius?
                    float d2 = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float nearest = std::clamp(center[k], grid.box[t][0][k], grid.box[t][1][k]);
                        d2 += (nearest - center[k]) * (nearest - center[k]);
                    }
                    if (d2 <= radius * radius)
                        found.push_back(t);
                }
            }
    std::sort(found.begin(), found.end()); // the order of `indices`: the same list whatever the cells' order
    for (const uint32_t t : found)
        out.insert(out.end(), {indices[3 * t], indices[3 * t + 1], indices[3 * t + 2]});
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

LampFaceSample LampFaceCoord(Vec3 lamp, Vec3 point, int slot, int columns, int rows)
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
    return {face, tile.x0 + (u * 0.5f + 0.5f) * tile.sx, tile.y0 + (0.5f - v * 0.5f) * tile.sy};
}

float LampCasterDistance(float clip_x, float clip_y, float clip_w)
{
    // clip x / w = (distance along right) / (distance along the face axis) / guard, the same for y; w = that axis.
    const float x = clip_x / clip_w * kLampFaceGuard, y = clip_y / clip_w * kLampFaceGuard;
    return clip_w * std::sqrt(1.0f + x * x + y * y);
}

} // namespace storm::fx
