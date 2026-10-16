#pragma once

// Legacy callers may include this boundary inside pack(1). GLM calculation
// types and their standard-library dependencies require the native ABI packing.
#pragma pack(push)
#pragma pack()
#include <cstring>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

static_assert(GLM_VERSION_MAJOR == 1 && GLM_VERSION_MINOR == 0 && GLM_VERSION_PATCH == 3,
              "Storm math is validated against pinned GLM 1.0.3");

namespace storm::math
{
// These types are calculation temporaries, never storage/serialization types.
// Explicit packed qualifiers avoid a caller's default-aligned GLM setting.
using GlmVec2 = glm::vec<2, float, glm::packed_highp>;
using GlmVec3 = glm::vec<3, float, glm::packed_highp>;
using GlmVec4 = glm::vec<4, float, glm::packed_highp>;
using GlmDVec2 = glm::vec<2, double, glm::packed_highp>;
using GlmDVec3 = glm::vec<3, double, glm::packed_highp>;
using GlmDVec4 = glm::vec<4, double, glm::packed_highp>;
using GlmQuat = glm::qua<float, glm::packed_highp>;
using GlmMat3 = glm::mat<3, 3, float, glm::packed_highp>;
using GlmMat4 = glm::mat<4, 4, float, glm::packed_highp>;

// Storm m[column][row] and GLM share the element arrangement. D3D consumes
// the same bytes as a row-vector matrix. Therefore Storm's A*B composition
// is represented by GLM(B)*GLM(A), not GLM(A)*GLM(B).
inline GlmMat4 loadGlmMatrix(const void *source)
{
    float values[16];
    std::memcpy(values, source, sizeof(values));
    GlmMat4 result(0.0f);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result[column][row] = values[column * 4 + row];
    return result;
}

inline void storeGlmMatrix(void *destination, const GlmMat4 &source)
{
    float values[16];
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            values[column * 4 + row] = source[column][row];
    std::memcpy(destination, values, sizeof(values));
}
} // namespace storm::math

#pragma pack(pop)
