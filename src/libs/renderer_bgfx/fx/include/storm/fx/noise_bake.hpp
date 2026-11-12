// Procedural noise (the procedural surfaces of tools/fx-preview). Nothing derives from game assets.
#pragma once

#include <cstdint>

namespace storm::fx
{

// Seamlessly tiling fractal gradient noise in -1..1 (period = `period` cells per unit square).
float PeriodicFbm(float u, float v, int period, int octaves, uint32_t seed, float gain = 0.5f);

} // namespace storm::fx
