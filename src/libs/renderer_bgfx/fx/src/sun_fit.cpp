#include "storm/fx/sun_fit.hpp"

#include "storm/fx/log.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
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

float Dot(const Vec3 &a, const Vec3 &b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 Direction(float azimuth_deg, float elevation_deg)
{
    const float a = azimuth_deg * 0.017453293f, e = elevation_deg * 0.017453293f;
    return {std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
}

} // namespace

Occluders::Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices,
                     std::span<const std::array<float, 2>> uvs, std::vector<Cut> cut, std::optional<Box> within)
    : Occluders(positions, indices, within)
{
    if (uvs.size() == positions.size() && cut.size() == triangles_)
    {
        uvs_.assign(uvs.begin(), uvs.end());
        cut_ = std::move(cut);
    }
}

Box BoundsOf(std::span<const Vec3> points, float margin)
{
    Box b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (const Vec3 &p : points)
        for (int k = 0; k < 3; ++k)
        {
            b.lo[k] = std::min(b.lo[k], p[k] - margin);
            b.hi[k] = std::max(b.hi[k], p[k] + margin);
        }
    return b;
}

bool Occluders::Covers(const Vec3 &point) const
{
    for (int k = 0; k < 3; ++k)
        if (!(point[k] >= lo_[k] && point[k] <= hi_[k]))
            return false;
    return triangles_ > 0;
}

Occluders::Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices, std::optional<Box> within)
    : positions_(positions.begin(), positions.end()), indices_(indices.begin(), indices.end())
{
    triangles_ = indices_.size() / 3;
    if (triangles_ == 0)
        return;
    // Triangles wholly outside `within` stay in the arrays (the triangle numbers of `cut` keep their meaning) but go
    // in no cell: no ray tests them.
    std::vector<uint8_t> kept(triangles_, 1);
    auto triangle_box = [&](size_t t) {
        Box b{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
            {
                b.lo[k] = std::min(b.lo[k], positions_[indices_[3 * t + j]][k]);
                b.hi[k] = std::max(b.hi[k], positions_[indices_[3 * t + j]][k]);
            }
        return b;
    };
    lo_ = {1e30f, 1e30f, 1e30f};
    hi_ = {-1e30f, -1e30f, -1e30f};
    excluded_bounds_ = {{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (size_t t = 0; t < triangles_; ++t)
    {
        const Box tri_box = triangle_box(t);
        bool inside = true;
        if (within)
            for (int k = 0; k < 3; ++k)
                inside = inside && tri_box.hi[k] >= within->lo[k] && tri_box.lo[k] <= within->hi[k];
        Vec3 &l = inside ? lo_ : excluded_bounds_.lo;
        Vec3 &h = inside ? hi_ : excluded_bounds_.hi;
        for (int k = 0; k < 3; ++k)
        {
            l[k] = std::min(l[k], tri_box.lo[k]);
            h[k] = std::max(h[k], tri_box.hi[k]);
        }
        if (!inside)
        {
            kept[t] = 0;
            ++excluded_;
        }
    }
    if (!excluded_)
        excluded_bounds_ = {};
    if (excluded_ == triangles_)
    {
        triangles_ = 0; // nothing a ray from `within` can reach
        return;
    }
    if (within)
        for (int k = 0; k < 3; ++k)
        {
            lo_[k] = std::max(lo_[k], within->lo[k]);
            hi_[k] = std::min(hi_[k], within->hi[k]);
        }
    // The grid's cells over the box with a cell of margin: grown while there are more than kMaxOccluderCells (each
    // costs 12 bytes while it is built), which only changes how many triangles a ray step tests, not what it hits.
    auto cells_for = [&](float cell) {
        double total = 1.0;
        for (int k = 0; k < 3; ++k)
            total *= std::floor((hi_[k] - lo_[k]) / cell + 2.0) + 1.0;
        return total;
    };
    const float asked = cell_;
    const double asked_cells = cells_for(asked);
    while (cells_for(cell_) > static_cast<double>(kMaxOccluderCells))
        cell_ *= 1.25f;
    if (cell_ != asked)
        Log(LogLevel::Error,
            "occluders: %zu triangles over %.0f x %.0f x %.0f m would take %.0f cells of %.2f m, more than the cap of "
            "%zu: cells of %.2f m instead (the same hits, slower rays)",
            triangles_, hi_[0] - lo_[0], hi_[1] - lo_[1], hi_[2] - lo_[2], asked_cells, asked, kMaxOccluderCells, cell_);
    for (int k = 0; k < 3; ++k)
    {
        lo_[k] -= cell_;
        hi_[k] += cell_;
        n_[k] = std::max(1, static_cast<int>((hi_[k] - lo_[k]) / cell_) + 1);
    }
    const size_t cells = static_cast<size_t>(n_[0]) * n_[1] * n_[2];
    auto visit = [&](size_t t, auto &&f) {
        const Vec3 &a = positions_[indices_[3 * t]], &b = positions_[indices_[3 * t + 1]], &c = positions_[indices_[3 * t + 2]];
        int c0[3], c1[3];
        for (int k = 0; k < 3; ++k)
        {
            c0[k] = std::clamp(static_cast<int>((std::min({a[k], b[k], c[k]}) - lo_[k]) / cell_), 0, n_[k] - 1);
            c1[k] = std::clamp(static_cast<int>((std::max({a[k], b[k], c[k]}) - lo_[k]) / cell_), 0, n_[k] - 1);
        }
        for (int z = c0[2]; z <= c1[2]; ++z)
            for (int y = c0[1]; y <= c1[1]; ++y)
                for (int x = c0[0]; x <= c1[0]; ++x)
                    f((static_cast<size_t>(z) * n_[1] + y) * n_[0] + x);
    };
    std::vector<uint32_t> counts(cells + 1, 0);
    for (size_t t = 0; t < triangles_; ++t)
        if (kept[t])
            visit(t, [&](size_t c) { ++counts[c + 1]; });
    for (size_t c = 1; c < counts.size(); ++c)
        counts[c] += counts[c - 1];
    start_ = counts;
    items_.resize(counts.back());
    std::vector<uint32_t> fill(counts.begin(), counts.end() - 1);
    for (size_t t = 0; t < triangles_; ++t)
        if (kept[t])
            visit(t, [&](size_t c) { items_[fill[c]++] = static_cast<uint32_t>(t); });
}

bool Occluders::Hits(uint32_t tri, const Vec3 &origin, const Vec3 &dir, float reach, Hit &hit) const
{
    const Vec3 &a = positions_[indices_[3 * tri]];
    const Vec3 e1 = Sub(positions_[indices_[3 * tri + 1]], a), e2 = Sub(positions_[indices_[3 * tri + 2]], a);
    const Vec3 p = Cross(dir, e2);
    const float det = Dot(e1, p);
    if (std::fabs(det) < 1e-9f)
        return false;
    const float inv = 1.0f / det;
    const Vec3 s = Sub(origin, a);
    const float u = Dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f)
        return false;
    const Vec3 q = Cross(s, e1);
    const float v = Dot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    const float t = Dot(e2, q) * inv;
    if (t <= 1e-3f || t >= reach)
        return false;
    if (!cut_.empty() && cut_[tri].mask)
    {
        // Through the hole the texture's alpha cuts there?
        const std::array<float, 2> &t0 = uvs_[indices_[3 * tri]], &t1 = uvs_[indices_[3 * tri + 1]],
                                   &t2 = uvs_[indices_[3 * tri + 2]];
        const float w = 1.0f - u - v;
        if (cut_[tri].mask->At(w * t0[0] + u * t1[0] + v * t2[0], w * t0[1] + u * t1[1] + v * t2[1]) <= cut_[tri].reference)
            return false; // through the hole
    }
    hit = {t, tri, u, v};
    return true;
}

template <bool Closest>
bool Occluders::March(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch, Hit &best) const
{
    if (triangles_ == 0)
        return false;
    if (scratch.stamp.size() != triangles_)
    {
        scratch.stamp.assign(triangles_, 0u);
        scratch.ray = 0;
    }
    if (++scratch.ray == 0)
    {
        std::fill(scratch.stamp.begin(), scratch.stamp.end(), 0u);
        scratch.ray = 1;
    }
    int cell[3], step[3];
    float next[3], delta[3];
    for (int k = 0; k < 3; ++k)
    {
        cell[k] = static_cast<int>(std::floor((origin[k] - lo_[k]) / cell_));
        step[k] = dir[k] > 0.0f ? 1 : -1;
        const float bound = lo_[k] + static_cast<float>(cell[k] + (dir[k] > 0.0f ? 1 : 0)) * cell_;
        next[k] = std::fabs(dir[k]) > 1e-9f ? (bound - origin[k]) / dir[k] : 1e30f;
        delta[k] = std::fabs(dir[k]) > 1e-9f ? cell_ / std::fabs(dir[k]) : 1e30f;
    }
    bool found = false;
    float t = 0.0f;
    while (t < reach)
    {
        if (cell[0] < 0 || cell[1] < 0 || cell[2] < 0 || cell[0] >= n_[0] || cell[1] >= n_[1] || cell[2] >= n_[2])
            return found;
        const size_t c = (static_cast<size_t>(cell[2]) * n_[1] + cell[1]) * n_[0] + cell[0];
        for (uint32_t i = start_[c]; i < start_[c + 1]; ++i)
        {
            const uint32_t tri = items_[i];
            if (scratch.stamp[tri] == scratch.ray)
                continue;
            scratch.stamp[tri] = scratch.ray;
            Hit hit;
            if (!Hits(tri, origin, dir, Closest && found ? best.t : reach, hit))
                continue;
            best = hit;
            found = true;
            if (!Closest)
                return true;
        }
        const int k = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        // A hit before this cell's far side is nearer than anything a later cell holds.
        if (found && best.t <= next[k])
            return true;
        t = next[k];
        next[k] += delta[k];
        cell[k] += step[k];
    }
    return found;
}

bool Occluders::Occluded(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch) const
{
    Hit hit;
    return March<false>(origin, dir, reach, scratch, hit);
}

std::optional<Occluders::Hit> Occluders::Closest(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch) const
{
    Hit hit;
    if (!March<true>(origin, dir, reach, scratch, hit))
        return std::nullopt;
    return hit;
}

namespace
{

SunFit Rate(const Occluders &occluders, std::span<const SunFitSample> samples, const Vec3 &dir, float reach,
            Occluders::Scratch &scratch)
{
    SunFit fit;
    fit.dir = dir;
    if (samples.size() < 20)
        return fit;
    double mean = 0.0;
    for (const SunFitSample &s : samples)
        mean += s.luminance;
    mean /= static_cast<double>(samples.size());
    double variance = 0.0;
    for (const SunFitSample &s : samples)
        variance += (s.luminance - mean) * (s.luminance - mean);
    variance /= static_cast<double>(samples.size());
    if (variance <= 1e-8)
        return fit;
    double in_shadow = 0.0, sum_shadow = 0.0, sum_sun = 0.0;
    for (const SunFitSample &s : samples)
    {
        // A few centimetres above the ground: the ground itself is no occluder.
        if (occluders.Occluded({s.position[0], s.position[1] + 0.05f, s.position[2]}, dir, reach, scratch))
        {
            in_shadow += 1.0;
            sum_shadow += s.luminance;
        }
        else
            sum_sun += s.luminance;
    }
    const double n = static_cast<double>(samples.size()), in_sun = n - in_shadow;
    fit.shadowed = static_cast<float>(in_shadow / n);
    if (in_shadow < 5.0 || in_sun < 5.0)
        return fit;
    fit.r = static_cast<float>((sum_sun / in_sun - sum_shadow / in_shadow) * std::sqrt(in_shadow * in_sun) / (n * std::sqrt(variance)));
    fit.valid = fit.r >= kSunFitMinR;
    return fit;
}

struct Candidate
{
    float az, el;
};

// The best of `candidates`, rated on `threads` threads.
std::pair<SunFit, Candidate> Best(const Occluders &occluders, std::span<const SunFitSample> samples, float reach,
                                  const std::vector<Candidate> &candidates, unsigned threads)
{
    std::vector<SunFit> rated(candidates.size());
    ParallelRanges(
        candidates.size(), threads,
        [&](size_t first, size_t last) {
            Occluders::Scratch scratch;
            for (size_t i = first; i < last; ++i)
                rated[i] = Rate(occluders, samples, Direction(candidates[i].az, candidates[i].el), reach, scratch);
        },
        1);
    // The first of the best (as one thread would find it).
    std::pair<SunFit, Candidate> best{SunFit{}, Candidate{0.0f, 45.0f}};
    for (size_t i = 0; i < candidates.size(); ++i)
        if (rated[i].r > best.first.r)
            best = {rated[i], candidates[i]};
    return best;
}

} // namespace

// The samples open to the sky: ground under a roof (inside a house, under an arcade) is dark for every direction
// and would favour low suns that shade most of the ground.
std::vector<SunFitSample> OpenSky(const Occluders &occluders, std::span<const SunFitSample> samples, float reach)
{
    Occluders::Scratch scratch;
    std::vector<SunFitSample> open;
    open.reserve(samples.size());
    for (const SunFitSample &s : samples)
        if (!occluders.Occluded({s.position[0], s.position[1] + 0.05f, s.position[2]}, {0.0f, 1.0f, 0.0f}, reach, scratch))
            open.push_back(s);
    return open;
}

SunFit FitBakedSun(const Occluders &occluders, std::span<const SunFitSample> all, float reach, unsigned threads)
{
    const std::vector<SunFitSample> open = OpenSky(occluders, all, reach);
    const std::span<const SunFitSample> samples(open);
    // Coarse: every 8° of azimuth, 6° of elevation from 8° up to 86°; then 1° steps around the best.
    std::vector<Candidate> coarse;
    for (float el = 8.0f; el <= 86.0f; el += 6.0f)
        for (float az = 0.0f; az < 360.0f; az += 8.0f)
            coarse.push_back({az, el});
    const auto [first, at] = Best(occluders, samples, reach, coarse, threads);
    std::vector<Candidate> fine;
    for (float el = at.el - 5.0f; el <= at.el + 5.0f; el += 1.0f)
        for (float az = at.az - 7.0f; az <= at.az + 7.0f; az += 1.0f)
            if (el >= 3.0f && el <= 89.0f)
                fine.push_back({az, el});
    SunFit best = Best(occluders, samples, reach, fine, threads).first;
    if (first.r > best.r)
        best = first;
    best.valid = best.r >= kSunFitMinR;
    return best;
}

} // namespace storm::fx
