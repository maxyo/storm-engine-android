#pragma once

#include "float_to_int.h"

#if STORM_MATH_HAS_SSE
using SeaFloat4 = __m128;
inline SeaFloat4 SeaSqrt(SeaFloat4 value) { return _mm_sqrt_ps(value); }
inline SeaFloat4 SeaAdd(SeaFloat4 a, SeaFloat4 b) { return _mm_add_ps(a, b); }

// INTEL COMMENT:
// This function simply gathers 4 floats and places them on a __m128 variable.
static inline void SSE_GatherFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 *pm128Result)
{
    __m128 xmm0 = _mm_load_ss(pf0); // 0 0 0 pf0
    __m128 xmm1 = _mm_load_ss(pf1); // 0 0 0 pf1
    __m128 xmm2 = _mm_load_ss(pf2); // 0 0 0 pf2
    __m128 xmm3 = _mm_load_ss(pf3); // 0 0 0 pf3

    xmm0 = _mm_movelh_ps(xmm0, xmm2);                           // 0 pf2 0 pf0
    xmm1 = _mm_shuffle_ps(xmm1, xmm3, _MM_SHUFFLE(0, 1, 0, 1)); // pf3 0 pf1 0

    *pm128Result = _mm_or_ps(xmm0, xmm1); // pf3 pf2 pf1 pf0
}

// INTEL COMMENT:
// This function simply scatters the contents of a __m128 variable on to 4 floats.
static inline void SSE_ScatterFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 m128Value)
{
    __m128 xmm1 = _mm_shuffle_ps(m128Value, m128Value, _MM_SHUFFLE(3, 2, 1, 1)); // 3 2 1 1
    __m128 xmm2 = _mm_movehl_ps(_mm_setzero_ps(), m128Value);                    // 0 0 3 2
    __m128 xmm3 = _mm_shuffle_ps(m128Value, m128Value, _MM_SHUFFLE(3, 2, 1, 3)); // 3 2 1 3

    _mm_store_ss(pf0, m128Value); // 3 2 1 0
    _mm_store_ss(pf1, xmm1);      // 3 2 1 1
    _mm_store_ss(pf2, xmm2);      // 0 0 3 2
    _mm_store_ss(pf3, xmm3);      // 3 2 1 3
}

// INTEL COMMENT:
// This function performs the maths of a normalise using SSE instructions - 4 normalises in 9 instructions - very fast.
static inline void SSE_Normalise(__m128 m128X, __m128 m128Y, __m128 m128Z, __m128 *pm128X, __m128 *pm128Y,
                                 __m128 *pm128Z)
{
    __m128 xmm0 = _mm_mul_ps(m128X, m128X); // X * X
    __m128 xmm1 = _mm_mul_ps(m128Y, m128Y); // Y * Y
    __m128 xmm2 = _mm_mul_ps(m128Z, m128Z); // Z * Z

    xmm0 = _mm_add_ps(xmm0, xmm1); // X * X + Y * Y
    xmm1 = _mm_add_ps(xmm0, xmm2); // X * X + Y * Y + Z * Z

    xmm0 = _mm_rsqrt_ps(xmm1); // 1 / sqrt( X * X + Y * Y + Z * Z )

    *pm128X = _mm_mul_ps(m128X, xmm0); // RecipLength * X
    *pm128Y = _mm_mul_ps(m128Y, xmm0); // RecipLength * Y
    *pm128Z = _mm_mul_ps(m128Z, xmm0); // RecipLength * Z
}

static inline void GatherFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 *pm128Result)
{
    __m128 xmm0 = _mm_load_ss(pf0); // 0 0 0 pf0
    __m128 xmm1 = _mm_load_ss(pf1); // 0 0 0 pf1
    __m128 xmm2 = _mm_load_ss(pf2); // 0 0 0 pf2
    __m128 xmm3 = _mm_load_ss(pf3); // 0 0 0 pf3

    xmm0 = _mm_movelh_ps(xmm0, xmm2);                           // 0 pf2 0 pf0
    xmm1 = _mm_shuffle_ps(xmm1, xmm3, _MM_SHUFFLE(0, 1, 0, 1)); // pf3 0 pf1 0

    *pm128Result = _mm_or_ps(xmm0, xmm1); // pf3 pf2 pf1 pf0
}

static inline void ScatterFourFloats(float *pf0, float *pf1, float *pf2, float *pf3, __m128 m128Value)
{
    __m128 xmm1 = _mm_shuffle_ps(m128Value, m128Value, _MM_SHUFFLE(3, 2, 1, 1)); // 3 2 1 1
    __m128 xmm2 = _mm_movehl_ps(_mm_setzero_ps(), m128Value);                    // 0 0 3 2
    __m128 xmm3 = _mm_shuffle_ps(m128Value, m128Value, _MM_SHUFFLE(3, 2, 1, 3)); // 3 2 1 3

    _mm_store_ss(pf0, m128Value); // 3 2 1 0
    _mm_store_ss(pf1, xmm1);      // 3 2 1 1
    _mm_store_ss(pf2, xmm2);      // 0 0 3 2
    _mm_store_ss(pf3, xmm3);      // 3 2 1 3
}

static inline void Normalise(__m128 m128X, __m128 m128Y, __m128 m128Z, __m128 *pm128X, __m128 *pm128Y, __m128 *pm128Z)
{
    __m128 xmm0 = _mm_mul_ps(m128X, m128X); // X * X
    __m128 xmm1 = _mm_mul_ps(m128Y, m128Y); // Y * Y
    __m128 xmm2 = _mm_mul_ps(m128Z, m128Z); // Z * Z

    xmm0 = _mm_add_ps(xmm0, xmm1); // X * X + Y * Y
    xmm1 = _mm_add_ps(xmm0, xmm2); // X * X + Y * Y + Z * Z

    xmm0 = _mm_rsqrt_ps(xmm1); // 1 / sqrt( X * X + Y * Y + Z * Z )

    *pm128X = _mm_mul_ps(m128X, xmm0); // RecipLength * X
    *pm128Y = _mm_mul_ps(m128Y, xmm0); // RecipLength * Y
    *pm128Z = _mm_mul_ps(m128Z, xmm0); // RecipLength * Z
}

#else
// Portable implementation of the same four independent wave-normal lanes.
// This is real scalar math; a NEON backend can be added after device profiling.
struct SeaFloat4 { float lane[4]; };
inline SeaFloat4 SeaSqrt(SeaFloat4 value)
{
    for (auto &lane : value.lane) lane = std::sqrt(lane);
    return value;
}
inline SeaFloat4 SeaAdd(SeaFloat4 a, SeaFloat4 b)
{
    for (int i = 0; i < 4; ++i) a.lane[i] += b.lane[i];
    return a;
}
inline void SSE_GatherFourFloats(float *a, float *b, float *c, float *d, SeaFloat4 *out)
{
    *out = {{*a, *b, *c, *d}};
}
inline void SSE_ScatterFourFloats(float *a, float *b, float *c, float *d, SeaFloat4 value)
{
    *a = value.lane[0]; *b = value.lane[1];
    *c = value.lane[2]; *d = value.lane[3];
}
inline void SSE_Normalise(SeaFloat4 x, SeaFloat4 y, SeaFloat4 z,
                          SeaFloat4 *nx, SeaFloat4 *ny, SeaFloat4 *nz)
{
    for (int i = 0; i < 4; ++i)
    {
        const float lengthSquared = (x.lane[i] * x.lane[i] + y.lane[i] * y.lane[i]) + z.lane[i] * z.lane[i];
        const float reciprocal = 1.0f / std::sqrt(lengthSquared);
        nx->lane[i] = x.lane[i] * reciprocal;
        ny->lane[i] = y.lane[i] * reciprocal;
        nz->lane[i] = z.lane[i] * reciprocal;
    }
}
#endif
