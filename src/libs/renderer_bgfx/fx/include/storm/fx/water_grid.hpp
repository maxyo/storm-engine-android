// Camera-centred sea grid for Water's height-field and Gerstner paths: nested
// square levels, each twice as coarse as the previous one (a geometry
// clipmap). One vertex buffer serves every level; the vertex shader scales
// and places it.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace storm::fx
{

struct WaterGridMesh
{
    int cells = 0; // cells per side, a multiple of 4
    // (cells + 1)² vertices of 3 floats: cell x, cell z (both -cells/2 .. cells/2) and the edge code read by
    // vs_fx_water.sc: 0 inside, 1 outer edge, 2 / 3 outer edge between two vertices of the coarser level (x / z).
    std::vector<float> vertices;
    std::vector<uint16_t> full;                 // level 0: the whole square
    std::array<std::vector<uint16_t>, 4> rings; // other levels: the square minus the finer level's footprint,
                                                // one per offset of that footprint (index = ox + 2 * oz)
};

WaterGridMesh BuildWaterGrid(int cells);

struct WaterGridLevel
{
    std::array<float, 2> origin{0.0f, 0.0f}; // world x, z of cell coordinate (0, 0)
    float step = 1.0f;                       // cell size in metres
    int ring = -1;                           // index into WaterGridMesh::rings, -1 = full square
};

// Levels around the camera, finest first. Every level snaps to the grid of the
// next coarser one, so vertices never slide over the wave field.
std::vector<WaterGridLevel> PlaceWaterGrid(float camera_x, float camera_z, float base_step, int level_count);

} // namespace storm::fx
