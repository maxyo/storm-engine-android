// Vector/matrix contract over legacy_api.h: the same source runs on the
// current GLM types and, for run.py --baseline, on the archived class API.
#include "legacy_api.h"
#include "sample_log.h"
#include <d3d9.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

using mt::DVec3;
using mt::Mat4;
using mt::Vec3;

static void require(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}
static void near(double a, double b, const char *reason, double tolerance = 3e-5)
{
    require(std::abs(a - b) <= tolerance * (1.0 + std::abs(b)), reason);
}
static void vectorNear(const Vec3 &a, const Vec3 &b, const char *reason)
{
    near(a.x, b.x, reason); near(a.y, b.y, reason); near(a.z, b.z, reason);
}
static void layoutAndBytes()
{
    static_assert(sizeof(Vec3) == 12 && alignof(Vec3) == 4);
    static_assert(sizeof(DVec3) == 24 && alignof(DVec3) == alignof(double));
    static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == mt::kMat4Alignment);
    static_assert(offsetof(Vec3, x) == 0 && offsetof(Vec3, y) == 4 && offsetof(Vec3, z) == 8);
    static_assert(offsetof(DVec3, x) == 0 && offsetof(DVec3, y) == 8 && offsetof(DVec3, z) == 16);
    static_assert(std::is_trivially_copyable_v<Vec3> && std::is_trivially_copyable_v<DVec3>);
    static_assert(std::is_standard_layout_v<Vec3> && std::is_standard_layout_v<DVec3>);
    static_assert(sizeof(mt::ProjPoint) == 16 && offsetof(mt::ProjPoint, rhw) == 12);
#ifndef STORM_MATH_TEST_BASELINE
    // The storage types are GLM's own, with nothing Storm-specific in between.
    static_assert(std::is_same_v<Vec3, glm::vec3> && std::is_same_v<DVec3, glm::dvec3> &&
                  std::is_same_v<Mat4, glm::mat4>);
    static_assert(sizeof(glm::mat4::col_type) == 16 && std::is_standard_layout_v<glm::mat4> &&
                  std::is_trivially_copyable_v<glm::mat4>);
#endif
    require(std::endian::native == std::endian::little, "legacy binary fixtures require little endian");
    // The exact byte sequence consumed by existing SaveVector and animation I/O.
    const unsigned char fixture[] = {0,0,128,63, 0,0,0,192, 0,0,0,63};
    Vec3 saved(1.f, -2.f, .5f), loaded;
    require(std::memcmp(&saved, fixture, 12) == 0, "SaveVector bytes changed");
    std::memcpy(&loaded, fixture, 12);
    vectorNear(loaded, saved, "read historical vector bytes");
    const unsigned char doubleFixture[] = {0,0,0,0,0,0,240,63, 0,0,0,0,0,0,0,192, 0,0,0,0,0,0,224,63};
    DVec3 savedDouble(1., -2., .5);
    require(std::memcmp(&savedDouble, doubleFixture, 24) == 0, "double vector bytes changed");
    Vec3 stride[] = {saved, Vec3(2.f, 4.f, 6.f)};
    require(reinterpret_cast<char *>(&stride[1]) - reinterpret_cast<char *>(&stride[0]) == 12, "vertex stride changed");
    // Default construction: identity matrix (every generation); the adapter's
    // identity() is the explicit spelling.
    Mat4 matrix;
    const float identityBytes[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    require(std::memcmp(&matrix, identityBytes, 64) == 0, "default matrix is not identity");
    matrix = mt::translation(1.f, 2.f, 3.f);
    matrix = mt::identity();
    require(std::memcmp(&matrix, identityBytes, 64) == 0, "identity() is not identity");
    for (int i = 0; i < 16; ++i) mt::data(matrix)[i] = float(i + 1);
    require(mt::pos(matrix).x == 13 && mt::vy(matrix).z == 7 && mt::at(matrix, 2, 1) == 10, "matrix field layout changed");
    require(mt::vx(matrix).y == 2 && mt::vz(matrix).x == 9 && mt::pos(matrix).z == 15, "matrix axis accessors changed");
    // Row 3 (elements 12..14) is the translation; the accessors alias storage.
    require(mt::at(matrix, 3, 0) == 13 && mt::at(matrix, 3, 1) == 14 && mt::at(matrix, 3, 2) == 15, "translation row moved");
    require(reinterpret_cast<const char *>(&mt::pos(matrix)) - reinterpret_cast<const char *>(&matrix) == 48 &&
            reinterpret_cast<const char *>(&mt::vy(matrix)) - reinterpret_cast<const char *>(&matrix) == 16 &&
            reinterpret_cast<const char *>(&mt::vz(matrix)) - reinterpret_cast<const char *>(&matrix) == 32 &&
            static_cast<const void *>(&mt::vx(matrix)) == static_cast<const void *>(&matrix) &&
            static_cast<const void *>(mt::data(matrix)) == static_cast<const void *>(&matrix),
            "matrix accessors must alias the 16 floats");
    std::array<float, 16> persisted{};
    std::memcpy(persisted.data(), mt::data(matrix), 64);
    for (int i = 0; i < 16; ++i) require(persisted[i] == float(i + 1), "matrix float order changed");
    Mat4 restored;
    std::memcpy(mt::data(restored), persisted.data(), 64);
    require(std::memcmp(mt::data(restored), mt::data(matrix), 64) == 0, "matrix bytes changed");
    auto *d3d = mt::d3dMatrix(matrix);
    require(static_cast<const void *>(d3d) == static_cast<const void *>(&matrix) && d3d->m[3][0] == 13 && d3d->m[1][2] == 7,
            "D3DMATRIX view must be the same bytes");
    // D3D uses the same bytes with row-vector multiplication.
    Vec3 point(2.f, -3.f, 5.f);
    Vec3 expected(
        point.x*d3d->m[0][0] + point.y*d3d->m[1][0] + point.z*d3d->m[2][0] + d3d->m[3][0],
        point.x*d3d->m[0][1] + point.y*d3d->m[1][1] + point.z*d3d->m[2][1] + d3d->m[3][1],
        point.x*d3d->m[0][2] + point.y*d3d->m[1][2] + point.z*d3d->m[2][2] + d3d->m[3][2]);
    vectorNear(mt::transformPoint(matrix, point), expected, "D3D row-vector boundary transposed");
}

// MESSAGE::Send-style callers pass vectors by value through C varargs and the
// receiver (src/libs/core/src/message.cpp) reads them back as this plain
// struct, so the two must be passed identically by the platform ABI.
struct PlainVector { float x, y, z; };
static void collectVarargs(std::vector<PlainVector> &vectors, std::vector<double> &scalars, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    for (; *format; ++format)
    {
        if (*format == 'c') vectors.push_back(va_arg(args, PlainVector));
        else if (*format == 'f') scalars.push_back(va_arg(args, double));
        else if (*format == 'l') scalars.push_back(va_arg(args, int));
        else if (*format == 'p') scalars.push_back(*va_arg(args, const float *));
    }
    va_end(args);
}
static void varargsRoundTrip()
{
    static_assert(sizeof(PlainVector) == sizeof(Vec3) && alignof(PlainVector) == alignof(Vec3));
    static_assert(std::is_trivially_copyable_v<Vec3> && std::is_trivially_destructible_v<Vec3>);
    std::vector<Vec3> sent;
    for (int i = 0; i < 12; ++i) sent.emplace_back(1.5f + i, -2.25f * i, 1.f / (i + 3));
    std::vector<PlainVector> vectors;
    std::vector<double> scalars;
    const float pointee = 42.5f;
    // Enough arguments to exhaust integer and floating registers and spill to
    // the stack, with scalars interleaved to shift the vector slots.
    collectVarargs(vectors, scalars, "clcfccpcflccccfccl", sent[0], 7, sent[1], 3.5, sent[2], sent[3], &pointee, sent[4],
                   -8.25, 9, sent[5], sent[6], sent[7], sent[8], 1e-3, sent[9], sent[10], -11);
    collectVarargs(vectors, scalars, "c", sent[11]);
    require(vectors.size() == sent.size(), "varargs vector count");
    for (std::size_t i = 0; i < sent.size(); ++i)
        require(std::memcmp(&vectors[i], &sent[i], sizeof(PlainVector)) == 0, "vector by value through varargs changed");
    const double expectedScalars[] = {7, 3.5, 42.5, -8.25, 9, 1e-3, -11};
    require(scalars.size() == std::size(expectedScalars), "varargs scalar count");
    for (std::size_t i = 0; i < scalars.size(); ++i)
        require(scalars[i] == expectedScalars[i], "scalars interleaved with vectors through varargs changed");
}
static void geometryContracts()
{
    const float rightAngle = std::acos(-1.f) / 2;
    const Mat4 x = mt::rotationX(rightAngle), y = mt::rotationY(rightAngle), z = mt::rotationZ(rightAngle);
    vectorNear(mt::transformPoint(x, Vec3(0.f,1.f,0.f)), Vec3(0.f,0.f,1.f), "X rotation sign");
    vectorNear(mt::transformPoint(y, Vec3(0.f,0.f,1.f)), Vec3(1.f,0.f,0.f), "Y rotation sign");
    vectorNear(mt::transformPoint(z, Vec3(1.f,0.f,0.f)), Vec3(0.f,1.f,0.f), "Z rotation sign");
    vectorNear(mt::cross(Vec3(1.f,0.f,0.f), Vec3(0.f,1.f,0.f)), Vec3(0.f,0.f,1.f), "cross handedness");
    vectorNear(mt::normalizeOrZero(Vec3(0.f)), Vec3(0.f), "zero vector normalization");
    const auto zeroDouble = mt::normalize(DVec3(0.0));
    require(std::isnan(zeroDouble.x) && std::isnan(zeroDouble.y) && std::isnan(zeroDouble.z), "legacy double zero normalization");
    near(mt::length(Vec3(3.f,12.f,4.f)), 13, "3D length");
    near(mt::lengthXZ(Vec3(3.f,12.f,4.f)), 5, "XZ length");
    near(mt::lengthSquared(Vec3(3.f,12.f,4.f)), 169, "squared length");
    near(mt::dot(Vec3(1.f,2.f,3.f), Vec3(4.f,-5.f,6.f)), 12, "dot product");
    const Mat4 translation = mt::translation(10.f,20.f,30.f), scale = mt::scaling(2.f,3.f,4.f);
    vectorNear(mt::transformPoint(mt::mulRow(scale, translation), Vec3(1.f,2.f,3.f)), Vec3(12.f,26.f,42.f), "composition order");
    vectorNear(mt::transformPoint(mt::mulRow(translation, scale), Vec3(1.f,2.f,3.f)), Vec3(22.f,66.f,132.f), "noncommuting composition");
    Mat4 into;
    mt::mulRowInto(into, scale, translation);
    vectorNear(mt::transformPoint(into, Vec3(1.f,2.f,3.f)), Vec3(12.f,26.f,42.f), "in-place composition order");
    Mat4 view;
    require(mt::viewMatrix(view, Vec3(0.f,0.f,-5.f), Vec3(0.f), Vec3(0.f,1.f,0.f)), "valid LH view");
    vectorNear(mt::transformPoint(view, Vec3(0.f)), Vec3(0.f,0.f,5.f), "view must look down positive Z");
    require(!mt::viewMatrix(view, Vec3(2.f,3.f,4.f), Vec3(2.f,3.f,4.f), Vec3(0.f,1.f,0.f)), "degenerate view result");
    vectorNear(mt::transformPoint(view, Vec3(2.f,3.f,4.f)), Vec3(0.f), "degenerate view translation");
    const Mat4 projection = mt::projectionMatrix(rightAngle, 1600, 900, .5f, 100.f);
    struct Input { Vec3 p; float guard; } points[] = {{Vec3(0.f,0.f,.5f),11.f},{Vec3(0.f,0.f,100.f),22.f}};
    struct Output { mt::ProjPoint p; float guard; } screen[2]{};
    screen[0].guard = 77; screen[1].guard = 88;
    mt::projectPoints(projection, &points[0].p, &screen[0].p, 2, 800, 450, sizeof(Input), sizeof(Output));
    near(screen[0].p.z, 0, "D3D near depth must be zero");
    near(screen[1].p.z, 1, "D3D far depth must be one");
    near(screen[0].p.x, 800, "viewport center X"); near(screen[0].p.y, 450, "viewport center Y");
    near(screen[0].p.rhw, 2, "reciprocal W");
    require(screen[0].guard == 77 && screen[1].guard == 88 && points[1].guard == 22, "strided projection corrupted adjacent fields");
    Vec3 above(0.f, .1f, 1.f); mt::ProjPoint pixel;
    mt::projectPoints(projection, &above, &pixel, 1, 800, 450, sizeof(Vec3), sizeof(pixel));
    require(pixel.y < 450, "screen Y must point down");
    // Storm's historical vertical FOV is scaled linearly. A generic GLM
    // perspective(fovy,aspect) replacement would change existing camera framing.
    near(mt::data(projection)[5], 1.0 / std::tan((900.f/1600.f) * rightAngle * .5f), "legacy FOV convention");
}
static void randomContracts(const char *sampleFile)
{
    std::mt19937 rng(0x53544f52);
    std::uniform_real_distribution<float> values(-10.f,10.f);
    SampleLog log;
    // Draws are sequenced explicitly: argument evaluation order is unspecified.
    auto vector = [&] { const float x = values(rng), y = values(rng), z = values(rng); return Vec3(x, y, z); };
    for (int trial = 0; trial < 3000; ++trial)
    {
        Mat4 a, b, product;
        for (int i=0; i<16; ++i) { mt::data(a)[i]=values(rng); mt::data(b)[i]=values(rng); }
        mt::mulRowInto(product, a, b);
        const Mat4 returned = mt::mulRow(a, b);
        require(std::memcmp(&returned, &product, 64) == 0, "mulRow and mulRowInto disagree");
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
        {
            double expected = 0;
            for (int k=0; k<4; ++k) expected += double(mt::at(a,col,k)) * mt::at(b,k,row);
            near(mt::at(product,col,row), expected, "general 4x4 product", 5e-5);
        }
        log.floats(mt::data(product), 16);
        Mat4 transposed(a); mt::transpose4x4(transposed);
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
            require(mt::at(transposed,col,row) == mt::at(a,row,col), "4x4 transpose");
        transposed = a; mt::transpose3x3(transposed);
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
            require(mt::at(transposed,col,row) == (col<3 && row<3 ? mt::at(a,row,col) : mt::at(a,col,row)), "3x3 transpose changed affine weights");
        Mat4 copied(b); mt::copy3x3(copied, a);
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
            require(mt::at(copied,col,row) == (col<3 && row<3 ? mt::at(a,col,row) : mt::at(b,col,row)), "copy3x3 must copy only the rotation block");
        const Vec3 p = vector(), q = vector();
        log.vec(mt::add(p, q));
        log.vec(mt::sub(p, q));
        log.vec(mt::mul(p, q));
        log.vec(mt::div(p, q));
        log.vec(mt::scale(p, 3.25f));
        log.vec(mt::divScalar(p, 3.25f));
        log.vec(mt::negate(p));
        log.vec(mt::cross(p, q));
        log.vec(mt::normalizeOrZero(p));
        log.vec(mt::transformPoint(a, p));
        log.scalar(mt::dot(p, q));
        log.scalar(mt::lengthSquared(p));
        log.scalar(mt::length(p));
        log.scalar(mt::lengthXZ(p));
        const DVec3 dp(p), dq(q);
        log.vec(mt::add(dp, dq));
        log.vec(mt::sub(dp, dq));
        log.vec(mt::mul(dp, dq));
        log.vec(mt::div(dp, dq));
        log.vec(mt::scale(dp, 3.25));
        log.vec(mt::divScalar(dp, 3.25));
        log.vec(mt::negate(dp));
        log.vec(mt::cross(dp, dq));
        log.vec(mt::normalize(dp));
        log.scalar(mt::dot(dp, dq));
        log.scalar(mt::lengthSquared(dp));
        const float ax = values(rng), ay = values(rng), az = values(rng);
        const Vec3 origin = vector();
        const Mat4 rigid = mt::eulerMatrix(ax, ay, az, origin.x, origin.y, origin.z);
        const Vec3 world = mt::transformPoint(rigid, p);
        log.vec(world);
        const Vec3 local = mt::inverseTransformPoint(rigid, world);
        vectorNear(local,p,"rigid inverse point");
        log.vec(local);
        const Vec3 normal = mt::inverseTransformNormal(rigid, mt::sub(world, mt::pos(rigid)));
        vectorNear(normal,p,"rigid inverse direction");
        log.vec(normal);
        Mat4 inverse(rigid); mt::invertRigid(inverse);
        vectorNear(mt::transformPoint(inverse, world),p,"invertRigid is the rigid inverse");
        log.floats(mt::data(inverse), 16);
        // The general (non-rigid) matrix exercises the inverse helpers' raw arithmetic.
        log.vec(mt::inverseTransformPoint(a, p));
        log.vec(mt::inverseTransformNormal(a, p));
        Mat4 generalInverse(a); mt::invertRigid(generalInverse);
        log.floats(mt::data(generalInverse), 16);
        // Non-affine projection of a strided array; W is kept away from zero.
        Mat4 projective(a);
        mt::at(projective,0,3)=.01f; mt::at(projective,1,3)=.02f; mt::at(projective,2,3)=.03f; mt::at(projective,3,3)=20.f;
        struct Input { Vec3 p; float guard; } input[2] = {{p, 1.f}, {q, 2.f}};
        struct Output { float guard; mt::ProjPoint p; } output[2]{};
        mt::projectPoints(projective, &input[0].p, &output[0].p, 2, 800.f, 450.f, sizeof(Input), sizeof(Output));
        for (const auto &o : output)
        { log.scalar(o.p.x); log.scalar(o.p.y); log.scalar(o.p.z); log.scalar(o.p.rhw); }
        require(output[0].guard == 0 && output[1].guard == 0 && input[0].guard == 1 && input[1].guard == 2, "strided projection guards");
    }
    // Signed zeros: the legacy unary minus flipped the sign bit of a zero.
    log.vec(mt::negate(Vec3(0.f)));
    log.vec(mt::negate(DVec3(0.0)));
    log.write(sampleFile);
}
int main(int argc,char **argv)
{
    layoutAndBytes(); varargsRoundTrip(); geometryContracts(); randomContracts(argc>1 ? argv[1] : nullptr);
    std::cout << "PASS: ABI/serialized bytes, varargs by value, row-column order, orientation, LH depth, strides, 3000 randomized cases ("
              << (mt::kBaseline ? "baseline class API" : "GLM + storm::math") << ")\n";
}
