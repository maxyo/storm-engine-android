#include "storm/fx/parallax_trust.hpp"

#include "image_ops.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
using Plane = std::vector<float>;

float Luminance(const uint8_t *p)
{
    return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
}

// A binary opening or closing with a square element of radius r (on 0/1 planes).
Plane Open(const Plane &b, int w, int h, int r)
{
    return image::Extreme(image::Extreme(b, w, h, r, false), w, h, r, true);
}
Plane Close(const Plane &b, int w, int h, int r)
{
    return image::Extreme(image::Extreme(b, w, h, r, true), w, h, r, false);
}

// The blobs of `b` (4-connected) with an area in [min_area, max_area] that fill at least `extent` of their bounding box.
Plane Blobs(const Plane &b, int w, int h, float min_area, float max_area, float extent)
{
    Plane out(b.size(), 0.0f);
    std::vector<int> label(b.size(), 0);
    std::vector<size_t> stack, members;
    int next = 0;
    for (size_t start = 0; start < b.size(); ++start)
    {
        if (b[start] < 0.5f || label[start])
            continue;
        ++next;
        members.clear();
        stack.assign(1, start);
        label[start] = next;
        int x0 = w, x1 = 0, y0 = h, y1 = 0;
        while (!stack.empty())
        {
            const size_t at = stack.back();
            stack.pop_back();
            members.push_back(at);
            const int x = static_cast<int>(at % w), y = static_cast<int>(at / w);
            x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k)
            {
                if (nx[k] < 0 || nx[k] >= w || ny[k] < 0 || ny[k] >= h)
                    continue;
                const size_t n = static_cast<size_t>(ny[k]) * w + nx[k];
                if (b[n] >= 0.5f && !label[n])
                {
                    label[n] = next;
                    stack.push_back(n);
                }
            }
        }
        const float area = static_cast<float>(members.size());
        const float box = static_cast<float>(x1 - x0 + 1) * static_cast<float>(y1 - y0 + 1);
        if (area >= min_area && area <= max_area && area / box >= extent)
            for (const size_t m : members)
                out[m] = 1.0f;
    }
    return out;
}
} // namespace

std::vector<float> ParallaxTrust(std::span<const uint8_t> rgba, std::span<const float> height, int w, int h,
                                 const TrustParams &params)
{
    const size_t n = static_cast<size_t>(w) * h;
    if (w < 4 || h < 4 || height.size() != n || rgba.size() < n * 4)
        return {};
    const float s = static_cast<float>(std::max(w, h)) / 512.0f;
    const auto R = [s](float v) { return std::max(1, static_cast<int>(std::lround(v * s))); };
    const auto Area = [s](float v) { return v * s * s; };

    Plane lum(n);
    for (size_t t = 0; t < n; ++t)
        lum[t] = Luminance(&rgba[t * 4]);
    const Plane hv(height.begin(), height.end());
    const Plane lum_mean = image::Box(image::Box(lum, w, h, std::max(1, R(3) / 2), true), w, h, std::max(1, R(3) / 2), false);
    const Plane lum_large = image::Gaussian(lum, w, h, 24.0f * s);

    // Recess.
    const int closing = R(kTrustClosingTexels);
    const Plane closed = image::Extreme(image::Extreme(hv, w, h, closing, true), w, h, closing, false);
    Plane pit(n);
    for (size_t t = 0; t < n; ++t)
        pit[t] = (closed[t] - hv[t] > params.pit_depth && lum_mean[t] / std::max(lum_large[t], 0.05f) < params.pit_dark) ? 1.0f : 0.0f;
    pit = Open(Close(pit, w, h, R(2)), w, h, R(2));
    pit = Blobs(pit, w, h, Area(params.pit_min_area), Area(8000.0f), params.pit_extent);

    // Glass.
    const int sd_radius = std::max(1, R(5) / 2);
    const Plane mean = image::Box(image::Box(lum, w, h, sd_radius, true), w, h, sd_radius, false);
    Plane squared(n);
    for (size_t t = 0; t < n; ++t)
        squared[t] = lum[t] * lum[t];
    const Plane mean_square = image::Box(image::Box(squared, w, h, sd_radius, true), w, h, sd_radius, false);
    Plane glass(n);
    for (size_t t = 0; t < n; ++t)
        glass[t] = (lum_mean[t] < params.glass_luminance &&
                    std::sqrt(std::max(mean_square[t] - mean[t] * mean[t], 0.0f)) < params.glass_flat)
                       ? 1.0f
                       : 0.0f;
    glass = Blobs(Open(glass, w, h, R(4)), w, h, Area(params.glass_min_area), 1e9f, 0.5f);

    // Grown and feathered.
    Plane bad(n);
    for (size_t t = 0; t < n; ++t)
        bad[t] = std::max(pit[t], glass[t]);
    bad = image::Extreme(bad, w, h, R(params.grow_texels), true);
    for (float &v : bad)
        v = 1.0f - v;
    Plane trust = image::Gaussian(bad, w, h, kTrustFeatherSigma * s);
    for (float &v : trust)
        v = std::clamp(v, 0.0f, 1.0f);
    return trust;
}

float TrustCoverage(std::span<const float> trust)
{
    if (trust.empty())
        return 1.0f;
    size_t trusted = 0;
    for (const float v : trust)
        trusted += v > 0.5f;
    return static_cast<float>(trusted) / static_cast<float>(trust.size());
}

std::vector<uint8_t> TrustThumbnail(std::span<const float> trust, int w, int h)
{
    const int tw = TrustThumbnailSize(w), th = TrustThumbnailSize(h);
    std::vector<uint8_t> out(static_cast<size_t>(tw) * th);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
        {
            float sum = 0.0f;
            int count = 0;
            for (int dy = 0; dy < 4 && y * 4 + dy < h; ++dy)
                for (int dx = 0; dx < 4 && x * 4 + dx < w; ++dx, ++count)
                    sum += trust[static_cast<size_t>(y * 4 + dy) * w + x * 4 + dx];
            out[static_cast<size_t>(y) * tw + x] = static_cast<uint8_t>(std::lround(sum / static_cast<float>(count) * 255.0f));
        }
    return out;
}

} // namespace storm::fx
