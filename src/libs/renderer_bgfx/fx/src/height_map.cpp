#include "storm/fx/height_map.hpp"

#include "image_ops.hpp"

#include "storm/fx/parallax_trust.hpp"
#include "storm/fx/relief_store.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace storm::fx
{
bool ReliefHasHeight(ReliefClass relief)
{
    switch (relief)
    {
    case ReliefClass::Stone:
    case ReliefClass::Wood:
    case ReliefClass::Building:
    case ReliefClass::Plaster:
    case ReliefClass::Metal:
    case ReliefClass::Prop:
    case ReliefClass::HeightMap:
        return true;
    case ReliefClass::None:
        return false;
    }
    return false;
}

float ReliefDepthTexels(ReliefClass relief)
{
    switch (relief)
    {
    case ReliefClass::Stone:
        return 3.0f;
    case ReliefClass::Building:
        return 2.0f;
    case ReliefClass::HeightMap: // the game's own detail heights, tiled small over their surface
        return 1.5f;
    case ReliefClass::Plaster:
        return 1.25f;
    case ReliefClass::Wood:
        return 1.0f;
    case ReliefClass::Prop:
        return 0.75f;
    case ReliefClass::Metal: // cannons, fittings: curved small parts, a shallow relief
        return 0.375f;
    case ReliefClass::None:
        return 0.0f;
    }
    return 0.0f;
}

void RefreshReliefDepth(NormalMap &map, ReliefClass relief)
{
    map.depth = map.material.empty() ? 0.0f : ReliefDepthTexels(relief) * static_cast<float>(map.width) /
                                               static_cast<float>(kReliefMapSize);
}

bool ReliefTakesParallax(ReliefClass relief)
{
    return relief == ReliefClass::Stone || relief == ReliefClass::Building;
}

bool AddReliefMaterial(NormalMap &map, ReliefClass relief, std::span<const float> height, std::span<const uint8_t> gloss,
                       std::span<const uint8_t> albedo)
{
    map.material.clear();
    map.trust.clear();
    map.trust_coverage = 1.0f;
    map.depth = 0.0f;
    const int w = map.width, h = map.height;
    const size_t texels = static_cast<size_t>(w) * h;
    if (map.format != NormalMapFormat::Rg8 || w < 4 || h < 4 || !ReliefHasHeight(relief) || height.size() != texels ||
        (!gloss.empty() && gloss.size() != texels) || (!albedo.empty() && albedo.size() != texels * 4))
        return false;

    // Each region against its own neighbourhood: (h - mean) / spread, the spread floored so that a plain wall's noise
    // is not blown up into relief.
    const float scale = static_cast<float>(w) / static_cast<float>(kReliefMapSize), sigma = kHeightRegionTexels * scale;
    std::vector<float> v(height.begin(), height.end());
    const std::vector<float> mean = image::Gaussian(v, w, h, sigma);
    std::vector<float> square(texels);
    for (size_t t = 0; t < texels; ++t)
        square[t] = (v[t] - mean[t]) * (v[t] - mean[t]);
    const std::vector<float> spread = image::Gaussian(square, w, h, sigma);
    constexpr float kFloor = 0.03f; // of the model's 0..1: below it a region is flat
    for (size_t t = 0; t < texels; ++t)
        v[t] = (v[t] - mean[t]) / std::sqrt(spread[t] + kFloor * kFloor);
    std::vector<float> sorted = v;
    const auto at = [&](double share) {
        const size_t i = std::min(texels - 1, static_cast<size_t>(share * static_cast<double>(texels - 1)));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(i), sorted.end());
        return sorted[i];
    };
    const float low = at(0.02), high = at(0.98);
    if (!(high - low > 1e-3f))
        return false;

    // RG8 mips: each the box average of the one above (the depth stays in texels of the top mip).
    std::vector<float> rh(texels), rg(texels);
    for (size_t t = 0; t < texels; ++t)
    {
        rh[t] = std::clamp((v[t] - low) / (high - low), 0.0f, 1.0f);
        rg[t] = gloss.empty() ? 0.0f : gloss[t] / 255.0f;
    }
    // Where the height cannot be trusted (windows, glass: parallax_trust.hpp) it goes to the top, by the trust: the
    // parallax shifts nothing there. A texture with almost no trusted texel keeps its height whole (the cavity reads it)
    // and gets no parallax at all (kParallaxMinCoverage, the draw).
    if (!albedo.empty())
    {
        const std::vector<float> trust =
            ParallaxTrust(albedo, rh, w, h, relief == ReliefClass::Stone ? kTrustMasonry : kTrustStrict);
        map.trust_coverage = TrustCoverage(trust);
        map.trust = TrustThumbnail(trust, w, h);
        if (map.trust_coverage >= kParallaxMinCoverage)
            for (size_t t = 0; t < texels; ++t)
                rh[t] = 1.0f - trust[t] * (1.0f - rh[t]);
    }
    map.material.reserve(map.rg.size());
    int lw = w, lh = h;
    for (int mip = 0; mip < map.mip_count; ++mip)
    {
        for (size_t t = 0; t < rh.size(); ++t)
        {
            map.material.push_back(static_cast<uint8_t>(std::lround(rh[t] * 255.0f)));
            map.material.push_back(static_cast<uint8_t>(std::lround(rg[t] * 255.0f)));
        }
        if (mip + 1 == map.mip_count)
            break;
        const int nw = std::max(lw / 2, 1), nh = std::max(lh / 2, 1), sx = lw > 1 ? 2 : 1, sy = lh > 1 ? 2 : 1;
        std::vector<float> nh_(static_cast<size_t>(nw) * nh), ng(nh_.size());
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
            {
                float a = 0.0f, b = 0.0f;
                for (int dy = 0; dy < sy; ++dy)
                    for (int dx = 0; dx < sx; ++dx)
                    {
                        const size_t i = static_cast<size_t>(y * sy + dy) * lw + x * sx + dx;
                        a += rh[i];
                        b += rg[i];
                    }
                nh_[static_cast<size_t>(y) * nw + x] = a / static_cast<float>(sx * sy);
                ng[static_cast<size_t>(y) * nw + x] = b / static_cast<float>(sx * sy);
            }
        rh = std::move(nh_);
        rg = std::move(ng);
        lw = nw;
        lh = nh;
    }
    RefreshReliefDepth(map, relief);
    return true;
}

std::vector<float> GreyHeights(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return {};
    std::vector<float> v(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = rgba[i * 4] / 255.0f;
    int w = width, h = height;
    while (w > map_width || h > map_height)
    {
        const int nw = w > map_width ? std::max(w / 2, 1) : w, nh = h > map_height ? std::max(h / 2, 1) : h;
        const int sx = w / nw, sy = h / nh;
        std::vector<float> out(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
            {
                float sum = 0.0f;
                for (int dy = 0; dy < sy; ++dy)
                    for (int dx = 0; dx < sx; ++dx)
                        sum += v[static_cast<size_t>(y * sy + dy) * w + x * sx + dx];
                out[static_cast<size_t>(y) * nw + x] = sum / static_cast<float>(sx * sy);
            }
        v = std::move(out);
        w = nw;
        h = nh;
    }
    return w == map_width && h == map_height ? v : std::vector<float>{};
}

} // namespace storm::fx
