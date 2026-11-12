// Sun visibility of lit models outdoors, read from the location's own lightmaps.
//
// A town's ground and walls carry their sun and shade in baked lightmaps (lighting\<time>\...\shadow.tga, one set
// per time of day). A character standing on that ground must be in the sun where the ground under him shows sun and
// in the shade where it shows shade, whatever direction the run-time sun has (it is not the one the lightmaps were
// baked with, so a shadow map of the location's geometry put characters in shade on sunlit ground).
//
// GroundLight keeps the up-facing triangles of the lightmapped models (world positions, lightmap UVs) on an xz grid
// and each lightmap as luminance. SunAt(p) finds the ground under p (the highest such triangle within kGroundReach
// below), reads the lightmap there and maps it to 0 (as dark as the shade of this lightmap) .. 1 (as bright as its
// sun): the lightmap's 10th and 90th luminance percentiles over its ground. SunVisibility(p) averages SunAt over a
// small disk, so a character walking over a shadow edge darkens over about half a metre and texel edges do not
// flicker. Positions with no lightmapped ground under them (decks, the sea, interiors) have no value.
#pragma once

#include "storm/fx/sky_model.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr float kGroundReach = 3.0f;      // metres below the sample point where the ground may be
inline constexpr float kGroundSoftRadius = 0.3f; // metres: the disk SunVisibility averages over
inline constexpr float kGroundCell = 2.0f;       // metres: the xz grid of the triangle lookup

struct GroundLightmap
{
    int width = 0, height = 0;
    std::vector<uint8_t> luminance; // width * height, row 0 = v 0
};

class GroundLight
{
  public:
    // `uvs` per vertex (the lightmap's texture coordinates), `lightmap` per triangle (an index into `lightmaps`).
    // Triangles facing down or steeper than 60° are left out: they are walls and roofs' undersides, not ground.
    void Build(std::span<const Vec3> positions, std::span<const std::array<float, 2>> uvs, std::span<const uint32_t> indices,
               std::span<const uint32_t> lightmap, std::vector<GroundLightmap> lightmaps);
    void Clear();
    bool Empty() const { return triangles_.empty(); }
    size_t Triangles() const { return triangles_.size(); }

    std::optional<float> SunAt(const Vec3 &p) const;
    std::optional<float> SunVisibility(const Vec3 &p) const;
    // The luminance (0..1) a lightmap counts as its shade and its sun.
    std::array<float, 2> Levels(size_t lightmap) const { return levels_[lightmap]; }

  private:
    struct Triangle
    {
        Vec3 a, b, c;
        std::array<float, 2> ta, tb, tc;
        uint32_t lightmap;
    };
    float Luminance(const Triangle &t, float u, float v) const; // barycentric weights of b and c
    std::vector<Triangle> triangles_;
    std::vector<GroundLightmap> lightmaps_;
    std::vector<std::array<float, 2>> levels_;
    float min_x_ = 0.0f, min_z_ = 0.0f;
    int cells_x_ = 0, cells_z_ = 0;
    std::vector<uint32_t> cell_start_, cell_items_; // triangles per cell (CSR)
};

} // namespace storm::fx
