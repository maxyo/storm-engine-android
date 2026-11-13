// How open to the sky every point of a location is: one volume for towns, interiors, ship decks and holds, baked from
// the place's collision data (Occluders; what the location's own traces hit) and sampled per pixel.
//
// Per cell, from its centre:
//   * sky (R): the cosine-weighted share of the upper hemisphere (around +y) that reaches past the place's geometry;
//     the rays run to the place's bounds (at most `reach`), so a hall or a cave chamber with a ceiling 12 m up reads
//     closed;
//   * open (G): the share of the directions more than 15° above the horizon that reach out, unweighted: whether a sun
//     in the sky can get here at all (an awning, an arcade, a gallery close it; a narrow alley keeps some).
// B and A are 0. A cell whose centre lies inside the geometry (every ray closed, its neighbours open) takes the mean of
// its open neighbours, so trilinear filtering next to walls does not pull the surfaces dark.
//
// The volume does not depend on the thread count; its cache key (VisibilityKey) covers everything it is made from.
#pragma once

#include "storm/fx/sun_fit.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storm::fx
{

// Bump on any change of the bake that changes its output (the disk cache is keyed by it).
inline constexpr uint32_t kVisibilityBakeVersion = 1;

struct VisibilityParams
{
    Vec3 lo{1.0f, 1.0f, 1.0f}; // the part of the world that gets cells; empty (lo > hi): the geometry's box
    Vec3 hi{-1.0f, -1.0f, -1.0f};
    float cell = 0.5f;               // metres; grown when the box would need more than max_cells
    uint32_t max_cells = 1u << 18;   // 64 x 64 x 64
    float reach = 60.0f;             // metres: no ray runs farther (nor beyond the bounds of the geometry)
    int rays = 24;                   // per cell, over the upper hemisphere
    unsigned threads = 1;
};

struct VisibilityVolume
{
    std::array<int, 3> size{0, 0, 0}; // cells along x, y, z
    Vec3 origin{0.0f, 0.0f, 0.0f};    // world corner of cell (0, 0, 0)
    float cell = 0.0f;
    std::vector<uint8_t> rgba;        // 4 per cell, x fastest, then y, then z

    bool Empty() const { return rgba.empty(); }
    // Trilinear between cell centres, clamped to the grid, as the GPU samples it: {sky, open}; {1, 1} when empty.
    std::array<float, 2> Sample(const Vec3 &world) const;
};

// `positions`, `indices`: the geometry the cells are laid over (its box, limited by params.lo..hi).
VisibilityVolume BakeVisibility(const Occluders &occluders, std::span<const Vec3> positions,
                                std::span<const uint32_t> indices, const VisibilityParams &params);

// Key of the bake: the geometry, the parameters that change the output and the version.
uint64_t VisibilityKey(std::span<const Vec3> positions, std::span<const uint32_t> indices, const VisibilityParams &params);
std::vector<uint8_t> EncodeVisibility(const VisibilityVolume &volume, uint64_t key);
// Refuses (nullopt, `error` says why) an entry of another key, size or version.
std::optional<VisibilityVolume> DecodeVisibility(std::span<const uint8_t> bytes, uint64_t key, std::string &error);

} // namespace storm::fx
