// The log line of an Occluders that left triangles out (fx::Occluders `within`): what the place's rays cannot reach.
#pragma once

#include "shim/shim.hpp"
#include "storm/fx/sun_fit.hpp"

#include <string>

namespace storm::effects
{

inline void LogExcluded(const char *what, const std::string &name, const fx::Occluders &occluders)
{
    if (!occluders.Excluded())
        return;
    const fx::Box &b = occluders.ExcludedBounds();
    shim::logf("effects: %s of %s: %zu of %zu triangles out of the rays' reach left out (spanning x %.0f..%.0f, y %.0f..%.0f, "
               "z %.0f..%.0f m: a backdrop)",
               what, name.c_str(), occluders.Excluded(), occluders.Triangles(), b.lo[0], b.hi[0], b.lo[1], b.hi[1], b.lo[2],
               b.hi[2]);
}

} // namespace storm::effects
