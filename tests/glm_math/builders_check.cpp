#ifdef GLM_ENABLE_EXPERIMENTAL
#define STORM_TEST_CALLER_EXPERIMENTAL
#endif
#include "matrix.h"
#if defined(GLM_ENABLE_EXPERIMENTAL) && !defined(STORM_TEST_CALLER_EXPERIMENTAL)
#error "The math boundary leaked GLM experimental opt-in into the caller"
#endif
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char *reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}
static void near(double actual, double expected, const char *reason)
{
    require(std::abs(actual - expected) <= 5e-5 * (1 + std::abs(expected)), reason);
}
static void vectorNear(const CVECTOR &actual, const CVECTOR &expected, const char *reason)
{
    near(actual.x, expected.x, reason);
    near(actual.y, expected.y, reason);
    near(actual.z, expected.z, reason);
}
static void matrixNear(const CMatrix &actual, const CMatrix &expected, const char *reason)
{
    for (int i = 0; i < 16; ++i)
        near(actual.matrix[i], expected.matrix[i], reason);
}
static void contracts()
{
    const float x = .37f, y = -.61f, z = .83f;
    CMatrix rx, ry, rz, built;
    rx.BuildRotateX(x);
    ry.BuildRotateY(y);
    rz.BuildRotateZ(z);
    built.BuildMatrix(x, y, z);
    matrixNear(built, (rz * rx) * ry, "legacy ZXY Euler composition");
    built.BuildMatrixXYZ(x, y, z, 0, 0, 0);
    matrixNear(built, (rx * ry) * rz, "legacy XYZ Euler composition");
    built.BuildMatrix(x, y, z, 7, 8, 9);
    vectorNear(built * CVECTOR(0.f), CVECTOR(7.f, 8.f, 9.f), "Euler position survives rotation");

    CMatrix mirror;
    mirror.BuildMirrorMatrix(0, 1, 0, 2);
    require(std::signbit(mirror.m[1][0]), "mirror retains negative-zero off-diagonal");
    vectorNear(mirror * CVECTOR(1.f, 5.f, 3.f), CVECTOR(1.f, -1.f, 3.f), "plane D convention");
    vectorNear(mirror * CVECTOR(7.f, 2.f, -4.f), CVECTOR(7.f, 2.f, -4.f), "points on mirror plane stay fixed");
    matrixNear(mirror * mirror, CMatrix(), "normalized reflection is involutory");
    mirror.BuildMirrorMatrix(0, 2, 0, 3);
    vectorNear(mirror * CVECTOR(1.f, 5.f, 3.f), CVECTOR(1.f, -23.f, 3.f), "nonunit normal must not be normalized");

    CMatrix view;
    require(!view.BuildViewMatrix(CVECTOR(2.f, 3.f, 4.f), CVECTOR(2.f, 3.f, 4.f), CVECTOR(0.f)), "coincident eye/target status");
    vectorNear(view * CVECTOR(2.f, 3.f, 4.f), CVECTOR(0.f), "coincident eye/target translation");
    require(view.BuildViewMatrix(CVECTOR(0.f), CVECTOR(0.f, 1.f, 0.f), CVECTOR(0.f, 1.f, 0.f)), "parallel up remains successful");
    matrixNear(view, CMatrix(), "parallel vertical up retains identity basis");
    require(view.BuildViewMatrix(CVECTOR(0.f, 0.f, -5.f), CVECTOR(0.f), CVECTOR(0.f)), "zero up fallback status");
    vectorNear(view * CVECTOR(0.f), CVECTOR(0.f, 0.f, 5.f), "zero up fallback looks along positive Z");

    CMatrix inversePosition(x, y, z);
    inversePosition.SetInversePosition(7, 8, 9);
    vectorNear(inversePosition * CVECTOR(7.f, 8.f, 9.f), CVECTOR(0.f), "inverse position cancels basis transform");
    CMatrix shifted(x, y, z, 3, 4, 5);
    shifted.MoveInversePosition(7, 8, 9);
    vectorNear(shifted * CVECTOR(7.f, 8.f, 9.f), CVECTOR(3.f, 4.f, 5.f), "inverse move retains previous translation");

    CMatrix projection;
    projection.BuildProjectionMatrix(1.2f, 1920, 1080, .25f, 250.f);
    near(projection.matrix[0], 1 / std::tan(1.2f * .5f), "legacy horizontal FOV");
    near(projection.matrix[5], 1 / std::tan((1080.f / 1920.f) * 1.2f * .5f), "linear vertical FOV policy");
    CVECTOR points[] = {CVECTOR(0.f, 0.f, .25f), CVECTOR(0.f, 0.f, 250.f)};
    MTX_PRJ_VECTOR output[2];
    projection.Projection(points, output, 2, 960, 540, sizeof(CVECTOR), sizeof(MTX_PRJ_VECTOR));
    near(output[0].z, 0, "LH D3D near depth zero");
    near(output[1].z, 1, "LH D3D far depth one");
    projection.BuildProjectionMatrix(1.2f, 1920, 1080, 1, 1);
    require(std::isinf(projection.matrix[10]) && std::isinf(projection.matrix[14]), "equal depth planes retain infinities");
    require(projection.matrix[3] == 0 && projection.matrix[11] == 1 && projection.matrix[15] == 0, "singular projection fixed fields");
}
static void differential(const char *sampleFile)
{
    std::mt19937 random(0x4255494c);
    std::uniform_real_distribution<float> angle(-30.f, 30.f), coordinate(-100.f, 100.f);
    std::vector<double> samples;
    auto record = [&](const CMatrix &matrix) {
        for (int i = 0; i < 16; ++i)
            samples.push_back(matrix.matrix[i]);
    };
    auto point = [&] {
        const float x = coordinate(random), y = coordinate(random), z = coordinate(random);
        return CVECTOR(x, y, z);
    };
    for (int i = 0; i < 4000; ++i)
    {
        const float ax = angle(random), ay = angle(random), az = angle(random);
        const auto position = point();
        CMatrix matrix;
        matrix.BuildMatrix(ax, ay, az, position.x, position.y, position.z);
        record(matrix);
        matrix.BuildMatrixXYZ(ax, ay, az, position.x, position.y, position.z);
        record(matrix);
        matrix.BuildRotateX(ax); record(matrix);
        matrix.BuildRotateY(ay); record(matrix);
        matrix.BuildRotateZ(az); record(matrix);
        matrix.SetInversePosition(position.x, position.y, position.z); record(matrix);
        matrix.MoveInversePosition(position.z, position.x, position.y); record(matrix);
        const auto normal = point();
        matrix.BuildMirrorMatrix(normal.x, normal.y, normal.z, coordinate(random)); record(matrix);
        const auto target = point(), up = point();
        samples.push_back(matrix.BuildViewMatrix(position, target, up)); record(matrix);
        // Include random non-affine bases for the generic inverse-position helpers.
        for (int j = 0; j < 16; ++j)
            matrix.matrix[j] = coordinate(random);
        matrix.SetInversePosition(position.x, position.y, position.z); record(matrix);
        matrix.MoveInversePosition(position.z, position.x, position.y); record(matrix);
        const float nearPlane = .1f + .01f * std::abs(coordinate(random));
        const float farPlane = nearPlane + .5f + 100 * std::abs(coordinate(random));
        matrix.BuildProjectionMatrix(.1f + .05f * std::abs(angle(random)),
                                     100 + std::abs(coordinate(random)),
                                     100 + std::abs(coordinate(random)), nearPlane, farPlane);
        record(matrix);
    }
    // Degenerate, parallel, near-parallel, huge and non-finite inputs compare
    // against the archived implementation, including its flags/NaN/Inf policy.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (CVECTOR from : {CVECTOR(0.f), CVECTOR(1.f, 2.f, 3.f)})
    {
        for (CVECTOR direction : {CVECTOR(0.f), CVECTOR(0.f, 1.f, 0.f), CVECTOR(1e-12f, 1.f, 0.f),
                                  CVECTOR(0.f, 0.f, 1.f), CVECTOR(1e20f), CVECTOR(nan)})
        {
            for (CVECTOR up : {CVECTOR(0.f), CVECTOR(0.f, 1.f, 0.f), CVECTOR(0.f, 0.f, 1.f), CVECTOR(1e-12f, 1.f, 0.f)})
            {
                CMatrix view;
                samples.push_back(view.BuildViewMatrix(from, from + direction, up)); record(view);
            }
        }
    }
    for (float angle : {0.f, 1e-20f, 1e10f, inf, nan})
    {
        CMatrix matrix;
        matrix.BuildRotateX(angle); record(matrix);
        matrix.BuildRotateY(angle); record(matrix);
        matrix.BuildRotateZ(angle); record(matrix);
        matrix.BuildMatrix(angle, .2f, -.3f, 4, 5, 6); record(matrix);
        matrix.BuildMatrixXYZ(angle, .2f, -.3f, 4, 5, 6); record(matrix);
    }
    for (float near : {0.f, 1.f, -1.f, inf, nan})
    {
        for (float far : {0.f, 1.f, -1.f, inf, nan})
        {
            CMatrix matrix;
            matrix.BuildProjectionMatrix(1.2f, 1920, 1080, near, far); record(matrix);
        }
    }
    if (sampleFile)
    {
        std::ofstream out(sampleFile, std::ios::binary);
        out.write(reinterpret_cast<const char *>(samples.data()), samples.size() * sizeof(double));
        out.close();
        require(!out.fail(), "write builder differential samples");
    }
}
int main(int argc, char **argv)
{
    contracts();
    differential(argc > 1 ? argv[1] : nullptr);
    std::cout << "PASS: Euler order, view degeneracy, mirror plane, legacy FOV/LH depth and randomized builders\n";
}
