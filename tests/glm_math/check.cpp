#include "matrix.h"
#include "math3d/quaternion.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

static void require(bool condition, const char *reason)
{
    if (!condition) throw std::runtime_error(reason);
}
static void near(double a, double b, const char *reason, double tolerance = 3e-5)
{
    require(std::abs(a - b) <= tolerance * (1.0 + std::abs(b)), reason);
}
static void vectorNear(const CVECTOR &a, const CVECTOR &b, const char *reason)
{
    near(a.x, b.x, reason); near(a.y, b.y, reason); near(a.z, b.z, reason);
}
static void layoutAndBytes()
{
    static_assert(sizeof(CVECTOR) == 12 && alignof(CVECTOR) == 4);
    static_assert(sizeof(DVECTOR) == 24 && alignof(DVECTOR) == alignof(double));
    static_assert(sizeof(CMatrix) == 64 && alignof(CMatrix) == 16);
    static_assert(sizeof(Vector) == 12 && alignof(Vector) == 1);
    static_assert(sizeof(Matrix) == 64 && alignof(Matrix) == 1);
    static_assert(sizeof(Quaternion) == 16 && alignof(Quaternion) == 1);
    static_assert(offsetof(CVECTOR, y) == 4 && offsetof(CVECTOR, z) == 8);
    static_assert(std::is_trivially_copyable_v<CVECTOR> && std::is_trivially_copyable_v<DVECTOR>);
    require(std::endian::native == std::endian::little, "legacy binary fixtures require little endian");
    // The exact byte sequence consumed by existing SaveVector and animation I/O.
    const unsigned char fixture[] = {0,0,128,63, 0,0,0,192, 0,0,0,63};
    CVECTOR saved(1.f, -2.f, .5f), loaded;
    require(std::memcmp(&saved, fixture, 12) == 0, "SaveVector bytes changed");
    std::memcpy(&loaded, fixture, 12);
    vectorNear(loaded, saved, "read historical vector bytes");
    CVECTOR stride[] = {saved, CVECTOR(2.f, 4.f, 6.f)};
    require(reinterpret_cast<char *>(&stride[1]) - reinterpret_cast<char *>(&stride[0]) == 12, "vertex stride changed");
    CMatrix matrix;
    for (int i = 0; i < 16; ++i) matrix.matrix[i] = float(i + 1);
    require(matrix.Pos().x == 13 && matrix.Vy().z == 7 && matrix.m[2][1] == 10, "matrix field layout changed");
    std::array<float, 16> persisted{};
    std::memcpy(persisted.data(), matrix.matrix, 64);
    CMatrix restored;
    std::memcpy(restored.matrix, persisted.data(), 64);
    require(std::memcmp(restored.matrix, matrix.matrix, 64) == 0, "matrix bytes changed");
    auto *d3d = static_cast<D3DMATRIX *>(matrix);
    // D3D uses the same bytes with row-vector multiplication.
    CVECTOR point(2.f, -3.f, 5.f);
    CVECTOR expected(
        point.x*d3d->m[0][0] + point.y*d3d->m[1][0] + point.z*d3d->m[2][0] + d3d->m[3][0],
        point.x*d3d->m[0][1] + point.y*d3d->m[1][1] + point.z*d3d->m[2][1] + d3d->m[3][1],
        point.x*d3d->m[0][2] + point.y*d3d->m[1][2] + point.z*d3d->m[2][2] + d3d->m[3][2]);
    vectorNear(matrix * point, expected, "D3D row-vector boundary transposed");
}
static void geometryContracts()
{
    const float rightAngle = std::acos(-1.f) / 2;
    CMatrix x, y, z;
    x.BuildRotateX(rightAngle); y.BuildRotateY(rightAngle); z.BuildRotateZ(rightAngle);
    vectorNear(x * CVECTOR(0.f,1.f,0.f), CVECTOR(0.f,0.f,1.f), "X rotation sign");
    vectorNear(y * CVECTOR(0.f,0.f,1.f), CVECTOR(1.f,0.f,0.f), "Y rotation sign");
    vectorNear(z * CVECTOR(1.f,0.f,0.f), CVECTOR(0.f,1.f,0.f), "Z rotation sign");
    vectorNear(CVECTOR(1.f,0.f,0.f) ^ CVECTOR(0.f,1.f,0.f), CVECTOR(0.f,0.f,1.f), "cross handedness");
    vectorNear(!CVECTOR(0.f), CVECTOR(0.f), "zero CVECTOR normalization");
    const auto zeroDouble = !DVECTOR(0.0);
    require(std::isnan(zeroDouble.x) && std::isnan(zeroDouble.y) && std::isnan(zeroDouble.z), "legacy double zero normalization");
    near(CVECTOR(3.f,12.f,4.f).GetLength(), 13, "3D length");
    near(CVECTOR(3.f,12.f,4.f).GetLength2D(), 5, "XZ length");
    CMatrix translation, scale;
    translation.BuildPosition(10.f,20.f,30.f); scale.BuildScale(2.f,3.f,4.f);
    vectorNear((scale * translation) * CVECTOR(1.f,2.f,3.f), CVECTOR(12.f,26.f,42.f), "composition order");
    vectorNear((translation * scale) * CVECTOR(1.f,2.f,3.f), CVECTOR(22.f,66.f,132.f), "noncommuting composition");
    CMatrix view;
    require(view.BuildViewMatrix(CVECTOR(0.f,0.f,-5.f), CVECTOR(0.f), CVECTOR(0.f,1.f,0.f)), "valid LH view");
    vectorNear(view * CVECTOR(0.f), CVECTOR(0.f,0.f,5.f), "view must look down positive Z");
    require(!view.BuildViewMatrix(CVECTOR(2.f,3.f,4.f), CVECTOR(2.f,3.f,4.f), CVECTOR(0.f,1.f,0.f)), "degenerate view result");
    vectorNear(view * CVECTOR(2.f,3.f,4.f), CVECTOR(0.f), "degenerate view translation");
    CMatrix projection;
    projection.BuildProjectionMatrix(rightAngle, 1600, 900, .5f, 100.f);
    struct Input { CVECTOR p; float guard; } points[] = {{CVECTOR(0.f,0.f,.5f),11.f},{CVECTOR(0.f,0.f,100.f),22.f}};
    struct Output { MTX_PRJ_VECTOR p; float guard; } screen[2]{};
    screen[0].guard = 77; screen[1].guard = 88;
    projection.Projection(&points[0].p, &screen[0].p, 2, 800, 450, sizeof(Input), sizeof(Output));
    near(screen[0].p.z, 0, "D3D near depth must be zero");
    near(screen[1].p.z, 1, "D3D far depth must be one");
    near(screen[0].p.x, 800, "viewport center X"); near(screen[0].p.y, 450, "viewport center Y");
    near(screen[0].p.rhw, 2, "reciprocal W");
    require(screen[0].guard == 77 && screen[1].guard == 88 && points[1].guard == 22, "strided projection corrupted adjacent fields");
    CVECTOR above(0.f, .1f, 1.f); MTX_PRJ_VECTOR pixel;
    projection.Projection(&above, &pixel, 1, 800, 450, sizeof(CVECTOR), sizeof(pixel));
    require(pixel.y < 450, "screen Y must point down");
    // Storm's historical vertical FOV is scaled linearly. A generic GLM
    // perspective(fovy,aspect) replacement would change existing camera framing.
    near(projection.matrix[5], 1.0 / std::tan((900.f/1600.f) * rightAngle * .5f), "legacy FOV convention");
}
static void randomContracts(const char *sampleFile)
{
    std::mt19937 rng(0x53544f52);
    std::uniform_real_distribution<float> values(-10.f,10.f);
    std::vector<double> samples;
    for (int trial = 0; trial < 3000; ++trial)
    {
        CMatrix a, b, product;
        for (int i=0; i<16; ++i) { a.matrix[i]=values(rng); b.matrix[i]=values(rng); }
        product.EqMultiply(a,b);
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
        {
            double expected = 0;
            for (int k=0; k<4; ++k) expected += double(a.m[col][k]) * b.m[k][row];
            near(product.m[col][row], expected, "general 4x4 product", 5e-5);
            samples.push_back(product.m[col][row]);
        }
        CMatrix transposed(a); transposed.Transposition4x4();
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
            require(transposed.m[col][row] == a.m[row][col], "4x4 transpose");
        transposed = a; transposed.Transposition3X3();
        for (int col=0; col<4; ++col) for (int row=0; row<4; ++row)
            require(transposed.m[col][row] == (col<3 && row<3 ? a.m[row][col] : a.m[col][row]), "3x3 transpose changed affine weights");
        CVECTOR p(values(rng),values(rng),values(rng)), q(values(rng),values(rng),values(rng));
        for (auto v : {p+q,p-q,p*q,p/q,p*3.25f,p/3.25f,p^q,!p,a*p})
        { samples.push_back(v.x); samples.push_back(v.y); samples.push_back(v.z); }
        samples.push_back(p|q); samples.push_back(~p); samples.push_back(p.GetLength()); samples.push_back(p.GetLength2D());
        const DVECTOR dp(p), dq(q);
        for (auto v : {dp+dq,dp-dq,dp*dq,dp/dq,dp*3.25,dp/3.25,dp^dq,!dp})
        { samples.push_back(v.x); samples.push_back(v.y); samples.push_back(v.z); }
        samples.push_back(dp|dq); samples.push_back(~dp);
        CMatrix rigid(values(rng),values(rng),values(rng),values(rng),values(rng),values(rng));
        CVECTOR local, normal;
        rigid.MulToInv(rigid*p,local);
        vectorNear(local,p,"rigid inverse point");
        rigid.MulToInvNorm(rigid*p-rigid.Pos(),normal);
        vectorNear(normal,p,"rigid inverse direction");
        samples.push_back(local.x); samples.push_back(local.y); samples.push_back(local.z);
        CMatrix inverse(rigid); inverse.Transposition();
        vectorNear(inverse*(rigid*p),p,"legacy Transposition is rigid inverse");
    }
    if(sampleFile)
    {
        std::ofstream out(sampleFile,std::ios::binary);
        out.write(reinterpret_cast<const char *>(samples.data()),samples.size()*sizeof(double));
        out.close(); require(!out.fail(),"write differential samples");
    }
}
int main(int argc,char **argv)
{
    layoutAndBytes(); geometryContracts(); randomContracts(argc>1 ? argv[1] : nullptr);
    std::cout << "PASS: ABI/serialized bytes, row-column order, orientation, LH depth, strides, 3000 randomized cases\n";
}
