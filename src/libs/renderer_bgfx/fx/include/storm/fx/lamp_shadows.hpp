// CPU side of the lamp shadows of interiors: the few strongest location lamps around the camera (fireplaces,
// candle clusters, lanterns) get a cube of six depth faces packed into a 2D atlas. Static location geometry is drawn
// into one atlas when a lamp gets its slot; characters into a second atlas while they move. Receivers (fs_ff_lit,
// fs_ff_baked; ff_fragment.sh) select the face by the major axis of the lamp-to-pixel vector, compare against both
// atlases and remove that lamp's share of their light where it is hidden.
//
// Face convention (shared with ff_fragment.sh ffLampShadow): for L = P - lamp, the face is the axis with the largest
// |component|; u = dot(L, right) / |major|, v = dot(L, up) / |major|, both -1..1 over the face (a guard band widens
// the projected field so filtering never reads the neighbour face); depth = the perspective depth of |major|.
#pragma once

#include "storm/fx/sky_model.hpp"

#include <array>
#include <span>
#include <vector>

namespace storm::fx
{

inline constexpr int kMaxShadowLamps = 4;
inline constexpr float kLampShadowNear = 0.3f; // metres: the faces' near plane; the lamp's own holder, glass and candlestick stay out
inline constexpr float kLampFaceGuard = 1.05f;  // field of view scale over 90°: the face's border texels

struct Lamp
{
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f}; // diffuse colour with the current intensity
    float range = 10.0f;
    float att0 = 0.0f, att1 = 0.0f, att2 = 1.0f;
};

// Candles of one candlestick or sconce are separate lights a few centimetres apart: one shadow for them. Lamps
// closer than `radius` merge (colour summed, position weighted by brightness, the farthest range, the summed
// colour's attenuation of the brightest).
std::vector<Lamp> ClusterLamps(std::span<const Lamp> lamps, float radius);

// Brightness of `lamp` at `point` (the D3D attenuation times the colour's luminance, zero beyond its range).
float LampReach(const Lamp &lamp, Vec3 point);

// Up to `count` lamps that cast the most light around `eye`, strongest first. A lamp of `previous` keeps its slot
// while it stays within 1.5x of the weakest newcomer (no flicker of shadows when two are close).
std::vector<int> SelectShadowLamps(std::span<const Lamp> lamps, Vec3 eye, int count, std::span<const int> previous);

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

// The receiver side, as the shader computes it: face, atlas position from the top-left (sample at u = x,
// v = origin_bottom_left ? 1 - y : y) and the 0..1 depth to compare.
struct LampShadowSample
{
    int face;
    float x, y, depth;
};
LampShadowSample LampShadowCoord(Vec3 lamp, Vec3 point, float far_plane, int slot, int columns, int rows);

} // namespace storm::fx
