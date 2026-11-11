// storm::island::OverheadContact (src/libs/island/include/island_overhang.hpp) against a synthetic island:
// a rock lip over deep water (a horizontal slab at y = 11 over x in [0, 20]) and a sea floor at y = -20.
// A ship whose top is above the lip touches it; a low boat passes under; open water never touches.
#include "island_overhang.hpp"

#include <cassert>
#include <cstdio>

namespace
{
int traces = 0;
// Segment vs. the slab y = 11 (x in [0, 20]) and the floor y = -20: fraction of the hit, 2 for none.
float trace(const glm::vec3 &a, const glm::vec3 &b)
{
    ++traces;
    float best = 2.0f;
    for (const float planeY : {11.0f, -20.0f})
    {
        if ((a.y - planeY) * (b.y - planeY) > 0.0f || a.y == b.y)
            continue;
        const float t = (planeY - a.y) / (b.y - a.y);
        const float x = a.x + (b.x - a.x) * t;
        if (planeY == 11.0f && (x < 0.0f || x > 20.0f))
            continue;
        if (t < best)
            best = t;
    }
    return best;
}
} // namespace

int main()
{
    using storm::island::OverheadContact;
    assert(OverheadContact(trace, 10.0f, 0.0f, 30.0f));   // masts reach the lip: contact, like grounding
    assert(!OverheadContact(trace, 10.0f, 0.0f, 8.0f));   // a hull lower than the lip passes under it
    assert(!OverheadContact(trace, 30.0f, 0.0f, 30.0f));  // open water beside the lip
    assert(!OverheadContact(trace, -5.0f, 0.0f, 300.0f)); // the sea floor below the water never counts
    traces = 0;
    assert(!OverheadContact(trace, 10.0f, 0.0f, 0.0f) && traces == 0); // no height: no check at all
    std::puts("PASS: a ship touches rock hanging over the water below its top, never the sea floor");
}
