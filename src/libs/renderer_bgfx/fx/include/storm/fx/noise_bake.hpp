// Procedural textures baked on the CPU at start-up (a few milliseconds each).
// Nothing here is loaded from disk and nothing derives from game assets.
#pragma once

#include <cstdint>
#include <vector>

namespace storm::fx
{

struct BakedTexture
{
    int size = 0;                 // square, power of two
    int mip_count = 0;            // full chain down to 1x1
    std::vector<uint8_t> rgba;    // all mips, top first, tightly packed RGBA8
};

// Seamlessly tiling fractal gradient noise in -1..1 (period = `period` cells per unit square).
float PeriodicFbm(float u, float v, int period, int octaves, uint32_t seed, float gain = 0.5f);

// Water surface detail, tiling:
//   R, G  slope of the ripple height field along U and V, stored as 0.5 + 0.5 * n (unit-length tangent-space
//         normal is (n.x, sqrt(1 - n.x² - n.y²), n.y))
//   B     foam pattern (bubbly cells, 1 = dense foam)
//   A     ripple height 0..1
// Mips are box filtered and the slopes are NOT renormalised, so distant water
// gets smoother instead of noisier.
BakedTexture BakeWaterDetail(int size = 256, uint32_t seed = 0x5ea5eedu);

// Cloud field, tiling: R low-frequency coverage noise, G mid-frequency billows,
// B high-frequency wisps, A = R blurred (cheap self-shadow term).
BakedTexture BakeCloudNoise(int size = 256, uint32_t seed = 0xc10d5u);

} // namespace storm::fx
