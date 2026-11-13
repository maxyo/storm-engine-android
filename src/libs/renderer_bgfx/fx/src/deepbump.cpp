#include "storm/fx/deepbump.hpp"

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

std::vector<float> DeepBumpInput(std::span<const uint8_t> rgba, int width, int height, int map_width, int map_height)
{
    std::vector<float> grey(static_cast<size_t>(width) * height);
    if (rgba.size() < grey.size() * 4)
        return {};
    for (size_t i = 0; i < grey.size(); ++i)
        grey[i] = (static_cast<float>(rgba[i * 4]) + rgba[i * 4 + 1] + rgba[i * 4 + 2]) / (3.0f * 255.0f);
    while (width > map_width || height > map_height)
        grey = Halve(grey, width, height, 1);
    return grey;
}

void DeepBumpInputSize(int width, int height, int &padded_width, int &padded_height)
{
    padded_width = (width + 2 * kDeepBumpPad + 31) / 32 * 32;
    padded_height = (height + 2 * kDeepBumpPad + 31) / 32 * 32;
}

std::vector<float> DeepBumpNormals(std::span<const float> grey, int width, int height, const DeepBumpInfer &infer)
{
    const size_t texels = static_cast<size_t>(width) * height;
    if (width <= 0 || height <= 0 || grey.size() < texels)
        return {};
    int pw = 0, ph = 0;
    DeepBumpInputSize(width, height, pw, ph);
    const size_t plane = static_cast<size_t>(pw) * ph;
    std::vector<float> image(plane), out(plane * 3);
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
            image[static_cast<size_t>(y) * pw + x] =
                grey[static_cast<size_t>(Wrap(y - kDeepBumpPad, height)) * width + Wrap(x - kDeepBumpPad, width)];
    if (!infer(image.data(), pw, ph, out.data()))
        return {};
    std::vector<float> normals(texels * 3);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t o = static_cast<size_t>(y + kDeepBumpPad) * pw + x + kDeepBumpPad, t = static_cast<size_t>(y) * width + x;
            float n[3] = {out[o] - 0.5f, -(out[plane + o] - 0.5f), out[2 * plane + o] - 0.5f}; // the model's +y points up
            const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            for (int c = 0; c < 3; ++c)
                normals[t * 3 + c] = length > 0.0f ? n[c] / length : (c == 2 ? 1.0f : 0.0f);
        }
    return normals;
}

NormalMap DeepBumpNormalMap(std::span<const float> normals, int width, int height)
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
