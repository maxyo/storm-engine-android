// MODELR::Trace's skinned-geometry ray test (src/libs/model/src/skinned_trace.hpp) on a two-object model: the second
// object's triangles start at striangle * 3 in the shared index buffer, its indices count from its start_vertex.
#include "skinned_trace.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;
void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
} // namespace

int main()
{
    using storm::model::TraceObject;
    // Object 0: one triangle in the plane z = 5 near the origin. Object 1: two triangles in the plane z = 2 around
    // x = 10 (its vertices after object 0's, its triangles after object 0's).
    const std::vector<glm::vec3> vertices = {
        {-1, -1, 5}, {1, -1, 5}, {0, 1, 5},                // object 0
        {9, -1, 2},  {11, -1, 2}, {11, 1, 2}, {9, 1, 2},   // object 1
    };
    const std::vector<uint16_t> indices = {
        0, 1, 2,          // object 0, triangle 0
        0, 1, 2, 0, 2, 3, // object 1, triangles 0-1 (relative to its start_vertex 3)
    };
    const TraceObject first{0, 1, 0}, second{1, 2, 3};
    check(storm::model::triangleIndex(second, 0) == 3 && storm::model::triangleIndex(second, 1) == 6,
          "the second object's triangles start at striangle * 3");

    // A ray along +z through x = 10.5, y = 0.5 hits object 1 at z = 2 (fraction 0.2 of a 10 long segment).
    const glm::vec3 src(10.5f, 0.5f, 0.0f), dir(0.0f, 0.0f, 10.0f);
    float best = 2.0f;
    best = storm::model::traceObject(indices.data(), vertices.data(), first, src, dir, best);
    check(best == 2.0f, "the ray misses object 0");
    best = storm::model::traceObject(indices.data(), vertices.data(), second, src, dir, best);
    check(std::fabs(best - 0.2f) < 1e-6f, "the ray hits object 1 at 0.2");
    // A ray through object 0 hits it at 0.5, and object 1 does not take that away.
    best = 2.0f;
    best = storm::model::traceObject(indices.data(), vertices.data(), first, {0.0f, 0.0f, 0.0f}, dir, best);
    best = storm::model::traceObject(indices.data(), vertices.data(), second, {0.0f, 0.0f, 0.0f}, dir, best);
    check(std::fabs(best - 0.5f) < 1e-6f, "the ray hits object 0 at 0.5");
    // The formula the engine had (gt * 3 + striangle) reads the second object's first triangle from index 1:
    // (1, 2, 0) of object 0, i.e. vertices 4, 5, 3 of object 1, and its second triangle from index 4 (1, 2, 0 again):
    // the second triangle of object 1 (9..11 x 0..1 upper-left half) is never tested.
    const glm::vec3 upperLeft(9.3f, 0.7f, 0.0f);
    best = storm::model::traceObject(indices.data(), vertices.data(), second, upperLeft, dir, 2.0f);
    check(std::fabs(best - 0.2f) < 1e-6f, "the second object's second triangle is hit");

    if (failures == 0)
        std::printf("PASS: skinned trace over two objects\n");
    return failures == 0 ? 0 : 1;
}
