#pragma once

// One neutral spelling of the vector/matrix operations the math harnesses
// exercise, so the very same harness source builds against two generations of
// the engine math headers and dumps the same sequence of samples:
//
//   CURRENT  (default)                  glm::vec3 / glm::dvec3 / glm::mat4 and
//                                       the storm::math helpers of storm_glm.h.
//   BASELINE (STORM_MATH_TEST_BASELINE) the CVECTOR / DVECTOR / CMatrix classes
//                                       and, in mt::m3, the math3d classes
//                                       (Vector, Matrix, Quaternion, Color,
//                                       Plane, Sphere) of an archived revision
//                                       (run.py --baseline <rev>).
//
// The legacy classes no longer exist in the tree: the CURRENT half is GLM and
// storm::math only, and nothing outside the BASELINE half may name them.
// Each function is spelled the way engine code spells the operation in that
// generation, so a differential run measures what the migration changed.
//
// Matrix convention (both halves): 16 floats in D3D order, translation in
// elements 12..14, row vectors. mulRow(a, b) means "apply a, then b".

#include <cstdint>

#ifdef STORM_MATH_TEST_BASELINE
#include "matrix.h"
#include "math3d/quaternion.h"
#include "math3d/sphere.h"
#include "math3d/color.h"
#else
#include "storm_glm.h"
#endif

namespace mt
{
#ifdef STORM_MATH_TEST_BASELINE
//============================================================================
// BASELINE: legacy class API
//============================================================================
using Vec3 = CVECTOR;
using DVec3 = DVECTOR;
using Mat4 = CMatrix;
using ProjPoint = MTX_PRJ_VECTOR;
inline constexpr bool kBaseline = true;
// The legacy matrix class was over-aligned for its long-gone inline assembly.
inline constexpr std::size_t kMat4Alignment = 16;

// The legacy matrix methods were never const-qualified.
inline Mat4 &mut(const Mat4 &m) { return const_cast<Mat4 &>(m); }

// --- vectors (float and double) ---
template <typename V> V add(const V &a, const V &b) { return a + b; }
template <typename V> V sub(const V &a, const V &b) { return a - b; }
template <typename V> V mul(const V &a, const V &b) { return a * b; }
template <typename V> V div(const V &a, const V &b) { return a / b; }
template <typename V> V negate(const V &a) { return -a; }
inline Vec3 scale(const Vec3 &v, float f) { return v * f; }
inline DVec3 scale(const DVec3 &v, double f) { return v * f; }
inline Vec3 divScalar(const Vec3 &v, float f) { return v / f; }
inline DVec3 divScalar(const DVec3 &v, double f) { return v / f; }
inline float dot(const Vec3 &a, const Vec3 &b) { return a | b; }
inline double dot(const DVec3 &a, const DVec3 &b) { return a | b; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) { return a ^ b; }
inline DVec3 cross(const DVec3 &a, const DVec3 &b) { return a ^ b; }
inline float lengthSquared(const Vec3 &v) { return ~v; }
inline double lengthSquared(const DVec3 &v) { return ~v; }
inline float length(const Vec3 &v) { return v.GetLength(); }
inline float lengthXZ(const Vec3 &v) { return v.GetLength2D(); }
// Zero maps to zero.
inline Vec3 normalizeOrZero(const Vec3 &v) { return !v; }
// No zero guard: the zero vector yields NaN.
inline DVec3 normalize(const DVec3 &v) { return !v; }

// --- matrix builders ---
inline Mat4 identity() { return Mat4(); }
// Rotation Z, then X, then Y, then translation.
inline Mat4 eulerMatrix(float ax, float ay, float az, float x = 0.0f, float y = 0.0f, float z = 0.0f)
{
    Mat4 m;
    m.BuildMatrix(ax, ay, az, x, y, z);
    return m;
}
inline Mat4 rotationX(float a) { Mat4 m; m.BuildRotateX(a); return m; }
inline Mat4 rotationY(float a) { Mat4 m; m.BuildRotateY(a); return m; }
inline Mat4 rotationZ(float a) { Mat4 m; m.BuildRotateZ(a); return m; }
inline Mat4 translation(float x, float y, float z) { Mat4 m; m.BuildPosition(x, y, z); return m; }
inline Mat4 scaling(float x, float y, float z) { Mat4 m; m.BuildScale(x, y, z); return m; }
inline Mat4 projectionMatrix(float viewAngle, float vpWidth, float vpHeight, float zNear, float zFar)
{
    Mat4 m;
    m.BuildProjectionMatrix(viewAngle, vpWidth, vpHeight, zNear, zFar);
    return m;
}
inline bool viewMatrix(Mat4 &result, const Vec3 &from, const Vec3 &to, const Vec3 &up)
{
    return result.BuildViewMatrix(from, to, up);
}
inline Mat4 mirrorMatrix(float nx, float ny, float nz, float d)
{
    Mat4 m;
    m.BuildMirrorMatrix(nx, ny, nz, d);
    return m;
}

// --- matrix operations ---
// "Apply a, then b" (row-vector product a*b).
inline Mat4 mulRow(const Mat4 &a, const Mat4 &b) { return a * b; }
// Same product into existing storage; result must not alias a or b.
inline void mulRowInto(Mat4 &result, const Mat4 &a, const Mat4 &b) { result.EqMultiply(a, b); }
inline Vec3 transformPoint(const Mat4 &m, const Vec3 &p) { return mut(m) * p; }
inline Vec3 inverseTransformPoint(const Mat4 &m, const Vec3 &p)
{
    Vec3 result;
    mut(m).MulToInv(p, result);
    return result;
}
inline Vec3 inverseTransformNormal(const Mat4 &m, const Vec3 &n)
{
    Vec3 result;
    mut(m).MulToInvNorm(n, result);
    return result;
}
inline void transpose3x3(Mat4 &m) { m.Transposition3X3(); }
inline void transpose4x4(Mat4 &m) { m.Transposition4x4(); }
inline void invertRigid(Mat4 &m) { m.Transposition(); }
inline void copy3x3(Mat4 &destination, const Mat4 &source) { mut(source).Get3X3(destination); }
inline void setInversePosition(Mat4 &m, float x, float y, float z) { m.SetInversePosition(x, y, z); }
inline void moveInversePosition(Mat4 &m, float x, float y, float z) { m.MoveInversePosition(x, y, z); }
inline void projectPoints(const Mat4 &m, const Vec3 *source, ProjPoint *destination, int32_t count, float halfWidth,
                          float halfHeight, int32_t sourceStride, int32_t destinationStride)
{
    mut(m).Projection(const_cast<Vec3 *>(source), destination, count, halfWidth, halfHeight, sourceStride,
                      destinationStride);
}

// --- accessors ---
inline Vec3 &vx(Mat4 &m) { return m.Vx(); }
inline Vec3 &vy(Mat4 &m) { return m.Vy(); }
inline Vec3 &vz(Mat4 &m) { return m.Vz(); }
inline Vec3 &pos(Mat4 &m) { return m.Pos(); }
inline const Vec3 &vx(const Mat4 &m) { return m.Vx(); }
inline const Vec3 &vy(const Mat4 &m) { return m.Vy(); }
inline const Vec3 &vz(const Mat4 &m) { return m.Vz(); }
inline const Vec3 &pos(const Mat4 &m) { return m.Pos(); }
inline float *data(Mat4 &m) { return m.matrix; }
inline const float *data(const Mat4 &m) { return m.matrix; }
// Element [i][j] of the D3D row-major view (i = row, j = column).
inline float &at(Mat4 &m, int i, int j) { return m.m[i][j]; }
inline float at(const Mat4 &m, int i, int j) { return m.m[i][j]; }
inline D3DMATRIX *d3dMatrix(const Mat4 &m) { return static_cast<D3DMATRIX *>(m); }

//============================================================================
// BASELINE: math3d classes. Only the operations the engine still performs
// (now through storm::math and stock GLM) are mapped.
//============================================================================
namespace m3
{
using Vec3 = ::Vector;
using Mat4 = ::Matrix;
using Quat = ::Quaternion;
using Color = ::Color;
using Plane = ::Plane;

inline Vec3 vec(float x, float y, float z) { return ::Vector(x, y, z); }
// Components in storage order x, y, z, w.
inline Quat quat(float x, float y, float z, float w) { return ::Quaternion(x, y, z, w); }
inline Color color(float r, float g, float b, float a) { return ::Color(r, g, b, a); }
inline Mat4 identity() { return ::Matrix(); }
inline float *data(Mat4 &m) { return m.matrix; }
inline const float *data(const Mat4 &m) { return m.matrix; }
inline Vec3 normal(const Plane &p) { return p.N; }
inline float distance(const Plane &p) { return p.D; }

// --- vectors ---
// Normalizes in place (zero stays zero), returns the previous length.
inline float normalizeInPlace(Vec3 &v) { return v.Normalize(); }
inline Vec3 negated(const Vec3 &v) { return -v; }
inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float k)
{
    Vec3 result;
    result.Lerp(a, b, k);
    return result;
}
inline float angleY(const Vec3 &v, float defAngle) { return v.GetAY(defAngle); }

// --- matrices ---
// Rotation Z, then X, then Y, then translation.
inline Mat4 eulerMatrix(const Vec3 &ang, const Vec3 &position) { return ::Matrix(ang, position); }
inline Vec3 eulerAngles(const Mat4 &m)
{
    Vec3 result;
    m.GetAngles(result);
    return result;
}
inline Vec3 transformPoint(const Mat4 &m, const Vec3 &p) { return m.MulVertex(p); }
inline Vec3 rotateNormal(const Mat4 &m, const Vec3 &n) { return m.MulNormal(n); }
inline void transpose(Mat4 &m) { m.Transposition(); }
// In-place inverse of a rotation+translation matrix.
inline void invertRigid(Mat4 &m) { m.Inverse(); }

// --- quaternions ---
inline Quat quatFromMatrix(const Mat4 &m) { return ::Quaternion(m); }
inline Quat slerp(const Quat &a, const Quat &b, float k)
{
    Quat result;
    result.SLerp(a, b, k);
    return result;
}
inline Mat4 quatMatrix(const Quat &q)
{
    Mat4 result;
    q.GetMatrix(result);
    return result;
}

// --- colours ---
inline Color colorFromARGB(uint32_t argb) { return ::Color(argb); }
inline uint32_t colorToARGB(const Color &c) { return static_cast<uint32_t>(c); }
// Interpolates r, g, b; alpha is the given one (Color::Lerp leaves it alone).
inline Color colorLerp(const Color &a, const Color &b, float k, float alpha)
{
    Color result(0.0f, 0.0f, 0.0f, alpha);
    result.Lerp(a, b, k);
    return result;
}

// --- planes and spheres ---
inline Plane planeFromPointNormal(const Vec3 &n, const Vec3 &point) { return ::Plane(n, point); }
inline bool intersectSegmentPlane(const Plane &plane, const Vec3 &src, const Vec3 &dst, Vec3 &res)
{
    return plane.Intersection(src, dst, res);
}
inline bool intersectSegmentSphere(const Vec3 &src, const Vec3 &dst, const Vec3 &center, float r)
{
    ::Sphere sphere;
    sphere.x = center.x;
    sphere.y = center.y;
    sphere.z = center.z;
    sphere.r = r;
    return sphere.Intersection(src, dst);
}
} // namespace m3

#else
//============================================================================
// CURRENT: GLM types and storm::math helpers only
//============================================================================
using Vec3 = glm::vec3;
using DVec3 = glm::dvec3;
using Mat4 = glm::mat4;
using ProjPoint = storm::math::ProjectedPoint;
inline constexpr bool kBaseline = false;
inline constexpr std::size_t kMat4Alignment = alignof(float);

// --- vectors (float and double) ---
template <typename V> V add(const V &a, const V &b) { return a + b; }
template <typename V> V sub(const V &a, const V &b) { return a - b; }
template <typename V> V mul(const V &a, const V &b) { return a * b; }
template <typename V> V div(const V &a, const V &b) { return a / b; }
template <typename V> V negate(const V &a) { return -a; }
inline Vec3 scale(const Vec3 &v, float f) { return v * f; }
inline DVec3 scale(const DVec3 &v, double f) { return v * f; }
inline Vec3 divScalar(const Vec3 &v, float f) { return v / f; }
inline DVec3 divScalar(const DVec3 &v, double f) { return v / f; }
inline float dot(const Vec3 &a, const Vec3 &b) { return glm::dot(a, b); }
inline double dot(const DVec3 &a, const DVec3 &b) { return glm::dot(a, b); }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) { return glm::cross(a, b); }
inline DVec3 cross(const DVec3 &a, const DVec3 &b) { return glm::cross(a, b); }
inline float lengthSquared(const Vec3 &v) { return storm::math::lengthSquared(v); }
inline double lengthSquared(const DVec3 &v) { return storm::math::lengthSquared(v); }
inline float length(const Vec3 &v) { return glm::length(v); }
inline float lengthXZ(const Vec3 &v) { return storm::math::lengthXZ(v); }
// Zero maps to zero.
inline Vec3 normalizeOrZero(const Vec3 &v) { return storm::math::normalizeOrZero(v); }
// No zero guard: the zero vector yields NaN.
inline DVec3 normalize(const DVec3 &v) { return glm::normalize(v); }

// --- matrix builders ---
inline Mat4 identity() { return Mat4(1.0f); }
// Rotation Z, then X, then Y, then translation.
inline Mat4 eulerMatrix(float ax, float ay, float az, float x = 0.0f, float y = 0.0f, float z = 0.0f)
{
    return storm::math::eulerMatrix(ax, ay, az, x, y, z);
}
inline Mat4 rotationX(float a) { return storm::math::rotationX(a); }
inline Mat4 rotationY(float a) { return storm::math::rotationY(a); }
inline Mat4 rotationZ(float a) { return storm::math::rotationZ(a); }
inline Mat4 translation(float x, float y, float z) { return storm::math::translation(x, y, z); }
inline Mat4 scaling(float x, float y, float z) { return storm::math::scaling(x, y, z); }
inline Mat4 projectionMatrix(float viewAngle, float vpWidth, float vpHeight, float zNear, float zFar)
{
    return storm::math::projectionMatrix(viewAngle, vpWidth, vpHeight, zNear, zFar);
}
inline bool viewMatrix(Mat4 &result, const Vec3 &from, const Vec3 &to, const Vec3 &up)
{
    return storm::math::viewMatrix(result, from, to, up);
}
inline Mat4 mirrorMatrix(float nx, float ny, float nz, float d) { return storm::math::mirrorMatrix(nx, ny, nz, d); }

// --- matrix operations ---
// "Apply a, then b": the row-vector product a*b is b * a in GLM.
inline Mat4 mulRow(const Mat4 &a, const Mat4 &b) { return b * a; }
// Same product into existing storage; result must not alias a or b.
inline void mulRowInto(Mat4 &result, const Mat4 &a, const Mat4 &b) { result = b * a; }
inline Vec3 transformPoint(const Mat4 &m, const Vec3 &p) { return storm::math::transformPoint(m, p); }
inline Vec3 inverseTransformPoint(const Mat4 &m, const Vec3 &p) { return storm::math::inverseTransformPoint(m, p); }
inline Vec3 inverseTransformNormal(const Mat4 &m, const Vec3 &n) { return storm::math::inverseTransformNormal(m, n); }
inline void transpose3x3(Mat4 &m) { storm::math::transpose3x3(m); }
inline void transpose4x4(Mat4 &m) { m = glm::transpose(m); }
inline void invertRigid(Mat4 &m) { storm::math::invertRigid(m); }
inline void copy3x3(Mat4 &destination, const Mat4 &source) { storm::math::copy3x3(destination, source); }
inline void setInversePosition(Mat4 &m, float x, float y, float z) { storm::math::setInversePosition(m, x, y, z); }
inline void moveInversePosition(Mat4 &m, float x, float y, float z) { storm::math::moveInversePosition(m, x, y, z); }
inline void projectPoints(const Mat4 &m, const Vec3 *source, ProjPoint *destination, int32_t count, float halfWidth,
                          float halfHeight, int32_t sourceStride, int32_t destinationStride)
{
    storm::math::projectPoints(m, source, destination, count, halfWidth, halfHeight, sourceStride, destinationStride);
}

// --- accessors ---
inline Vec3 &vx(Mat4 &m) { return storm::math::vx(m); }
inline Vec3 &vy(Mat4 &m) { return storm::math::vy(m); }
inline Vec3 &vz(Mat4 &m) { return storm::math::vz(m); }
inline Vec3 &pos(Mat4 &m) { return storm::math::pos(m); }
inline const Vec3 &vx(const Mat4 &m) { return storm::math::vx(m); }
inline const Vec3 &vy(const Mat4 &m) { return storm::math::vy(m); }
inline const Vec3 &vz(const Mat4 &m) { return storm::math::vz(m); }
inline const Vec3 &pos(const Mat4 &m) { return storm::math::pos(m); }
inline float *data(Mat4 &m) { return storm::math::data(m); }
inline const float *data(const Mat4 &m) { return storm::math::data(m); }
// Element [i][j] of the D3D row-major view (i = row, j = column).
inline float &at(Mat4 &m, int i, int j) { return m[i][j]; }
inline float at(const Mat4 &m, int i, int j) { return m[i][j]; }
inline D3DMATRIX *d3dMatrix(const Mat4 &m) { return storm::math::d3dMatrix(m); }
//============================================================================
// CURRENT: what replaced the math3d classes, spelled as the engine spells it.
//============================================================================
namespace m3
{
using Vec3 = glm::vec3;
using Mat4 = glm::mat4;
using Quat = glm::quat;
using Color = glm::vec4; // r, g, b, a
using Plane = glm::vec4; // N, D

inline Vec3 vec(float x, float y, float z) { return glm::vec3(x, y, z); }
// Components in storage order x, y, z, w (glm::quat's constructor takes w first).
inline Quat quat(float x, float y, float z, float w) { return glm::quat(w, x, y, z); }
inline Color color(float r, float g, float b, float a) { return glm::vec4(r, g, b, a); }
inline Mat4 identity() { return glm::mat4(1.0f); }
inline float *data(Mat4 &m) { return storm::math::data(m); }
inline const float *data(const Mat4 &m) { return storm::math::data(m); }
inline Vec3 normal(const Plane &p) { return glm::vec3(p); }
inline float distance(const Plane &p) { return p.w; }

// --- vectors ---
// Normalizes in place (zero stays zero), returns the previous length.
inline float normalizeInPlace(Vec3 &v) { return storm::math::normalizeInPlace(v); }
inline Vec3 negated(const Vec3 &v) { return storm::math::negated(v); }
inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float k) { return storm::math::lerp(a, b, k); }
inline float angleY(const Vec3 &v, float defAngle) { return storm::math::angleY(v, defAngle); }

// --- matrices ---
// Rotation Z, then X, then Y, then translation.
inline Mat4 eulerMatrix(const Vec3 &ang, const Vec3 &position) { return storm::math::eulerMatrix(ang, position); }
inline Vec3 eulerAngles(const Mat4 &m) { return storm::math::eulerAngles(m); }
inline Vec3 transformPoint(const Mat4 &m, const Vec3 &p) { return storm::math::transformPoint(m, p); }
inline Vec3 rotateNormal(const Mat4 &m, const Vec3 &n) { return glm::mat3(m) * n; }
inline void transpose(Mat4 &m) { m = glm::transpose(m); }
// In-place inverse of a rotation+translation matrix.
inline void invertRigid(Mat4 &m) { storm::math::invertRigid(m); }

// --- quaternions ---
inline Quat quatFromMatrix(const Mat4 &m) { return storm::math::quatFromMatrix(m); }
inline Quat slerp(const Quat &a, const Quat &b, float k) { return storm::math::slerp(a, b, k); }
inline Mat4 quatMatrix(const Quat &q) { return glm::mat4_cast(q); }

// --- colours ---
inline Color colorFromARGB(uint32_t argb) { return storm::math::colorFromARGB(argb); }
inline uint32_t colorToARGB(const Color &c) { return storm::math::colorToARGB(c); }
// Interpolates r, g, b; alpha is the given one.
inline Color colorLerp(const Color &a, const Color &b, float k, float alpha)
{
    return glm::vec4(storm::math::lerp(glm::vec3(a), glm::vec3(b), k), alpha);
}

// --- planes and spheres ---
inline Plane planeFromPointNormal(const Vec3 &n, const Vec3 &point)
{
    return storm::math::planeFromPointNormal(n, point);
}
inline bool intersectSegmentPlane(const Plane &plane, const Vec3 &src, const Vec3 &dst, Vec3 &res)
{
    return storm::math::intersectSegmentPlane(plane, src, dst, res);
}
inline bool intersectSegmentSphere(const Vec3 &src, const Vec3 &dst, const Vec3 &center, float r)
{
    return storm::math::intersectSegmentSphere(src, dst, center, r);
}
} // namespace m3
#endif
} // namespace mt
