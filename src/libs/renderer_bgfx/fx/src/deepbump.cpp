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

int DeepBumpTileCount(int width, int height)
{
    constexpr int stride = kDeepBumpTile - kDeepBumpOverlap;
    const auto along = [](int n) { return (n + kDeepBumpOverlap + stride - 1) / stride; };
    return along(width) * along(height);
}

std::vector<float> DeepBumpNormals(std::span<const float> grey, int width, int height, const DeepBumpInfer &infer)
{
    constexpr int T = kDeepBumpTile, O = kDeepBumpOverlap, S = T - O;
    const size_t texels = static_cast<size_t>(width) * height;
    if (width <= 0 || height <= 0 || grey.size() < texels)
        return {};
    float ramp[T];
    for (int i = 0; i < T; ++i)
        ramp[i] = static_cast<float>(std::min(std::min(i + 1, T - i), O)) / O;
    std::vector<float> sum(texels * 3, 0.0f), weight(texels, 0.0f), tile(static_cast<size_t>(T) * T),
        out(static_cast<size_t>(3) * T * T);
    // Tile origins in texture coordinates (the image wraps): -O, -O + S, ... while the tile starts inside the image.
    for (int ty = -O; ty < height; ty += S)
        for (int tx = -O; tx < width; tx += S)
        {
            for (int y = 0; y < T; ++y)
                for (int x = 0; x < T; ++x)
                    tile[static_cast<size_t>(y) * T + x] =
                        grey[static_cast<size_t>(Wrap(ty + y, height)) * width + Wrap(tx + x, width)];
            if (!infer(tile.data(), out.data()))
                return {};
            for (int y = 0; y < T; ++y)
            {
                const int py = ty + y;
                if (py < 0 || py >= height)
                    continue;
                for (int x = 0; x < T; ++x)
                {
                    const int px = tx + x;
                    if (px < 0 || px >= width)
                        continue;
                    const float m = ramp[y] * ramp[x];
                    const size_t t = static_cast<size_t>(py) * width + px, o = static_cast<size_t>(y) * T + x;
                    for (int c = 0; c < 3; ++c)
                        sum[t * 3 + c] += out[c * T * T + o] * m;
                    weight[t] += m;
                }
            }
        }
    for (size_t t = 0; t < texels; ++t)
    {
        float n[3];
        for (int c = 0; c < 3; ++c)
            n[c] = sum[t * 3 + c] / std::max(weight[t], 1e-6f) - 0.5f;
        n[1] = -n[1]; // the model's +y points up the image; ours follows +v (down the rows)
        const float length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int c = 0; c < 3; ++c)
            sum[t * 3 + c] = length > 0.0f ? n[c] / length : (c == 2 ? 1.0f : 0.0f);
    }
    return sum;
}

float TiltRms(const NormalMap &rg8)
{
    const size_t texels = static_cast<size_t>(rg8.width) * rg8.height;
    if (rg8.format != NormalMapFormat::Rg8 || rg8.rg.size() < texels * 2 || texels == 0)
        return 0.0f;
    double square = 0.0;
    for (size_t t = 0; t < texels; ++t)
    {
        const double x = rg8.rg[t * 2] / 255.0 * 2.0 - 1.0, y = rg8.rg[t * 2 + 1] / 255.0 * 2.0 - 1.0;
        square += x * x + y * y;
    }
    return static_cast<float>(std::sqrt(square / static_cast<double>(texels)));
}

NormalMap DeepBumpNormalMap(std::span<const float> normals, int width, int height, float target_tilt)
{
    NormalMap map;
    if (width <= 0 || height <= 0 || normals.size() < static_cast<size_t>(width) * height * 3)
        return map;
    map.width = width;
    map.height = height;
    std::vector<float> level(normals.begin(), normals.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(width) * height * 3));
    double square = 0.0;
    for (size_t t = 0; t < level.size() / 3; ++t)
        square += level[t * 3] * level[t * 3] + level[t * 3 + 1] * level[t * 3 + 1];
    const float rms = static_cast<float>(std::sqrt(square / static_cast<double>(level.size() / 3)));
    const float k = target_tilt / std::max(rms, 1e-6f);
    map.rg.reserve(NormalMapBytes(width, height));
    int w = width, h = height;
    for (;;)
    {
        ++map.mip_count;
        for (size_t t = 0; t < static_cast<size_t>(w) * h; ++t)
        {
            float x = level[t * 3] * k, y = level[t * 3 + 1] * k;
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
