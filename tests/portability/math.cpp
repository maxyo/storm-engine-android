#include "float_to_int.h"
#include "sse.h"
#include <xmmintrin.h> // x86 reference oracle; this test is intentionally host-only
#include <cfenv>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include "math3d.h"
#include "math_inlines.h"

static void require(bool ok, const char *message)
{
    if (!ok) throw std::runtime_error(message);
}
static void check_conversion(float value)
{
    volatile float input = value;
    const auto vector = _mm_load_ss(const_cast<const float *>(&input));
    require(storm::math::round_to_int(input) == _mm_cvtss_si32(vector), "round differs from SSE");
    require(storm::math::truncate_to_int(input) == _mm_cvttss_si32(vector), "truncate differs from SSE");
    require(ftoi(input) == _mm_cvtss_si32(vector), "ftoi wrapper");
    require(fftoi(input) == _mm_cvtss_si32(vector), "fftoi wrapper");
    require(fftol(input) == _mm_cvttss_si32(vector), "fftol wrapper");
    require(ffloor(input) == _mm_cvtss_si32(_mm_add_ss(vector, _mm_set_ss(-.5f))), "legacy ffloor wrapper");
    require(fceil(input) == _mm_cvtss_si32(_mm_add_ss(vector, _mm_set_ss(.5f))), "legacy fceil wrapper");
}
int main()
{
    std::mt19937 random(0x53544f52);
    const int original = std::fegetround();
    for (int rounding : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    {
        require(std::fesetround(rounding) == 0, "rounding mode unavailable");
        for (float v : {0.f, -0.f, .5f, -.5f, 1.5f, -1.5f, 2.5f, -2.5f,
                        2147483648.f, -2147483648.f, 2147483520.f,
                        INFINITY, -INFINITY, NAN}) check_conversion(v);
        for (int i = 0; i < 100000; ++i)
        {
            const uint32_t bits = random();
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            check_conversion(value);
        }
    }
    std::fesetround(FE_TONEAREST);
    std::uniform_real_distribution<float> distribution(-100.f, 100.f);
    for (int iteration = 0; iteration < 10000; ++iteration)
    {
        float x[4], y[4], z[4], output[3][4];
        for (int i = 0; i < 4; ++i) { x[i] = distribution(random); y[i] = distribution(random); z[i] = distribution(random); }
        SeaFloat4 vx, vy, vz, nx, ny, nz;
        SSE_GatherFourFloats(x, x+1, x+2, x+3, &vx);
        SSE_GatherFourFloats(y, y+1, y+2, y+3, &vy);
        SSE_GatherFourFloats(z, z+1, z+2, z+3, &vz);
        SSE_Normalise(vx, vy, vz, &nx, &ny, &nz);
        SSE_ScatterFourFloats(output[0], output[0]+1, output[0]+2, output[0]+3, nx);
        SSE_ScatterFourFloats(output[1], output[1]+1, output[1]+2, output[1]+3, ny);
        SSE_ScatterFourFloats(output[2], output[2]+1, output[2]+2, output[2]+3, nz);
        for (int i = 0; i < 4; ++i)
        {
            const double length = std::sqrt(double(x[i])*x[i] + double(y[i])*y[i] + double(z[i])*z[i]);
            // SSE reciprocal square root is approximate. Scalar output is more
            // accurate, so parity is numeric rather than bit-for-bit here.
            require(std::abs(output[0][i] - x[i]/length) < .0005, "normal x");
            require(std::abs(output[1][i] - y[i]/length) < .0005, "normal y");
            require(std::abs(output[2][i] - z[i]/length) < .0005, "normal z");
        }
        const auto sum = SeaAdd(vx, vz);
        const auto root = SeaSqrt(SeaAdd(sum, sum));
        SSE_ScatterFourFloats(output[0], output[0]+1, output[0]+2, output[0]+3, root);
        for (int i = 0; i < 4; ++i)
        {
            float expected = std::sqrt(2.f * (x[i] + z[i]));
            require(output[0][i] == expected || (std::isnan(output[0][i]) && std::isnan(expected)), "sqrt/add lanes");
        }
    }
    std::fesetround(original);
    std::cout << "PASS: 400056 float conversions; 40000 normal lanes; SSE=" << STORM_MATH_HAS_SSE << '\n';
}
