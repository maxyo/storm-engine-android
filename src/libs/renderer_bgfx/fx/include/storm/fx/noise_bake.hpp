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

// Cloud field, tiling: R low-frequency coverage noise, G mid-frequency billows,
// B high-frequency wisps, A = R blurred (cheap self-shadow term).
BakedTexture BakeCloudNoise(int size = 256, uint32_t seed = 0xc10d5u);

} // namespace storm::fx
