// Far-LOD ceiling measurement (branch test/far-lod-ceiling, local-testing/far-lod/DESIGN.md §7 step 1).
//
// Model triangles drawn per category (the model folder) and per pass (main, sea reflection, sea cube faces), and a
// cull of every model object farther than `cull_m` from the camera (graphics option fx_farlod_cull, metres, 0 off):
// the upper bound of what far LODs can save on a device. Game thread only.
#pragma once
#include <cstdint>
#include <string>

namespace storm::render::far_lod
{
enum Category : int { Island, Ship, Location, Other, kCategories };
enum Pass : int { Main, Reflection, CubeFace, kPasses };
inline constexpr const char *kCategoryName[kCategories] = {"isl", "ship", "loc", "oth"};
inline constexpr const char *kPassName[kPasses] = {"main", "refl", "cube"};

struct Totals
{
    uint64_t triangles[kCategories][kPasses] = {};
    uint64_t draws[kCategories][kPasses] = {};
    uint64_t culled[kCategories] = {}; // triangles left out by the cull
    uint64_t island_slices = 0;        // ISLAND::Realize depth slices
};

inline Totals totals;           // since the last Take()
inline float cull_m = 0.f;      // fx_farlod_cull
inline Pass pass = Main;        // set by the sea's reflection and cube-face renders

// Set by NODER::Draw for the GEOM::Draw that follows (category < 0: not counted).
struct Current
{
    int category = -1;
    float cam[3] = {}; // the camera in the node's space
};
inline Current current;

// The category of a model by its path relative to RESOURCE/MODELS.
Category CategoryOf(const std::string &model);
// Per-frame means of the totals since the previous call (key=value pairs), and a restart.
std::string Take(uint64_t frames);
} // namespace storm::render::far_lod
