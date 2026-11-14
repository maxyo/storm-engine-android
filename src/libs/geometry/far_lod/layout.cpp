// The layout check and the depth ties of far_lod.hpp (from the mesh order work of feat/meshopt).
#include "far_lod.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace storm::geometry::far_lod
{
namespace
{
// Objects that draw nothing take no part: their ranges are not checked.
bool draws(const MeshObject &object)
{
    return object.triangle_count > 0;
}

std::vector<uint32_t> localIndices(const MeshObject &object, std::span<const uint16_t> triangles)
{
    const auto first = triangles.begin() + 3 * static_cast<size_t>(object.first_triangle);
    return {first, first + 3 * static_cast<size_t>(object.triangle_count)};
}

// Bins of tie candidates up to this size test every pair.
constexpr size_t kSmallBin = 32;

// Overlaps thinner than this (model units) are triangles touching along an edge, not covering the same pixels.
constexpr double kTouch = 1e-3;

struct Plane
{
    std::array<std::array<double, 3>, 3> corners;
    std::array<double, 3> normal;
    double distance = 0;
    bool valid = false;
};

Plane plane(const MeshObject &object, const MeshBuffer &buffer, const uint16_t *indices)
{
    Plane result;
    for (int k = 0; k < 3; ++k)
    {
        float p[3];
        std::memcpy(p, buffer.vertices + size_t(buffer.stride) * (object.first_vertex + indices[k]), sizeof(p));
        result.corners[k] = {p[0], p[1], p[2]};
    }
    const auto &a = result.corners[0], &b = result.corners[1], &c = result.corners[2];
    const std::array<double, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    std::array<double, 3> n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (length < 1e-12) // no area: covers no pixel
        return result;
    for (double &x : n)
        x /= length;
    result.normal = n;
    result.distance = n[0] * a[0] + n[1] * a[1] + n[2] * a[2];
    result.valid = true;
    return result;
}

int dominantAxis(const std::array<double, 3> &n)
{
    const double x = std::abs(n[0]), y = std::abs(n[1]), z = std::abs(n[2]);
    return x >= y && x >= z ? 0 : y >= z ? 1 : 2;
}

// The two triangles, seen along `axis`, share area (separating axis test on their edges).
bool overlapping(const Plane &a, const Plane &b, int axis)
{
    const int i = (axis + 1) % 3, j = (axis + 2) % 3;
    for (const Plane *edges : {&a, &b})
        for (int e = 0; e < 3; ++e)
        {
            const auto &p = edges->corners[e], &q = edges->corners[(e + 1) % 3];
            const double nx = q[j] - p[j], ny = p[i] - q[i];
            const double length = std::sqrt(nx * nx + ny * ny);
            if (length < 1e-12)
                continue;
            double alo = INFINITY, ahi = -INFINITY, blo = INFINITY, bhi = -INFINITY;
            for (const auto &x : a.corners)
            {
                const double d = (x[i] * nx + x[j] * ny) / length;
                alo = std::min(alo, d), ahi = std::max(ahi, d);
            }
            for (const auto &x : b.corners)
            {
                const double d = (x[i] * nx + x[j] * ny) / length;
                blo = std::min(blo, d), bhi = std::max(bhi, d);
            }
            if (ahi <= blo + kTouch || bhi <= alo + kTouch)
                return false;
        }
    return true;
}

bool tie(const Plane &a, const Plane &b)
{
    for (const double sign : {1.0, -1.0})
        if (std::abs(a.normal[0] - sign * b.normal[0]) < kTieNormal &&
            std::abs(a.normal[1] - sign * b.normal[1]) < kTieNormal &&
            std::abs(a.normal[2] - sign * b.normal[2]) < kTieNormal &&
            std::abs(a.distance - sign * b.distance) < kTieDistance)
            return overlapping(a, b, dominantAxis(a.normal));
    return false;
}

struct KeyHash
{
    size_t operator()(const std::array<int64_t, 4> &key) const
    {
        uint64_t h = 1469598103934665603ull;
        for (const int64_t k : key)
            h = (h ^ static_cast<uint64_t>(k)) * 1099511628211ull;
        return h;
    }
};
} // namespace

std::optional<std::string> CheckMeshLayout(std::span<const MeshObject> objects,
                                           std::span<const uint32_t> buffer_vertex_counts,
                                           std::span<const uint16_t> triangles)
{
    if (triangles.size() % 3 != 0)
        return "the triangle array is not whole triangles";
    const uint64_t triangle_count = triangles.size() / 3;
    std::vector<std::pair<uint64_t, uint64_t>> triangle_ranges;
    std::vector<std::vector<std::pair<uint64_t, uint64_t>>> vertex_ranges(buffer_vertex_counts.size());
    for (size_t o = 0; o < objects.size(); ++o)
    {
        const MeshObject &object = objects[o];
        if (!draws(object))
            continue;
        const std::string name = "object " + std::to_string(o);
        if (object.vertex_buffer >= buffer_vertex_counts.size())
            return name + " names vertex buffer " + std::to_string(object.vertex_buffer) + " of " +
                   std::to_string(buffer_vertex_counts.size());
        if (uint64_t(object.first_vertex) + object.vertex_count > buffer_vertex_counts[object.vertex_buffer])
            return name + " has vertices past the end of its buffer";
        if (uint64_t(object.first_triangle) + object.triangle_count > triangle_count)
            return name + " has triangles past the end of the triangle array";
        for (const uint32_t index : localIndices(object, triangles))
            if (index >= object.vertex_count)
                return name + " has an index outside its vertex range";
        triangle_ranges.emplace_back(object.first_triangle, uint64_t(object.first_triangle) + object.triangle_count);
        if (object.vertex_count > 0)
            vertex_ranges[object.vertex_buffer].emplace_back(object.first_vertex,
                                                             uint64_t(object.first_vertex) + object.vertex_count);
    }
    const auto overlapping = [](std::vector<std::pair<uint64_t, uint64_t>> &ranges) {
        std::ranges::sort(ranges);
        return std::ranges::adjacent_find(ranges, [](const auto &a, const auto &b) { return b.first < a.second; }) !=
               ranges.end();
    };
    if (overlapping(triangle_ranges))
        return "two objects share triangles";
    for (auto &ranges : vertex_ranges)
        if (overlapping(ranges))
            return "two objects share vertices";
    return std::nullopt;
}

namespace
{
// Pairs (i < j) of `planes` that tie in the depth test.
std::vector<std::pair<uint32_t, uint32_t>> tiePairs(const std::vector<Plane> &planes)
{
    // Candidates share a bin of (normal, distance) with cells twice the tolerances: a triangle goes into every cell its
    // tolerance box touches, under its normal and under the reversed one, so no tying pair is missed.
    constexpr double kNormalCell = 2 * kTieNormal, kDistanceCell = 2 * kTieDistance;
    // (cell, triangle) entries sorted by cell: a run of one cell is a bin (one array and a sort, not a map of vectors:
    // a large flat model has hundreds of thousands of bins).
    std::vector<std::pair<std::array<int64_t, 4>, uint32_t>> entries;
    entries.reserve(planes.size() * 4);
    for (uint32_t t = 0; t < planes.size(); ++t)
    {
        const Plane &p = planes[t];
        if (!p.valid)
            continue;
        for (const double sign : {1.0, -1.0})
        {
            const std::array<double, 4> value{sign * p.normal[0], sign * p.normal[1], sign * p.normal[2], sign * p.distance};
            const std::array<double, 4> cell{kNormalCell, kNormalCell, kNormalCell, kDistanceCell};
            const std::array<double, 4> tolerance{kTieNormal, kTieNormal, kTieNormal, kTieDistance};
            std::array<int64_t, 4> lo, hi;
            for (int k = 0; k < 4; ++k)
            {
                lo[k] = static_cast<int64_t>(std::floor((value[k] - tolerance[k]) / cell[k]));
                hi[k] = static_cast<int64_t>(std::floor((value[k] + tolerance[k]) / cell[k]));
            }
            for (int64_t a = lo[0]; a <= hi[0]; ++a)
                for (int64_t b = lo[1]; b <= hi[1]; ++b)
                    for (int64_t c = lo[2]; c <= hi[2]; ++c)
                        for (int64_t d = lo[3]; d <= hi[3]; ++d)
                            entries.push_back({{a, b, c, d}, t});
        }
    }
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    struct Box
    {
        double lo[2], hi[2];
    };
    std::vector<Box> boxes;
    std::vector<double> extents;
    std::unordered_map<std::array<int64_t, 4>, std::vector<uint32_t>, KeyHash> grid;
    std::ranges::sort(entries);
    std::vector<uint32_t> members;
    for (size_t run = 0; run < entries.size();)
    {
        size_t end = run + 1;
        while (end < entries.size() && entries[end].first == entries[run].first)
            ++end;
        members.clear();
        for (size_t e = run; e < end; ++e)
            members.push_back(entries[e].second);
        run = end;
        if (members.size() < 2)
            continue;
        if (members.size() <= kSmallBin)
        {
            // Few candidates: every pair (a grid costs more than it saves).
            for (size_t a = 0; a < members.size(); ++a)
                for (size_t b = a + 1; b < members.size(); ++b)
                {
                    const uint32_t s = members[a], t = members[b];
                    if (s != t && tie(planes[s], planes[t]))
                        pairs.emplace_back(std::min(s, t), std::max(s, t));
                }
            continue;
        }
        // A grid over the bin's triangles seen along one axis: a pair is tested in the cell holding the lower corner of
        // the overlap of their boxes.
        const int axis = dominantAxis(planes[members.front()].normal);
        const int i = (axis + 1) % 3, j = (axis + 2) % 3;
        boxes.resize(members.size());
        extents.resize(members.size());
        for (size_t m = 0; m < members.size(); ++m)
        {
            const Plane &p = planes[members[m]];
            boxes[m] = {{std::min({p.corners[0][i], p.corners[1][i], p.corners[2][i]}),
                         std::min({p.corners[0][j], p.corners[1][j], p.corners[2][j]})},
                        {std::max({p.corners[0][i], p.corners[1][i], p.corners[2][i]}),
                         std::max({p.corners[0][j], p.corners[1][j], p.corners[2][j]})}};
            extents[m] = std::max(boxes[m].hi[0] - boxes[m].lo[0], boxes[m].hi[1] - boxes[m].lo[1]);
        }
        // The median size, not the mean: a few huge triangles (the sea floor's) in a bin of small ones would make one
        // cell of everything and the test quadratic.
        std::nth_element(extents.begin(), extents.begin() + extents.size() / 2, extents.end());
        const double cell = std::max(extents[extents.size() / 2], 1e-3);
        grid.clear();
        for (uint32_t m = 0; m < members.size(); ++m)
            for (auto x = static_cast<int64_t>(std::floor(boxes[m].lo[0] / cell));
                 x <= static_cast<int64_t>(std::floor(boxes[m].hi[0] / cell)); ++x)
                for (auto y = static_cast<int64_t>(std::floor(boxes[m].lo[1] / cell));
                     y <= static_cast<int64_t>(std::floor(boxes[m].hi[1] / cell)); ++y)
                    grid[{x, y, 0, 0}].push_back(m);
        for (const auto &[at, inside] : grid)
            for (size_t a = 0; a < inside.size(); ++a)
                for (size_t b = a + 1; b < inside.size(); ++b)
                {
                    const Box &p = boxes[inside[a]], &q = boxes[inside[b]];
                    const double x = std::max(p.lo[0], q.lo[0]), y = std::max(p.lo[1], q.lo[1]);
                    if (x > std::min(p.hi[0], q.hi[0]) || y > std::min(p.hi[1], q.hi[1]))
                        continue;
                    if (static_cast<int64_t>(std::floor(x / cell)) != at[0] ||
                        static_cast<int64_t>(std::floor(y / cell)) != at[1])
                        continue;
                    const uint32_t s = members[inside[a]], t = members[inside[b]];
                    if (s != t && tie(planes[s], planes[t]))
                        pairs.emplace_back(std::min(s, t), std::max(s, t));
                }
    }
    std::ranges::sort(pairs);
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    return pairs;
}
} // namespace

std::vector<std::pair<uint32_t, uint32_t>> DepthTiePairs(const MeshObject &object, const MeshBuffer &buffer,
                                                         std::span<const uint16_t> triangles)
{
    std::vector<Plane> planes(object.triangle_count);
    for (uint32_t t = 0; t < object.triangle_count; ++t)
        planes[t] = plane(object, buffer, &triangles[3 * size_t(object.first_triangle + t)]);
    return tiePairs(planes);
}

std::vector<std::pair<TriangleRef, TriangleRef>> ModelDepthTies(std::span<const MeshObject> objects,
                                                                std::span<const MeshBuffer> buffers,
                                                                std::span<const uint16_t> triangles)
{
    std::vector<Plane> planes;
    std::vector<TriangleRef> refs;
    for (uint32_t o = 0; o < objects.size(); ++o)
        for (uint32_t t = 0; t < objects[o].triangle_count; ++t)
        {
            planes.push_back(plane(objects[o], buffers[objects[o].vertex_buffer],
                                   &triangles[3 * size_t(objects[o].first_triangle + t)]));
            refs.push_back({o, t});
        }
    std::vector<std::pair<TriangleRef, TriangleRef>> result;
    for (const auto &[a, b] : tiePairs(planes))
        result.push_back({refs[a], refs[b]});
    return result;
}

} // namespace storm::geometry::far_lod
