#include "storm/fx/cutout.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{

float AlphaMask::At(float u, float v) const
{
    if (alpha.empty())
        return 1.0f;
    const auto wrap = [](float t, int size) {
        const int i = static_cast<int>(std::floor((t - std::floor(t)) * static_cast<float>(size)));
        return std::clamp(i, 0, size - 1);
    };
    return static_cast<float>(alpha[static_cast<size_t>(wrap(v, height)) * width + wrap(u, width)]) / 255.0f;
}

bool MakeAlphaMask(std::span<const uint8_t> rgba, int width, int height, float reference, AlphaMask &mask)
{
    const size_t texels = static_cast<size_t>(width) * height;
    if (texels == 0 || rgba.size() < texels * 4)
        return false;
    size_t holes = 0;
    for (size_t i = 0; i < texels; ++i)
        holes += static_cast<float>(rgba[i * 4 + 3]) <= reference * 255.0f ? 1 : 0;
    if (holes * 100 < texels)
        return false;
    mask.width = width;
    mask.height = height;
    mask.alpha.resize(texels);
    for (size_t i = 0; i < texels; ++i)
        mask.alpha[i] = rgba[i * 4 + 3];
    return true;
}

void AppendCutTriangles(std::vector<Vec3> &positions, std::vector<uint32_t> &indices, std::span<const std::array<float, 2>> uvs,
                        std::span<const uint32_t> triangles, const AlphaMask &mask, float reference, float piece,
                        float shrink)
{
    for (size_t t = 0; t + 2 < triangles.size(); t += 3)
    {
        const uint32_t i[3] = {triangles[t], triangles[t + 1], triangles[t + 2]};
        if (std::max({i[0], i[1], i[2]}) >= positions.size() || std::max({i[0], i[1], i[2]}) >= uvs.size())
            continue;
        const Vec3 p[3] = {positions[i[0]], positions[i[1]], positions[i[2]]};
        float longest = 0.0f;
        for (int e = 0; e < 3; ++e)
        {
            const Vec3 &a = p[e], &b = p[(e + 1) % 3];
            longest = std::max(longest, std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) +
                                                  (a[2] - b[2]) * (a[2] - b[2])));
        }
        // n rows of pieces along every edge: n * n small triangles, each a barycentric copy of the whole.
        const int n = std::clamp(static_cast<int>(std::ceil(longest / std::max(piece, 1e-3f))), 1, 128);
        const auto point = [&](int a, int b) { // barycentric (n - a - b, a, b) / n
            const float wb = static_cast<float>(a) / static_cast<float>(n), wc = static_cast<float>(b) / static_cast<float>(n);
            const float wa = 1.0f - wb - wc;
            return Vec3{p[0][0] * wa + p[1][0] * wb + p[2][0] * wc, p[0][1] * wa + p[1][1] * wb + p[2][1] * wc,
                        p[0][2] * wa + p[1][2] * wb + p[2][2] * wc};
        };
        const auto keep = [&](float a, float b) { // the piece whose centre is at barycentric (.., a, b) / n
            const float wb = a / static_cast<float>(n), wc = b / static_cast<float>(n), wa = 1.0f - wb - wc;
            const float u = uvs[i[0]][0] * wa + uvs[i[1]][0] * wb + uvs[i[2]][0] * wc;
            const float v = uvs[i[0]][1] * wa + uvs[i[1]][1] * wb + uvs[i[2]][1] * wc;
            return mask.At(u, v) > reference;
        };
        const auto emit = [&](Vec3 a, Vec3 b, Vec3 c) {
            const auto base = static_cast<uint32_t>(positions.size());
            if (shrink < 1.0f)
                for (int k = 0; k < 3; ++k)
                {
                    const float centre = (a[k] + b[k] + c[k]) / 3.0f;
                    a[k] = centre + (a[k] - centre) * shrink;
                    b[k] = centre + (b[k] - centre) * shrink;
                    c[k] = centre + (c[k] - centre) * shrink;
                }
            positions.insert(positions.end(), {a, b, c});
            indices.insert(indices.end(), {base, base + 1, base + 2});
        };
        for (int a = 0; a < n; ++a)
            for (int b = 0; a + b < n; ++b)
            {
                if (keep(static_cast<float>(a) + 1.0f / 3.0f, static_cast<float>(b) + 1.0f / 3.0f))
                    emit(point(a, b), point(a + 1, b), point(a, b + 1));
                if (a + b + 1 < n && keep(static_cast<float>(a) + 2.0f / 3.0f, static_cast<float>(b) + 2.0f / 3.0f))
                    emit(point(a + 1, b), point(a + 1, b + 1), point(a, b + 1));
            }
    }
}

} // namespace storm::fx
