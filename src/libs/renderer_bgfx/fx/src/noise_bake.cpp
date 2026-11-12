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
// Appends box-filtered mips of the size*size RGBA8 image at the start of `rgba`.
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

} // namespace storm::fx
