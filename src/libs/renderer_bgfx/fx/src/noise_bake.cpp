#include "storm/fx/noise_bake.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{
namespace
{
constexpr float kPi = 3.14159265358979f;

uint32_t Hash(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = x * 0x9e3779b1u ^ (y * 0x85ebca77u + seed * 0xc2b2ae3du);
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Gradient noise on a lattice that wraps every `period` cells; result roughly in -1..1.
float PeriodicNoise(float x, float y, int period, uint32_t seed)
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    const auto corner = [&](int cx, int cy, float dx, float dy) {
        const auto wrap = [period](int v) { return static_cast<uint32_t>(((v % period) + period) % period); };
        const float angle = static_cast<float>(Hash(wrap(cx), wrap(cy), seed) & 0xffffu) * (2.0f * kPi / 65536.0f);
        return std::cos(angle) * dx + std::sin(angle) * dy;
    };
    const float a = corner(ix, iy, tx, ty);
    const float b = corner(ix + 1, iy, tx - 1.0f, ty);
    const float c = corner(ix, iy + 1, tx, ty - 1.0f);
    const float d = corner(ix + 1, iy + 1, tx - 1.0f, ty - 1.0f);
    const float u = Fade(tx), v = Fade(ty);
    return 1.4142f * ((a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v);
}

// Distance to the nearest jittered feature point on a wrapping grid, 0..~1.
float PeriodicCells(float x, float y, int period, uint32_t seed)
{
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    float best = 4.0f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int cx = ix + dx, cy = iy + dy;
            const auto wrap = [period](int v) { return static_cast<uint32_t>(((v % period) + period) % period); };
            const uint32_t h = Hash(wrap(cx), wrap(cy), seed);
            const float px = static_cast<float>(cx) + static_cast<float>(h & 0xffffu) / 65536.0f;
            const float py = static_cast<float>(cy) + static_cast<float>(h >> 16) / 65536.0f;
            best = std::min(best, (px - x) * (px - x) + (py - y) * (py - y));
        }
    return std::sqrt(best);
}

float Smoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

uint8_t ToByte(float v)
{
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// Appends box-filtered mips of the size*size RGBA8 image at the start of `rgba`.
int AppendMips(std::vector<uint8_t> &rgba, int size)
{
    int mips = 1;
    size_t src = 0;
    for (int s = size; s > 1; s /= 2, ++mips)
    {
        const int d = s / 2;
        const size_t dst = rgba.size();
        rgba.resize(dst + static_cast<size_t>(d) * d * 4);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    const auto at = [&](int px, int py) { return rgba[src + (static_cast<size_t>(py) * s + px) * 4 + c]; };
                    const int sum = at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1);
                    rgba[dst + (static_cast<size_t>(y) * d + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
        src = dst;
    }
    return mips;
}
} // namespace

float PeriodicFbm(float u, float v, int period, int octaves, uint32_t seed, float gain)
{
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o)
    {
        sum += amp * PeriodicNoise(u * static_cast<float>(period), v * static_cast<float>(period), period, seed + 131u * o);
        norm += amp;
        amp *= gain;
        period *= 2;
    }
    return sum / norm;
}

BakedTexture BakeWaterDetail(int size, uint32_t seed)
{
    BakedTexture out;
    out.size = size;
    std::vector<float> height(static_cast<size_t>(size) * size);
    const float inv = 1.0f / static_cast<float>(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) * inv, v = (static_cast<float>(y) + 0.5f) * inv;
            // Domain warp gives the ripples their irregular, flowing crests.
            const float wu = u + 0.045f * PeriodicFbm(u, v, 3, 2, seed + 7u);
            const float wv = v + 0.045f * PeriodicFbm(u, v, 3, 2, seed + 11u);
            float h = 0.0f, amp = 1.0f, norm = 0.0f;
            int period = 5;
            for (int o = 0; o < 5; ++o)
            {
                const float n = PeriodicNoise(wu * static_cast<float>(period), wv * static_cast<float>(period), period, seed + 17u * o);
                // Sharpened crests (1 - |n|)² on the larger octaves, plain noise on the small ones.
                const float ridge = 1.0f - std::fabs(n);
                h += amp * (o < 3 ? ridge * ridge : 0.5f + 0.5f * n);
                norm += amp;
                amp *= 0.52f;
                period *= 2;
            }
            height[static_cast<size_t>(y) * size + x] = h / norm;
        }

    out.rgba.resize(static_cast<size_t>(size) * size * 4);
    // Slope scale: one texel step of the height field covers 1/size of the tile.
    const float slope_scale = 0.045f * static_cast<float>(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const auto h = [&](int px, int py) { return height[static_cast<size_t>((py + size) % size) * size + (px + size) % size]; };
            float sx = -(h(x + 1, y) - h(x - 1, y)) * 0.5f * slope_scale;
            float sy = -(h(x, y + 1) - h(x, y - 1)) * 0.5f * slope_scale;
            const float len = std::sqrt(sx * sx + 1.0f + sy * sy);
            sx /= len;
            sy /= len;
            const float u = (static_cast<float>(x) + 0.5f) * inv, v = (static_cast<float>(y) + 0.5f) * inv;
            // Foam: bubbly cells broken up by streaks of fractal noise.
            const float cells = PeriodicCells(u * 14.0f, v * 14.0f, 14, seed + 101u);
            const float fine = PeriodicCells(u * 37.0f, v * 37.0f, 37, seed + 103u);
            const float breakup = 0.5f + 0.5f * PeriodicFbm(u, v, 4, 4, seed + 107u);
            const float foam = Smoothstep(0.25f, 0.85f, (1.0f - Smoothstep(0.05f, 0.55f, cells) * 0.75f) * (0.55f + 0.45f * fine) * (0.35f + breakup));
            const size_t o = (static_cast<size_t>(y) * size + x) * 4;
            out.rgba[o + 0] = ToByte(0.5f + 0.5f * sx);
            out.rgba[o + 1] = ToByte(0.5f + 0.5f * sy);
            out.rgba[o + 2] = ToByte(foam);
            out.rgba[o + 3] = ToByte(h(x, y));
        }
    out.mip_count = AppendMips(out.rgba, size);
    return out;
}

BakedTexture BakeCloudNoise(int size, uint32_t seed)
{
    BakedTexture out;
    out.size = size;
    out.rgba.resize(static_cast<size_t>(size) * size * 4);
    const float inv = 1.0f / static_cast<float>(size);
    std::vector<float> coverage(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) * inv, v = (static_cast<float>(y) + 0.5f) * inv;
            const float wu = u + 0.06f * PeriodicFbm(u, v, 2, 3, seed + 3u);
            const float wv = v + 0.06f * PeriodicFbm(u, v, 2, 3, seed + 5u);
            const float low = 0.5f + 0.5f * PeriodicFbm(wu, wv, 3, 5, seed, 0.55f);
            // Billows: inverted cells give the rounded cauliflower look of cumulus.
            const float billow = 1.0f - PeriodicCells(wu * 9.0f, wv * 9.0f, 9, seed + 31u);
            const float mid = std::clamp(0.6f * billow + 0.4f * (0.5f + 0.5f * PeriodicFbm(wu, wv, 8, 3, seed + 37u)), 0.0f, 1.0f);
            const float high = 0.5f + 0.5f * PeriodicFbm(u, v, 24, 3, seed + 41u);
            coverage[static_cast<size_t>(y) * size + x] = low;
            const size_t o = (static_cast<size_t>(y) * size + x) * 4;
            out.rgba[o + 1] = ToByte(mid);
            out.rgba[o + 2] = ToByte(high);
        }
    // Stretch the coverage to the full range so the shader's cover threshold means the same for every seed.
    const auto [lowest, highest] = std::minmax_element(coverage.begin(), coverage.end());
    const float offset = *lowest, scale = 1.0f / std::max(*highest - *lowest, 1e-6f);
    for (size_t i = 0; i < coverage.size(); ++i)
    {
        coverage[i] = (coverage[i] - offset) * scale;
        out.rgba[i * 4] = ToByte(coverage[i]);
    }
    // Alpha: coverage averaged over a 9x9 neighbourhood (wrapping).
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            float sum = 0.0f;
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -4; dx <= 4; ++dx)
                    sum += coverage[static_cast<size_t>((y + dy + size) % size) * size + (x + dx + size) % size];
            out.rgba[(static_cast<size_t>(y) * size + x) * 4 + 3] = ToByte(sum / 81.0f);
        }
    out.mip_count = AppendMips(out.rgba, size);
    return out;
}

} // namespace storm::fx
