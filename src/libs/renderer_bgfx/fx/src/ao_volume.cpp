#include "storm/fx/ao_volume.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

// voxelizer.h's triangle/box overlap test (src/voxelizer_impl.c).
extern "C" int storm_vx_triangle_box_overlap(const float centre[3], const float half[3], const float a[3], const float b[3],
                                             const float c[3]);

namespace storm::fx
{
namespace
{
constexpr char kMagic[4] = {'S', 'A', 'O', 'V'};

// Fixed directions over the whole sphere (Fibonacci spiral): the same for every cell and every build.
std::array<Vec3, kAoDirections> Directions()
{
    std::array<Vec3, kAoDirections> d{};
    const float golden = 2.39996323f;
    for (int i = 0; i < kAoDirections; ++i)
    {
        const float y = 1.0f - (2.0f * static_cast<float>(i) + 1.0f) / static_cast<float>(kAoDirections);
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float a = golden * static_cast<float>(i);
        d[i] = {r * std::cos(a), y, r * std::sin(a)};
    }
    return d;
}

struct Grid
{
    std::array<int, 3> size{};
    size_t Index(int x, int y, int z) const
    {
        return (static_cast<size_t>(z) * size[1] + y) * size[0] + x;
    }
    bool Inside(int x, int y, int z) const
    {
        return x >= 0 && y >= 0 && z >= 0 && x < size[0] && y < size[1] && z < size[2];
    }
};

void Hash(uint64_t &h, const void *data, size_t bytes)
{
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ull;
    }
}

template <typename T> void Put(std::vector<uint8_t> &out, const T &value)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

template <typename T> bool Get(std::span<const uint8_t> data, size_t &at, T &value)
{
    if (at + sizeof(T) > data.size())
        return false;
    std::memcpy(&value, data.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}
} // namespace

const std::array<Vec3, kAoLobes> &AoLobeAxes()
{
    static const float k = 0.57735027f;
    static const std::array<Vec3, kAoLobes> axes = {{{k, k, k}, {k, -k, -k}, {-k, k, -k}, {-k, -k, k}}};
    return axes;
}

float AoVolume::Combine(const float lobes[kAoLobes], Vec3 normal)
{
    float sum = 0.0f, weight = 0.0f;
    for (int i = 0; i < kAoLobes; ++i)
    {
        const Vec3 &a = AoLobeAxes()[i];
        const float w = std::max(normal[0] * a[0] + normal[1] * a[1] + normal[2] * a[2], 0.0f);
        sum += w * lobes[i];
        weight += w;
    }
    return weight > 0.0f ? sum / weight : 1.0f;
}

float AoVolume::At(Vec3 world, Vec3 normal) const
{
    int c[3];
    for (int i = 0; i < 3; ++i)
    {
        c[i] = static_cast<int>(std::floor((world[i] - origin[i]) / cell));
        if (c[i] < 0 || c[i] >= size[i])
            return 1.0f;
    }
    const size_t index = (static_cast<size_t>(c[2]) * size[1] + c[1]) * size[0] + c[0];
    float lobes[kAoLobes];
    for (int k = 0; k < kAoLobes; ++k)
        lobes[k] = visibility[index * kAoLobes + k] / 255.0f;
    return Combine(lobes, normal);
}

float AoVolume::Sample(Vec3 world, Vec3 normal) const
{
    if (Empty())
        return 1.0f;
    int c0[3];
    float f[3];
    for (int i = 0; i < 3; ++i)
    {
        const float u = std::clamp((world[i] - origin[i]) / cell - 0.5f, 0.0f, static_cast<float>(size[i] - 1));
        c0[i] = std::min(static_cast<int>(u), size[i] - 2 < 0 ? 0 : size[i] - 2);
        f[i] = size[i] > 1 ? u - static_cast<float>(c0[i]) : 0.0f;
    }
    float lobes[kAoLobes] = {};
    for (int corner = 0; corner < 8; ++corner)
    {
        float w = 1.0f;
        int c[3];
        for (int i = 0; i < 3; ++i)
        {
            const int bit = (corner >> i) & 1;
            c[i] = std::min(c0[i] + bit, size[i] - 1);
            w *= bit ? f[i] : 1.0f - f[i];
        }
        const size_t index = (static_cast<size_t>(c[2]) * size[1] + c[1]) * size[0] + c[0];
        for (int k = 0; k < kAoLobes; ++k)
            lobes[k] += w * visibility[index * kAoLobes + k] / 255.0f;
    }
    return Combine(lobes, normal);
}

uint64_t AoBakeKey(const AoGeometry &geometry, const AoBakeParams &params)
{
    uint64_t h = 14695981039346656037ull;
    Hash(h, &kAoBakeVersion, sizeof(kAoBakeVersion));
    Hash(h, &params.cell, sizeof(params.cell));
    Hash(h, &params.radius, sizeof(params.radius));
    Hash(h, &params.max_cells, sizeof(params.max_cells));
    Hash(h, params.bounds_lo.data(), sizeof(params.bounds_lo));
    Hash(h, params.bounds_hi.data(), sizeof(params.bounds_hi));
    const uint64_t walk = params.walk.size();
    Hash(h, &walk, sizeof(walk));
    if (walk)
        Hash(h, params.walk.data(), params.walk.size() * sizeof(Vec3));
    const uint64_t counts[2] = {geometry.positions.size(), geometry.indices.size()};
    Hash(h, counts, sizeof(counts));
    Hash(h, geometry.positions.data(), geometry.positions.size_bytes());
    Hash(h, geometry.indices.data(), geometry.indices.size_bytes());
    return h;
}

AoVolume BakeAoVolume(const AoGeometry &geometry, const AoBakeParams &params)
{
    AoVolume volume;
    if (geometry.positions.empty() || geometry.indices.size() < 3)
        return volume;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const uint32_t index : geometry.indices)
    {
        if (index >= geometry.positions.size())
            continue;
        for (int i = 0; i < 3; ++i)
        {
            lo[i] = std::min(lo[i], geometry.positions[index][i]);
            hi[i] = std::max(hi[i], geometry.positions[index][i]);
        }
    }
    if (params.bounds_lo[0] <= params.bounds_hi[0])
        for (int i = 0; i < 3; ++i)
        {
            lo[i] = std::max(lo[i], params.bounds_lo[i]);
            hi[i] = std::min(hi[i], params.bounds_hi[i]);
        }
    if (lo[0] > hi[0] || lo[1] > hi[1] || lo[2] > hi[2])
        return volume;

    // Cells: one cell of margin on every side; grow the cell until the grid fits the budget.
    float cell = params.cell;
    for (;;)
    {
        size_t cells = 1;
        for (int i = 0; i < 3; ++i)
            cells *= static_cast<size_t>(std::ceil((hi[i] - lo[i]) / cell)) + 2;
        if (cells <= params.max_cells)
            break;
        cell *= 1.1f;
    }
    volume.cell = cell;
    for (int i = 0; i < 3; ++i)
    {
        volume.size[i] = static_cast<int>(std::ceil((hi[i] - lo[i]) / cell)) + 2;
        volume.origin[i] = lo[i] - cell;
    }

    // Occupancy at half a cell: cell (x, y, z) covers voxels 2x..2x+1 on each axis.
    const float voxel = cell * 0.5f;
    Grid occ{{volume.size[0] * 2, volume.size[1] * 2, volume.size[2] * 2}};
    std::vector<uint8_t> solid(static_cast<size_t>(occ.size[0]) * occ.size[1] * occ.size[2], 0);
    // A hair over half a voxel: a floor lying exactly on a voxel boundary must not slip between two layers (the
    // library's "precision" factor).
    const float half[3] = {voxel * 0.51f, voxel * 0.51f, voxel * 0.51f};
    for (size_t t = 0; t + 2 < geometry.indices.size(); t += 3)
    {
        const uint32_t a = geometry.indices[t], b = geometry.indices[t + 1], c = geometry.indices[t + 2];
        if (a >= geometry.positions.size() || b >= geometry.positions.size() || c >= geometry.positions.size())
            continue;
        const Vec3 &p1 = geometry.positions[a], &p2 = geometry.positions[b], &p3 = geometry.positions[c];
        int from[3], to[3];
        for (int i = 0; i < 3; ++i)
        {
            const float mn = std::min({p1[i], p2[i], p3[i]}), mx = std::max({p1[i], p2[i], p3[i]});
            from[i] = std::max(0, static_cast<int>(std::floor((mn - volume.origin[i]) / voxel)) - 1);
            to[i] = std::min(occ.size[i] - 1, static_cast<int>(std::floor((mx - volume.origin[i]) / voxel)) + 1);
        }
        for (int z = from[2]; z <= to[2]; ++z)
            for (int y = from[1]; y <= to[1]; ++y)
                for (int x = from[0]; x <= to[0]; ++x)
                {
                    uint8_t &voxel_solid = solid[occ.Index(x, y, z)];
                    if (voxel_solid)
                        continue;
                    const float centre[3] = {volume.origin[0] + (x + 0.5f) * voxel, volume.origin[1] + (y + 0.5f) * voxel,
                                             volume.origin[2] + (z + 0.5f) * voxel};
                    if (storm_vx_triangle_box_overlap(centre, half, p1.data(), p2.data(), p3.data()))
                        voxel_solid = 1;
                }
    }

    // Visibility per cell: march every direction from the cell centre (a voxel corner) in half-voxel steps.
    const auto directions = Directions();
    const Grid cells{volume.size};
    const size_t count = static_cast<size_t>(cells.size[0]) * cells.size[1] * cells.size[2];
    volume.visibility.assign(count * kAoLobes, 255);
    std::vector<uint8_t> inside(count, 0);
    // Cells with no solid voxel within `radius` (Chebyshev, so a superset of the sphere) see everything: a separable
    // sliding-window count of solid voxels finds them without marching (most of a hall's air).
    const int reach = static_cast<int>(std::ceil(params.radius / voxel)) + 1;
    std::vector<uint16_t> near_x(solid.size()), near_xy(solid.size());
    std::vector<uint8_t> near(solid.size());
    auto dilate = [&](auto read, auto write, int axis) {
        const int n = occ.size[axis];
        int other[2] = {(axis + 1) % 3, (axis + 2) % 3};
        for (int b = 0; b < occ.size[other[1]]; ++b)
            for (int a = 0; a < occ.size[other[0]]; ++a)
            {
                auto at = [&](int i) {
                    int c[3];
                    c[axis] = i;
                    c[other[0]] = a;
                    c[other[1]] = b;
                    return occ.Index(c[0], c[1], c[2]);
                };
                int count = 0;
                for (int i = 0; i < std::min(n, reach + 1); ++i)
                    count += read(at(i)) ? 1 : 0;
                for (int i = 0; i < n; ++i)
                {
                    write(at(i), count > 0);
                    if (i + reach + 1 < n)
                        count += read(at(i + reach + 1)) ? 1 : 0;
                    if (i - reach >= 0)
                        count -= read(at(i - reach)) ? 1 : 0;
                }
            }
    };
    dilate([&](size_t i) { return solid[i] != 0; }, [&](size_t i, bool v) { near_x[i] = v; }, 0);
    dilate([&](size_t i) { return near_x[i] != 0; }, [&](size_t i, bool v) { near_xy[i] = v; }, 1);
    dilate([&](size_t i) { return near_xy[i] != 0; }, [&](size_t i, bool v) { near[i] = v; }, 2);

    // In voxel units the cell centre is the voxel corner (2x + 1, 2y + 1, 2z + 1); rays advance one voxel a step
    // (the voxelization is conservative: every voxel a triangle touches is solid).
    // Weight of an occluder at distance t: 1 - (t / radius)², so table tops and barrels a little away still count
    // nearly fully and only the far end of the radius fades.
    const int steps = std::max(1, static_cast<int>(params.radius / voxel));
    std::vector<float> weight(static_cast<size_t>(steps) + 1);
    for (int s = 1; s <= steps; ++s)
    {
        const float t = voxel * static_cast<float>(s) / params.radius;
        weight[s] = 1.0f - t * t;
    }
    std::array<Vec3, kAoDirections> increment{};
    for (int i = 0; i < kAoDirections; ++i)
        for (int j = 0; j < 3; ++j)
            increment[i][j] = directions[i][j];
    // Each lobe weighs the directions by their cosine to its axis (the receiver's hemisphere, AoLobeAxes).
    float lobe_weight[kAoLobes][kAoDirections] = {};
    float lobe_total[kAoLobes] = {};
    for (int k = 0; k < kAoLobes; ++k)
        for (int i = 0; i < kAoDirections; ++i)
        {
            const Vec3 &a = AoLobeAxes()[k];
            lobe_weight[k][i] = std::max(directions[i][0] * a[0] + directions[i][1] * a[1] + directions[i][2] * a[2], 0.0f);
            lobe_total[k] += lobe_weight[k][i];
        }
    const float bound[3] = {static_cast<float>(occ.size[0]), static_cast<float>(occ.size[1]), static_cast<float>(occ.size[2])};
    auto bake_slices = [&](int z_begin, int z_end) {
        for (int z = z_begin; z < z_end; ++z)
            for (int y = 0; y < cells.size[1]; ++y)
                for (int x = 0; x < cells.size[0]; ++x)
                {
                    const size_t index = cells.Index(x, y, z);
                    // The centre is the corner of 8 voxels; inside geometry when all of them are solid, i.e. when no
                    // direction can leave it.
                    bool enclosed = true;
                    for (int k = 0; k < 8 && enclosed; ++k)
                        enclosed = solid[occ.Index(2 * x + (k & 1), 2 * y + ((k >> 1) & 1), 2 * z + ((k >> 2) & 1))] != 0;
                    if (enclosed)
                    {
                        inside[index] = 1;
                        continue;
                    }
                    if (!near[occ.Index(2 * x + 1, 2 * y + 1, 2 * z + 1)])
                        continue; // keeps 255
                    float occlusion[kAoLobes] = {};
                    for (int di = 0; di < kAoDirections; ++di)
                    {
                        const Vec3 &d = increment[di];
                        float p[3] = {2.0f * x + 1.0f, 2.0f * y + 1.0f, 2.0f * z + 1.0f};
                        for (int s = 1; s <= steps; ++s)
                        {
                            p[0] += d[0];
                            p[1] += d[1];
                            p[2] += d[2];
                            if (!(p[0] >= 0.0f && p[1] >= 0.0f && p[2] >= 0.0f && p[0] < bound[0] && p[1] < bound[1] &&
                                  p[2] < bound[2]))
                                break;
                            if (solid[occ.Index(static_cast<int>(p[0]), static_cast<int>(p[1]), static_cast<int>(p[2]))])
                            {
                                for (int k = 0; k < kAoLobes; ++k)
                                    occlusion[k] += weight[s] * lobe_weight[k][di];
                                break;
                            }
                        }
                    }
                    for (int k = 0; k < kAoLobes; ++k)
                    {
                        const float visible = 1.0f - occlusion[k] / lobe_total[k];
                        volume.visibility[index * kAoLobes + k] =
                            static_cast<uint8_t>(std::lround(std::clamp(visible, 0.0f, 1.0f) * 255.0f));
                    }
                }
    };
    const unsigned threads = std::clamp(params.threads, 1u, static_cast<unsigned>(std::max(1, cells.size[2])));
    if (threads == 1)
        bake_slices(0, cells.size[2]);
    else
    {
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < threads; ++i)
            workers.emplace_back(bake_slices, static_cast<int>(cells.size[2] * i / threads),
                                 static_cast<int>(cells.size[2] * (i + 1) / threads));
        for (auto &worker : workers)
            worker.join();
    }

    // What the volume adds is local: under a table, behind a barrel, in a corner. Where the whole place is narrow or
    // low (a ship's corridor and crew quarters, a hold between its ribs, a hut) the floor one walks on has walls
    // within the radius everywhere and sees less than an open floor does: the place would go dark as a whole, though
    // its baked colours already hold how light it is. So the volume is measured against the place's own walking
    // floor: what an upward surface sees at the walk points (their median), against what an open floor sees
    // (kAoOpenSurface). Every cell is raised by that ratio, never lowered, by no more than kAoWalkLift: the floor one
    // walks on reads as open, and what stands on it darkens it as before.
    if (!params.walk.empty())
    {
        std::vector<float> seen;
        seen.reserve(params.walk.size());
        for (const Vec3 &point : params.walk)
        {
            const Vec3 above{point[0], point[1] + 0.5f * volume.cell, point[2]};
            bool within = true;
            for (int k = 0; k < 3; ++k)
                within = within && above[k] >= volume.origin[k] &&
                         above[k] < volume.origin[k] + static_cast<float>(volume.size[k]) * volume.cell;
            if (within)
                seen.push_back(volume.Sample(above, {0.0f, 1.0f, 0.0f}));
        }
        if (!seen.empty())
        {
            const auto middle = seen.begin() + static_cast<std::ptrdiff_t>(seen.size() / 2);
            std::nth_element(seen.begin(), middle, seen.end());
            const float lift = std::clamp(kAoOpenSurface / std::max(*middle, 0.05f), 1.0f, kAoWalkLift);
            if (lift > 1.0f)
                for (uint8_t &value : volume.visibility)
                    value = static_cast<uint8_t>(std::min(255.0f, std::round(static_cast<float>(value) * lift)));
        }
    }

    // Cells inside geometry take the mean of their open neighbours (two rings), so the trilinear filter next to a wall
    // reads the open side's value instead of a dark solid. Deeper cells are never sampled and keep 255.
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<uint8_t> next = volume.visibility;
        std::vector<uint8_t> next_inside = inside;
        for (int z = 0; z < cells.size[2]; ++z)
            for (int y = 0; y < cells.size[1]; ++y)
                for (int x = 0; x < cells.size[0]; ++x)
                {
                    const size_t index = cells.Index(x, y, z);
                    if (!inside[index])
                        continue;
                    int sum[kAoLobes] = {}, n = 0;
                    for (int dz = -1; dz <= 1; ++dz)
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                                if (cells.Inside(x + dx, y + dy, z + dz) && !inside[cells.Index(x + dx, y + dy, z + dz)])
                                {
                                    for (int k = 0; k < kAoLobes; ++k)
                                        sum[k] += volume.visibility[cells.Index(x + dx, y + dy, z + dz) * kAoLobes + k];
                                    ++n;
                                }
                    if (n > 0)
                    {
                        for (int k = 0; k < kAoLobes; ++k)
                            next[index * kAoLobes + k] = static_cast<uint8_t>((sum[k] + n / 2) / n);
                        next_inside[index] = 0;
                    }
                }
        volume.visibility.swap(next);
        inside.swap(next_inside);
    }
    return volume;
}

std::vector<uint8_t> EncodeAoVolume(const AoVolume &volume, uint64_t key)
{
    std::vector<uint8_t> out;
    out.insert(out.end(), kMagic, kMagic + 4);
    Put(out, kAoBakeVersion);
    Put(out, key);
    for (int i = 0; i < 3; ++i)
        Put(out, static_cast<int32_t>(volume.size[i]));
    for (int i = 0; i < 3; ++i)
        Put(out, volume.origin[i]);
    Put(out, volume.cell);
    out.insert(out.end(), volume.visibility.begin(), volume.visibility.end());
    return out;
}

std::optional<AoVolume> DecodeAoVolume(std::span<const uint8_t> data, uint64_t key, std::string &error)
{
    size_t at = 0;
    if (data.size() < 4 || std::memcmp(data.data(), kMagic, 4) != 0)
    {
        error = "not an AO volume";
        return std::nullopt;
    }
    at = 4;
    uint32_t version = 0;
    uint64_t stored_key = 0;
    AoVolume volume;
    int32_t size[3] = {};
    bool ok = Get(data, at, version) && Get(data, at, stored_key);
    for (int i = 0; ok && i < 3; ++i)
        ok = Get(data, at, size[i]);
    for (int i = 0; ok && i < 3; ++i)
        ok = Get(data, at, volume.origin[i]);
    ok = ok && Get(data, at, volume.cell);
    if (!ok)
    {
        error = "truncated header";
        return std::nullopt;
    }
    if (version != kAoBakeVersion)
    {
        error = "bake version " + std::to_string(version) + ", expected " + std::to_string(kAoBakeVersion);
        return std::nullopt;
    }
    if (stored_key != key)
    {
        error = "made from other geometry or parameters";
        return std::nullopt;
    }
    size_t cells = 1;
    for (int i = 0; i < 3; ++i)
    {
        if (size[i] <= 0 || size[i] > 4096)
        {
            error = "invalid size";
            return std::nullopt;
        }
        volume.size[i] = size[i];
        cells *= static_cast<size_t>(size[i]);
    }
    if (data.size() - at != cells * kAoLobes || !(volume.cell > 0.0f))
    {
        error = "cell data does not match the size";
        return std::nullopt;
    }
    volume.visibility.assign(data.begin() + static_cast<std::ptrdiff_t>(at), data.end());
    return volume;
}

} // namespace storm::fx
