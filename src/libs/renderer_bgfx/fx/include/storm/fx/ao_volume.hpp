// CPU side of the ambient-occlusion volume for interiors: a coarse 3D grid of ambient visibility baked from the
// static location geometry when the location is entered, cached on disk, and sampled once per pixel by the lit and
// baked fixed-function variants. It darkens what the baked vertex colours of the interiors cannot show: under
// tables and benches, behind barrels, in corners, under galleries and stairs (the colours are smooth and the floors
// are a few large quads: local-testing/shadows/DESIGN.md).
//
// Bake: the triangles are voxelized (voxelizer.h, MIT) into an occupancy grid of half a cell, then every cell
// marches kAoDirections fixed directions over the whole sphere up to `radius` through it. A cell stores its
// obscurance-weighted visibility over the sphere (255 = nothing within `radius`); a point on an open floor sees
// about half of the sphere. Cells whose centre lies inside geometry take the mean of their open neighbours, so
// trilinear filtering next to walls does not pull surfaces dark. The result does not depend on the thread count.
#pragma once

#include "storm/fx/sky_model.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storm::fx
{

inline constexpr int kAoDirections = 32;
// Bump on any change of the bake that changes its output (the disk cache is keyed by it).
inline constexpr uint32_t kAoBakeVersion = 1;

struct AoBakeParams
{
    // The part of the world that gets a volume (an interior: where one walks, with a margin); geometry outside it
    // only occludes inside it. Empty (lo > hi): the bounding box of the geometry.
    Vec3 bounds_lo{1.0f, 1.0f, 1.0f};
    Vec3 bounds_hi{-1.0f, -1.0f, -1.0f};
    float cell = 0.2f;              // metres; grown when the box would need more than max_cells
    float radius = 1.5f;            // metres: occluders farther than this do not count
    uint32_t max_cells = 1u << 20;  // 128 x 64 x 128
    unsigned threads = 1;           // worker threads of the bake (the result is identical for any count)
};

struct AoVolume
{
    std::array<int, 3> size{0, 0, 0}; // cells along x, y, z
    Vec3 origin{0.0f, 0.0f, 0.0f};    // world position of the corner of cell (0, 0, 0)
    float cell = 0.0f;                // cell edge in metres
    std::vector<uint8_t> visibility;  // x fastest, then y, then z; 255 = open

    bool Empty() const
    {
        return visibility.empty();
    }
    // Visibility of the cell containing `world` (nearest, no filtering; for tests and tools). 1 outside the grid.
    float At(Vec3 world) const;
    // Trilinear visibility at `world` (cell centres as samples, clamped to the grid), as the GPU samples it.
    float Sample(Vec3 world) const;
};

// Static geometry in world space: positions and a triangle list (three indices per triangle).
struct AoGeometry
{
    std::span<const Vec3> positions;
    std::span<const uint32_t> indices;
};

// Key of the bake of `geometry` with `params` (content hash of positions, indices, the parameters that change the
// output, and kAoBakeVersion): the name of its disk cache entry.
uint64_t AoBakeKey(const AoGeometry &geometry, const AoBakeParams &params);

// Bakes the volume over the bounding box of `geometry` (limited to the bounds) grown by one cell on every side.
// Empty volume for empty geometry or geometry outside the bounds. Measured on the (heavily loaded) development host
// with 8 threads: Tavern01 (43 k vertices, 272 k cells) 0.65 s, StoreSmall 1.3 s; the renderer bakes on a texture
// worker and caches the result.
AoVolume BakeAoVolume(const AoGeometry &geometry, const AoBakeParams &params);

// Disk cache format (little endian): "SAOV", version, key, size, origin, cell, cells. Decode returns nothing and
// sets `error` when the data is not a complete volume of this version and key.
std::vector<uint8_t> EncodeAoVolume(const AoVolume &volume, uint64_t key);
std::optional<AoVolume> DecodeAoVolume(std::span<const uint8_t> data, uint64_t key, std::string &error);

} // namespace storm::fx
