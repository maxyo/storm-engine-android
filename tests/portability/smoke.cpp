#include "float_to_int.h"
#include "sse.h"
#include <cfenv>
#include <cmath>
#include <iostream>
#include "math3d.h"
#include "math_inlines.h"

int main()
{
    if (std::fesetround(FE_TONEAREST) != 0) return 1;
    if (ftoi(2.5f) != 2 || fftol(-1.75f) != -1 ||
        storm::math::round_to_int(2.5f) != 2 ||
        storm::math::round_to_int(-1.5f) != -2 ||
        storm::math::truncate_to_int(-1.75f) != -1) return 2;
    float x[4] = {3, 0, 0, -3}, y[4] = {4, 3, 0, 0}, z[4] = {0, 4, 5, 4};
    SeaFloat4 vx, vy, vz, nx, ny, nz;
    SSE_GatherFourFloats(x, x+1, x+2, x+3, &vx);
    SSE_GatherFourFloats(y, y+1, y+2, y+3, &vy);
    SSE_GatherFourFloats(z, z+1, z+2, z+3, &vz);
    SSE_Normalise(vx, vy, vz, &nx, &ny, &nz);
    float output[4];
    SSE_ScatterFourFloats(output, output+1, output+2, output+3, nx);
    for (int i = 0; i < 4; ++i)
        if (std::abs(output[i] - x[i] / 5.f) > .0005f) return 3;
    std::cout << "Portable math check passed (not an engine startup test)\n";
}
