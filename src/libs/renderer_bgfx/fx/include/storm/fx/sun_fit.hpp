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

#include "storm/fx/cutout.hpp"
#include "storm/fx/sky_model.hpp"

#include <array>
#include <memory>

#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

// Triangles on a uniform grid for any-hit rays (3D DDA).
class Occluders
{
  public:
    Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices, float cell = 2.0f);
    // Triangles whose texture's alpha cuts holes (foliage, nets): a ray passes where the mask at the hit is at or
    // below the material's reference, as the colour pass's alpha test lets the background through (fx/cutout.hpp).
    struct Cut
    {
        std::shared_ptr<const AlphaMask> mask;
        float reference = 0.5f;
    };
    // `uvs` per position; `cut` per triangle (an empty mask: solid).
    Occluders(std::span<const Vec3> positions, std::span<const uint32_t> indices, std::span<const std::array<float, 2>> uvs,
              std::vector<Cut> cut, float cell = 2.0f);
    bool Empty() const { return triangles_ == 0; }
    // Per-thread state of the rays (the triangles a ray tested already).
    struct Scratch
    {
        std::vector<uint32_t> stamp;
        uint32_t ray = 0;
    };
    // True when the ray from `origin` along unit `dir` hits a triangle within `reach`.
    bool Occluded(const Vec3 &origin, const Vec3 &dir, float reach, Scratch &scratch) const;

  private:
    std::vector<Vec3> positions_;
    std::vector<uint32_t> indices_;
    size_t triangles_ = 0;
    Vec3 lo_{}, hi_{};
    float cell_ = 2.0f;
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
