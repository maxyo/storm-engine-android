// The exact clip of the blob shadow's polygons (polygon_clip.hpp): what is behind all planes stays, the area of the
// kept part is the area of the polygon inside the volume, and clipping a polygon that was clipped by a wider volume
// gives the same polygon as clipping it directly (the fan of Shadow::Fan relies on that).
#include "polygon_clip.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
float area(const glm::vec3 *p, int n)
{
    glm::vec3 sum(0.0f);
    for (int i = 1; i + 1 < n; i++)
        sum += glm::cross(p[i] - p[0], p[i + 1] - p[0]);
    return 0.5f * glm::length(sum);
}

int failures = 0;
void expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}
} // namespace

int main()
{
    // The box x in [-1, 1], z in [-1, 1] (a point is behind a plane when N*p - D <= 0)
    const PLANE box[4] = {{1, 0, 0, 1}, {-1, 0, 0, 1}, {0, 0, 1, 1}, {0, 0, -1, 1}};
    PLANE wide[4];
    for (int i = 0; i < 4; i++)
    {
        wide[i] = box[i];
        wide[i].D += 0.5f;
    }

    glm::vec3 inside[32] = {{-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}, {0, 0, 0.5f}};
    expect(storm::shadow::clipPolygon(box, 4, inside, 3) == 3, "a polygon inside stays whole");

    glm::vec3 outside[32] = {{2, 0, 2}, {3, 0, 2}, {2, 0, 3}};
    expect(storm::shadow::clipPolygon(box, 4, outside, 3) == 0, "a polygon outside is dropped");

    // A large triangle covering the box: the part inside is the box itself (area 4)
    glm::vec3 cover[32] = {{-10, 0, -10}, {10, 0, -10}, {0, 0, 10}};
    int n = storm::shadow::clipPolygon(box, 4, cover, 3);
    expect(n >= 3 && std::fabs(area(cover, n) - 4.0f) < 1e-3f, "a triangle over the box keeps its square");

    // Half the polygon is out: x in [0, 2] -> [0, 1]
    glm::vec3 half[32] = {{0, 0, -0.5f}, {2, 0, -0.5f}, {2, 0, 0.5f}, {0, 0, 0.5f}};
    n = storm::shadow::clipPolygon(box, 4, half, 4);
    expect(n >= 3 && std::fabs(area(half, n) - 1.0f) < 1e-4f, "a polygon crossing a side keeps the part inside");

    // Clipping by the wide volume first changes nothing
    glm::vec3 direct[32] = {{-10, 0, -3}, {10, 0, -1}, {1, 0, 10}};
    glm::vec3 twice[32] = {{-10, 0, -3}, {10, 0, -1}, {1, 0, 10}};
    const int nd = storm::shadow::clipPolygon(box, 4, direct, 3);
    int nw = storm::shadow::clipPolygon(wide, 4, twice, 3);
    nw = storm::shadow::clipPolygon(box, 4, twice, nw);
    expect(nd >= 3 && nw >= 3 && std::fabs(area(direct, nd) - area(twice, nw)) < 1e-3f,
           "clipping through a wider volume gives the same area");

    if (failures == 0)
        std::printf("shadow_polygon_clip: ok\n");
    return failures == 0 ? 0 : 1;
}
