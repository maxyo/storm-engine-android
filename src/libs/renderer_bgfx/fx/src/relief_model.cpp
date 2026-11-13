#include "storm/fx/relief_model.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
// Box halving of an interleaved field with `channels` values per texel (odd sizes drop the last row / column).
std::vector<float> Halve(const std::vector<float> &in, int &width, int &height, int channels)
{
    const int out_w = std::max(width / 2, 1), out_h = std::max(height / 2, 1);
    const int sx = width > 1 ? 2 : 1, sy = height > 1 ? 2 : 1;
    std::vector<float> out(static_cast<size_t>(out_w) * out_h * channels);
    for (int y = 0; y < out_h; ++y)
        for (int x = 0; x < out_w; ++x)
            for (int c = 0; c < channels; ++c)
            {
                float sum = 0.0f;
                for (int dy = 0; dy < sy; ++dy)
                    for (int dx = 0; dx < sx; ++dx)
                        sum += in[(static_cast<size_t>(y * sy + dy) * width + x * sx + dx) * channels + c];
                out[(static_cast<size_t>(y) * out_w + x) * channels + c] = sum / static_cast<float>(sx * sy);
            }
    width = out_w;
    height = out_h;
    return out;
}

int Wrap(int i, int n)
{
    return ((i % n) + n) % n;
}
} // namespace

std::vector<float> ReliefModelInput(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height)
{
    if (width <= 0 || height <= 0 || map_width <= 0 || map_height <= 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
        return {};
    std::vector<float> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t i = 0; i < rgb.size() / 3; ++i)
        for (int c = 0; c < 3; ++c)
            rgb[i * 3 + c] = static_cast<float>(rgba[i * 4 + c]) / 255.0f;
    while (width > map_width || height > map_height)
        rgb = Halve(rgb, width, height, 3);
    const int pw = width + 2 * kReliefModelPad, ph = height + 2 * kReliefModelPad;
    const size_t plane = static_cast<size_t>(pw) * ph;
    std::vector<float> image(plane * 3);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
        {
            const size_t from = (static_cast<size_t>(Wrap(y - kReliefModelPad, height)) * width + Wrap(x - kReliefModelPad, width)) * 3;
            for (int c = 0; c < 3; ++c)
                image[c * plane + static_cast<size_t>(y) * pw + x] = rgb[from + c];
        }
    return image;
}

std::vector<float> ReliefModelRun(std::span<const float> input, int map_width, int map_height, const ReliefInfer &infer)
{
    const int pw = map_width + 2 * kReliefModelPad, ph = map_height + 2 * kReliefModelPad;
    const size_t plane = static_cast<size_t>(pw) * ph, texels = static_cast<size_t>(map_width) * map_height;
    if (map_width <= 0 || map_height <= 0 || input.size() < plane * 3)
        return {};
    std::vector<float> out(plane * 3);
    if (!infer(input.data(), pw, ph, out.data()))
        return {};
    std::vector<float> planes(texels * 3);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < map_height; ++y)
            for (int x = 0; x < map_width; ++x)
                planes[c * texels + static_cast<size_t>(y) * map_width + x] =
                    out[c * plane + static_cast<size_t>(y + kReliefModelPad) * pw + x + kReliefModelPad];
    return planes;
}

std::vector<float> ReliefModelNormals(std::span<const float> planes, int width, int height)
{
    const size_t texels = static_cast<size_t>(width) * height;
    if (planes.size() < texels * 3)
        return {};
    std::vector<float> normals(texels * 3);
    for (size_t t = 0; t < texels; ++t)
    {
        float n[3] = {planes[t] * 2.0f - 1.0f, planes[texels + t] * 2.0f - 1.0f, planes[2 * texels + t] * 2.0f - 1.0f};
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int c = 0; c < 3; ++c)
            normals[t * 3 + c] = length > 0.0f ? n[c] / length : (c == 2 ? 1.0f : 0.0f);
    }
    return normals;
}

std::vector<float> ReliefModelHeight(std::span<const float> planes, int width, int height)
{
    const size_t texels = static_cast<size_t>(width) * height;
    if (planes.size() < texels * 3)
        return {};
    std::vector<float> h(texels);
    for (size_t t = 0; t < texels; ++t)
        h[t] = std::clamp((planes[t] + planes[texels + t] + planes[2 * texels + t]) / 3.0f, 0.0f, 1.0f);
    return h;
}

NormalMap NormalMapOfNormals(std::span<const float> normals, int width, int height)
{
    NormalMap map;
    if (width <= 0 || height <= 0 || normals.size() < static_cast<size_t>(width) * height * 3)
        return map;
    map.width = width;
    map.height = height;
    std::vector<float> level(normals.begin(), normals.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(width) * height * 3));
    map.rg.reserve(NormalMapBytes(width, height));
    int w = width, h = height;
    for (;;)
    {
        ++map.mip_count;
        for (size_t t = 0; t < static_cast<size_t>(w) * h; ++t)
        {
            float x = level[t * 3], y = level[t * 3 + 1];
            const float length = std::sqrt(x * x + y * y);
            if (length > 0.99f)
                x *= 0.99f / length, y *= 0.99f / length;
            map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * x, 0.0f, 1.0f) * 255.0f)));
            map.rg.push_back(static_cast<uint8_t>(std::lround(std::clamp(0.5f + 0.5f * y, 0.0f, 1.0f) * 255.0f)));
        }
        if (w == 1 && h == 1)
            return map;
        level = Halve(level, w, h, 3);
        for (size_t t = 0; t < level.size() / 3; ++t)
        {
            const float length = std::sqrt(level[t * 3] * level[t * 3] + level[t * 3 + 1] * level[t * 3 + 1] +
                                           level[t * 3 + 2] * level[t * 3 + 2]);
            for (int c = 0; c < 3; ++c)
                level[t * 3 + c] = length > 0.0f ? level[t * 3 + c] / length : (c == 2 ? 1.0f : 0.0f);
        }
    }
}

} // namespace storm::fx
