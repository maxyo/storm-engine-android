#pragma once
// Ray test against the skinned geometry of a model (MODELR::Trace): the shots of the hero's buckshot, sun glow and
// lightning occlusion test characters this way. Pure functions over the model's index buffer and its vertices
// already transformed by the bones, so a host check (tests/model_trace) runs the same code.
#include "storm_glm.h"

#include <cstdint>

namespace storm::model
{
// One object of a geometry: its triangles in the shared index buffer and the first vertex its indices count from.
struct TraceObject
{
    int32_t striangle = 0; // first triangle in the index buffer (GEOS::OBJECT::striangle, in triangles)
    int32_t ntriangles = 0;
    int32_t start_vertex = 0;
};

// Index of the first of the three indices of an object's triangle `triangle` in the index buffer.
constexpr int32_t triangleIndex(const TraceObject &object, int32_t triangle)
{
    return (object.striangle + triangle) * 3;
}

// Nearest hit of the segment src -> src + dir on the object's triangles, as a fraction of the segment, if closer
// than `best` (Möller-Trumbore); `best` otherwise.
inline float traceObject(const uint16_t *indices, const glm::vec3 *vertices, const TraceObject &object,
                         const glm::vec3 &src, const glm::vec3 &dir, float best)
{
    for (int32_t triangle = 0; triangle < object.ntriangles; triangle++)
    {
        const int32_t t = triangleIndex(object, triangle);
        const glm::vec3 &v0 = vertices[indices[t + 0] + object.start_vertex];
        const glm::vec3 a = vertices[indices[t + 1] + object.start_vertex] - v0;
        const glm::vec3 b = vertices[indices[t + 2] + object.start_vertex] - v0;
        const glm::vec3 pvec = glm::cross(dir, b);
        const float invdet = 1.0f / glm::dot(a, pvec);

        const glm::vec3 c = src - v0;
        const float U = glm::dot(c, pvec) * invdet;
        if (U < 0.0f || U > 1.0f)
            continue;

        const glm::vec3 qvec = glm::cross(c, a);
        const float V = glm::dot(dir, qvec) * invdet;
        if (V < 0.0f || U + V > 1.0f)
            continue;

        const float d = glm::dot(b, qvec) * invdet;
        if (d >= 0.0f && d <= 1.0f && d < best)
            best = d;
    }
    return best;
}
} // namespace storm::model
