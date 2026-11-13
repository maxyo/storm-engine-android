// Float images of the derived maps (normal_map.cpp, gloss_map.cpp): separable filters, halving. Internal to fx.
#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace storm::fx::img
{
struct Field
{
    int width, height;
    bool wrap;
    std::vector<float> values;

    // Texel index of coordinate i on an axis of `length` (wrapped or clamped).
    int Index(int i, int length) const { return wrap ? ((i % length) + length) % length : std::clamp(i, 0, length - 1); }
    // Indices of -pad .. length + pad - 1 on an axis: offsets[i + pad].
    std::vector<int> Indices(int length, int pad) const
    {
        std::vector<int> indices(static_cast<size_t>(length + 2 * pad));
        for (int i = -pad; i < length + pad; ++i)
            indices[static_cast<size_t>(i + pad)] = Index(i, length);
        return indices;
    }
};

// One filter pass along x (horizontal) or y: out[i] = sum_k taps[k] * in[i + k - r].
inline Field Convolve(const Field &in, std::span<const float> taps, bool horizontal)
{
    Field out = in;
    const int r = static_cast<int>(taps.size() / 2);
    const int length = horizontal ? in.width : in.height, lines = horizontal ? in.height : in.width;
    const std::vector<int> at = in.Indices(length, r);
    const size_t step = horizontal ? 1 : static_cast<size_t>(in.width), line_step = horizontal ? static_cast<size_t>(in.width) : 1;
    for (int line = 0; line < lines; ++line)
    {
        const float *src = in.values.data() + line * line_step;
        float *dst = out.values.data() + line * line_step;
        for (int i = 0; i < length; ++i)
        {
            float sum = 0.0f;
            for (size_t k = 0; k < taps.size(); ++k)
                sum += taps[k] * src[at[static_cast<size_t>(i) + k] * step];
            dst[i * step] = sum;
        }
    }
    return out;
}

// 3x3 binomial.
inline Field Blur(const Field &in)
{
    static constexpr float kTaps[] = {0.25f, 0.5f, 0.25f};
    return Convolve(Convolve(in, kTaps, true), kTaps, false);
}

inline Field Halve(const Field &in)
{
    Field out{std::max(in.width / 2, 1), std::max(in.height / 2, 1), in.wrap, {}};
    out.values.resize(static_cast<size_t>(out.width) * out.height);
    const int sx = in.width > 1 ? 2 : 1, sy = in.height > 1 ? 2 : 1;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
        {
            float sum = 0.0f;
            for (int dy = 0; dy < sy; ++dy)
                for (int dx = 0; dx < sx; ++dx)
                    sum += in.values[static_cast<size_t>(y * sy + dy) * in.width + x * sx + dx];
            out.values[static_cast<size_t>(y) * out.width + x] = sum / static_cast<float>(sx * sy);
        }
    return out;
}

// One box filter of width 2r+1 along x (horizontal) or y, with a running sum.
inline Field Box(const Field &in, int r, bool horizontal)
{
    Field out = in;
    const int length = horizontal ? in.width : in.height, lines = horizontal ? in.height : in.width;
    const std::vector<int> at = in.Indices(length, r + 1);
    const size_t step = horizontal ? 1 : static_cast<size_t>(in.width), line_step = horizontal ? static_cast<size_t>(in.width) : 1;
    const float scale = 1.0f / static_cast<float>(2 * r + 1);
    for (int line = 0; line < lines; ++line)
    {
        const float *src = in.values.data() + line * line_step;
        float *dst = out.values.data() + line * line_step;
        const auto value = [&](int i) { return src[at[static_cast<size_t>(i + r + 1)] * step]; };
        float sum = 0.0f;
        for (int i = -r; i <= r; ++i)
            sum += value(i);
        for (int i = 0; i < length; ++i)
        {
            dst[i * step] = sum * scale;
            sum += value(i + r + 1) - value(i - r);
        }
    }
    return out;
}

// Gaussian of `sigma` texels as three box passes per axis (their variance adds up: 3 (w² - 1) / 12 = sigma²).
inline Field Gaussian(const Field &in, float sigma)
{
    const int r = std::max(1, static_cast<int>(std::lround((std::sqrt(4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f)));
    Field out = in;
    for (int pass = 0; pass < 3; ++pass)
        out = Box(Box(out, r, true), r, false);
    return out;
}

} // namespace storm::fx::img
