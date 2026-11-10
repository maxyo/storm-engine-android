#include "storm/fx/water_grid.hpp"

#include <algorithm>
#include <cmath>

namespace storm::fx
{

WaterGridMesh BuildWaterGrid(int cells)
{
    WaterGridMesh mesh;
    cells = std::clamp(cells / 4 * 4, 8, 252);
    mesh.cells = cells;
    const int half = cells / 2;
    const int side = cells + 1;
    mesh.vertices.reserve(static_cast<size_t>(side) * side * 3);
    for (int z = -half; z <= half; ++z)
        for (int x = -half; x <= half; ++x)
        {
            const bool edge_x = x == -half || x == half; // on an edge that runs along z
            const bool edge_z = z == -half || z == half; // on an edge that runs along x
            float code = 0.0f;
            if (edge_x || edge_z)
            {
                code = 1.0f;
                if (edge_z && !edge_x && (x & 1))
                    code = 2.0f;
                else if (edge_x && !edge_z && (z & 1))
                    code = 3.0f;
            }
            mesh.vertices.insert(mesh.vertices.end(), {static_cast<float>(x), static_cast<float>(z), code});
        }

    const auto emit = [&](std::vector<uint16_t> &out, int hole_x, int hole_z, bool with_hole) {
        // Cells are indexed in units of this level; the finer level covers 2 * (cells / 4) of them per side.
        const int quarter = cells / 4;
        for (int z = 0; z < cells; ++z)
            for (int x = 0; x < cells; ++x)
            {
                const int cx = x - half, cz = z - half;
                if (with_hole && cx >= hole_x - quarter && cx < hole_x + quarter && cz >= hole_z - quarter && cz < hole_z + quarter)
                    continue;
                const auto v = [side](int px, int pz) { return static_cast<uint16_t>(pz * side + px); };
                out.insert(out.end(), {v(x, z), v(x, z + 1), v(x + 1, z), v(x + 1, z), v(x, z + 1), v(x + 1, z + 1)});
            }
    };
    emit(mesh.full, 0, 0, false);
    for (int variant = 0; variant < 4; ++variant)
        emit(mesh.rings[variant], variant & 1, variant >> 1, true);
    return mesh;
}

std::vector<WaterGridLevel> PlaceWaterGrid(float camera_x, float camera_z, float base_step, int level_count)
{
    std::vector<WaterGridLevel> levels(static_cast<size_t>(std::max(level_count, 1)));
    float step = base_step;
    for (size_t i = 0; i < levels.size(); ++i, step *= 2.0f)
    {
        levels[i].step = step;
        // Snap to the next coarser grid: its vertices are every second vertex of this level.
        levels[i].origin = {std::floor(camera_x / (2.0f * step)) * 2.0f * step, std::floor(camera_z / (2.0f * step)) * 2.0f * step};
    }
    for (size_t i = 1; i < levels.size(); ++i)
    {
        const float coarse = levels[i].step;
        const int ox = static_cast<int>(std::lround((levels[i - 1].origin[0] - levels[i].origin[0]) / coarse));
        const int oz = static_cast<int>(std::lround((levels[i - 1].origin[1] - levels[i].origin[1]) / coarse));
        levels[i].ring = std::clamp(ox, 0, 1) + 2 * std::clamp(oz, 0, 1);
    }
    return levels;
}

} // namespace storm::fx
