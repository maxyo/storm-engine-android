// Contract of the operations that replaced the math3d classes (Vector, Matrix,
// Quaternion, Color, Plane, Sphere): the storm::math helpers of storm_glm.h and
// the stock GLM calls the engine uses for them. Everything goes through the
// mt::m3 half of legacy_api.h, so for run.py --baseline the same source, with
// the same inputs in the same order, runs on the math3d classes of an archived
// revision. Every resulting scalar is dumped as a double sample.
#include "legacy_api.h"
#include "sample_log.h"
#ifndef STORM_MATH_TEST_BASELINE
#include "plane.h"
#endif
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace m3 = mt::m3;
using m3::Color;
using m3::Mat4;
using m3::Plane;
using m3::Quat;
using m3::Vec3;

static void require(bool condition, const char *reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}
static void near(double a, double b, const char *reason, double tolerance = 5e-5)
{
    require(std::abs(a - b) <= tolerance * (1 + std::abs(b)), reason);
}
static void vectorNear(const Vec3 &a, const Vec3 &b, const char *reason)
{
    near(a.x, b.x, reason);
    near(a.y, b.y, reason);
    near(a.z, b.z, reason);
}
static void quatNear(const Quat &a, const Quat &b, const char *reason)
{
    near(a.x, b.x, reason);
    near(a.y, b.y, reason);
    near(a.z, b.z, reason);
    near(a.w, b.w, reason);
}
static bool sameBits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

// ---------------------------------------------------------------------------
// Layout of the GLM types the engine persists (vertex buffers, saves,
// animation and camera track files, shader constants).
// ---------------------------------------------------------------------------
#ifndef STORM_MATH_TEST_BASELINE
#pragma pack(push, 1)
struct PackedHolder
{
    char prefix;
    glm::vec3 v;
    glm::vec4 v4;
    glm::quat q;
    glm::mat4 m;
    PLANE plane;
    char suffix;
};
#pragma pack(pop)

static void layout()
{
    static_assert(std::is_same_v<Vec3, glm::vec3> && std::is_same_v<Mat4, glm::mat4> &&
                  std::is_same_v<Quat, glm::quat> && std::is_same_v<Color, glm::vec4> &&
                  std::is_same_v<Plane, glm::vec4>);

    static_assert(sizeof(glm::vec3) == 12 && alignof(glm::vec3) == alignof(float));
    static_assert(offsetof(glm::vec3, x) == 0 && offsetof(glm::vec3, y) == 4 && offsetof(glm::vec3, z) == 8);
    static_assert(std::is_standard_layout_v<glm::vec3> && std::is_trivially_copyable_v<glm::vec3>);

    static_assert(sizeof(glm::vec4) == 16 && alignof(glm::vec4) == alignof(float));
    static_assert(offsetof(glm::vec4, x) == 0 && offsetof(glm::vec4, y) == 4 && offsetof(glm::vec4, z) == 8 &&
                  offsetof(glm::vec4, w) == 12);
    // Colours are (r, g, b, a) in the same four floats.
    static_assert(offsetof(glm::vec4, r) == 0 && offsetof(glm::vec4, g) == 4 && offsetof(glm::vec4, b) == 8 &&
                  offsetof(glm::vec4, a) == 12);
    static_assert(std::is_standard_layout_v<glm::vec4> && std::is_trivially_copyable_v<glm::vec4>);

    // Animation and camera track files are read straight into x, y, z, w.
    static_assert(sizeof(glm::quat) == 16 && alignof(glm::quat) == alignof(float));
    static_assert(offsetof(glm::quat, x) == 0 && offsetof(glm::quat, y) == 4 && offsetof(glm::quat, z) == 8 &&
                  offsetof(glm::quat, w) == 12);
    static_assert(std::is_standard_layout_v<glm::quat> && std::is_trivially_copyable_v<glm::quat>);

    static_assert(sizeof(glm::mat4) == 64 && alignof(glm::mat4) == alignof(float));
    static_assert(sizeof(glm::mat4::col_type) == 16);
    static_assert(std::is_standard_layout_v<glm::mat4> && std::is_trivially_copyable_v<glm::mat4>);

    static_assert(sizeof(PLANE) == 16 && alignof(PLANE) == alignof(float));
    static_assert(offsetof(PLANE, Nx) == 0 && offsetof(PLANE, Ny) == 4 && offsetof(PLANE, Nz) == 8 &&
                  offsetof(PLANE, D) == 12);
    static_assert(std::is_standard_layout_v<PLANE> && std::is_trivially_copyable_v<PLANE>);

    static_assert(sizeof(storm::math::ProjectedPoint) == 16 && offsetof(storm::math::ProjectedPoint, rhw) == 12);

    // GLM types keep their size inside the engine's pack(1) structures.
    static_assert(sizeof(PackedHolder) == 1 + 12 + 16 + 16 + 64 + 16 + 1);
    static_assert(offsetof(PackedHolder, v) == 1 && offsetof(PackedHolder, v4) == 13 &&
                  offsetof(PackedHolder, q) == 29 && offsetof(PackedHolder, m) == 45 &&
                  offsetof(PackedHolder, plane) == 109);

    require(std::endian::native == std::endian::little, "byte fixtures require little endian");
    // 1, -2, 0.5, 3 as serialized floats.
    const unsigned char bytes[] = {0, 0, 128, 63, 0, 0, 0, 192, 0, 0, 0, 63, 0, 0, 64, 64};
    const glm::vec3 v(1.f, -2.f, .5f);
    require(std::memcmp(&v, bytes, 12) == 0, "glm::vec3 bytes changed");
    const glm::vec4 v4(1.f, -2.f, .5f, 3.f);
    require(std::memcmp(&v4, bytes, 16) == 0, "glm::vec4 bytes changed");
    // The constructor takes w first; storage is x, y, z, w.
    const glm::quat q(3.f, 1.f, -2.f, .5f);
    require(std::memcmp(&q, bytes, 16) == 0, "glm::quat x, y, z, w serialization changed");
    glm::quat loaded;
    std::memcpy(&loaded, bytes, 16);
    require(loaded.x == 1.f && loaded.y == -2.f && loaded.z == .5f && loaded.w == 3.f, "glm::quat load changed");
    const glm::quat identity;
    require(identity.x == 0.f && identity.y == 0.f && identity.z == 0.f && identity.w == 1.f,
            "default quaternion is not the identity rotation");
    const auto plane = storm::math::planeFromPointNormal(glm::vec3(1.f, -2.f, .5f), glm::vec3(3.f, 0.f, 0.f));
    require(std::memcmp(&plane, bytes, 16) == 0, "plane (N, D) bytes changed");
    const PLANE clip{1.f, -2.f, .5f, 3.f};
    require(std::memcmp(&clip, bytes, 16) == 0, "PLANE bytes changed");

    float fixture[16];
    for (int i = 0; i < 16; ++i)
        fixture[i] = float(i + 1);
    glm::mat4 m;
    std::memcpy(&m, fixture, 64);
    // Translation is the fourth column, m[3]; m[i][j] is D3D row i, column j.
    require(m[3][0] == 13 && m[3][1] == 14 && m[3][2] == 15 && m[3][3] == 16 && m[2][1] == 10,
            "glm::mat4 element order changed");
    require(storm::math::pos(m) == glm::vec3(13.f, 14.f, 15.f) && storm::math::vy(m) == glm::vec3(5.f, 6.f, 7.f),
            "matrix axis accessors changed");
    require(static_cast<const void *>(storm::math::data(m)) == static_cast<const void *>(&m) &&
                std::memcmp(storm::math::data(m), fixture, 64) == 0,
            "glm::mat4 is not the 16 floats in D3D order");
    const glm::mat4 defaulted;
    const float identityFloats[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    require(std::memcmp(&defaulted, identityFloats, 64) == 0, "default matrix is not identity");
    const glm::vec3 zero;
    require(zero.x == 0.f && zero.y == 0.f && zero.z == 0.f, "default vector is not zero");
}
#else
static void layout()
{
}
#endif

// ---------------------------------------------------------------------------
// Known answers; they hold for both header generations.
// ---------------------------------------------------------------------------
static void contracts()
{
    const float pi = std::acos(-1.f);
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // Vectors
    Vec3 v = m3::vec(3.f, 4.f, 12.f);
    near(m3::normalizeInPlace(v), 13, "normalization returns the previous length");
    vectorNear(v, m3::vec(3.f / 13, 4.f / 13, 12.f / 13), "normalized components");
    Vec3 zero = m3::vec(0.f, -0.f, 0.f);
    require(m3::normalizeInPlace(zero) == 0.f && zero.x == 0.f && zero.y == 0.f && zero.z == 0.f &&
                std::signbit(zero.y),
            "zero vector must stay untouched");
    Vec3 poisoned = m3::vec(nan, 3.f, 4.f);
    require(std::isnan(m3::normalizeInPlace(poisoned)) && poisoned.y == 3.f && poisoned.z == 4.f,
            "NaN normalization leaves the vector unchanged");
    const Vec3 negative = m3::negated(m3::vec(0.f, -0.f, 1.5f));
    require(sameBits(negative.x, -0.f) && sameBits(negative.y, 0.f) && negative.z == -1.5f,
            "negation must flip the sign bit of zero components");
    const Vec3 from = m3::vec(1.f, 2.f, 3.f), to = m3::vec(5.f, -2.f, 4.f);
    require(m3::lerp(from, to, 0.f).x == 1.f && m3::lerp(from, to, 0.f).y == 2.f, "lerp(0) is the first point");
    vectorNear(m3::lerp(from, to, 1.f), to, "lerp(1) is the second point");
    vectorNear(m3::lerp(from, to, .25f), m3::vec(2.f, 1.f, 3.25f), "lerp(0.25)");
    vectorNear(m3::lerp(from, to, 2.f), m3::vec(9.f, -6.f, 5.f), "lerp extrapolates");
    near(m3::angleY(m3::vec(0.f, 9.f, 1.f), 7.f), 0, "heading along +Z");
    near(m3::angleY(m3::vec(1.f, 9.f, 0.f), 7.f), pi / 2, "heading along +X");
    near(m3::angleY(m3::vec(-1.f, 9.f, 0.f), 7.f), -pi / 2, "heading along -X");
    near(m3::angleY(m3::vec(0.f, 9.f, -1.f), 7.f), pi, "heading along -Z");
    require(m3::angleY(m3::vec(0.f, 9.f, 0.f), 7.f) == 7.f, "vertical vector yields the default heading");
    require(m3::angleY(m3::vec(1e-6f, 9.f, 1e-6f), 7.f) == 7.f, "tiny horizontal part yields the default heading");

    // Matrices
    const Vec3 angles = m3::vec(.3f, -.4f, .5f), position = m3::vec(7.f, 8.f, 9.f);
    const Mat4 rigid = m3::eulerMatrix(angles, position);
    vectorNear(m3::transformPoint(rigid, m3::vec(0.f, 0.f, 0.f)), position, "translation is applied to points");
    vectorNear(m3::rotateNormal(rigid, m3::vec(0.f, 0.f, 0.f)), m3::vec(0.f, 0.f, 0.f), "no translation for normals");
    vectorNear(m3::eulerAngles(rigid), angles, "eulerAngles inverts eulerMatrix");
    require(m3::data(rigid)[12] == 7.f && m3::data(rigid)[13] == 8.f && m3::data(rigid)[14] == 9.f &&
                m3::data(rigid)[15] == 1.f && m3::data(rigid)[3] == 0.f,
            "translation lives in elements 12..14");
    // Rotation Z, then X, then Y: a quarter turn about Z maps +X to +Y...
    vectorNear(m3::rotateNormal(m3::eulerMatrix(m3::vec(0.f, 0.f, pi / 2), position), m3::vec(1.f, 0.f, 0.f)),
               m3::vec(0.f, 1.f, 0.f), "Z rotation orientation");
    // ...about X maps +Y to +Z, about Y maps +Z to +X.
    vectorNear(m3::rotateNormal(m3::eulerMatrix(m3::vec(pi / 2, 0.f, 0.f), position), m3::vec(0.f, 1.f, 0.f)),
               m3::vec(0.f, 0.f, 1.f), "X rotation orientation");
    vectorNear(m3::rotateNormal(m3::eulerMatrix(m3::vec(0.f, pi / 2, 0.f), position), m3::vec(0.f, 0.f, 1.f)),
               m3::vec(1.f, 0.f, 0.f), "Y rotation orientation");
    const Vec3 point = m3::vec(1.f, -2.f, 3.f);
    Mat4 inverse = rigid;
    m3::invertRigid(inverse);
    vectorNear(m3::transformPoint(inverse, m3::transformPoint(rigid, point)), point, "rigid inverse undoes the transform");
    Mat4 transposed = rigid;
    m3::transpose(transposed);
    require(m3::data(transposed)[3] == 7.f && m3::data(transposed)[7] == 8.f && m3::data(transposed)[11] == 9.f &&
                m3::data(transposed)[12] == 0.f && m3::data(transposed)[1] == m3::data(rigid)[4],
            "transpose is the full 4x4 transpose");
    m3::transpose(transposed);
    require(std::memcmp(m3::data(transposed), m3::data(rigid), 64) == 0, "double transpose changed the matrix");
    // Gimbal poles: y is reported as zero and the heading goes to z.
    const Vec3 up = m3::eulerAngles(m3::eulerMatrix(m3::vec(pi / 2, 0.f, .25f), position));
    require(up.x == 3.141592654f * .5f && up.y == 0.f, "looking straight down pole");
    near(up.z, .25, "pole heading");
    const Vec3 down = m3::eulerAngles(m3::eulerMatrix(m3::vec(-pi / 2, 0.f, .25f), position));
    require(down.x == -3.141592654f * .5f && down.y == 0.f, "looking straight up pole");
    near(down.z, .25, "opposite pole heading");

    // Quaternions
    quatNear(m3::quatFromMatrix(m3::identity()), m3::quat(0.f, 0.f, 0.f, 1.f), "identity matrix to quaternion");
    const Mat4 quarterZ = m3::eulerMatrix(m3::vec(0.f, 0.f, pi / 2), m3::vec(0.f, 0.f, 0.f));
    const Quat qz = m3::quatFromMatrix(quarterZ);
    quatNear(qz, m3::quat(0.f, 0.f, std::sqrt(.5f), std::sqrt(.5f)), "quarter turn about Z as a quaternion");
    vectorNear(m3::rotateNormal(m3::quatMatrix(qz), m3::vec(1.f, 0.f, 0.f)), m3::vec(0.f, 1.f, 0.f),
               "quaternion matrix orientation");
    const Mat4 back = m3::quatMatrix(m3::quatFromMatrix(rigid));
    for (int i = 0; i < 12; ++i)
        near(m3::data(back)[i], m3::data(rigid)[i], "matrix -> quaternion -> matrix keeps the rotation");
    require(m3::data(back)[12] == 0.f && m3::data(back)[13] == 0.f && m3::data(back)[14] == 0.f &&
                m3::data(back)[15] == 1.f,
            "quaternion matrix has no translation");
    // Half turns take the trace <= 0 branches.
    quatNear(m3::quatFromMatrix(m3::eulerMatrix(m3::vec(0.f, pi, 0.f), position)), m3::quat(0.f, 1.f, 0.f, 0.f),
             "half turn about Y");
    const Quat identity = m3::quat(0.f, 0.f, 0.f, 1.f), opposite = m3::quat(0.f, 0.f, 0.f, -1.f);
    quatNear(m3::slerp(identity, opposite, 0.f), opposite, "slerp flips the first endpoint to the shortest arc");
    quatNear(m3::slerp(identity, qz, 0.f), identity, "slerp(0)");
    quatNear(m3::slerp(identity, qz, 1.f), qz, "slerp(1)");
    quatNear(m3::slerp(identity, qz, .5f), m3::quat(0.f, 0.f, std::sin(pi / 8), std::cos(pi / 8)), "slerp(0.5)");
    // Close quaternions are blended linearly and not renormalized.
    quatNear(m3::slerp(identity, m3::quat(0.f, 0.f, .1f, .99f), .5f), m3::quat(0.f, 0.f, .05f, .995f),
             "near quaternions use a plain lerp");
    quatNear(m3::slerp(m3::quat(0.f, 0.f, 0.f, 2.f), m3::quat(0.f, 0.f, 0.f, 2.f), .5f), m3::quat(0.f, 0.f, 0.f, 2.f),
             "slerp does not renormalize");

    // Colours
    const Color unpacked = m3::colorFromARGB(0xFF804020u);
    near(unpacked.r, 128 / 255., "red channel");
    near(unpacked.g, 64 / 255., "green channel");
    near(unpacked.b, 32 / 255., "blue channel");
    require(unpacked.a == 1.f, "alpha channel");
    require(m3::colorToARGB(unpacked) == 0xFF804020u, "ARGB round trip");
    // Clamped to 0..1, rounded to nearest with ties to even (127.5 -> 128, 0.5 -> 0).
    require(m3::colorToARGB(m3::color(2.f, -1.f, .5f, 1.f)) == 0xFFFF0080u, "colour packing clamps and rounds");
    require(m3::colorToARGB(m3::color(.5f / 255, 1.5f / 255, 2.5f / 255, 0.f)) == 0x00000202u,
            "colour packing rounds ties to even");
    const Color blended = m3::colorLerp(m3::color(0.f, 1.f, .5f, .1f), m3::color(1.f, 0.f, .5f, .2f), .25f, .75f);
    require(blended.r == .25f && blended.g == .75f && blended.b == .5f && blended.a == .75f,
            "colour lerp interpolates r, g, b only");

    // Planes
    const Plane ground = m3::planeFromPointNormal(m3::vec(0.f, 1.f, 0.f), m3::vec(5.f, 2.f, -3.f));
    require(m3::distance(ground) == 2.f && m3::normal(ground).y == 1.f && m3::normal(ground).x == 0.f,
            "plane is (N, N . point)");
    Vec3 hit = m3::vec(-7.f, -8.f, -9.f);
    require(m3::intersectSegmentPlane(ground, m3::vec(1.f, 0.f, 1.f), m3::vec(3.f, 4.f, 5.f), hit),
            "segment crosses the plane");
    vectorNear(hit, m3::vec(2.f, 2.f, 3.f), "segment/plane intersection point");
    hit = m3::vec(-7.f, -8.f, -9.f);
    require(!m3::intersectSegmentPlane(ground, m3::vec(1.f, 3.f, 1.f), m3::vec(3.f, 4.f, 5.f), hit) &&
                hit.x == -7.f && hit.y == -8.f && hit.z == -9.f,
            "segment on one side: no hit, result untouched");
    require(m3::intersectSegmentPlane(ground, m3::vec(1.f, 2.f, 1.f), m3::vec(3.f, 4.f, 5.f), hit) && hit.x == 1.f &&
                hit.y == 2.f && hit.z == 1.f,
            "segment starting on the plane hits at its start");
    require(m3::intersectSegmentPlane(ground, m3::vec(1.f, 2.f, 1.f), m3::vec(3.f, 2.f, 5.f), hit) && hit.x == 1.f &&
                hit.z == 1.f,
            "segment lying in the plane reports its start");

    // Spheres
    const Vec3 center = m3::vec(1.f, 2.f, 3.f);
    require(m3::intersectSegmentSphere(m3::vec(-9.f, 2.f, 3.f), m3::vec(11.f, 2.f, 3.f), center, 2.f),
            "segment through the sphere");
    require(m3::intersectSegmentSphere(m3::vec(-9.f, 2.f, 3.f), m3::vec(1.f, 2.f, 3.f), center, 2.f),
            "segment ending inside the sphere");
    require(m3::intersectSegmentSphere(m3::vec(1.f, 2.f, 3.f), m3::vec(11.f, 2.f, 3.f), center, 2.f),
            "segment starting inside the sphere");
    require(!m3::intersectSegmentSphere(m3::vec(.5f, 2.f, 3.f), m3::vec(1.5f, 2.f, 3.f), center, 2.f),
            "segment entirely inside the sphere does not cross its surface");
    require(!m3::intersectSegmentSphere(m3::vec(-9.f, 2.f, 3.f), m3::vec(-5.f, 2.f, 3.f), center, 2.f),
            "segment stopping short of the sphere");
    require(!m3::intersectSegmentSphere(m3::vec(-9.f, 5.f, 3.f), m3::vec(11.f, 5.f, 3.f), center, 2.f),
            "segment passing by the sphere");
    require(m3::intersectSegmentSphere(m3::vec(-9.f, 4.f, 3.f), m3::vec(11.f, 4.f, 3.f), center, 2.f),
            "tangent segment touches the sphere");
    require(m3::intersectSegmentSphere(center, center, center, 2.f) &&
                !m3::intersectSegmentSphere(m3::vec(9.f, 9.f, 9.f), m3::vec(9.f, 9.f, 9.f), center, 2.f),
            "a degenerate segment is a point-in-sphere test");
}

// ---------------------------------------------------------------------------
// Samples and inputs
// ---------------------------------------------------------------------------
struct Sink : SampleLog
{
    void f(float value, Where at = Where::current()) { scalar(value, at); }
    void b(bool value, Where at = Where::current()) { scalar(value ? 1.0 : 0.0, at); }
    void v(const Vec3 &value, Where at = Where::current()) { vec(value, at); }
    void q(const Quat &value, Where at = Where::current())
    {
        scalar(value.x, at);
        scalar(value.y, at);
        scalar(value.z, at);
        scalar(value.w, at);
    }
    void c(const Color &value, Where at = Where::current())
    {
        scalar(value.r, at);
        scalar(value.g, at);
        scalar(value.b, at);
        scalar(value.a, at);
    }
    // One sample per channel, so a mismatch is at most 255 "units".
    void argb(uint32_t value, Where at = Where::current())
    {
        scalar(double(value >> 24), at);
        scalar(double((value >> 16) & 0xff), at);
        scalar(double((value >> 8) & 0xff), at);
        scalar(double(value & 0xff), at);
    }
    void p(const Plane &value, Where at = Where::current())
    {
        vec(m3::normal(value), at);
        scalar(m3::distance(value), at);
    }
    void m(const Mat4 &value, Where at = Where::current()) { floats(m3::data(value), 16, at); }
};

static Mat4 inputRigid(const Vec3 &ang, const Vec3 &position);

struct Gen
{
    std::mt19937 rng{0x4d334421u};
    std::uniform_real_distribution<float> plainValues{-10.f, 10.f};
    std::uniform_real_distribution<float> unitValues{-1.f, 1.f};
    std::uniform_real_distribution<float> angleValues{-7.f, 7.f};
    // 0: plain, 1: finite edge values mixed in, 2: infinities and NaN too.
    int mode = 0;

    void trial(int index)
    {
        const int slot = index % 10;
        mode = slot < 6 ? 0 : slot < 9 ? 1 : 2;
    }
    bool edge(float &value)
    {
        static const float finite[] = {0.f,     -0.f,    1.f,    -1.f,   0.5f,   2.f,    1e-30f, -1e-30f,
                                       1e-42f,  -1e-42f, 1e-20f, 1e-10f, 1e19f,  -1e19f, 1e30f,  -1e30f,
                                       3.3e38f, 1e10f,   -1e10f, 1e-5f,  1e-6f,  -1e-6f};
        static const float nonFinite[] = {std::numeric_limits<float>::infinity(),
                                          -std::numeric_limits<float>::infinity(),
                                          std::numeric_limits<float>::quiet_NaN()};
        if (mode == 0 || rng() % 3 != 0)
            return false;
        if (mode == 2 && rng() % 4 == 0)
            value = nonFinite[rng() % 3];
        else
            value = finite[rng() % (sizeof(finite) / sizeof(finite[0]))];
        return true;
    }
    float f()
    {
        float value;
        if (!edge(value))
            value = plainValues(rng);
        return value;
    }
    float unit()
    {
        float value;
        if (!edge(value))
            value = unitValues(rng);
        return value;
    }
    float angle()
    {
        float value;
        if (!edge(value))
            value = angleValues(rng);
        return value;
    }
    // Finite, well-scaled values regardless of the mode.
    float tame() { return plainValues(rng); }
    float tameUnit() { return unitValues(rng); }
    float tameAngle() { return angleValues(rng); }
    uint32_t u() { return static_cast<uint32_t>(rng()); }
    Vec3 vec()
    {
        const float x = f();
        const float y = f();
        const float z = f();
        return m3::vec(x, y, z);
    }
    Vec3 tameVec()
    {
        const float x = tame();
        const float y = tame();
        const float z = tame();
        return m3::vec(x, y, z);
    }
    Vec3 angles()
    {
        const float x = angle();
        const float y = angle();
        const float z = angle();
        return m3::vec(x, y, z);
    }
    Vec3 tameAngles()
    {
        const float x = tameAngle();
        const float y = tameAngle();
        const float z = tameAngle();
        return m3::vec(x, y, z);
    }
    // Arbitrary (not normalized) quaternion.
    Quat quat()
    {
        const float x = unit();
        const float y = unit();
        const float z = unit();
        const float w = unit();
        return m3::quat(x, y, z, w);
    }
    // Components in -1.5..1.5, so about a third is out of range.
    Color color()
    {
        const float r = unit();
        const float g = unit();
        const float b = unit();
        const float a = unit();
        return m3::color(r * 1.5f, g * 1.5f, b * 1.5f, a * 1.5f);
    }
    Mat4 general()
    {
        Mat4 result = m3::identity();
        for (int i = 0; i < 16; ++i)
            m3::data(result)[i] = f();
        return result;
    }
    // Rotation + translation (edge values appear in the angles and position).
    Mat4 rigid()
    {
        const Vec3 ang = angles();
        const Vec3 position = vec();
        return inputRigid(ang, position);
    }
    Mat4 tameRigid()
    {
        const Vec3 ang = tameAngles();
        const Vec3 position = tameVec();
        return inputRigid(ang, position);
    }
    Mat4 anyMatrix()
    {
        switch (rng() % 5)
        {
        case 0:
            return m3::identity();
        case 1:
            return general();
        default:
            return rigid();
        }
    }
};

// Rotation Z, then X, then Y, then translation, computed here in double so
// that both header generations get bit-identical matrices as inputs. (The
// engine's own builder is sampled on its own in matrixOps.)
static Mat4 inputRigid(const Vec3 &ang, const Vec3 &position)
{
    const double sx = std::sin(double(ang.x)), cx = std::cos(double(ang.x));
    const double sy = std::sin(double(ang.y)), cy = std::cos(double(ang.y));
    const double sz = std::sin(double(ang.z)), cz = std::cos(double(ang.z));
    const double values[16] = {cz * cy + sz * sx * sy,  sz * cx, cz * -sy + sz * sx * cy,  0.0,
                               -sz * cy + cz * sx * sy, cz * cx, sz * sy + cz * sx * cy,   0.0,
                               cx * sy,                 -sx,     cx * cy,                  0.0,
                               position.x,              position.y, position.z,            1.0};
    Mat4 result = m3::identity();
    for (int i = 0; i < 16; ++i)
        m3::data(result)[i] = static_cast<float>(values[i]);
    return result;
}

// Rotation of a matrix as a quaternion, again computed here in double: an
// input for the quaternion operations that is bit-identical in both
// generations. (The engine's conversion is sampled on its own.)
static Quat inputQuat(const Mat4 &matrix)
{
    const float *m = m3::data(matrix);
    const auto at = [&](int i, int j) { return double(m[i * 4 + j]); };
    double q[4];
    const double trace = at(0, 0) + at(1, 1) + at(2, 2);
    if (trace > 0.0)
    {
        const double s = std::sqrt(trace + 1.0);
        q[3] = s * 0.5;
        q[0] = (at(1, 2) - at(2, 1)) * (0.5 / s);
        q[1] = (at(2, 0) - at(0, 2)) * (0.5 / s);
        q[2] = (at(0, 1) - at(1, 0)) * (0.5 / s);
    }
    else
    {
        int i = 0;
        if (at(1, 1) > at(0, 0))
            i = at(2, 2) > at(1, 1) ? 2 : 1;
        else if (at(2, 2) > at(0, 0))
            i = 2;
        const int j = (i + 1) % 3, k = (j + 1) % 3;
        double s = std::sqrt(at(i, i) - (at(j, j) + at(k, k)) + 1.0);
        q[i] = s * 0.5;
        if (s != 0.0)
            s = 0.5 / s;
        q[j] = (at(i, j) + at(j, i)) * s;
        q[k] = (at(i, k) + at(k, i)) * s;
        q[3] = (at(j, k) - at(k, j)) * s;
    }
    return m3::quat(float(q[0]), float(q[1]), float(q[2]), float(q[3]));
}

// Whether quaternion -> matrix stays inside the float range: its products of
// two components, doubled and summed in pairs, must not overflow. Beyond that
// the two generations round differently (2 * (a + b) against 2 * a + 2 * b),
// e.g. inf - inf = NaN against 2 * 0 = 0; no rotation gets anywhere near.
static bool matrixInRange(const Quat &q)
{
    for (const float c : {q.x, q.y, q.z, q.w})
        if (!std::isnan(c) && std::abs(c) > 1e18f)
            return false;
    return true;
}

static Quat scaled(const Quat &q, float k)
{
    return m3::quat(q.x * k, q.y * k, q.z * k, q.w * k);
}

// ---------------------------------------------------------------------------
// Randomized operations
// ---------------------------------------------------------------------------
static void vectorOps(Gen &g, Sink &o)
{
    const Vec3 a = g.vec();
    const Vec3 b = g.vec();
    const float k = g.f();
    const float blend = g.tameUnit() * .5f + .5f;

    Vec3 n = a;
    o.f(m3::normalizeInPlace(n));
    o.v(n);
    // Already normalized input.
    o.f(m3::normalizeInPlace(n));
    o.v(n);
    o.v(m3::negated(a));
    o.v(m3::lerp(a, b, k));
    o.v(m3::lerp(a, b, blend));
    o.v(m3::lerp(a, a, k));
    o.f(m3::angleY(a, k));
    o.f(m3::angleY(n, k));
    // No or almost no horizontal part: the default angle is returned.
    o.f(m3::angleY(m3::vec(0.f, a.y, -0.f), k));
    o.f(m3::angleY(m3::vec(a.x * 1e-6f, a.y, a.z * 1e-6f), k));
    o.f(m3::angleY(m3::vec(a.x * 3e-6f, a.y, a.z * 3e-6f), k));
    // Along the axes: the clamp and the sign of x decide.
    o.f(m3::angleY(m3::vec(0.f, a.y, a.z), k));
    o.f(m3::angleY(m3::vec(-0.f, a.y, a.z), k));
    o.f(m3::angleY(m3::vec(a.x, a.y, 0.f), k));
}

static void matrixOps(Gen &g, Sink &o)
{
    const Vec3 ang = g.angles();
    const Vec3 position = g.vec();
    const Vec3 v = g.vec();
    // The engine's builder, and its inverse on the builder's own output.
    const Mat4 built = m3::eulerMatrix(ang, position);
    o.m(built);
    o.v(m3::eulerAngles(built));
    const Mat4 rigid = inputRigid(ang, position);
    o.v(m3::eulerAngles(rigid));
    o.v(m3::transformPoint(rigid, v));
    o.v(m3::rotateNormal(rigid, v));
    Mat4 work = rigid;
    m3::transpose(work);
    o.m(work);
    work = rigid;
    m3::invertRigid(work);
    o.m(work);
    o.v(m3::transformPoint(work, m3::transformPoint(rigid, v)));
    // Inverse view of a rotated normal, as the camera code composes it.
    o.v(m3::transformPoint(work, m3::negated(v)));

    const Mat4 any = g.anyMatrix();
    o.v(m3::eulerAngles(any));
    o.v(m3::transformPoint(any, v));
    o.v(m3::rotateNormal(any, v));
    work = any;
    m3::transpose(work);
    o.m(work);
    work = any;
    m3::invertRigid(work);
    o.m(work);

    // Gimbal poles: x of exactly +-pi/2 as a float, and one float beside it.
    const float halfPi = std::acos(-1.f) / 2;
    const float poles[] = {halfPi,
                           -halfPi,
                           std::nextafter(halfPi, 0.f),
                           std::nextafter(halfPi, 4.f),
                           -std::nextafter(halfPi, 0.f),
                           halfPi * 3,
                           halfPi * 5};
    const float ay = g.tameAngle();
    const float az = g.tameAngle();
    const float pole = poles[g.u() % (sizeof(poles) / sizeof(poles[0]))];
    o.m(m3::eulerMatrix(m3::vec(pole, ay, az), position));
    o.v(m3::eulerAngles(m3::eulerMatrix(m3::vec(pole, ay, az), position)));
    o.v(m3::eulerAngles(inputRigid(m3::vec(pole, ay, az), position)));
    // Hand-made forward axis exactly at, beyond and beside the poles.
    Mat4 forced = g.tameRigid();
    const float vertical[] = {1.f, -1.f, 1.0000001f, -1.0000001f, 0.99999994f, -0.99999994f, 2.f, -2.f, 0.f, -0.f};
    m3::data(forced)[9] = vertical[g.u() % (sizeof(vertical) / sizeof(vertical[0]))];
    o.v(m3::eulerAngles(forced));
}

static void quaternionOps(Gen &g, Sink &o)
{
    // Arbitrary, unnormalized (edge values included).
    const Quat p = g.quat();
    const Quat q = g.quat();
    const float k = g.f();
    for (const float blend : {-.25f, 0.f, .25f, .5f, 1.f, 1.25f, k})
        o.q(m3::slerp(p, q, blend));
    // Up to the largest and smallest scales whose products stay representable.
    for (const float scale : {1.f, 1e18f, 1e-19f, 1e-30f})
    {
        const Quat stretched = scaled(p, scale);
        if (matrixInRange(stretched))
            o.m(m3::quatMatrix(stretched));
    }

    // Rotations.
    const Mat4 ma = g.tameRigid();
    const Mat4 mb = g.tameRigid();
    o.q(m3::quatFromMatrix(ma));
    o.q(m3::quatFromMatrix(mb));
    const Quat a = inputQuat(ma);
    const Quat b = inputQuat(mb);
    const float blend = g.tameUnit() * .5f + .5f;
    o.m(m3::quatMatrix(a));
    o.q(m3::slerp(a, b, blend));
    o.q(m3::slerp(a, b, k));
    // Antipodal: the same rotation with the opposite sign.
    o.q(m3::slerp(a, scaled(a, -1.f), blend));
    o.q(m3::slerp(a, scaled(b, -1.f), blend));
    // Identical and nearly identical.
    o.q(m3::slerp(a, a, blend));
    const Quat nudged = m3::quat(a.x + 1e-6f, a.y - 1e-6f, a.z, a.w);
    o.q(m3::slerp(a, nudged, blend));
    o.q(m3::slerp(a, scaled(nudged, -1.f), blend));
    // Unnormalized and degenerate.
    o.q(m3::slerp(scaled(a, 2.f), b, blend));
    o.q(m3::slerp(a, scaled(b, .5f), blend));
    o.q(m3::slerp(scaled(a, 3.f), scaled(b, 3.f), blend));
    o.q(m3::slerp(a, m3::quat(0.f, 0.f, 0.f, 0.f), blend));
    o.q(m3::slerp(m3::quat(0.f, 0.f, 0.f, 0.f), m3::quat(0.f, -0.f, 0.f, 0.f), blend));
    // The emitter blend end to end, exactly as the engine chains it.
    o.m(m3::quatMatrix(m3::slerp(m3::quatFromMatrix(ma), m3::quatFromMatrix(mb), blend)));
    // The same with identical inputs at every step: slerp, back to a matrix,
    // positions interpolated linearly.
    const Quat mixed = m3::slerp(a, b, blend);
    Mat4 result = m3::quatMatrix(mixed);
    const Vec3 pa = m3::vec(m3::data(ma)[12], m3::data(ma)[13], m3::data(ma)[14]);
    const Vec3 pb = m3::vec(m3::data(mb)[12], m3::data(mb)[13], m3::data(mb)[14]);
    const Vec3 at = m3::lerp(pa, pb, blend);
    m3::data(result)[12] = at.x;
    m3::data(result)[13] = at.y;
    m3::data(result)[14] = at.z;
    o.m(result);
    o.v(m3::transformPoint(result, pa));
    // The camera track: quaternion to matrix, two axes mirrored, the eye
    // moved into view space.
    Mat4 view = m3::quatMatrix(mixed);
    for (const int axis : {0, 2})
    {
        const Vec3 mirrored = m3::negated(m3::vec(m3::data(view)[axis * 4], m3::data(view)[axis * 4 + 1],
                                                  m3::data(view)[axis * 4 + 2]));
        m3::data(view)[axis * 4] = mirrored.x;
        m3::data(view)[axis * 4 + 1] = mirrored.y;
        m3::data(view)[axis * 4 + 2] = mirrored.z;
    }
    m3::transpose(view);
    const Vec3 eye = m3::transformPoint(view, m3::negated(at));
    o.m(view);
    o.v(eye);

    // Any matrix (identity, rotation with edge angles, arbitrary 16 floats):
    // every branch of the conversion, including trace <= 0 and s == 0.
    const Mat4 any = g.anyMatrix();
    o.q(m3::quatFromMatrix(any));
    const Quat fromAny = inputQuat(any);
    if (matrixInRange(fromAny))
        o.m(m3::quatMatrix(fromAny));
    // Half turns about one axis put the trace at -1 with two equal diagonals.
    const float pi = std::acos(-1.f);
    const Vec3 halfTurns[] = {m3::vec(pi, 0.f, 0.f), m3::vec(0.f, pi, 0.f), m3::vec(0.f, 0.f, pi),
                              m3::vec(pi, pi, 0.f),  m3::vec(pi, 0.f, pi),  m3::vec(pi / 2, pi, pi / 2)};
    const Mat4 half = inputRigid(halfTurns[g.u() % 6], m3::vec(0.f, 0.f, 0.f));
    o.q(m3::quatFromMatrix(half));
    // Diagonal matrices choose the branch directly.
    Mat4 diagonal = m3::identity();
    const float entries[] = {1.f, -1.f, 0.f, -0.f, .5f, -.5f, 1e-20f, -1e-20f};
    m3::data(diagonal)[0] = entries[g.u() % 8];
    m3::data(diagonal)[5] = entries[g.u() % 8];
    m3::data(diagonal)[10] = entries[g.u() % 8];
    o.q(m3::quatFromMatrix(diagonal));
}

static void colorOps(Gen &g, Sink &o)
{
    const uint32_t packed = g.u();
    const Color unpacked = m3::colorFromARGB(packed);
    o.c(unpacked);
    const uint32_t repacked = m3::colorToARGB(unpacked);
    require(repacked == packed, "ARGB -> colour -> ARGB must be lossless");
    o.argb(repacked);

    const Color a = g.color();
    const Color b = g.color();
    const float k = g.f();
    const float blend = g.tameUnit() * .5f + .5f;
    o.argb(m3::colorToARGB(a));
    o.argb(m3::colorToARGB(b));
    Color mixed = m3::colorLerp(a, b, blend, a.a);
    o.c(mixed);
    o.argb(m3::colorToARGB(mixed));
    mixed = m3::colorLerp(a, b, k, b.a);
    o.c(mixed);
    o.argb(m3::colorToARGB(mixed));
    mixed = m3::colorLerp(unpacked, m3::colorFromARGB(g.u()), blend, unpacked.a);
    o.c(mixed);
    o.argb(m3::colorToARGB(mixed));
    // Exact and nearly exact rounding ties, and channels around both limits.
    const float n = float(g.u() % 256);
    const Color ties = m3::color((n + .5f) / 255.f, (n - .5f) / 255.f, (n + .49999f) / 255.f, (n + .50001f) / 255.f);
    o.argb(m3::colorToARGB(ties));
    const float tiny = std::numeric_limits<float>::denorm_min();
    const Color limits = m3::color(std::nextafter(1.f, 2.f), std::nextafter(1.f, 0.f), -tiny, tiny);
    o.argb(m3::colorToARGB(limits));
}

static void planeOps(Gen &g, Sink &o)
{
    const Vec3 normal = g.vec();
    const Vec3 point = g.vec();
    const Plane plane = m3::planeFromPointNormal(normal, point);
    o.p(plane);
    const Vec3 src = g.vec();
    const Vec3 dst = g.vec();
    const auto probe = [&](const Plane &pl, const Vec3 &from, const Vec3 &to, SampleLog::Where at = SampleLog::Where::current()) {
        Vec3 res = m3::vec(-7.f, -8.f, -9.f);
        o.b(m3::intersectSegmentPlane(pl, from, to, res), at);
        o.v(res, at);
    };
    probe(plane, src, dst);
    probe(plane, dst, src);
    // Ends on the plane (up to the rounding of N . p - D), and a point segment.
    probe(plane, point, dst);
    probe(plane, src, point);
    probe(plane, point, point);
    probe(plane, src, src);

    // Axis-aligned plane: the ends touch it exactly.
    const float height = g.tame();
    const Plane level = m3::planeFromPointNormal(m3::vec(0.f, 1.f, 0.f), m3::vec(g.tame(), height, g.tame()));
    o.p(level);
    const Vec3 a = g.tameVec();
    const Vec3 b = g.tameVec();
    const Vec3 onA = m3::vec(a.x, height, a.z);
    const Vec3 onB = m3::vec(b.x, height, b.z);
    const Vec3 above = m3::vec(a.x, height + std::abs(a.y) + 1.f, a.z);
    const Vec3 below = m3::vec(b.x, height - std::abs(b.y) - 1.f, b.z);
    probe(level, onA, above);
    probe(level, above, onA);
    probe(level, onA, onB);
    probe(level, onA, onA);
    probe(level, above, below);
    probe(level, below, above);
    probe(level, above, m3::vec(b.x, height + .5f, b.z));
    probe(level, below, m3::vec(a.x, height - .5f, a.z));
    // Degenerate plane.
    probe(m3::planeFromPointNormal(m3::vec(0.f, 0.f, 0.f), point), src, dst);
}

static void sphereOps(Gen &g, Sink &o)
{
    // Arbitrary, including edge values, negative and zero radii.
    const Vec3 center = g.vec();
    const float radius = g.f();
    const Vec3 src = g.vec();
    const Vec3 dst = g.vec();
    o.b(m3::intersectSegmentSphere(src, dst, center, radius));
    o.b(m3::intersectSegmentSphere(dst, src, center, std::abs(radius)));
    o.b(m3::intersectSegmentSphere(src, center, center, std::abs(radius)));
    o.b(m3::intersectSegmentSphere(center, dst, center, std::abs(radius)));
    o.b(m3::intersectSegmentSphere(src, src, center, std::abs(radius)));
    o.b(m3::intersectSegmentSphere(src, dst, center, 0.f));
    o.b(m3::intersectSegmentSphere(center, center, center, 0.f));

    // Well-scaled sphere on a grid, so the touching cases are exact.
    const Vec3 c = m3::vec(std::floor(g.tame()), std::floor(g.tame()), std::floor(g.tame()));
    const float r = float(1 + g.u() % 4);
    const float far = r + 1.f + float(g.u() % 4);
    const auto at = [&](float x, float y, float z) { return m3::vec(c.x + x, c.y + y, c.z + z); };
    // Through the centre, both directions.
    o.b(m3::intersectSegmentSphere(at(-far, 0.f, 0.f), at(far, 0.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(0.f, far, 0.f), at(0.f, -far, 0.f), c, r));
    // Ends exactly on the surface, from outside and from inside.
    o.b(m3::intersectSegmentSphere(at(-far, 0.f, 0.f), at(-r, 0.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(-r, 0.f, 0.f), at(-far, 0.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(0.f, 0.f, 0.f), at(0.f, 0.f, r), c, r));
    o.b(m3::intersectSegmentSphere(at(0.f, 0.f, r), at(0.f, 0.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(0.f, r, 0.f), at(0.f, -r, 0.f), c, r));
    // Stops one float short of the surface / reaches one float inside.
    o.b(m3::intersectSegmentSphere(at(-far, 0.f, 0.f), m3::vec(std::nextafter(c.x - r, c.x - far), c.y, c.z), c, r));
    o.b(m3::intersectSegmentSphere(at(-far, 0.f, 0.f), m3::vec(std::nextafter(c.x - r, c.x), c.y, c.z), c, r));
    // Tangent lines, and just beside them.
    o.b(m3::intersectSegmentSphere(at(-far, r, 0.f), at(far, r, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(-far, 0.f, -r), at(far, 0.f, -r), c, r));
    o.b(m3::intersectSegmentSphere(at(-far, r + .001f, 0.f), at(far, r + .001f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(-far, r - .001f, 0.f), at(far, r - .001f, 0.f), c, r));
    // Entirely inside, entirely outside on one side, and point segments.
    o.b(m3::intersectSegmentSphere(at(-r * .5f, 0.f, 0.f), at(r * .5f, .25f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(far, 0.f, 0.f), at(far + 3.f, 1.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(r, 0.f, 0.f), at(r, 0.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(far, 0.f, 0.f), at(far, 0.f, 0.f), c, r));
    o.b(m3::intersectSegmentSphere(at(.5f, 0.f, 0.f), at(.5f, 0.f, 0.f), c, r));
    // Shorter than the point-segment threshold but not a point.
    o.b(m3::intersectSegmentSphere(at(0.f, 0.f, 0.f), at(1e-12f, 0.f, 0.f), m3::vec(0.f, 0.f, 0.f), r));
    // Random chords of the same sphere.
    const Vec3 from = g.tameVec();
    const Vec3 to = g.tameVec();
    o.b(m3::intersectSegmentSphere(from, to, c, r));
    o.b(m3::intersectSegmentSphere(from, to, c, r * 4.f));
    o.b(m3::intersectSegmentSphere(from, to, c, r * 16.f));
}

// ---------------------------------------------------------------------------
// Fixed edge cases
// ---------------------------------------------------------------------------
static void fixedCases(Sink &o)
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float big = std::numeric_limits<float>::max();
    const float tiny = std::numeric_limits<float>::denorm_min();
    const float specials[] = {0.f, -0.f, 1.f, -1.f, tiny, -tiny, 1e-30f, 1e-20f, 1e19f, 1e30f, big, -big, inf, -inf, nan};
    const int count = sizeof(specials) / sizeof(specials[0]);

    // Vectors built from every pair of special values.
    for (int i = 0; i < count; ++i)
        for (int j = 0; j < count; ++j)
        {
            const Vec3 a = m3::vec(specials[i], specials[j], specials[(i + j) % count]);
            const Vec3 b = m3::vec(specials[j], 1.f, specials[i]);
            Vec3 n = a;
            o.f(m3::normalizeInPlace(n));
            o.v(n);
            o.v(m3::negated(a));
            o.v(m3::lerp(a, b, .25f));
            o.v(m3::lerp(a, b, specials[j]));
            o.f(m3::angleY(a, 5.f));
            o.f(m3::angleY(b, specials[i]));
            Vec3 res = m3::vec(-7.f, -8.f, -9.f);
            o.b(m3::intersectSegmentPlane(m3::planeFromPointNormal(b, a), a, b, res));
            o.v(res);
            o.b(m3::intersectSegmentSphere(a, b, m3::vec(0.f, 0.f, 0.f), 1.f));
            o.b(m3::intersectSegmentSphere(m3::vec(-2.f, 0.f, 0.f), m3::vec(2.f, 0.f, 0.f), a, specials[j]));
        }

    // Colours: every special value in every channel, and whole ARGB extremes.
    for (int i = 0; i < count; ++i)
    {
        o.argb(m3::colorToARGB(m3::color(specials[i], .25f, .5f, .75f)));
        o.argb(m3::colorToARGB(m3::color(.25f, specials[i], .5f, .75f)));
        o.argb(m3::colorToARGB(m3::color(.25f, .5f, specials[i], .75f)));
        o.argb(m3::colorToARGB(m3::color(.25f, .5f, .75f, specials[i])));
        const Color mixed = m3::colorLerp(m3::color(specials[i], 0.f, 1.f, .5f), m3::color(1.f, specials[i], 0.f, .5f),
                                          .5f, specials[i]);
        o.c(mixed);
        o.argb(m3::colorToARGB(mixed));
    }
    for (const float out : {-1e9f, -2.f, -1.f, -.001f, 1.001f, 2.f, 255.f, 256.f, 1e9f, 4.3e9f})
        o.argb(m3::colorToARGB(m3::color(out, out * .5f, -out, out + 1.f)));
    for (const uint32_t packed : {0u, 0xffffffffu, 0xff000000u, 0x00ffffffu, 0x80808080u, 0x01020304u, 0x7f7f7f7fu})
    {
        const Color c = m3::colorFromARGB(packed);
        o.c(c);
        o.argb(m3::colorToARGB(c));
    }
    // All 256 channel values survive the round trip.
    for (uint32_t value = 0; value < 256; ++value)
    {
        const uint32_t packed = value * 0x01010101u;
        const Color c = m3::colorFromARGB(packed);
        require(m3::colorToARGB(c) == packed, "channel value does not survive the ARGB round trip");
        o.f(c.r);
        // Half-way values between two channel steps.
        o.argb(m3::colorToARGB(m3::color((float(value) + .5f) / 255.f, (float(value) + .5f) * (1.f / 255.f),
                                         std::nextafter(c.r, 2.f), std::nextafter(c.r, -1.f))));
    }

    // Quaternions around the linear/spherical threshold (cos = 0.9) and at
    // both ends of the arc.
    const Quat first = m3::quat(0.f, 0.f, 0.f, 1.f);
    for (const float cosine : {-1.f, -.9001f, -.9f, -.8999f, -.5f, -1e-8f, 0.f, 1e-8f, .5f, .8999f, .9f, .9001f,
                               .99999994f, 1.f})
    {
        const Quat second = m3::quat(std::sqrt(1.f - cosine * cosine), 0.f, 0.f, cosine);
        for (const float t : {0.f, .25f, .5f, 1.f, -1.f, 2.f, nan, inf})
        {
            o.q(m3::slerp(first, second, t));
            o.q(m3::slerp(second, first, t));
        }
    }
    for (int i = 0; i < count; ++i)
    {
        const Quat special = m3::quat(specials[i], 0.f, specials[(i + 3) % count], 1.f);
        o.q(m3::slerp(first, special, .5f));
        o.q(m3::slerp(special, special, .5f));
        o.m(m3::quatMatrix(special));
        o.m(m3::quatMatrix(m3::quat(specials[i], specials[i], specials[i], specials[i])));
        Mat4 m = m3::identity();
        m3::data(m)[0] = specials[i];
        m3::data(m)[5] = specials[(i + 1) % count];
        m3::data(m)[10] = specials[(i + 2) % count];
        m3::data(m)[1] = specials[(i + 3) % count];
        o.q(m3::quatFromMatrix(m));
        o.v(m3::eulerAngles(m));
        m3::data(m)[9] = specials[i];
        o.v(m3::eulerAngles(m));
        o.v(m3::transformPoint(m, m3::vec(1.f, specials[i], -1.f)));
        o.v(m3::rotateNormal(m, m3::vec(1.f, specials[i], -1.f)));
        Mat4 work = m;
        m3::invertRigid(work);
        o.m(work);
        work = m;
        m3::transpose(work);
        o.m(work);
        o.m(m3::eulerMatrix(m3::vec(specials[i], specials[(i + 1) % count], specials[(i + 2) % count]),
                            m3::vec(specials[i], 0.f, 1.f)));
    }
    // Euler angles on a grid that includes every quarter turn.
    const float quarter = std::acos(-1.f) / 2;
    for (int x = -4; x <= 4; ++x)
        for (int y = -4; y <= 4; ++y)
            for (int z = -2; z <= 2; ++z)
            {
                const Vec3 ang = m3::vec(x * quarter, y * quarter, z * quarter * .5f);
                const Vec3 position = m3::vec(float(x), float(y), float(z));
                o.m(m3::eulerMatrix(ang, position));
                o.v(m3::eulerAngles(m3::eulerMatrix(ang, position)));
                const Mat4 m = inputRigid(ang, position);
                o.v(m3::eulerAngles(m));
                o.q(m3::quatFromMatrix(m));
                o.m(m3::quatMatrix(inputQuat(m)));
            }
}

static void differential(const char *sampleFile)
{
    Gen g;
    Sink o;
    for (int trial = 0; trial < 4000; ++trial)
    {
        g.trial(trial);
        vectorOps(g, o);
        matrixOps(g, o);
        quaternionOps(g, o);
        colorOps(g, o);
        planeOps(g, o);
        sphereOps(g, o);
    }
    fixedCases(o);
    o.write(sampleFile);
}

int main(int argc, char **argv)
{
    layout();
    contracts();
    differential(argc > 1 ? argv[1] : nullptr);
    std::cout << "PASS: persisted GLM layouts, vector/Euler/quaternion/colour/plane/sphere helpers and randomized "
                 "samples ("
              << (mt::kBaseline ? "baseline math3d classes" : "GLM + storm::math") << ")\n";
}
