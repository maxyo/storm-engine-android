// Far-LOD ceiling measurement counters (storm/render/far_lod_stats.hpp).
#include <storm/render/far_lod_stats.hpp>

#include <cstdio>
#include <strings.h>

namespace storm::render::far_lod
{
Category CategoryOf(const std::string &model)
{
    const auto starts = [&](const char *prefix) { return strncasecmp(model.c_str(), prefix, std::char_traits<char>::length(prefix)) == 0; };
    if (starts("islands"))
        return Island;
    if (starts("ships"))
        return Ship;
    if (starts("locations"))
        return Location;
    return Other;
}

std::string Take(uint64_t frames)
{
    std::string line;
    if (frames > 0)
    {
        char item[96];
        const double n = static_cast<double>(frames);
        for (int c = 0; c < kCategories; ++c)
        {
            for (int p = 0; p < kPasses; ++p)
            {
                std::snprintf(item, sizeof(item), " fl_%s_%s_kt=%.1f fl_%s_%s_draws=%.1f", kCategoryName[c], kPassName[p],
                              totals.triangles[c][p] / n / 1000.0, kCategoryName[c], kPassName[p], totals.draws[c][p] / n);
                line += item;
            }
            std::snprintf(item, sizeof(item), " fl_%s_culled_kt=%.1f", kCategoryName[c], totals.culled[c] / n / 1000.0);
            line += item;
        }
        std::snprintf(item, sizeof(item), " fl_island_slices=%.2f fl_cull_m=%.0f", totals.island_slices / n, cull_m);
        line += item;
    }
    totals = {};
    return line;
}
} // namespace storm::render::far_lod
