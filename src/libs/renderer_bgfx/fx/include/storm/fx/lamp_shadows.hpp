// CPU side of the lamps that relight a location per pixel, each with its own shadow maps (the renderer's part:
// src/effects/lamp_shadows.hpp). Candles of one fixture merge into one lamp; a lamp lights within its radius; the
// lamps around the camera get a slot: six cube faces in a 2D depth atlas holding the distance to the lamp, and a
// number in the lamp grid that tells every cell of the place which lamps reach it. The room light is the colour the
// place's own baked vertex colours have around a point: the ambient of the models that walk through it.
//
// Face convention (shared with ff_fragment.sh ffLampVisibility): for L = P - lamp, the face is the axis with the
// largest |component|; u = dot(L, right) / |major|, v = dot(L, up) / |major|, both -1..1 over the face (a guard band
// widens the projected field so filtering never reads the neighbour face).
#pragma once

#include "storm/fx/light_falloff.hpp"
#include "storm/fx/sky_model.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr int kMaxShadowLamps = 16;
// Metres: the faces' near plane. Sconces stand a few centimetres off their wall and candles 15-25 cm over their table:
// both must be in the map. The lamp's own holder is kept out by kLampHolderRadius instead.
inline constexpr float kLampShadowNear = 0.01f;
// Metres around the flame in which the still geometry casts nothing (the wick and the flame's own glow). The lamp's
// fixture (FindFixtures) casts nothing whatever its size; for a lamp whose fixture is not found this sphere is all.
inline constexpr float kLampHolderRadius = 0.12f;
inline constexpr int kLampGridCapacity = 8; // lamps per cell of the lamp grid
inline constexpr float kLampFaceGuard = 1.05f;  // field of view scale over 90°: the face's border texels

struct Lamp
{
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f}; // diffuse colour with the current intensity
    // Its shape (light_falloff.hpp, from the game light's range and attenuation): it lights nothing beyond `range`,
    // which is also the far end of its shadow faces.
    float range = 10.0f;
    float intensity = 1.0f;
    float core = 0.4f;
};
LightShape ShapeOf(const Lamp &lamp);

// Candles of one candlestick or sconce are separate lights a few centimetres apart: one shadow for them. Lamps
// closer than `radius` merge (colour summed, position weighted by brightness, the largest range, intensity and core
// of its flames: a shape that does not follow their flicker).
std::vector<Lamp> ClusterLamps(std::span<const Lamp> lamps, float radius);

// A lamp's own fixture: the pieces of the still geometry (triangles joined by shared vertices) that hold its flames
// (a candlestick, a torch, a lantern's frame and glass, a chandelier, a sconce's arm) and what carries them from below
// or above (a lantern's post, a chandelier's chain). They cast no shadow of that lamp: a flame sits inside its own
// fixture, whose real light fills in the shadows of its bars. A piece holds a flame when
//   * the flame is inside its box (grown by kFixtureEnclose) and it is at most kFixtureEnclosing across, or
//   * the flame is within kFixtureTouch of its box and it is narrow (at most kFixtureNarrow across in x and z, at
//     most kFixtureTall high);
// narrow pieces stacked on those (their x-z boxes overlap, their heights meet within kFixtureGap) join, while the
// fixture stays narrow and at most kFixtureTall high. A table under a candle, a wall behind a sconce, a beam over a
// lamp are no fixture: they are wide. `flames`: per lamp the positions of its flames.
inline constexpr float kFixtureEnclose = 0.05f;
inline constexpr float kFixtureEnclosing = 2.0f;
inline constexpr float kFixtureTouch = 0.25f;
inline constexpr float kFixtureNarrow = 0.6f;
inline constexpr float kFixtureTall = 5.0f;
inline constexpr float kFixtureGap = 0.03f;
struct Fixture
{
    std::vector<uint32_t> triangles; // triangle numbers (index triple t is indices[3 t .. 3 t + 2]), ascending
    int pieces = 0;
    Vec3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f}; // its box (pieces > 0)
};
std::vector<Fixture> FindFixtures(std::span<const Vec3> positions, std::span<const uint32_t> indices,
                                  std::span<const std::vector<Vec3>> flames);

// Which lamps reach which part of the room: a grid of `cell`-sized cells over lo..hi holding up to kLampGridCapacity
// lamp numbers (index + 1, 0 = none; strongest at the cell first) per cell. `rgba` is a 3D RGBA8 image of
// 2 nx x ny x nz texels: the first four numbers of cell (x, y, z) at texel x, the other four at texel nx + x.
struct LampGrid
{
    int nx = 0, ny = 0, nz = 0;
    Vec3 lo{0.0f, 0.0f, 0.0f};
    Vec3 cell{1.0f, 1.0f, 1.0f};
    std::vector<uint8_t> rgba;
    int overflow_cells = 0; // cells reached by more lamps than they hold (the weakest there are left out)
    int max_lamps = 0;      // the most lamps over one cell
};
// `lamps`: range = the radius each one lights. Cells are at least `cell` metres and at most
// max_cells per axis (larger rooms get larger cells).
LampGrid BuildLampGrid(std::span<const Lamp> lamps, Vec3 lo, Vec3 hi, float cell, int max_cells_xz, int max_cells_y);

// The light of a place around a point, from its baked vertex colours: a grid of cells over lo..hi, each holding the
// mean colour of the surfaces in and around it (cells without a surface take their neighbours'). It is the light a
// model standing there receives, which is what falls on level surfaces (floors, table tops: the baked colour of a
// floor is the light that reaches it from above and around); walls, which the bakes keep darker, count for a fifth
// of their area. `rgba` is a 3D RGBA8 image of nx x ny x nz texels, to be sampled with linear filtering.
struct RoomLight
{
    int nx = 0, ny = 0, nz = 0;
    Vec3 lo{0.0f, 0.0f, 0.0f};
    Vec3 cell{1.0f, 1.0f, 1.0f};
    std::vector<uint8_t> rgba;
};
// `colors`: one D3DCOLOR (0xAARRGGBB) per position. Cells are at least `cell` metres wide and `cell_y` high (a
// storey: the floor and what stands on it share a cell) and at most max_cells per axis.
// Empty (no rgba) when no surface lies inside lo..hi.
RoomLight BuildRoomLight(std::span<const Vec3> positions, std::span<const uint32_t> colors, std::span<const uint32_t> indices,
                         Vec3 lo, Vec3 hi, float cell, float cell_y, int max_cells_xz, int max_cells_y);

// The triangles of a place by where they are, so the ones around a lamp are found without walking all of them (a lamp
// that takes a slot, a carried lantern every frame it moves): a grid of cells of `cell` metres over the triangles'
// bounds (at most kTriangleGridCells per axis; larger places get larger cells), each listing the triangles whose
// bounding box touches it.
inline constexpr int kTriangleGridCells = 48;
struct TriangleGrid
{
    std::array<int, 3> size{0, 0, 0};
    Vec3 lo{0.0f, 0.0f, 0.0f};
    Vec3 cell{1.0f, 1.0f, 1.0f};
    std::vector<uint32_t> first;     // per cell (x fastest, then y, then z): where its list starts in `triangles`; one extra at the end
    std::vector<uint32_t> triangles; // triangle numbers (index triple t is indices[3 t .. 3 t + 2])
    std::vector<std::array<Vec3, 2>> box; // per triangle: its bounding box (lo > hi: a triangle with a bad index)
    size_t count = 0;                // triangles
};
TriangleGrid BuildTriangleGrid(std::span<const Vec3> positions, std::span<const uint32_t> indices, float cell);
// The triangles (as index triples of `indices`, in the order of `indices`) that can cast a shadow of a lamp at
// `center` lighting `radius` metres: those whose bounding box touches that sphere, but for the triangle numbers of
// `indices` in `skip` (ascending: the lamp's own fixture). Appended to `out`; `seen` is scratch memory the caller keeps
// between calls.
void TrianglesInSphere(const TriangleGrid &grid, std::span<const uint32_t> indices, Vec3 center, float radius,
                       std::vector<uint32_t> &out, std::vector<uint32_t> &seen, std::span<const uint32_t> skip = {});

struct CubeFace
{
    Vec3 forward, right, up;
};
// +X, -X, +Y, -Y, +Z, -Z.
const std::array<CubeFace, 6> &CubeFaces();

// World -> clip of `face` of a lamp at `position` (row-vector convention as bx; clip z / w = 0..1 over near..far),
// with the guard band. The renderer maps the clip square into the face's atlas tile (TileClip).
std::array<float, 16> LampFaceViewProj(Vec3 position, int face, float far_plane);

// True when a sphere may cover pixels of `face` (conservative: tested against the face's four side planes with the
// guard band, and in front of the lamp). Characters are drawn only into the faces they can shadow.
bool SphereInLampFace(Vec3 lamp, int face, Vec3 center, float radius);

// Atlas tiles: lamp slot s, face f -> tile s * 6 + f, `columns` x `rows` tiles of equal size. A tile in atlas
// coordinates measured from the top-left corner (0..1): x0, y0 and its size sx, sy.
struct AtlasTile
{
    float x0, y0, sx, sy;
};
AtlasTile LampAtlasTile(int slot, int face, int columns, int rows);
// Clip-space transform (row-vector) that maps a face's clip square onto its tile of the atlas render target (atlas
// clip y up, tile y0 from the top). Render into the tile with this after LampFaceViewProj and a scissor on the tile.
std::array<float, 16> TileClip(const AtlasTile &tile);

// The receiver side, as the shader computes it: the face and the atlas position from the top-left (sample at u = x,
// v = origin_bottom_left ? 1 - y : y). The value compared there is the distance to the lamp over its radius.
struct LampFaceSample
{
    int face;
    float x, y;
};
LampFaceSample LampFaceCoord(Vec3 lamp, Vec3 point, int slot, int columns, int rows);
// The caster side: what fs_ff_lamp_caster computes from a fragment at clip position (x, y, w) of a face (before the
// tile transform): the distance to the lamp.
float LampCasterDistance(float clip_x, float clip_y, float clip_w);

} // namespace storm::fx
