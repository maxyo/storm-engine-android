#pragma once
// The exact clip of a polygon by the sides of the blob shadow's projector (Shadow::project).
#include "plane.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>

namespace storm::shadow
{
// The part of a convex polygon (at most 16 vertices) behind all of `planes` (a point is behind one when
// N*p - D <= 0); returns its vertex count, 0 when nothing is left.
inline int32_t clipPolygon(const PLANE *planes, int32_t nplanes, glm::vec3 *poly, int32_t n)
{
    glm::vec3 other[32];
    glm::vec3 *in = poly, *out = other;
    for (int32_t p = 0; p < nplanes && n > 0; p++)
    {
        const PLANE &pl = planes[p];
        const auto side = [&](const glm::vec3 &v) { return pl.Nx * v.x + pl.Ny * v.y + pl.Nz * v.z - pl.D; };
        int32_t m = 0;
        for (int32_t i = 0; i < n; i++)
        {
            const glm::vec3 &a = in[i];
            const glm::vec3 &b = in[(i + 1) % n];
            const float da = side(a), db = side(b);
            if (da <= 0.0f)
                out[m++] = a;
            if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f))
                out[m++] = a + (b - a) * (da / (da - db));
        }
        std::swap(in, out);
        n = m;
    }
    if (in != poly)
        std::copy(in, in + n, poly);
    return n;
}
} // namespace storm::shadow
