// Small wrapping (tiling) image filters on float planes, shared by the relief's height and its parallax trust.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace storm::fx::image
{

// One wrapping box filter of width 2r+1 along x (horizontal) or y, with a running sum.
inline std::vector<float> Box(const std::vector<float> &in, int w, int h, int r, bool horizontal)
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
inline std::vector<float> Gaussian(std::vector<float> v, int w, int h, float sigma)
{
    const int r = std::max(1, static_cast<int>(std::lround((std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f)));
    for (int pass = 0; pass < 3; ++pass)
        v = Box(Box(v, w, h, r, true), w, h, r, false);
    return v;
}

// Maximum / minimum over a (2r+1)-square, wrapping: the grey dilation and erosion with a square element.
inline std::vector<float> Extreme(const std::vector<float> &in, int w, int h, int r, bool maximum)
{
    const auto pass = [&](const std::vector<float> &src, bool horizontal) {
        std::vector<float> out(src.size());
        const int length = horizontal ? w : h, lines = horizontal ? h : w;
        for (int line = 0; line < lines; ++line)
            for (int i = 0; i < length; ++i)
            {
                float best = maximum ? -1e30f : 1e30f;
                for (int d = -r; d <= r; ++d)
                {
                    const int j = ((i + d) % length + length) % length;
                    const float v = horizontal ? src[static_cast<size_t>(line) * w + j] : src[static_cast<size_t>(j) * w + line];
                    best = maximum ? std::max(best, v) : std::min(best, v);
                }
                (horizontal ? out[static_cast<size_t>(line) * w + i] : out[static_cast<size_t>(i) * w + line]) = best;
            }
        return out;
    };
    return pass(pass(in, true), false);
}

} // namespace storm::fx::image
