#pragma once
// Ships vs. island overhangs.
//
// The island depth map (foam/islands/<island>.tga) says where the sea floor is too shallow; it knows
// nothing about rock above the water. Where a cliff lip or overhang hangs over deep water, a hull that
// only checked the depth map sailed under it and stood inside the island. A hull of height `height`
// also touches the island where the island mesh lies between the water and that height.

#include "storm_glm.h"

namespace storm::island
{

// Lowest point of the upward check: just above the water, so the sea floor under the hull never counts.
inline constexpr float kOverheadCheckFrom = 0.5f;

// `trace(src, dst)`: the island collision trace, <= 1 when the segment hits the island.
template <class Trace> bool OverheadContact(Trace trace, float x, float z, float height)
{
    if (height <= kOverheadCheckFrom)
        return false;
    return trace(glm::vec3(x, kOverheadCheckFrom, z), glm::vec3(x, height, z)) <= 1.0f;
}

} // namespace storm::island
