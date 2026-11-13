#include "storm/fx/sun_fit.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
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
                     std::span<const std::array<float, 2>> uvs, std::vector<Cut> cut, float cell)
    : Occluders(positions, indices, cell)
{
    if (uvs.size() == positions.size() && cut.size() == triangles_)
    {
        uvs_.assign(uvs.begin(), uvs.end());
        cut_ = std::move(cut);
    }
}

Occluders::Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices, float cell)
    : positions_(positions.begin(), positions.end()), indices_(indices.begin(), indices.end()), cell_(cell)
{
    triangles_ = indices_.size() / 3;
    if (triangles_ == 0)
        return;
    lo_ = {1e30f, 1e30f, 1e30f};
    hi_ = {-1e30f, -1e30f, -1e30f};
    for (const uint32_t i : indices_)
        for (int k = 0; k < 3; ++k)
        {
            lo_[k] = std::min(lo_[k], positions_[i][k]);
            hi_[k] = std::max(hi_[k], positions_[i][k]);
        }
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
        visit(t, [&](size_t c) { ++counts[c + 1]; });
    for (size_t c = 1; c < counts.size(); ++c)
        counts[c] += counts[c - 1];
    start_ = counts;
    items_.resize(counts.back());
    std::vector<uint32_t> fill(counts.begin(), counts.end() - 1);
    for (size_t t = 0; t < triangles_; ++t)
        visit(t, [&](size_t c) { items_[fill[c]++] = static_cast<uint32_t>(t); });
}

bool Occluders::Occluded(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch) const
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
    float t = 0.0f;
    while (t < reach)
    {
        if (cell[0] < 0 || cell[1] < 0 || cell[2] < 0 || cell[0] >= n_[0] || cell[1] >= n_[1] || cell[2] >= n_[2])
            return false;
        const size_t c = (static_cast<size_t>(cell[2]) * n_[1] + cell[1]) * n_[0] + cell[0];
        for (uint32_t i = start_[c]; i < start_[c + 1]; ++i)
        {
            const uint32_t tri = items_[i];
            if (scratch.stamp[tri] == scratch.ray)
                continue;
            scratch.stamp[tri] = scratch.ray;
            const Vec3 &a = positions_[indices_[3 * tri]];
            const Vec3 e1 = Sub(positions_[indices_[3 * tri + 1]], a), e2 = Sub(positions_[indices_[3 * tri + 2]], a);
            const Vec3 p = Cross(dir, e2);
            const float det = Dot(e1, p);
            if (std::fabs(det) < 1e-9f)
                continue;
            const float inv = 1.0f / det;
            const Vec3 s = Sub(origin, a);
            const float u = Dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f)
                continue;
            const Vec3 q = Cross(s, e1);
            const float v = Dot(dir, q) * inv;
            if (v < 0.0f || u + v > 1.0f)
                continue;
            const float hit = Dot(e2, q) * inv;
            if (hit <= 1e-3f || hit >= reach)
                continue;
            if (!cut_.empty() && cut_[tri].mask)
            {
                // Through the hole the texture's alpha cuts there?
                const std::array<float, 2> &t0 = uvs_[indices_[3 * tri]], &t1 = uvs_[indices_[3 * tri + 1]],
                                           &t2 = uvs_[indices_[3 * tri + 2]];
                const float w = 1.0f - u - v;
                if (cut_[tri].mask->At(w * t0[0] + u * t1[0] + v * t2[0], w * t0[1] + u * t1[1] + v * t2[1]) <=
                    cut_[tri].reference)
                    continue; // through the hole
            }
            return true;
        }
        const int k = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        t = next[k];
        next[k] += delta[k];
        cell[k] += step[k];
    }
    return false;
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
    threads = std::clamp(threads, 1u, static_cast<unsigned>(std::max<size_t>(1, candidates.size())));
    std::vector<std::pair<SunFit, Candidate>> best(threads, {SunFit{}, Candidate{0.0f, 45.0f}});
    auto work = [&](unsigned t) {
        Occluders::Scratch scratch;
        for (size_t i = t; i < candidates.size(); i += threads)
        {
            const SunFit f = Rate(occluders, samples, Direction(candidates[i].az, candidates[i].el), reach, scratch);
            if (f.r > best[t].first.r)
                best[t] = {f, candidates[i]};
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t)
        pool.emplace_back(work, t);
    work(0);
    for (std::thread &th : pool)
        th.join();
    return *std::max_element(best.begin(), best.end(), [](const auto &a, const auto &b) { return a.first.r < b.first.r; });
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
