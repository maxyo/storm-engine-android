// One bounce of indirect light: a shadowed wall, the ground in an alley, a character in the shade take the light the
// lit surfaces around them reflect, in those surfaces' colour.
//
// The scene (BuildBounceScene, once per place, cached): the place's drawn still geometry cut into surfels of about
// `surfel` metres (each with its position, normal and the mean colour of its texture there), and from every point of a
// lattice over the visibility volume's cells (every `stride`-th cell) `rays` rays over the whole sphere, each with the
// surfel and the side of it it hits first (or the sky).
//
// The light (per sun direction, per lamp set): the direct light each side of each surfel gets, in the units the
// shader lights with (a light's colour times its cosine and falloff, like fxSunDiffuse and ffLampAdd), with its
// shadow traced: SunOnSurfels, LampsOnSurfels.
//
// The field (GatherBounce): per lattice point, per channel, the L0 and L1 moments of the light the surfels reflect
// towards it (their albedo times their light; the sky gives none). The irradiance a surface with normal n receives
// there, in the same units, is c + 2 m.n (the Lambert lobe on a linear radiance), at least 0: a lit floor does not
// relight itself from the cell above it, a shadowed wall takes the light of the street in front of it.
//
// The texture: the field, trilinear between lattice points, on every cell of the visibility volume. Block 2 of the
// visibility texture (ff_fragment.sh ffBounce) holds sqrt(c / kBounceRange) in RGB and in A the y of
// v = 2 Y(m) / Y(c) (luminance, |v| <= 2); block 1's B and A hold its x and z. The shader's irradiance is
// c * max(0, 1 + v.n): the direction from the luminance, the colour from c.
#pragma once

#include "storm/fx/lamp_shadows.hpp"
#include "storm/fx/sun_fit.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace storm::fx
{

// Bump on any change of the scene bake that changes its output (the disk cache is keyed by it).
inline constexpr uint32_t kBounceBakeVersion = 2;
// The encoded colour: sqrt(c / kBounceRange), so the dark end keeps its steps.
inline constexpr float kBounceRange = 2.0f;
inline constexpr uint32_t kBounceNoTexture = 0xffffffffu;

using Rgb = std::array<float, 3>;

// A texture's colours from a small level (its mean over the texels under each), 3 bytes per texel. The surfaces
// reflect their stored colour times the light, as the fixed-function pipeline lights them (its lights and textures
// multiply as stored, not linearised): the bounce is in the units of the light it adds to.
struct BounceTexture
{
    int width = 0, height = 0;
    std::vector<uint8_t> srgb;
    bool Empty() const { return srgb.empty(); }
    Rgb At(float u, float v) const; // 0..1, bilinear, wrapping
};

struct BounceGeometry
{
    std::span<const Vec3> positions;
    std::span<const std::array<float, 2>> uvs;    // per position (empty: none)
    std::span<const uint32_t> indices;
    std::span<const uint32_t> triangle_texture;   // per triangle: into textures, or kBounceNoTexture
    std::span<const BounceTexture *const> textures; // null: unreadable
};

// The visibility volume's cells the field is laid over.
struct BounceGrid
{
    std::array<int, 3> size{0, 0, 0};
    Vec3 origin{0.0f, 0.0f, 0.0f};
    float cell = 0.0f;
};

struct BounceParams
{
    float surfel = 0.5f;                // metres, at least the cell size / 2
    int rays = 32;                      // per lattice point, over the sphere
    uint32_t max_records = 1u << 21;    // lattice points x rays; the stride grows until they fit
    float reach = 40.0f;                // metres
    unsigned threads = 1;
};

struct BounceScene
{
    BounceGrid grid;
    int stride = 1;
    std::array<int, 3> points{0, 0, 0}; // lattice points along x, y, z (x fastest)
    int rays = 0;
    std::vector<Vec3> position, normal; // per surfel (normal: the triangle's, by its winding)
    std::vector<Rgb> albedo;            // per surfel, 0..1
    // Per lattice point x ray: kMiss (the sky or beyond reach), else surfel * 2 + side (1: the side the normal faces).
    std::vector<uint32_t> hit;
    std::vector<uint8_t> inside;        // per lattice point: most rays hit back faces (in a wall, under the ground)
    std::vector<uint8_t> link;          // per lattice point: bit k, nothing between it and its +k neighbour
    uint32_t untextured = 0;            // triangles without a readable texture: grey 0.5 (logged)
    static constexpr uint32_t kMiss = 0xffffffffu;

    bool Empty() const { return hit.empty(); }
    size_t Points() const { return static_cast<size_t>(points[0]) * points[1] * points[2]; }
    Vec3 PointPosition(size_t index) const;
};

BounceScene BuildBounceScene(const Occluders &occluders, const BounceGeometry &geometry, const BounceGrid &grid,
                             const BounceParams &params);
uint64_t BounceKey(const BounceGeometry &geometry, const BounceGrid &grid, const BounceParams &params);
std::vector<uint8_t> EncodeBounceScene(const BounceScene &scene, uint64_t key);
std::optional<BounceScene> DecodeBounceScene(std::span<const uint8_t> bytes, uint64_t key, std::string &error);

// Per surfel, its two sides' direct light: [2 i] the back, [2 i + 1] the front (the side its normal faces).
using SurfelLight = std::vector<Rgb>;

// The sun (unit `sun`, towards it) of colour `color` on the surfels, its shadow traced through `occluders`.
SurfelLight SunOnSurfels(const Occluders &occluders, const BounceScene &scene, const Vec3 &sun, const Rgb &color,
                         unsigned threads);
// The place's lamps (range: the radius the relight windows them to, LampRadius) as ffLampAdd lights them, each
// from no nearer than its `holder` size, their shadows traced.
SurfelLight LampsOnSurfels(const Occluders &occluders, const BounceScene &scene, std::span<const Lamp> lamps,
                           std::span<const float> holder, unsigned threads);

struct BounceField
{
    std::array<int, 3> points{0, 0, 0};
    Vec3 origin{0.0f, 0.0f, 0.0f}; // world position of lattice point 0
    float spacing = 0.0f;          // metres between lattice points
    std::vector<float> moments;    // 12 per point: c (rgb), then m.x (rgb), m.y (rgb), m.z (rgb)

    bool Empty() const { return moments.empty(); }
    // Irradiance on a surface with unit normal `n` at `world` (trilinear between the points), as the shader reads the
    // texture: c (1 + v.n), at least 0.
    Rgb Irradiance(const Vec3 &world, const Vec3 &n) const;
    // Luminance of c summed over the points (the lamp share of a field's light).
    double Energy() const;
};

// `light`: SurfelLight of the scene (sun, lamps or their sum). Points inside the geometry take their open neighbours'
// mean; then one [1 2 1] blur along each axis (32 rays leave noise). Both only between linked neighbours: no light
// passes through a wall into the next room.
BounceField GatherBounce(const BounceScene &scene, const SurfelLight &light, unsigned threads);

// The two blocks' bytes of every cell of `grid`: `visibility` (RGBA8, its R and G kept) gets v.x / v.z in B / A,
// `bounce` (RGBA8, resized) sqrt(c / kBounceRange) and v.y. `scale`: c and m of `field` times it per channel
// (a field gathered for a white sun, lit by the weather's), plus `add` (unscaled, may be empty).
void EncodeBounceBlocks(const BounceGrid &grid, const BounceField &field, const Rgb &scale, const BounceField *add,
                        std::span<uint8_t> visibility, std::vector<uint8_t> &bounce, unsigned threads = 1);

} // namespace storm::fx
