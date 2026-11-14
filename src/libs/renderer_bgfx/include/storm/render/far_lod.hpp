// Far LODs of the models (src/libs/geometry/far_lod/README.md): the run-time settings, the view the next model draw
// sees, and the counters. Game thread only.
#pragma once

#include <cstdint>
#include <string>

namespace storm::render::far_lod
{

enum Pass : int
{
    Main,       // the scene
    Reflection, // the sea's planar reflection
    CubeFace,   // the sea's environment cube faces
    kPasses
};

// The largest error a level may show, in pixels of the target being drawn (fx::Settings::far_lod_pixels, per
// preset); 0: far LODs off, every model drawn whole.
inline float pixels = 0.f;
// The pass being drawn: set by the sea's reflection and cube-face renders.
inline Pass pass = Main;

// What the next GEOM draw is seen from, in the model's own space (set by NODER::Draw; invalid: drawn whole).
struct View
{
    bool valid = false;
    float camera[3] = {};
    float forward[3] = {}; // the view direction (unit length in world space; `scale` converts)
    float scale = 1.f;     // world metres per model unit
    // Perspective: pixels per unit of view-space tangent (half the viewport height × the projection's y scale) times
    // the widening from the screen's centre to its corner (a level is chosen by distance, not depth). Orthographic:
    // pixels per metre, `ortho` set.
    float pixels_per_unit = 0.f;
    bool ortho = false;
    float near_plane = 0.f, far_plane = 0.f;
};
inline View view;

struct Counters
{
    uint64_t full_triangles[kPasses] = {}; // what the drawn objects have
    uint64_t drawn_triangles[kPasses] = {};
    uint64_t composes = 0;      // composed index lists (a cluster's level changed)
    uint64_t compose_bytes = 0;
    uint64_t conflicts = 0;     // objects drawn whole: their composed indices were already used this frame
    uint64_t island_slices = 0; // ISLAND::Realize depth slices
};
inline Counters counters;
// Models whose LODs are being made (current value).
inline int pending = 0;

// Per-frame means since the previous call (" far_lod_..." key=value pairs) and a restart.
std::string Take(uint64_t frames);

} // namespace storm::render::far_lod
