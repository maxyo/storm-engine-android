// Differential and layout contract for the packed math3d types.
// Only the historical public API is used, so the same source builds against
// the current headers and against the headers of an older revision (run.py
// --baseline). Every resulting scalar is dumped as a double sample.
//
// math3d/triangle.h is not covered: it does not compile with GCC or Clang
// (`plane.n` is not a member of Plane; GCC also rejects the non-trivial Vector
// members inside its anonymous struct), and no engine source includes it.
#include <cstdint>
#include "math3d/quaternion.h"
#include "math3d/sphere.h"
#include "math3d/color.h"
#include "c_vector4.h"
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static void require(bool condition, const char *reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct OddMath3d
{
    char prefix;
    Vector v;
    Vector4 v4;
    Matrix m;
    Quaternion q;
    Plane p;
    Sphere s;
    Color c;
    char suffix;
};
#pragma pack(pop)
struct NaturalCVector4
{
    char prefix;
    CVECTOR4 v;
};

static void layout()
{
    static_assert(sizeof(Vector) == 12 && alignof(Vector) == 1);
    static_assert(offsetof(Vector, x) == 0 && offsetof(Vector, y) == 4 && offsetof(Vector, z) == 8);
    static_assert(offsetof(Vector, v) == 0 && sizeof(Vector::v) == 12);
    static_assert(std::is_standard_layout_v<Vector>);

    static_assert(sizeof(Vector4) == 16 && alignof(Vector4) == 1);
    static_assert(offsetof(Vector4, x) == 0 && offsetof(Vector4, y) == 4 && offsetof(Vector4, z) == 8 &&
                  offsetof(Vector4, w) == 12);
    static_assert(offsetof(Vector4, v4) == 0 && sizeof(Vector4::v4) == 16);
    static_assert(std::is_standard_layout_v<Vector4>);

    static_assert(sizeof(Matrix) == 64 && alignof(Matrix) == 1);
    static_assert(offsetof(Matrix, matrix) == 0 && offsetof(Matrix, m) == 0 && offsetof(Matrix, v) == 0);
    static_assert(sizeof(Matrix::matrix) == 64 && sizeof(Matrix::m) == 64 && sizeof(Matrix::v) == 64);
    static_assert(offsetof(Matrix, v.vx) == 0 && offsetof(Matrix, v.wx) == 12 && offsetof(Matrix, v.vy) == 16 &&
                  offsetof(Matrix, v.wy) == 28 && offsetof(Matrix, v.vz) == 32 && offsetof(Matrix, v.wz) == 44 &&
                  offsetof(Matrix, v.pos) == 48 && offsetof(Matrix, v.w) == 60);
    static_assert(std::is_standard_layout_v<Matrix>);

    static_assert(sizeof(Quaternion) == 16 && alignof(Quaternion) == 1);
    static_assert(offsetof(Quaternion, x) == 0 && offsetof(Quaternion, y) == 4 && offsetof(Quaternion, z) == 8 &&
                  offsetof(Quaternion, w) == 12);
    static_assert(offsetof(Quaternion, q) == 0 && sizeof(Quaternion::q) == 16);
    static_assert(std::is_standard_layout_v<Quaternion>);

    static_assert(sizeof(Plane) == 16 && alignof(Plane) == 1);
    static_assert(offsetof(Plane, N) == 0 && offsetof(Plane, D) == 12);
    static_assert(std::is_standard_layout_v<Plane>);

    static_assert(sizeof(Sphere) == 16 && alignof(Sphere) == 1);
    static_assert(offsetof(Sphere, x) == 0 && offsetof(Sphere, y) == 4 && offsetof(Sphere, z) == 8 &&
                  offsetof(Sphere, r) == 12 && offsetof(Sphere, v4) == 0);
    static_assert(std::is_standard_layout_v<Sphere>);

    static_assert(sizeof(Color) == 16 && alignof(Color) == 1);
    static_assert(offsetof(Color, r) == 0 && offsetof(Color, g) == 4 && offsetof(Color, b) == 8 &&
                  offsetof(Color, a) == 12);
    static_assert(std::is_standard_layout_v<Color>);

    // CVECTOR4 is declared outside pack(1): natural float alignment.
    static_assert(sizeof(CVECTOR4) == 16 && alignof(CVECTOR4) == alignof(float));
    static_assert(offsetof(CVECTOR4, x) == 0 && offsetof(CVECTOR4, y) == 4 && offsetof(CVECTOR4, z) == 8 &&
                  offsetof(CVECTOR4, w) == 12 && offsetof(CVECTOR4, v) == 0);
    static_assert(std::is_standard_layout_v<CVECTOR4> && std::is_trivially_copyable_v<CVECTOR4>);
    static_assert(offsetof(NaturalCVector4, v) == alignof(float));

    // The user-provided copy operations of these types are historical API.
    static_assert(!std::is_trivially_copyable_v<Vector> && !std::is_trivially_copyable_v<Vector4> &&
                  !std::is_trivially_copyable_v<Matrix> && !std::is_trivially_copyable_v<Quaternion> &&
                  !std::is_trivially_copyable_v<Plane> && !std::is_trivially_copyable_v<Sphere> &&
                  !std::is_trivially_copyable_v<Color>);
    static_assert(std::is_trivially_destructible_v<Vector> && std::is_trivially_destructible_v<Vector4> &&
                  std::is_trivially_destructible_v<Matrix> && std::is_trivially_destructible_v<Quaternion> &&
                  std::is_trivially_destructible_v<Plane> && std::is_trivially_destructible_v<Sphere> &&
                  std::is_trivially_destructible_v<Color>);

    static_assert(sizeof(OddMath3d) == 1 + 12 + 16 + 64 + 16 + 16 + 16 + 16 + 1);
    static_assert(offsetof(OddMath3d, v) == 1 && offsetof(OddMath3d, v4) == 13 && offsetof(OddMath3d, m) == 29 &&
                  offsetof(OddMath3d, q) == 93 && offsetof(OddMath3d, p) == 109 && offsetof(OddMath3d, s) == 125 &&
                  offsetof(OddMath3d, c) == 141);

    // Serialized bytes at odd addresses.
    const unsigned char bytes[] = {0, 0, 128, 63, 0, 0, 0, 192, 0, 0, 0, 63, 0, 0, 64, 64};
    OddMath3d odd;
    odd.v4 = Vector4(1.f, -2.f, .5f, 3.f);
    odd.p = Plane(Vector(1.f, -2.f, .5f), Vector(3.f, 0.f, 0.f));
    odd.c = Color(1.f, -2.f, .5f, 3.f);
    odd.s.Reset(Vector(1.f, -2.f, .5f));
    odd.s.r = 3.f;
    require(std::memcmp(&odd.v4, bytes, 16) == 0, "packed Vector4 bytes changed");
    require(std::memcmp(&odd.p, bytes, 16) == 0, "packed Plane bytes changed");
    require(std::memcmp(&odd.c, bytes, 16) == 0, "packed Color bytes changed");
    require(std::memcmp(&odd.s, bytes, 16) == 0, "packed Sphere bytes changed");
    CVECTOR4 c4(1.f, -2.f, .5f, 3.f);
    require(std::memcmp(&c4, bytes, 16) == 0, "CVECTOR4 bytes changed");
    for (int i = 0; i < 16; ++i)
        odd.m.matrix[i] = float(i + 1);
    require(odd.m.m[2][1] == 10 && odd.m.v.pos.x == 13 && odd.m.v.w == 16, "packed Matrix representation changed");
    // operator() and the D3DMATRIX view hand out aligned float access, so they
    // are only exercised on aligned storage.
    alignas(16) Matrix aligned(odd.m);
    require(aligned(3, 2) == 15 && aligned(2, 1) == 10, "Matrix element accessor changed");
    require(static_cast<D3DMATRIX *>(aligned)->m[1][2] == 7, "D3D view of Matrix changed");
}

// ---------------------------------------------------------------------------
// Samples and inputs
// ---------------------------------------------------------------------------
// Every sample remembers the harness source line that produced it, so run.py
// --details can attribute differential mismatches to an operation.
using Where = std::source_location;
struct Sink
{
    std::vector<double> samples;
    std::vector<uint32_t> lines;
    void d(double value, Where at = Where::current())
    {
        samples.push_back(value);
        lines.push_back(at.line());
    }
    void f(float value, Where at = Where::current())
    {
        d(value, at);
    }
    void b(bool value, Where at = Where::current())
    {
        d(value ? 1.0 : 0.0, at);
    }
    void v(const Vector &value, Where at = Where::current())
    {
        f(value.x, at);
        f(value.y, at);
        f(value.z, at);
    }
    void v4(const Vector4 &value, Where at = Where::current())
    {
        f(value.x, at);
        f(value.y, at);
        f(value.z, at);
        f(value.w, at);
    }
    void q(const Quaternion &value, Where at = Where::current())
    {
        f(value.x, at);
        f(value.y, at);
        f(value.z, at);
        f(value.w, at);
    }
    void p(const Plane &value, Where at = Where::current())
    {
        v(value.N, at);
        f(value.D, at);
    }
    void s(const Sphere &value, Where at = Where::current())
    {
        f(value.x, at);
        f(value.y, at);
        f(value.z, at);
        f(value.r, at);
    }
    void c(const Color &value, Where at = Where::current())
    {
        f(value.r, at);
        f(value.g, at);
        f(value.b, at);
        f(value.a, at);
    }
    void m(const Matrix &value, Where at = Where::current())
    {
        for (int i = 0; i < 16; ++i)
            f(value.matrix[i], at);
    }
};

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
        static const float finite[] = {0.f,     -0.f,    1.f,     -1.f,    0.5f,    2.f,     1e-30f,  -1e-30f,
                                       1e-42f,  -1e-42f, 1e-20f,  1e-10f,  1e19f,   -1e19f,  1e30f,   -1e30f,
                                       3.3e38f, 1e10f,   -1e10f,  1e-5f};
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
    float tameAngle() { return angleValues(rng); }
    uint32_t u() { return static_cast<uint32_t>(rng()); }
    Vector vec()
    {
        const float x = f();
        const float y = f();
        const float z = f();
        return Vector(x, y, z);
    }
    Vector tameVec()
    {
        const float x = tame();
        const float y = tame();
        const float z = tame();
        return Vector(x, y, z);
    }
    Vector unitVec()
    {
        const float x = unit();
        const float y = unit();
        const float z = unit();
        return Vector(x, y, z);
    }
    Vector4 vec4()
    {
        const float x = f();
        const float y = f();
        const float z = f();
        const float w = f();
        return Vector4(x, y, z, w);
    }
    Quaternion quat()
    {
        const float x = unit();
        const float y = unit();
        const float z = unit();
        const float w = unit();
        return Quaternion(x, y, z, w);
    }
    Color color()
    {
        const float r = unit();
        const float g = unit();
        const float b = unit();
        const float a = unit();
        return Color(r * 1.5f, g * 1.5f, b * 1.5f, a * 1.5f);
    }
    Matrix general()
    {
        Matrix result;
        for (int i = 0; i < 16; ++i)
            result.matrix[i] = f();
        return result;
    }
    // Rotation + translation (edge values appear in the angles and position).
    Matrix rigid()
    {
        const float ax = angle();
        const float ay = angle();
        const float az = angle();
        const float x = f();
        const float y = f();
        const float z = f();
        return Matrix(ax, ay, az, x, y, z);
    }
    Matrix tameRigid()
    {
        const float ax = tameAngle();
        const float ay = tameAngle();
        const float az = tameAngle();
        const float x = tame();
        const float y = tame();
        const float z = tame();
        return Matrix(ax, ay, az, x, y, z);
    }
    // Rank-deficient or almost rank-deficient basis.
    Matrix singularish()
    {
        Matrix result = general();
        const float k = tame();
        const float jitter = (rng() % 2) ? 0.f : 1e-6f;
        for (int row = 0; row < 3; ++row)
            result.m[2][row] = result.m[0][row] * k + result.m[1][row] + jitter;
        return result;
    }
    Matrix anyMatrix()
    {
        switch (rng() % 6)
        {
        case 0:
            return Matrix();
        case 1:
            return general();
        case 2:
            return singularish();
        default:
            return rigid();
        }
    }
};

// ---------------------------------------------------------------------------
// Vector
// ---------------------------------------------------------------------------
static void vectorOps(Gen &g, Sink &o, int trial)
{
    const Vector a = g.vec();
    const Vector b = g.vec();
    const Vector c = g.vec();
    const float s = g.f();
    const float t = g.f();
    const double ds = static_cast<double>(g.f()) * 1.0000001;
    const float lo = s < t ? s : t;
    const float hi = s < t ? t : s;

    // Constructors
    const float fa[3] = {a.x, a.y, a.z};
    const double da[3] = {a.x * 1.0000001, a.y * 0.9999999, a.z * 3.0};
    o.v(Vector(s));
    o.v(Vector(ds));
    o.v(Vector(a.x, a.y, a.z));
    o.v(Vector(fa));
    o.v(Vector(da));
    o.v(Vector(a));

    // Unary
    o.f(~a);
    o.v(!a);
    o.v(-a);

    // Assignment
    Vector r;
    r = s; o.v(r);
    r = ds; o.v(r);
    r = b; o.v(r);

    // Compound assignment
    r = a; r += s; o.v(r);
    r = a; r += ds; o.v(r);
    r = a; r += b; o.v(r);
    r = a; r -= s; o.v(r);
    r = a; r -= ds; o.v(r);
    r = a; r -= b; o.v(r);
    r = a; r *= s; o.v(r);
    r = a; r *= ds; o.v(r);
    r = a; r *= b; o.v(r);
    r = a; r /= s; o.v(r);
    r = a; r /= ds; o.v(r);
    r = a; r /= b; o.v(r);
    r = a; r |= b; o.v(r);
    r = a; r ^= b; o.v(r);
    r = a; r += r; o.v(r);
    r = a; r *= r; o.v(r);
    r = a; r ^= r; o.v(r);
    r = a; r |= r; o.v(r);

    // Binary
    o.v(a + s); o.v(s + a); o.v(a + ds); o.v(ds + a); o.v(a + b);
    o.v(a - s); o.v(s - a); o.v(a - ds); o.v(ds - a); o.v(a - b);
    o.v(a * s); o.v(s * a); o.v(a * ds); o.v(ds * a); o.v(a * b);
    o.v(a / s); o.v(s / a); o.v(a / ds); o.v(ds / a); o.v(a / b);
    o.f(a | b);
    o.v(a ^ b);
    o.b(a > b); o.b(a >= b); o.b(a < b); o.b(a <= b); o.b(a == b); o.b(a != b);
    o.b(a > a); o.b(a >= a); o.b(a < a); o.b(a <= a); o.b(a == a); o.b(a != a);
    const Vector shifted = a + 1e-36f;
    o.b(a == shifted); o.b(a <= shifted); o.b(a >= shifted);
    // Equality is an absolute 1e-34 window per component.
    const Vector tiny(a.x * 1e-35f, a.y * 1e-35f, a.z * 1e-35f);
    o.b(tiny == Vector(0.f)); o.b(tiny != Vector(0.f));
    o.b(Vector(0.f) == Vector(5e-35f, -5e-35f, 0.f)); o.b(Vector(0.f) == Vector(0.f, 2e-34f, 0.f));
    o.b(Vector(0.f) == Vector(0.f, 0.f, -1e-33f)); o.b(Vector(1e-34f, 0.f, 0.f) == Vector(1.5e-34f, 0.f, 0.f));

    // Transformation
    r = a; o.f(r.Normalize()); o.v(r);
    r = a; o.f(r.Normalize2D()); o.v(r);
    r = a; o.f(r.ClampLength(s)); o.v(r);
    r = a; o.f(r.ClampLength(fabsf(s) * 0.25f)); o.v(r);
    r = a; o.v(r.ClampX(lo, hi));
    r = a; o.v(r.ClampY(lo, hi));
    r = a; o.v(r.ClampZ(lo, hi));
    r = a; o.v(r.Clamp(lo, hi));
    r = a; o.v(r.Clamp(s, t));
    r = a; o.v(r.Min(b));
    r = a; o.v(r.Max(b));

    // Utilities
    o.v(r.Set(c.x, c.y, c.z));
    o.v(r.Set(b));
    o.f(a.GetAngle(b));
    o.f(a.GetAngle(a));
    o.f(a.GetAngle2D(b));
    o.f(a.GetAY());
    o.f(a.GetAY(s));
    o.f(a.Sin2D(b));
    o.f(a.Cos2D(b));
    o.f(a.Cross2D(b));
    o.v(a.Get2D());
    o.v(a.Get2D(s));
    r = a; o.v(r.Make2D(s));
    o.f(a.GetLength());
    o.f(a.GetLength2D());
    o.f(a.GetLength2D2());
    r = a; o.v(r.Rotate(s));
    r = a; o.v(r.Rotate(s, t));
    r = a; o.v(r.Rotate_PI2_CW());
    r = a; o.v(r.Rotate_PI2_CCW());
    r = c; o.v(r.Lerp(a, b, s));
    r = c; o.v(r.Lerp(a, b, g.unit()));
    r = a; o.v(r.Reflection(b));
    r = a; o.v(r.Reflection(!b));

    // rand() driven fillers: the consumption order of rand() is part of the behaviour.
    srand(static_cast<unsigned>(trial) * 2654435761u + 17u);
    r = a; o.v(r.Rand());
    r = a; o.v(r.Rand2D());
    r = a; o.v(r.Rand(a, b));
    r = a; o.v(r.Rand(a, s));
    o.d(rand());

    r = a; o.b(r.MoveByStep(b, s)); o.v(r);
    r = a; o.b(r.MoveByStep(b, fabsf(s) * 0.1f)); o.v(r);
    r = a; o.b(r.MoveByStep(a, 1.f)); o.v(r);
    r = a; o.b(r.MoveByStep(a + 1e-11f, 1.f)); o.v(r);
    r = a; o.b(r.MoveByStep2D(b, s)); o.v(r);
    r = a; o.b(r.MoveByStep2D(b, fabsf(s) * 0.1f)); o.v(r);
    r = a; o.b(r.MoveByStep2D(a, 1.f)); o.v(r);
    Vector boxMin = a, boxMax = a;
    boxMin.Min(b);
    boxMax.Max(b);
    o.b(c.InBox(boxMin, boxMax));
    o.b(a.InBox(boxMin, boxMax));
    o.b(c.InBox(a, b));
    o.b(a.InSphere(b, s));
    o.b(a.InSphere(b, 12.f));
    o.b(a.InSphere(a, 1e-3f));
}

// ---------------------------------------------------------------------------
// Vector4 / CVECTOR4
// ---------------------------------------------------------------------------
static void vector4Ops(Gen &g, Sink &o)
{
    const Vector4 a = g.vec4();
    const Vector b = g.vec();
    const float s = g.f();
    const double ds = static_cast<double>(g.f()) * 1.0000001;
    const float fa[3] = {a.x, a.y, a.z};
    const double da[3] = {a.x * 1.0000001, a.y * 0.9999999, a.z * 3.0};

    o.v4(Vector4(s));
    o.f(Vector4(ds).w); // only w is defined by this constructor
    o.v4(Vector4(a.x, a.y, a.z));
    o.v4(Vector4(a.x, a.y, a.z, a.w));
    o.v4(Vector4(fa));
    o.v4(Vector4(da));
    o.v4(Vector4(b));
    o.v4(Vector4(a));
    o.f(~a);
    o.v4(-a);
    Vector4 r(a);
    r = s; o.v4(r);
    r = ds; o.v4(r);
    r = b; o.v4(r);
    r = a; o.v4(r);
    r.Normalize(); o.v4(r);
    for (int i = 0; i < 4; ++i)
        o.f(a.v4[i]);

    float raw[4] = {a.x, a.y, a.z, a.w};
    const CVECTOR4 c0(s);
    const CVECTOR4 c1(a.x, a.y, a.z, a.w);
    const CVECTOR4 c2(raw);
    for (const auto &value : {c0, c1, c2})
    {
        o.f(value.x); o.f(value.y); o.f(value.z); o.f(value.w);
        o.f(value.v[0]); o.f(value.v[3]);
    }
}

// ---------------------------------------------------------------------------
// Matrix
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct ProjectionInput
{
    char prefix;
    Vector v;
    char suffix[3];
};
struct ProjectionOutput
{
    char prefix;
    Vector4 v;
    float guard;
};
#pragma pack(pop)

static void matrixOps(Gen &g, Sink &o)
{
    const Matrix a = g.anyMatrix();
    const Matrix b = g.anyMatrix();
    const Matrix rigid = g.rigid();
    const Matrix general = g.general();
    const Vector p = g.vec();
    const Vector n = g.vec();
    const Vector ang(g.angle(), 0.f, 0.f);
    const float ax = ang.x;
    const float ay = g.angle();
    const float az = g.angle();
    const Vector angles(ax, ay, az);
    const float s = g.f();
    const float t = g.f();
    const float u = g.f();
    const double ds = static_cast<double>(g.f()) * 1.0000001;

    // Constructors
    o.m(Matrix());
    o.m(Matrix(ax, ay, az, p.x, p.y, p.z));
    o.m(Matrix(ax, ay, az));
    o.m(Matrix(angles, p));
    o.m(Matrix(angles));
    o.m(Matrix(a));
    o.m(Matrix(a, b));
    o.m(Matrix(rigid, general));

    // Operators
    Matrix r;
    r = a; o.m(r);
    r = a; r = s; o.m(r);
    r = a; r = ds; o.m(r);
    r = a; r = p; o.m(r);
    r = a; r *= b; o.m(r);
    r = a; r *= r; o.m(r);
    o.m(a * b);
    o.m(b * a);
    o.m(rigid * general);
    o.v(a * p);
    o.v(p * a);

    // Filling
    r = a; o.m(r.SetIdentity());
    r = a; o.m(r.Set(b));
    r = a; o.m(r.Build(ax, ay, az, p.x, p.y, p.z));
    r = a; o.m(r.Build(ax, ay, az));
    r = a; o.m(r.Build(angles, p));
    r = a; o.m(r.Build(angles));
    r = a; o.m(r.BuildXYZ(ax, ay, az, p.x, p.y, p.z));
    r = a; o.m(r.BuildRotateX(ax));
    r = a; o.m(r.BuildRotateY(ay));
    r = a; o.m(r.BuildRotateZ(az));
    r = a; o.m(r.BuildPosition(p.x, p.y, p.z));
    r = a; o.m(r.BuildPosition(p));
    r = a; o.m(r.BuildScale(s));
    r = a; o.m(r.BuildScale(s, t, u));
    r = a; o.m(r.BuildScale(p));
    r = a; o.m(r.BuildProjection(fabsf(g.unit()) + 0.2f, 1600.f, 900.f, 0.1f + fabsf(s), 200.f + fabsf(t)));
    r = a; o.m(r.BuildProjection(s, t, u, p.x, p.y));
    r = a; o.m(r.BuildOrtoProjection(1600.f, 900.f, 0.1f + fabsf(s), 200.f + fabsf(t)));
    r = a; o.m(r.BuildOrtoProjection(s, t, u, p.x));
    r = a; o.m(r.BuildShadowProjection(fabsf(g.unit()) + 0.2f, 512.f, 256.f, 0.1f + fabsf(s), 200.f + fabsf(t)));
    r = a; o.m(r.BuildShadowProjection(s, t, u, p.x, p.y));
    r = a; o.b(r.BuildView(p, n, Vector(0.f, 1.f, 0.f))); o.m(r);
    r = a; o.b(r.BuildView(p, n, g.vec())); o.m(r);
    r = a; o.b(r.BuildView(p, p, n)); o.m(r);
    r = a; o.b(r.BuildView(p, p + Vector(0.f, 2.f, 0.f), Vector(0.f, 1.f, 0.f))); o.m(r);
    r = a; o.b(r.BuildView(p, p + n, n)); o.m(r);
    r = a; o.b(r.BuildOrient(p, n)); o.m(r);
    r = a; o.b(r.BuildOrient(p, p)); o.m(r);
    r = a; o.b(r.BuildOrient(Vector(0.f), n)); o.m(r);
    r = a; o.b(r.BuildOrient(p, Vector(0.f))); o.m(r);
    r = a; o.b(r.BuildOriented(p, n, Vector(0.f, 1.f, 0.f))); o.m(r);
    r = a; o.b(r.BuildOriented(p, n, g.vec())); o.m(r);
    r = a; o.b(r.BuildOriented(p, p, n)); o.m(r);
    r = a; o.b(r.BuildOriented(p, n, n - p)); o.m(r);
    r = a; o.b(r.BuildOriented(p, n, Vector(0.f))); o.m(r);
    const Vector mirror = !n;
    r = a; o.m(r.BuildMirror(mirror.x, mirror.y, mirror.z, s));
    r = a; o.m(r.BuildMirror(n.x, n.y, n.z, s));

    // Transformation
    r = a; o.m(r.RotateX(ax));
    r = a; o.m(r.RotateY(ay));
    r = a; o.m(r.RotateZ(az));
    r = a; o.m(r.Rotate(ax, ay, az));
    r = a; o.m(r.Rotate(angles));
    r = a; o.m(r.Move(p.x, p.y, p.z));
    r = a; o.m(r.Move(p));
    r = a; o.m(r.Scale(s));
    r = a; o.m(r.Scale3x3(s));
    r = a; o.m(r.Scale(s, t, u));
    r = a; o.m(r.Scale3x3(s, t, u));
    r = a; o.m(r.Scale(p));
    r = a; o.m(r.Scale3x3(p));
    r = a; o.m(r.Inverse());
    r = rigid; o.m(r.Inverse());
    r = a; o.m(r.Inverse(b));
    r = a; o.m(r.Inverse(rigid));
    r = a; o.m(r.InverseWhithScale());
    r = rigid; o.m(r.InverseWhithScale());
    r = general; o.m(r.InverseWhithScale());
    r = g.singularish(); o.m(r.InverseWhithScale());
    r = rigid; r.Scale3x3(s, t, u); o.m(r.InverseWhithScale());
    r = rigid; r.Scale3x3(1e-30f); o.m(r.InverseWhithScale());
    r = a; o.m(r.Transposition());
    r = general; o.m(r.Transposition());
    r = a; o.m(r.Transposition3X3());
    r = general; o.m(r.Transposition3X3());

    // Utilities
    r = a; o.m(r.SetRotate(b));
    r = a; o.m(r.EqMultiply(a, b));
    r = a; o.m(r.EqMultiply(r, b));
    r = a; o.m(r.EqMultiply(b, r));
    r = a; o.m(r.EqMultiply(r, r));
    r = a; o.m(r.EqMultiply(general, rigid));
    r = a; o.m(r.EqMultiplyFast(a, b));
    r = a; o.m(r.EqMultiplyFast(general, rigid));
    r = a; o.m(r.EqMultiplyFast(general, general));
    for (const Matrix *mtx : {&a, &rigid, &general})
    {
        o.v(mtx->MulVertex(p));
        o.v(mtx->MulNormal(p));
        o.v(mtx->MulVertexByInverse(p));
        o.v(mtx->MulNormalByInverse(p));
        o.v(mtx->GetCamPos());
        o.b(mtx->IsIdentity());
        o.b(mtx->IsScale());
        o.v4(mtx->Projection(p));
        o.v4(mtx->Projection(p, 800.f, 450.f));
        float gx = 11.f, gy = 12.f, gz = 13.f;
        mtx->GetAngles(gx, gy, gz);
        o.f(gx); o.f(gy); o.f(gz);
        alignas(float) Vector got(21.f, 22.f, 23.f); // GetAngles binds float& to the components
        mtx->GetAngles(got);
        o.v(got);
        o.v4(mtx->GetVectorX()); o.v4(mtx->GetVectorY()); o.v4(mtx->GetVectorZ()); o.v4(mtx->GetVectorW());
        alignas(16) const Matrix alignedCopy(*mtx);
        const D3DMATRIX *d3d = alignedCopy;
        o.f(d3d->m[3][1]); o.f(d3d->m[1][3]);
    }
    {
        Matrix nearIdentity;
        nearIdentity.m[1][2] = 5e-5f;
        nearIdentity.m[3][3] = 1.00005f;
        o.b(nearIdentity.IsIdentity()); o.b(nearIdentity.IsScale());
        nearIdentity.m[2][0] = 2e-4f;
        o.b(nearIdentity.IsIdentity()); o.b(nearIdentity.IsScale());
        nearIdentity.SetIdentity();
        nearIdentity.v.w = 1.0002f;
        o.b(nearIdentity.IsIdentity()); o.b(nearIdentity.IsScale());
        alignas(16) Matrix element(a);
        element(2, 1) = s; element(3, 3) = t; o.m(element);
    }

    // Projection of a real perspective matrix, single and strided array.
    Matrix projection;
    projection.BuildProjection(fabsf(g.unit()) + 0.3f, 1600.f, 900.f, 0.2f, 500.f);
    Matrix view;
    const Vector eye = g.tameVec();
    const Vector target = g.tameVec();
    view.BuildView(eye, target, Vector(0.f, 1.f, 0.f));
    const Matrix viewProjection(view, projection);
    ProjectionInput input[5];
    ProjectionOutput output[5];
    for (int i = 0; i < 5; ++i)
    {
        input[i].prefix = char(i);
        input[i].v = i < 3 ? g.tameVec() : g.vec();
        output[i].prefix = char(40 + i);
        output[i].v = Vector4(-7.f, -7.f, -7.f, -7.f);
        output[i].guard = 100.f + float(i);
        o.v4(viewProjection.Projection(input[i].v, 800.f, 450.f));
    }
    viewProjection.Projection(&output[0].v, &input[0].v, 4, 800.f, 450.f, sizeof(ProjectionInput),
                              sizeof(ProjectionOutput));
    for (int i = 0; i < 5; ++i)
    {
        o.v4(output[i].v);
        o.f(output[i].guard);
        o.d(output[i].prefix);
    }
    Vector tight[3] = {input[0].v, input[1].v, input[4].v};
    Vector4 tightResult[3];
    for (auto &value : tightResult)
        value = Vector4(-3.f, -3.f, -3.f, -3.f);
    a.Projection(tightResult, tight, 3);
    for (const auto &value : tightResult)
        o.v4(value);
    a.Projection(tightResult, tight, 0);
    a.Projection(tightResult, tight, -2);
    for (const auto &value : tightResult)
        o.v4(value);
}

// ---------------------------------------------------------------------------
// Quaternion
// ---------------------------------------------------------------------------
static void quaternionOps(Gen &g, Sink &o)
{
    const Quaternion a = g.quat();
    const Quaternion b = g.quat();
    const float s = g.f();
    const float ax = g.angle();
    const float ay = g.angle();
    const float az = g.angle();
    const Vector angles(ax, ay, az);
    const Matrix rotation = g.tameRigid();
    const Matrix rigid = g.rigid();
    const Matrix general = g.general();

    // Constructors
    o.q(Quaternion());
    o.q(Quaternion(ax, ay, az));
    o.q(Quaternion(angles));
    o.q(Quaternion(a.x, a.y, a.z, a.w));
    o.q(Quaternion(rotation));
    o.q(Quaternion(rigid));
    o.q(Quaternion(general));

    // Operators
    Quaternion r;
    r = a; o.q(-r); o.q(r);
    r = b; o.q(r);
    r = a; r += b; o.q(r);
    r = a; r += s; o.q(r);
    r = a; r -= b; o.q(r);
    r = a; r -= s; o.q(r);
    r = a; r *= s; o.q(r);
    r = a; r *= b; o.q(r);
    r = a; r *= r; o.q(r);
    r = a; r /= s; o.q(r);
    o.q(s + a); o.q(a + s); o.q(a + b);
    o.q(s - a); o.q(a - s); o.q(a - b);
    o.q(s * a); o.q(a * s); o.q(a * b); o.q(b * a);
    o.q(a / s);
    o.q(Quaternion() * a);
    o.q(a * Quaternion());

    // Filling
    r = a; r.SetIdentity(); o.q(r);
    r = a; r.Set(ax, ay, az); o.q(r);
    r = a; r.Set(angles); o.q(r);
    r = a; r.Set(b.x, b.y, b.z, b.w); o.q(r);
    r = a; r.Set(rotation); o.q(r);
    r = a; r.Set(general); o.q(r);
    // Every branch of the matrix conversion: half turns around each axis and diagonal dominance permutations.
    static const float pi = 3.14159265358979f;
    const float halfTurns[][3] = {{pi, 0.f, 0.f},      {0.f, pi, 0.f},      {0.f, 0.f, pi},      {pi, pi, 0.f},
                                  {pi * .99f, .3f, 0.f}, {.2f, pi * .98f, .1f}, {.1f, .2f, pi * .97f}, {0.f, 0.f, 0.f}};
    for (const auto &turn : halfTurns)
    {
        Matrix m(turn[0], turn[1], turn[2]);
        r.Set(m); o.q(r);
        Matrix back;
        r.GetMatrix(back); o.m(back);
    }
    {
        Matrix diagonal;
        const float d0 = g.f(), d1 = g.f(), d2 = g.f();
        diagonal.m[0][0] = d0; diagonal.m[1][1] = d1; diagonal.m[2][2] = d2;
        r.Set(diagonal); o.q(r);
        diagonal.m[0][0] = -fabsf(d0); diagonal.m[1][1] = -fabsf(d1); diagonal.m[2][2] = -fabsf(d2);
        r.Set(diagonal); o.q(r);
        diagonal.m[0][0] = -1.f; diagonal.m[1][1] = -1.f; diagonal.m[2][2] = -1.f;
        r.Set(diagonal); o.q(r);
        Matrix zero;
        for (int i = 0; i < 16; ++i)
            zero.matrix[i] = 0.f;
        r.Set(zero); o.q(r);
    }

    // Transformation
    r = a; o.q(r.Normalize()); o.q(r);
    r = a; r *= 1e-25f; o.q(r.Normalize());
    r = a; r *= 1e25f; o.q(r.Normalize());
    r.Set(0.f, 0.f, 0.f, 0.f); o.q(r.Normalize());
    r = a; o.q(r.Conjugate()); o.q(r);
    r = a; o.q(r.Inverse()); o.q(r);
    r.Set(0.f, -0.f, 0.f, 0.f); o.q(r.Inverse());

    Quaternion na = a, nb = b;
    na.Normalize();
    nb.Normalize();
    Quaternion antipodal = na;
    -antipodal;
    Quaternion nearA = na;
    nearA += 1e-3f;
    nearA.Normalize();
    Quaternion nearAntipodal = nearA;
    -nearAntipodal;
    const Quaternion conjugated = Quaternion(na).Conjugate();
    const float blends[] = {0.f, 1.f, 0.5f, 0.25f, g.unit(), s, -0.5f, 1.5f};
    for (const float blend : blends)
    {
        r = b; o.q(r.SLerp(na, nb, blend));
        r = b; o.q(r.SLerp(a, b, blend));
        r = b; o.q(r.SLerp(na, na, blend));
        r = b; o.q(r.SLerp(na, antipodal, blend));
        r = b; o.q(r.SLerp(na, nearA, blend));
        r = b; o.q(r.SLerp(na, nearAntipodal, blend));
        r = b; o.q(r.SLerp(na, conjugated, blend));
        r = na; o.q(r.SLerp(r, nb, blend));
        r = nb; o.q(r.SLerp(na, r, blend));
    }

    // Utilities
    Matrix mtx = general;
    a.GetMatrix(mtx); o.m(mtx);
    na.GetMatrix(mtx); o.m(mtx);
    Quaternion(rotation).GetMatrix(mtx); o.m(mtx);
    Quaternion scaled = a;
    scaled *= s;
    scaled.GetMatrix(mtx); o.m(mtx);
    o.f(a.GetLength()); o.f(a.GetLengthSqr());
    o.f(na.GetLength()); o.f(na.GetLengthSqr());
    o.f(scaled.GetLength()); o.f(scaled.GetLengthSqr());
    for (int i = 0; i < 4; ++i)
        o.f(a.q[i]);
}

// ---------------------------------------------------------------------------
// Plane
// ---------------------------------------------------------------------------
static void planeOps(Gen &g, Sink &o)
{
    const Vector normal = g.vec();
    const Vector point = g.vec();
    const Vector src = g.vec();
    const Vector dst = g.vec();

    o.p(Plane(normal.x, normal.y, normal.z));
    o.v(Plane(normal).N); // D is not defined by this constructor
    const Plane plane(normal, point);
    o.p(plane);
    o.p(Plane(plane));
    const Plane unit(!normal, point);
    o.p(unit);
    o.f(src * plane); o.f(plane * src); o.f(plane.Dist(src));
    o.f(src * unit); o.f(unit * src); o.f(unit.Dist(dst));

    Plane r(plane);
    o.p(r.Normalize()); o.p(r);
    r = Plane(0.f, 0.f, 0.f); r.D = 5.f; o.p(r.Normalize());
    r = Plane(normal * 1e-30f, point); o.p(r.Normalize());
    r = plane; o.p(r.Move(dst)); o.p(r);

    for (const Plane *pl : {&plane, &unit})
    {
        o.b(pl->Intersection(src, dst));
        o.b(pl->Intersection(src, src));
        Vector result(31.f, 32.f, 33.f);
        o.b(pl->Intersection(src, dst, result)); o.v(result);
        result = Vector(31.f, 32.f, 33.f);
        o.b(pl->Intersection(dst, src, result)); o.v(result);
        result = Vector(31.f, 32.f, 33.f);
        o.b(pl->Intersection(point, dst, result)); o.v(result);
        result = Vector(31.f, 32.f, 33.f);
        o.b(pl->Intersection(src, src, result)); o.v(result);
        result = Vector(31.f, 32.f, 33.f);
        o.b(pl->Intersection(src, src + (dst - src) * 2.f, result)); o.v(result);
        float k = 41.f;
        o.b(pl->IntersectionLine(src, dst, k)); o.f(k);
        k = 41.f;
        o.b(pl->IntersectionLine(src, src, k)); o.f(k);
        k = 41.f;
        o.b(pl->IntersectionLine(point, dst, k)); o.f(k);
    }
}

// ---------------------------------------------------------------------------
// Sphere
// ---------------------------------------------------------------------------
static void sphereOps(Gen &g, Sink &o)
{
    const Vector center = g.vec();
    const Vector p = g.vec();
    const Vector src = g.vec();
    const Vector dst = g.vec();
    const float radius = g.f();

    Sphere sphere;
    sphere.Reset(center);
    o.s(sphere);
    sphere.r = radius;
    o.s(Sphere(sphere));
    Sphere wide;
    wide.Reset(center);
    wide.r = fabsf(radius) + 6.f;
    Sphere other;
    other.Reset(p);
    other.r = fabsf(g.f());

    for (Sphere *sp : {&sphere, &wide})
    {
        o.b(sp->Intersection(p));
        o.b(sp->Intersection(center));
        o.b(sp->Intersection(src, dst));
        o.b(sp->Intersection(dst, src));
        o.b(sp->Intersection(src, src));
        o.b(sp->Intersection(center, dst));
        o.b(sp->Intersection(src, src + (center - src) * 0.5f));
        o.b(sp->Intersection(src, src + (center - src) * 3.f));
        o.b(sp->Intersection(other));
        o.b(sp->Intersection(*sp));
        const Vector dir = !(dst - src);
        float distance = 51.f;
        o.b(sp->Intersection(src, dir, &distance)); o.f(distance);
        distance = 51.f;
        o.b(sp->Intersection(src, !(center - src), &distance)); o.f(distance);
        o.b(sp->Intersection(src, dir, nullptr));
        distance = 51.f;
        o.b(Sphere::Intersection(src, dir, center, radius, &distance)); o.f(distance);
        distance = 51.f;
        o.b(Sphere::Intersection(src, !(center - src), center, sp->r, &distance)); o.f(distance);
        o.b(Sphere::Intersection(src, dir, center, sp->r, nullptr));
        o.f(sp->v4.x); o.f(sp->v4.w);
    }

    Sphere bound;
    bound.Reset(src);
    bound.AddPoint(src); o.s(bound);
    bound.AddPoint(dst); o.s(bound);
    bound.AddPoint(p); o.s(bound);
    bound.AddPoint(center); o.s(bound);
    bound.AddPoint(dst); o.s(bound);
    for (int i = 0; i < 4; ++i)
    {
        bound.AddPoint(g.vec());
        o.s(bound);
    }
}

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------
static void colorOps(Gen &g, Sink &o)
{
    const Color a = g.color();
    const Color b = g.color();
    const Vector v = g.unitVec();
    const Vector4 v4 = g.vec4();
    const float s = g.unit();
    const float t = g.unit();
    const float wide = g.f();
    const uint32_t packed = g.u();
    const uint32_t packed2 = g.u();
    const float lo = s < t ? s : t;
    const float hi = s < t ? t : s;
    const float fa[3] = {a.r, a.g, a.b};

    // Constructors
    o.c(Color());
    o.c(Color(s));
    o.c(Color(s, t));
    o.c(Color(a.r, a.g, a.b));
    o.c(Color(a.r, a.g, a.b, a.a));
    o.c(Color(fa));
    o.c(Color(fa, t));
    o.c(Color(v));
    o.c(Color(v, t));
    o.c(Color(v4));
    o.c(Color(packed));
    o.c(Color(a));

    // Unary
    o.f(~a);
    o.c(!a);
    o.c(-a);

    // Assignment
    Color r;
    r = a; r = s; o.c(r);
    r = a; r = packed; o.c(r);
    r = a; r = v; o.c(r);
    r = a; r = v4; o.c(r);
    r = b; o.c(r);

    // Compound assignment
    r = a; r += s; o.c(r);
    r = a; r += packed; o.c(r);
    r = a; r += v; o.c(r);
    r = a; r += v4; o.c(r);
    r = a; r += b; o.c(r);
    r = a; r -= s; o.c(r);
    r = a; r -= packed; o.c(r);
    r = a; r -= v; o.c(r);
    r = a; r -= v4; o.c(r);
    r = a; r -= b; o.c(r);
    r = a; r *= s; o.c(r);
    r = a; r *= packed; o.c(r);
    r = a; r *= v; o.c(r);
    r = a; r *= v4; o.c(r);
    r = a; r *= b; o.c(r);
    r = a; r /= s; o.c(r);
    r = a; r /= packed; o.c(r);
    r = a; r /= v; o.c(r);
    r = a; r /= v4; o.c(r);
    r = a; r /= b; o.c(r);
    r = a; r |= b; o.c(r);
    r = a; r |= r; o.c(r);
    r = a; r += r; o.c(r);
    r = a; r *= r; o.c(r);
    o.d(static_cast<uint32_t>(a));
    o.d(static_cast<uint32_t>(b));
    o.d(static_cast<uint32_t>(Color(packed)));

    // Binary
    o.c(a + s); o.c(s + a); o.c(a + packed); o.c(packed + a); o.c(a + v); o.c(v + a); o.c(a + v4); o.c(v4 + a);
    o.c(a + b);
    o.c(a - s); o.c(s - a); o.c(a - packed); o.c(packed - a); o.c(a - v); o.c(v - a); o.c(a - v4); o.c(v4 - a);
    o.c(a - b);
    o.c(a * s); o.c(s * a); o.c(a * packed); o.c(packed * a); o.c(a * v); o.c(v * a); o.c(a * v4); o.c(v4 * a);
    o.c(a * b);
    o.c(a / s); o.c(s / a); o.c(a / packed); o.c(packed / a); o.c(a / v); o.c(v / a); o.c(a / v4); o.c(v4 / a);
    o.c(a / b);
    o.c(a | b);

    // Transformation
    r = a; r.Clamp(); o.c(r);
    r = a; r.Clamp(lo, hi); o.c(r);
    r = a; r.Clamp(s, t); o.c(r);
    r = a; r.Min(b); o.c(r);
    r = a; r.Max(b); o.c(r);
    r = a; r.Brightness(s); o.c(r);
    r = a; r.Brightness(wide); o.c(r);
    r = a; r.Gamma(s); o.c(r);
    r = a; r.Gamma(-s); o.c(r);
    r = a; r.Gamma(wide); o.c(r);
    r = a; r.Gamma(0.f); o.c(r);
    r = a; r.Contrast(s); o.c(r);
    r = a; r.Contrast(wide); o.c(r);
    r = a; r.Processed(s, t, wide); o.c(r);
    r = a; r.Processed(t, s, 0.4f); o.c(r);

    // Utilities
    o.f(a.GetIntensity());
    r = a; o.c(r.Normalize()); o.c(r);
    r = a * 3.f; o.c(r.Normalize());
    r = a; r.Clamp(); o.c(r.Normalize());
    o.f(a.GetDistance(b));
    o.f(a.GetDistanceSqr(b));
    o.f(a.GetDistance(a));
    r = b; o.c(r.Lerp(a, b, s));
    r = a; o.c(r.Lerp(a, b, wide));
    r = b; o.c(r.LerpA(a, b, s));
    r = a; o.c(r.LerpA(a, b, wide));
    r = a; o.c(r.MulColor(wide));
    r = a; o.c(r.MulAlpha(wide));
    r = a; o.c(r.SwapRB());
    o.d(a.GetDword());
    o.d(Color(packed).GetDword());
    o.d(Color(packed2).GetDword());
    o.d((!a).GetDword());
    o.d((a * wide).GetDword());
    o.d(Color::Make565(packed));
    o.d(Color::Make555(packed));
    o.d(Color::Make1555(packed));
    o.d(Color::Make4444(packed));
}

// ---------------------------------------------------------------------------
// Self-consistency (independent of any baseline)
// ---------------------------------------------------------------------------
static void near(double a, double b, const char *reason, double tolerance = 5e-5)
{
    require(std::abs(a - b) <= tolerance * (1.0 + std::abs(b)), reason);
}
static void invariants()
{
    std::mt19937 rng(0x494e5631u);
    std::uniform_real_distribution<float> values(-10.f, 10.f);
    for (int trial = 0; trial < 500; ++trial)
    {
        float in[9];
        for (float &value : in)
            value = values(rng);
        const Matrix rigid(in[0] * .3f, in[1] * .3f, in[2] * .3f, in[3], in[4], in[5]);
        const Vector p(in[6], in[7], in[8]);
        const Vector moved = rigid.MulVertex(p);
        const Vector back = rigid.MulVertexByInverse(moved);
        near(back.x, p.x, "rigid inverse vertex"); near(back.y, p.y, "rigid inverse vertex");
        near(back.z, p.z, "rigid inverse vertex");
        Matrix inverse(rigid);
        inverse.Inverse();
        const Matrix identity(rigid, inverse);
        require(identity.IsIdentity() && !identity.IsScale(), "rigid inverse composition");
        Matrix scaled(rigid);
        scaled.Scale3x3(1.5f, 2.f, 0.75f);
        Matrix scaledInverse(scaled);
        scaledInverse.InverseWhithScale();
        require(Matrix(scaled, scaledInverse).IsIdentity(), "scaled inverse composition");
        require(scaled.IsScale(), "scale detection");

        Quaternion q(rigid);
        Matrix fromQuaternion;
        q.GetMatrix(fromQuaternion);
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                near(fromQuaternion.m[column][row], rigid.m[column][row], "matrix/quaternion round trip", 2e-4);
        near(q.GetLength(), 1.0, "rotation quaternion is unit");
        Quaternion interpolated;
        interpolated.SLerp(q, q, 0.37f);
        near(interpolated.x, q.x, "slerp of equal endpoints"); near(interpolated.w, q.w, "slerp of equal endpoints");

        const float nx = values(rng), ny = values(rng), nz = values(rng);
        const Vector normal = !Vector(nx, ny, nz);
        const Plane plane(normal, p);
        near(plane.Dist(p), 0.0, "plane contains its anchor", 1e-4);
        near(plane.Dist(p + normal * 2.f), 2.0, "plane distance", 1e-4);
        Vector reflected = normal * 3.f;
        reflected.Reflection(normal);
        near(reflected | normal, -3.0, "reflection flips the normal component", 1e-4);

        Sphere sphere;
        sphere.Reset(p);
        sphere.AddPoint(p + Vector(2.f, 0.f, 0.f));
        near(sphere.r, 1.0, "bounding sphere radius", 1e-4);
        near(sphere.x, p.x + 1.f, "bounding sphere center", 1e-4);

        Color color(0.25f, 0.5f, 0.75f, 1.f);
        require(color.GetDword() == 0xff4080bfu, "color packing");
        near(Color(0xff4080bfu).g, 128.0 / 255.0, "color unpacking");
    }
}

int main(int argc, char **argv)
{
    layout();
    invariants();
    Gen g;
    Sink o;
    const int trials = 2500;
    for (int trial = 0; trial < trials; ++trial)
    {
        g.trial(trial);
        vectorOps(g, o, trial);
        vector4Ops(g, o);
        matrixOps(g, o);
        quaternionOps(g, o);
        planeOps(g, o);
        sphereOps(g, o);
        colorOps(g, o);
    }
    if (argc > 1)
    {
        std::ofstream out(argv[1], std::ios::binary);
        out.write(reinterpret_cast<const char *>(o.samples.data()),
                  static_cast<std::streamsize>(o.samples.size() * sizeof(double)));
        out.close();
        require(!out.fail(), "write differential samples");
        std::ofstream where(std::string(argv[1]) + ".lines", std::ios::binary);
        where.write(reinterpret_cast<const char *>(o.lines.data()),
                    static_cast<std::streamsize>(o.lines.size() * sizeof(uint32_t)));
        where.close();
        require(!where.fail(), "write sample source lines");
    }
    std::cout << "PASS: math3d layout, invariants, " << trials << " randomized trials, " << o.samples.size()
              << " samples\n";
}
