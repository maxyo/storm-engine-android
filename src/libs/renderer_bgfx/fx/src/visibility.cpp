#include "storm/fx/visibility.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace storm::fx
{

namespace
{

constexpr float kOpenElevation = 0.258819f; // sin 15°: "open" counts the directions above this
constexpr uint32_t kMagic = 0x56495356u;    // "VSIV"

uint64_t Hash(uint64_t h, const void *data, size_t size)
{
    const auto *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

} // namespace

std::array<float, 2> VisibilityVolume::Sample(const Vec3 &world) const
{
    if (Empty())
        return {1.0f, 1.0f};
    float f[3];
    int i0[3], i1[3];
    for (int k = 0; k < 3; ++k)
    {
        const float c = std::clamp((world[k] - origin[k]) / cell - 0.5f, 0.0f, static_cast<float>(size[k] - 1));
        i0[k] = static_cast<int>(std::floor(c));
        i1[k] = std::min(i0[k] + 1, size[k] - 1);
        f[k] = c - static_cast<float>(i0[k]);
    }
    std::array<float, 2> out{0.0f, 0.0f};
    for (int corner = 0; corner < 8; ++corner)
    {
        const int x = corner & 1 ? i1[0] : i0[0], y = corner & 2 ? i1[1] : i0[1], z = corner & 4 ? i1[2] : i0[2];
        const float w = (corner & 1 ? f[0] : 1.0f - f[0]) * (corner & 2 ? f[1] : 1.0f - f[1]) * (corner & 4 ? f[2] : 1.0f - f[2]);
        const size_t at = ((static_cast<size_t>(z) * size[1] + y) * size[0] + x) * 4;
        out[0] += w * rgba[at] / 255.0f;
        out[1] += w * rgba[at + 1] / 255.0f;
    }
    return out;
}

VisibilityVolume BakeVisibility(const Occluders &occluders, std::span<const Vec3> positions,
                                std::span<const uint32_t> indices, const VisibilityParams &params)
{
    VisibilityVolume v;
    if (indices.empty() || occluders.Empty())
        return v;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const uint32_t i : indices)
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = std::min(lo[k], positions[i][k]);
            hi[k] = std::max(hi[k], positions[i][k]);
        }
    const float diagonal = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                     (hi[2] - lo[2]) * (hi[2] - lo[2]));
    if (params.lo[0] <= params.hi[0])
        for (int k = 0; k < 3; ++k)
        {
            lo[k] = std::max(lo[k], params.lo[k]);
            hi[k] = std::min(hi[k], params.hi[k]);
        }
    for (int k = 0; k < 3; ++k)
        if (lo[k] > hi[k])
            return v;
    float cell = std::max(params.cell, 0.05f);
    for (;;)
    {
        size_t cells = 1;
        for (int k = 0; k < 3; ++k)
            cells *= static_cast<size_t>(std::ceil((hi[k] - lo[k]) / cell)) + 2;
        if (cells <= params.max_cells)
            break;
        cell *= 1.1f;
    }
    v.cell = cell;
    for (int k = 0; k < 3; ++k)
    {
        v.size[k] = static_cast<int>(std::ceil((hi[k] - lo[k]) / cell)) + 2;
        v.origin[k] = lo[k] - cell;
    }
    const size_t count = static_cast<size_t>(v.size[0]) * v.size[1] * v.size[2];
    v.rgba.assign(count * 4, 0);
    const float reach = std::min(params.reach, diagonal + cell);
    const int rays = std::max(4, params.rays);
    std::vector<uint8_t> closed(count, 0); // every ray closed
    ParallelRanges(count, params.threads, [&](size_t first, size_t last) {
        Occluders::Scratch scratch;
        for (size_t c = first; c < last; ++c)
        {
            const int x = static_cast<int>(c % v.size[0]), y = static_cast<int>((c / v.size[0]) % v.size[1]),
                      z = static_cast<int>(c / (static_cast<size_t>(v.size[0]) * v.size[1]));
            const Vec3 o{v.origin[0] + (x + 0.5f) * cell, v.origin[1] + (y + 0.5f) * cell, v.origin[2] + (z + 0.5f) * cell};
            const float rotation = static_cast<float>((c * 2654435761u) % 1000) * 0.0062831853f; // per cell: no banding
            int sky = 0, high = 0, high_open = 0;
            for (int r = 0; r < rays; ++r)
            {
                // Cosine-weighted directions around +y (a spiral).
                const float u = (static_cast<float>(r) + 0.5f) / static_cast<float>(rays);
                const float radius = std::sqrt(u), phi = 2.3999632f * static_cast<float>(r) + rotation;
                const Vec3 d{radius * std::cos(phi), std::sqrt(std::max(0.0f, 1.0f - u)), radius * std::sin(phi)};
                const bool open = !occluders.Occluded(o, d, reach, scratch);
                sky += open ? 1 : 0;
                if (d[1] > kOpenElevation)
                {
                    ++high;
                    high_open += open ? 1 : 0;
                }
            }
            v.rgba[c * 4] = static_cast<uint8_t>(std::lround(255.0f * static_cast<float>(sky) / static_cast<float>(rays)));
            v.rgba[c * 4 + 1] =
                static_cast<uint8_t>(high ? std::lround(255.0f * static_cast<float>(high_open) / static_cast<float>(high)) : 0);
            closed[c] = sky == 0 ? 1 : 0;
        }
    });
    // Cells inside the geometry (all closed) next to open ones: the mean of those, read from the bake above.
    const std::vector<uint8_t> baked = v.rgba;
    const int step[3] = {1, v.size[0], v.size[0] * v.size[1]};
    for (size_t c = 0; c < count; ++c)
    {
        if (!closed[c])
            continue;
        const int at[3] = {static_cast<int>(c % v.size[0]), static_cast<int>((c / v.size[0]) % v.size[1]),
                           static_cast<int>(c / (static_cast<size_t>(v.size[0]) * v.size[1]))};
        int sum[2] = {0, 0}, n = 0;
        for (int k = 0; k < 3; ++k)
            for (int s = -1; s <= 1; s += 2)
            {
                if (at[k] + s < 0 || at[k] + s >= v.size[k])
                    continue;
                const size_t o = c + static_cast<size_t>(static_cast<std::ptrdiff_t>(s) * step[k]);
                if (closed[o])
                    continue;
                sum[0] += baked[o * 4];
                sum[1] += baked[o * 4 + 1];
                ++n;
            }
        if (n > 0)
        {
            v.rgba[c * 4] = static_cast<uint8_t>(sum[0] / n);
            v.rgba[c * 4 + 1] = static_cast<uint8_t>(sum[1] / n);
        }
    }
    return v;
}

uint64_t VisibilityKey(std::span<const Vec3> positions, std::span<const uint32_t> indices, const VisibilityParams &params)
{
    uint64_t key = 1469598103934665603ull;
    key = Hash(key, &kVisibilityBakeVersion, sizeof(kVisibilityBakeVersion));
    key = Hash(key, positions.data(), positions.size_bytes());
    key = Hash(key, indices.data(), indices.size_bytes());
    const float p[9] = {params.lo[0], params.lo[1], params.lo[2], params.hi[0], params.hi[1], params.hi[2],
                        params.cell, params.reach, static_cast<float>(params.rays)};
    key = Hash(key, p, sizeof(p));
    return Hash(key, &params.max_cells, sizeof(params.max_cells));
}

std::vector<uint8_t> EncodeVisibility(const VisibilityVolume &volume, uint64_t key)
{
    std::vector<uint8_t> out(4 + 8 + 12 + 12 + 4 + volume.rgba.size());
    uint8_t *p = out.data();
    auto put = [&](const void *data, size_t size) {
        std::memcpy(p, data, size);
        p += size;
    };
    put(&kMagic, 4);
    put(&key, 8);
    put(volume.size.data(), 12);
    put(volume.origin.data(), 12);
    put(&volume.cell, 4);
    put(volume.rgba.data(), volume.rgba.size());
    return out;
}

std::optional<VisibilityVolume> DecodeVisibility(std::span<const uint8_t> bytes, uint64_t key, std::string &error)
{
    constexpr size_t kHeader = 4 + 8 + 12 + 12 + 4;
    if (bytes.size() < kHeader)
    {
        error = "shorter than its header";
        return std::nullopt;
    }
    VisibilityVolume v;
    uint32_t magic = 0;
    uint64_t stored = 0;
    std::memcpy(&magic, bytes.data(), 4);
    std::memcpy(&stored, bytes.data() + 4, 8);
    std::memcpy(v.size.data(), bytes.data() + 12, 12);
    std::memcpy(v.origin.data(), bytes.data() + 24, 12);
    std::memcpy(&v.cell, bytes.data() + 36, 4);
    if (magic != kMagic || stored != key)
    {
        error = "another bake";
        return std::nullopt;
    }
    if (v.size[0] <= 0 || v.size[1] <= 0 || v.size[2] <= 0 || !(v.cell > 0.0f) ||
        bytes.size() != kHeader + static_cast<size_t>(v.size[0]) * v.size[1] * v.size[2] * 4)
    {
        error = "its size does not match its cells";
        return std::nullopt;
    }
    v.rgba.assign(bytes.begin() + kHeader, bytes.end());
    return v;
}

} // namespace storm::fx
