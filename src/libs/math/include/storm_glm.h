#pragma once

// Storm math conventions on top of GLM (see README.md in this directory).
#include "float_to_int.h"
// GLM is configured here and nowhere else. Callers may include this header
// inside a pack(1) region; GLM and the standard library need native packing.
#pragma pack(push)
#pragma pack()
// Default-constructed GLM values are zero vectors, identity matrices and
// identity quaternions. Every translation unit must agree on this, so GLM is
// only ever included through this header.
#ifndef GLM_FORCE_CTOR_INIT
#define GLM_FORCE_CTOR_INIT
#endif
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
// The Euler extension matches the game's rotation orders. Opt in only for
// this include; do not change the caller's GLM feature policy.
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#define STORM_GLM_RESTORE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>
#ifdef STORM_GLM_RESTORE_EXPERIMENTAL
#undef STORM_GLM_RESTORE_EXPERIMENTAL
#undef GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#pragma pack(pop)

static_assert(GLM_VERSION_MAJOR == 1 && GLM_VERSION_MINOR == 0 && GLM_VERSION_PATCH == 3,
              "Storm math is validated against pinned GLM 1.0.3");
static_assert(GLM_CONFIG_CTOR_INIT != GLM_CTOR_INIT_DISABLE, "include GLM through storm_glm.h");
// Vertex buffers, saves and animation files hold these exact layouts.
static_assert(sizeof(glm::vec3) == 12 && sizeof(glm::dvec3) == 24 && sizeof(glm::vec4) == 16 &&
              sizeof(glm::mat4) == 64 && alignof(glm::mat4) == alignof(float),
              "GLM must use its packed default types");

#pragma pack(push)
#pragma pack()
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

struct D3DMATRIX;

namespace storm::math
{
// Squared length.
inline float lengthSquared(const glm::vec3 &v)
{
    return glm::dot(v, v);
}
inline double lengthSquared(const glm::dvec3 &v)
{
    return glm::dot(v, v);
}

// Normalization that maps the zero vector to zero instead of NaN. The
// reciprocal length is taken in double, as the game's physics was tuned with.
inline glm::vec3 normalizeOrZero(const glm::vec3 &v)
{
    const double squared = glm::dot(v, v);
    if (squared == 0.0)
        return glm::vec3(0.0f);
    return glm::vec3(glm::dvec3(v) * (1.0 / std::sqrt(squared)));
}

// Normalizes in place (the zero vector stays zero) and returns the previous
// length. Length and reciprocal are taken in double.
inline float normalizeInPlace(glm::vec3 &v)
{
    const glm::dvec3 value(v);
    const double length = glm::length(value);
    if (length > 0.0)
        v = glm::vec3(value * (1.0 / length));
    return static_cast<float>(length);
}

// Component-wise negation. GLM's unary minus computes 0 - v, which turns a
// zero component into +0 whatever its sign was.
inline glm::vec3 negated(const glm::vec3 &v)
{
    return glm::vec3(-v.x, -v.y, -v.z);
}

// a + (b - a) * k. Not glm::mix, which evaluates a * (1 - k) + b * k and
// rounds differently.
inline glm::vec3 lerp(const glm::vec3 &a, const glm::vec3 &b, float k)
{
    return a + (b - a) * k;
}

// Heading of the vector around the Y axis (0 along +Z, positive towards +X),
// or defAngle when it has no horizontal part.
inline float angleY(const glm::vec3 &v, float defAngle = 0.0f)
{
    const glm::dvec2 horizontal(v.x, v.z);
    auto len = glm::dot(horizontal, horizontal);
    if (len > 0.00000000001)
        len = std::acos(glm::clamp(v.z / std::sqrt(len), -1.0, 1.0));
    else
        return defAngle;
    if (v.x < 0)
        len = -len;
    return static_cast<float>(len);
}

// Length of the projection onto the horizontal (XZ) plane.
inline float lengthXZ(const glm::vec3 &v)
{
    return static_cast<float>(glm::length(glm::dvec2(static_cast<double>(v.x), static_cast<double>(v.z))));
}

//============================================================================
// Matrices
//
// glm::mat4 holds the same 16 floats, in the same order, as a D3D matrix:
// m[i][j] is D3D row i, column j, and translation lives in m[3]. The game
// thinks in D3D row vectors (p' = p * World * View * Proj); GLM multiplies
// column vectors, so that product is written Proj * View * World * p here.
// "Apply a, then b" is therefore  b * a.
//============================================================================

// Axis and position columns as vectors (the fourth component is skipped).
// These view a four-float column as a three-float vector in place, like the
// D3D structure views below; the engine is built with -fno-strict-aliasing
// (src/CMakeLists.txt) so that such views are well defined.
inline glm::vec3 &vx(glm::mat4 &m) { return *reinterpret_cast<glm::vec3 *>(&m[0]); }
inline glm::vec3 &vy(glm::mat4 &m) { return *reinterpret_cast<glm::vec3 *>(&m[1]); }
inline glm::vec3 &vz(glm::mat4 &m) { return *reinterpret_cast<glm::vec3 *>(&m[2]); }
inline glm::vec3 &pos(glm::mat4 &m) { return *reinterpret_cast<glm::vec3 *>(&m[3]); }
inline const glm::vec3 &vx(const glm::mat4 &m) { return *reinterpret_cast<const glm::vec3 *>(&m[0]); }
inline const glm::vec3 &vy(const glm::mat4 &m) { return *reinterpret_cast<const glm::vec3 *>(&m[1]); }
inline const glm::vec3 &vz(const glm::mat4 &m) { return *reinterpret_cast<const glm::vec3 *>(&m[2]); }
inline const glm::vec3 &pos(const glm::mat4 &m) { return *reinterpret_cast<const glm::vec3 *>(&m[3]); }

// The 16 floats in D3D order.
inline float *data(glm::mat4 &m) { return &m[0][0]; }
inline const float *data(const glm::mat4 &m) { return &m[0][0]; }

// The matrix as the D3D structure it is byte-compatible with. D3D's API takes
// non-const pointers even for input, hence the const overload.
inline D3DMATRIX *d3dMatrix(const glm::mat4 &m) { return reinterpret_cast<D3DMATRIX *>(const_cast<float *>(&m[0][0])); }

// Rotation Z, then X, then Y (the game's Euler order), then translation.
inline glm::mat4 eulerMatrix(float angX, float angY, float angZ, float x = 0.0f, float y = 0.0f, float z = 0.0f)
{
    glm::mat4 result(glm::eulerAngleYXZ(angY, angX, angZ));
    result[3] = glm::vec4(x, y, z, 1.0f);
    return result;
}
inline glm::mat4 eulerMatrix(const glm::vec3 &ang, const glm::vec3 &position = glm::vec3(0.0f))
{
    return eulerMatrix(ang.x, ang.y, ang.z, position.x, position.y, position.z);
}

inline glm::mat4 rotationX(float ang) { return glm::eulerAngleX(ang); }
inline glm::mat4 rotationY(float ang) { return glm::eulerAngleY(ang); }
inline glm::mat4 rotationZ(float ang) { return glm::eulerAngleZ(ang); }

inline glm::mat4 translation(float x, float y, float z)
{
    glm::mat4 result(1.0f);
    result[3] = glm::vec4(x, y, z, 1.0f);
    return result;
}
inline glm::mat4 translation(const glm::vec3 &p) { return translation(p.x, p.y, p.z); }

inline glm::mat4 scaling(float x, float y, float z)
{
    glm::mat4 result(1.0f);
    result[0][0] = x;
    result[1][1] = y;
    result[2][2] = z;
    return result;
}
inline glm::mat4 scaling(float scale) { return scaling(scale, scale, scale); }
inline glm::mat4 scaling(const glm::vec3 &scale) { return scaling(scale.x, scale.y, scale.z); }

// Point transform including translation; no perspective divide. Written out
// so the sum runs left to right: this is the hottest matrix operation.
inline glm::vec3 transformPoint(const glm::mat4 &m, const glm::vec3 &p)
{
    return glm::vec3(m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
                     m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
                     m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2]);
}

// World-to-local for a rotation+translation matrix without inverting it.
inline glm::vec3 inverseTransformNormal(const glm::mat4 &m, const glm::vec3 &n)
{
    return glm::vec3(n.x * m[0][0] + n.y * m[0][1] + n.z * m[0][2],
                     n.x * m[1][0] + n.y * m[1][1] + n.z * m[1][2],
                     n.x * m[2][0] + n.y * m[2][1] + n.z * m[2][2]);
}
inline glm::vec3 inverseTransformPoint(const glm::mat4 &m, const glm::vec3 &point)
{
    return inverseTransformNormal(m, point - glm::vec3(m[3]));
}

// Transposes the rotation block in place; the rest is untouched.
inline void transpose3x3(glm::mat4 &m)
{
    std::swap(m[0][1], m[1][0]);
    std::swap(m[0][2], m[2][0]);
    std::swap(m[1][2], m[2][1]);
}

// In-place inverse of a rotation+translation matrix.
inline void invertRigid(glm::mat4 &m)
{
    const glm::vec3 position(m[3]);
    const float x = glm::dot(position, glm::vec3(m[0]));
    const float y = glm::dot(position, glm::vec3(m[1]));
    const float z = glm::dot(position, glm::vec3(m[2]));
    m[3] = glm::vec4(-x, -y, -z, m[3].w);
    transpose3x3(m);
}

// Copies the rotation block only.
inline void copy3x3(glm::mat4 &destination, const glm::mat4 &source)
{
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            destination[column][row] = source[column][row];
}

// position = -(p rotated by m)
inline void setInversePosition(glm::mat4 &m, float x, float y, float z)
{
    m[3] = glm::vec4(-(glm::mat3(m) * glm::vec3(x, y, z)), m[3].w);
}
// position -= p rotated by m
inline void moveInversePosition(glm::mat4 &m, float x, float y, float z)
{
    m[3] = glm::vec4(glm::vec3(m[3]) - glm::mat3(m) * glm::vec3(x, y, z), m[3].w);
}

// D3D left-handed perspective projection, depth 0..1. The vertical field of
// view is the horizontal angle scaled linearly by the aspect ratio, which is
// what the game's cameras and content assume (glm::perspective differs).
inline glm::mat4 projectionMatrix(float viewAngle, float vpWidth, float vpHeight, float zNear, float zFar)
{
    glm::mat4 result(0.0f);
    const double q = static_cast<double>(zFar) / static_cast<double>(zFar - zNear);
    result[0][0] = 1.0f / tanf(viewAngle * 0.5f);
    result[1][1] = 1.0f / tanf((vpHeight / vpWidth) * viewAngle * 0.5f);
    result[2][2] = static_cast<float>(q);
    result[2][3] = 1.0f;
    result[3][2] = -static_cast<float>(q) * zNear;
    return result;
}

// One projected point: viewport x, y, depth and reciprocal w.
struct ProjectedPoint
{
    float x, y, z;
    float rhw;
};

// Projects `num` points to a viewport; arrays are walked with explicit strides.
inline void projectPoints(const glm::mat4 &transform, const glm::vec3 *srcArray, ProjectedPoint *dstArray, int32_t num,
                          float vphWidth05, float vphHeight05, int32_t srcStrcSize, int32_t dstStrcSize)
{
    for (; num > 0; --num)
    {
        // Summed left to right like transformPoint (GLM's product pairs the terms).
        const auto &m = transform;
        const auto &p = *srcArray;
        const glm::vec4 clip(m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
                             m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
                             m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2],
                             m[0][3] * p.x + m[1][3] * p.y + m[2][3] * p.z + m[3][3]);
        const float reciprocalW = 1.0f / clip.w;
        dstArray->x = (1.0f + clip.x * reciprocalW) * vphWidth05;
        dstArray->y = (1.0f - clip.y * reciprocalW) * vphHeight05;
        dstArray->z = clip.z * reciprocalW;
        dstArray->rhw = reciprocalW;
        srcArray = reinterpret_cast<const glm::vec3 *>(reinterpret_cast<const char *>(srcArray) + srcStrcSize);
        dstArray = reinterpret_cast<ProjectedPoint *>(reinterpret_cast<char *>(dstArray) + dstStrcSize);
    }
}

// Euler angles (x, y, z) of an unscaled rotation matrix, the inverse of
// eulerMatrix. At the poles (looking straight up or down) y is 0 and the
// heading goes to z, which glm::extractEulerAngleYXZ does not do.
inline glm::vec3 eulerAngles(const glm::mat4 &m)
{
    float ax, ay, az;
    if (m[2][1] < 1.0f)
    {
        if (m[2][1] > -1.0f)
        {
            ax = static_cast<float>(std::asin(-m[2][1]));
            ay = static_cast<float>(std::atan2(m[2][0], m[2][2]));
            az = static_cast<float>(std::atan2(m[0][1], m[1][1]));
            return glm::vec3(ax, ay, az);
        }
        ax = 3.141592654f * 0.5f;
        ay = 0.0f;
        az = static_cast<float>(std::atan2(m[0][2], m[0][0]));
    }
    else
    {
        ax = -3.141592654f * 0.5f;
        ay = 0.0f;
        az = static_cast<float>(-std::atan2(m[0][2], m[0][0]));
    }
    return glm::vec3(ax, ay, az);
}

// Left-handed look-at. Returns false (and a pure translation) when the eye
// and target coincide. A zero or parallel up vector falls back instead of
// producing NaN, which glm::lookAtLH would.
inline bool viewMatrix(glm::mat4 &result, const glm::vec3 &lookFrom, const glm::vec3 &lookTo, const glm::vec3 &upVector)
{
    result = glm::mat4(1.0f);
    const auto origin = lookFrom;
    auto forward = lookTo - origin;
    float squared = glm::dot(forward, forward);
    if (squared == 0.0f)
    {
        result[3] = glm::vec4(-origin, 1.0f);
        return false;
    }
    forward *= 1.0f / sqrtf(squared);
    auto up = upVector;
    up -= forward * glm::dot(forward, up);
    squared = glm::dot(up, up);
    if (squared == 0.0f)
        up.y = 1.0f;
    else
        up *= 1.0f / sqrtf(squared);
    auto right = glm::cross(up, forward);
    squared = glm::dot(right, right);
    if (squared != 0.0f)
    {
        right *= 1.0f / sqrtf(squared);
        result = glm::mat4(glm::transpose(glm::mat3(right, up, forward)));
    }
    result[3] = glm::vec4(-(glm::mat3(result) * origin), 1.0f);
    return true;
}

// Reflection about the plane N . p = D. The normal is used as given.
inline glm::mat4 mirrorMatrix(float Nx, float Ny, float Nz, float D)
{
    const glm::vec3 normal(Nx, Ny, Nz);
    const auto twiceNegativeNormal = normal * -2.0f;
    auto basis = glm::outerProduct(twiceNegativeNormal, normal);
    // Add identity only on the diagonal, retaining signed-zero off-diagonals.
    basis[0][0] += 1.0f;
    basis[1][1] += 1.0f;
    basis[2][2] += 1.0f;
    glm::mat4 result(basis);
    result[3] = glm::vec4(twiceNegativeNormal * -D, 1.0f);
    return result;
}

//============================================================================
// Quaternions
//
// glm::quat stores x, y, z, w (animation and camera track files are read
// straight into it) but its constructor takes (w, x, y, z).
// glm::mat4_cast(q) is the game's quaternion-to-matrix conversion.
//============================================================================
static_assert(sizeof(glm::quat) == 4 * sizeof(float) && offsetof(glm::quat, x) == 0 && offsetof(glm::quat, w) == 12);

// Rotation of a matrix as a quaternion. Not glm::quat_cast: this picks the
// branch by the sign of the trace, so some rotations come out as -q of what
// GLM returns, and it scales in double.
inline glm::quat quatFromMatrix(const glm::mat4 &m)
{
    float q[4];
    const auto tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f)
    {
        double s = std::sqrt(tr + 1.0f);
        q[3] = static_cast<float>(s * 0.5);
        s = 0.5 / s;
        q[0] = static_cast<float>((m[1][2] - m[2][1]) * s);
        q[1] = static_cast<float>((m[2][0] - m[0][2]) * s);
        q[2] = static_cast<float>((m[0][1] - m[1][0]) * s);
    }
    else
    {
        int i = 0;
        if (m[1][1] > m[0][0])
            i = m[2][2] > m[1][1] ? 2 : 1;
        else if (m[2][2] > m[0][0])
            i = 2;
        const int j = (i + 1) % 3;
        const int k = (j + 1) % 3;
        auto s = std::sqrt((m[i][i] - (m[j][j] + m[k][k])) + 1.0);
        q[i] = static_cast<float>(s * 0.5);
        if (s != 0.0)
            s = 0.5 / s;
        q[j] = static_cast<float>((m[i][j] + m[j][i]) * s);
        q[k] = static_cast<float>((m[i][k] + m[k][i]) * s);
        q[3] = static_cast<float>((m[j][k] - m[k][j]) * s);
    }
    return glm::quat(q[3], q[0], q[1], q[2]);
}

// Shortest-arc interpolation as the animations were authored with. Not
// glm::slerp: this switches to a plain lerp below ~26 degrees (cos > 0.9),
// blends in double and does not renormalize.
inline glm::quat slerp(const glm::quat &q1, const glm::quat &q2, float kBlend)
{
    const glm::vec4 from(q1.x, q1.y, q1.z, q1.w);
    const glm::vec4 to(q2.x, q2.y, q2.z, q2.w);
    const auto products = from * to;
    double cosomega = products.x + products.y + products.z + products.w;
    double k = 1.0f;
    if (cosomega < 0.0f)
    {
        cosomega = -cosomega;
        k = -1.0f;
    }
    if (cosomega > 1.0)
        cosomega = 1.0;
    double k0, k1;
    if (1.0 - cosomega > 0.1)
    {
        const auto omega = std::acos(cosomega);
        const auto sinomega = 1.0 / std::sin(omega);
        k0 = std::sin(omega * (1.0 - kBlend)) * sinomega;
        k1 = std::sin(omega * kBlend) * sinomega;
    }
    else
    {
        k0 = 1.0 - kBlend;
        k1 = kBlend;
    }
    k0 *= k;
    const glm::vec4 result(glm::dvec4(from) * k0 + glm::dvec4(to) * k1);
    return glm::quat(result.w, result.x, result.y, result.z);
}

//============================================================================
// Colours: glm::vec4 holding (r, g, b, a) in 0..1.
//============================================================================

// Unpacks a D3D ARGB colour.
inline glm::vec4 colorFromARGB(uint32_t c)
{
    return glm::vec4(static_cast<unsigned char>(c >> 16) * (1.0f / 255.0f),
                     static_cast<unsigned char>(c >> 8) * (1.0f / 255.0f),
                     static_cast<unsigned char>(c >> 0) * (1.0f / 255.0f),
                     static_cast<unsigned char>(c >> 24) * (1.0f / 255.0f));
}

// Packs to D3D ARGB: components are clamped to 0..1 and rounded to nearest
// (ties to even).
inline uint32_t colorToARGB(const glm::vec4 &color)
{
    const auto c = glm::clamp(color, 0.0f, 1.0f);
    return (static_cast<uint32_t>(static_cast<uint8_t>(round_to_int(c.x * 255.0f))) << 16) +
           (static_cast<uint32_t>(static_cast<uint8_t>(round_to_int(c.y * 255.0f))) << 8) +
           (static_cast<uint32_t>(static_cast<uint8_t>(round_to_int(c.z * 255.0f))) << 0) +
           (static_cast<uint32_t>(static_cast<uint8_t>(round_to_int(c.w * 255.0f))) << 24);
}

//============================================================================
// Planes and spheres. A plane is glm::vec4(N, D) with the equation N . p = D.
//============================================================================

// Plane through `point` with the given normal.
inline glm::vec4 planeFromPointNormal(const glm::vec3 &normal, const glm::vec3 &point)
{
    return glm::vec4(normal, glm::dot(normal, point));
}

// Point where the segment src-dst crosses the plane. False (res untouched)
// when both ends lie strictly on the same side.
inline bool intersectSegmentPlane(const glm::vec4 &plane, const glm::vec3 &src, const glm::vec3 &dst, glm::vec3 &res)
{
    const glm::vec3 normal(plane);
    const auto dsrc = glm::dot(normal, src) - plane.w;
    auto ddst = glm::dot(normal, dst) - plane.w;
    if (dsrc * ddst > 0.0f)
        return false;
    ddst = dsrc - ddst;
    res = src;
    if (ddst != 0.0f)
        res += (dst - src) * dsrc / ddst;
    return true;
}

// Whether the segment src-dst crosses the sphere's surface. A segment that
// lies entirely inside the sphere does not count.
inline bool intersectSegmentSphere(const glm::vec3 &src, const glm::vec3 &dst, const glm::vec3 &center, float r)
{
    // Distance along a unit ray to the sphere's near surface.
    const auto rayHit = [&](const glm::vec3 &orig, const glm::vec3 &normdir, float &dist) {
        const auto toCenter = center - orig;
        const float distToOrtoPlane = glm::dot(normdir, toCenter);
        const float distFromOrtoPlaneToSphere2 =
            r * r - (glm::dot(toCenter, toCenter) - distToOrtoPlane * distToOrtoPlane);
        if (distFromOrtoPlaneToSphere2 < 0.0f)
            return false;
        dist = distToOrtoPlane - sqrtf(distFromOrtoPlaneToSphere2);
        return true;
    };
    auto dir = dst - src;
    const float len = normalizeInPlace(dir);
    if (len > 1e-10f)
    {
        float dist;
        if (!rayHit(src, dir, dist))
            return false;
        if (dist >= 0.0f)
            return dist <= len;
        if (!rayHit(dst, negated(dir), dist))
            return false;
        return dist >= 0.0f && dist <= len;
    }
    const auto toCenter = center - src;
    return glm::dot(toCenter, toCenter) <= r * r;
}
} // namespace storm::math
#pragma pack(pop)
