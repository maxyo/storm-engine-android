// Far LOD counters (storm/render/far_lod.hpp).
#include <storm/render/far_lod.hpp>

#include <cstdio>

namespace storm::render::far_lod
{
std::string Take(uint64_t frames)
{
    std::string line;
    if (frames > 0)
    {
        static constexpr const char *kPassName[kPasses] = {"main", "refl", "cube"};
        const double n = static_cast<double>(frames);
        char item[128];
        for (int p = 0; p < kPasses; ++p)
        {
            std::snprintf(item, sizeof(item), " far_lod_%s_kt=%.1f far_lod_%s_full_kt=%.1f", kPassName[p],
                          counters.drawn_triangles[p] / n / 1000.0, kPassName[p], counters.full_triangles[p] / n / 1000.0);
            line += item;
        }
        std::snprintf(item, sizeof(item), " far_lod_composes=%.2f far_lod_compose_kb=%.1f far_lod_conflicts=%.2f "
                                          "island_slices=%.2f far_lod_pending=%d far_lod_px=%.1f",
                      counters.composes / n, counters.compose_bytes / n / 1024.0, counters.conflicts / n,
                      counters.island_slices / n, pending, pixels);
        line += item;
    }
    counters = {};
    return line;
}
} // namespace storm::render::far_lod
