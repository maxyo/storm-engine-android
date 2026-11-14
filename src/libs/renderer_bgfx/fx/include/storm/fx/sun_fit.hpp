// The sun direction a location's lightmaps were baked with.
//
// The towns' ground lightmaps (lighting\<time of day>\...\shadow.tga) carry the shadows of their buildings, walls
// and palms for a sun that is not the game's run-time one (FortFrance's 8:00 set was baked with a sun 69° high from
// the south: the live 8:00 sun stands 28° high in the east). Run-time shadows of the same geometry must fall where the
// ground shows them, so they are cast along the bake's direction.
//
// FitBakedSun ray-casts the location's still geometry (Occluders) from lightmapped ground samples towards candidate
// directions and keeps the one whose "shadowed" agrees best with the lightmap's darkness (point-biserial correlation
// r of shadowed vs luminance): a coarse search over the sky, then a fine one around the best. FortFrance_town: r 0.62
// at the fit, 0.23 along the live sun; SentJons_town at noon 0.65 (the live noon sun 5° off).
#pragma once

#include "storm/fx/bakes.hpp"
#include "storm/fx/cutout.hpp"
#include "storm/fx/sky_model.hpp"

#include <array>
#include <memory>
#include <optional>

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace storm::fx
{

// The most cells an Occluders grid takes (16 MB kept, 48 MB while it is built): a larger box gets larger cells,
// logged as an error so that a location of that size is seen (its rays get slower).
inline constexpr size_t kMaxOccluderCells = size_t(1) << 22;

// The farthest any ray over an Occluders runs towards the sun (the town light's sun and walls, the bounce's sun).
inline constexpr float kSunRayReach = 300.0f;

struct Box
{
    Vec3 lo{}, hi{};
};
// The box of `points` grown by `margin` on every side (empty points: an empty box, lo > hi).
Box BoundsOf(std::span<const Vec3> points, float margin);

// Triangles on a uniform grid for any-hit rays (3D DDA), cells of 2 m grown under kMaxOccluderCells.
// `within`: the region the rays start in grown by their reach: triangles wholly outside it are left out (Excluded,
// a backdrop island kilometres away cannot shade the place and would only coarsen the grid) and the grid covers no
// more than it; a ray from outside it hits nothing.
class Occluders
{
  public:
    Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices, std::optional<Box> within = std::nullopt);
    // Triangles whose texture's alpha cuts holes (foliage, nets): a ray passes where the mask at the hit is at or
    // below the material's reference, as the colour pass's alpha test lets the background through (fx/cutout.hpp).
    struct Cut
    {
        std::shared_ptr<const AlphaMask> mask;
        float reference = 0.5f;
    };
    // `uvs` per position; `cut` per triangle (an empty mask: solid).
    Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices, std::span<const std::array<float, 2>> uvs,
              std::vector<Cut> cut, std::optional<Box> within = std::nullopt);
    bool Empty() const { return triangles_ == 0; }
    // Triangles left out (outside `within`) and the box they span (empty when none).
    size_t Excluded() const { return excluded_; }
    const Box &ExcludedBounds() const { return excluded_bounds_; }
    // `point` lies inside the grid (rays from outside it hit nothing).
    bool Covers(const Vec3 &point) const;
    // Per-thread state of the rays (the triangles a ray tested already).
    struct Scratch
    {
        std::vector<uint32_t> stamp;
        uint32_t ray = 0;
    };
    // True when the ray from `origin` along unit `dir` hits a triangle within `reach`.
    bool Occluded(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch) const;
    // The nearest triangle the ray hits within `reach` (alpha holes let it through): at origin + t dir, at
    // barycentric (u, v) of the triangle's second and third corners.
    struct Hit
    {
        float t = 0.0f;
        uint32_t triangle = 0;
        float u = 0.0f, v = 0.0f;
    };
    std::optional<Hit> Closest(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch) const;
    size_t Triangles() const { return triangles_; }

  private:
    bool Hits(uint32_t triangle, const Vec3 &origin, const Vec3 &dir, float reach, Hit &hit) const;
    template <bool Closest> bool March(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch, Hit &best) const;

    std::vector<Vec3> positions_;
    std::vector<uint32_t> indices_;
    size_t triangles_ = 0;
    Vec3 lo_{}, hi_{};
    float cell_ = 2.0f;
    size_t excluded_ = 0;
    Box excluded_bounds_{};
    int n_[3] = {0, 0, 0};
    std::vector<uint32_t> start_, items_; // triangle numbers per cell (CSR)
    std::vector<std::array<float, 2>> uvs_;
    std::vector<Cut> cut_;
};

struct SunFitSample
{
    Vec3 position{};     // on the ground
    float luminance = 0; // the lightmap there, 0..1
};

struct SunFit
{
    Vec3 dir{0.0f, 1.0f, 0.0f}; // unit vector towards the sun
    float r = 0.0f;             // agreement of the shadow mask with the lightmap (point-biserial correlation)
    float shadowed = 0.0f;      // share of the samples in its shadow
    bool valid = false;         // r below kSunFitMinR or no samples: no direction
};

inline constexpr float kSunFitMinR = 0.3f;

// `samples`: about a thousand spread over the lightmapped ground (more add little); `reach` in metres; the candidate
// directions are shared out over `threads`. FortFrance_town (114k triangles, 1000 samples): 1.2 s on one desktop core.
SunFit FitBakedSun(const Occluders &occluders, std::span<const SunFitSample> samples, float reach = 200.0f,
                   unsigned threads = 1);

} // namespace storm::fx
