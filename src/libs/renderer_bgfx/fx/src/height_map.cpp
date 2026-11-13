#include "storm/fx/height_map.hpp"

#include "storm/fx/relief_store.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace storm::fx
{
namespace
{
// One wrapping box filter of width 2r+1 along x (horizontal) or y, with a running sum.
std::vector<float> Box(const std::vector<float> &in, int w, int h, int r, bool horizontal)
{
    std::vector<float> out(in.size());
    const int length = horizontal ? w : h, lines = horizontal ? h : w;
    const size_t step = horizontal ? 1 : static_cast<size_t>(w), line_step = horizontal ? static_cast<size_t>(w) : 1;
    const float scale = 1.0f / static_cast<float>(2 * r + 1);
    const auto at = [length](int i) { return ((i % length) + length) % length; };
    for (int line = 0; line < lines; ++line)
    {
        const float *src = in.data() + line * line_step;
        float *dst = out.data() + line * line_step;
        float sum = 0.0f;
        for (int i = -r; i <= r; ++i)
            sum += src[at(i) * step];
        for (int i = 0; i < length; ++i)
        {
            dst[i * step] = sum * scale;
            sum += src[at(i + r + 1) * step] - src[at(i - r) * step];
        }
    }
    return out;
}

// A Gaussian of `sigma` texels as three box passes per axis (their variance adds up: 3 (w² - 1) / 12 = sigma²).
std::vector<float> Gaussian(std::vector<float> v, int w, int h, float sigma)
{
    const int r = std::max(1, static_cast<int>(std::lround((std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f)));
    for (int pass = 0; pass < 3; ++pass)
        v = Box(Box(v, w, h, r, true), w, h, r, false);
    return v;
}
} // namespace

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
        return 12.0f;
    case ReliefClass::Building:
        return 8.0f;
    case ReliefClass::HeightMap: // the game's own detail heights, tiled small over their surface
        return 6.0f;
    case ReliefClass::Plaster:
        return 5.0f;
    case ReliefClass::Wood:
        return 4.0f;
    case ReliefClass::Prop:
        return 3.0f;
    case ReliefClass::Metal: // cannons, fittings: curved small parts, a shallow relief
        return 1.5f;
    case ReliefClass::None:
        return 0.0f;
    }
    return 0.0f;
}

bool AddReliefMaterial(NormalMap &map, ReliefClass relief, std::span<const float> height, std::span<const uint8_t> gloss)
{
    map.material.clear();
    map.depth = 0.0f;
    const int w = map.width, h = map.height;
    const size_t texels = static_cast<size_t>(w) * h;
    if (map.format != NormalMapFormat::Rg8 || w < 4 || h < 4 || !ReliefHasHeight(relief) || height.size() != texels ||
        (!gloss.empty() && gloss.size() != texels))
        return false;

    // Each region against its own neighbourhood: (h - mean) / spread, the spread floored so that a plain wall's noise
    // is not blown up into relief.
    const float scale = static_cast<float>(w) / static_cast<float>(kReliefMapSize), sigma = kHeightRegionTexels * scale;
    std::vector<float> v(height.begin(), height.end());
    const std::vector<float> mean = Gaussian(v, w, h, sigma);
    std::vector<float> square(texels);
    for (size_t t = 0; t < texels; ++t)
        square[t] = (v[t] - mean[t]) * (v[t] - mean[t]);
    const std::vector<float> spread = Gaussian(square, w, h, sigma);
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
    map.depth = ReliefDepthTexels(relief) * scale;
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
