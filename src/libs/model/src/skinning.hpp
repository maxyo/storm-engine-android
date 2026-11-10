#pragma once

// CPU skinning kernel for two-bone blended vertices.
//
// For every vertex:
//   M   = bone[id0] * weight + bone[id1] * (1 - weight)      (per element)
//   M.x = -M.x                                               (elements 0, 4, 8, 12)
//   out = (M.col0 * v.x + M.col1 * v.y) + (M.col2 * v.z + M.col3)
// for position and normal alike (the legacy code transforms normals with the
// translation included; that is preserved).
//
// skinScalar() is the reference. skinSimd() runs the same operations, in the
// same order, on four lanes (NEON on arm64, SSE2 on x86-64). Floating-point
// contraction is disabled for both, so each lane performs exactly the IEEE
// multiply/add sequence of the reference and the outputs are bit-identical;
// tests/skinning asserts that on the host and compiles the arm64 kernel.
//
// Layouts mirror GEOS::AVERTEX0 / GEOS::VERTEX0 (asserted in model.cpp).

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define STORM_SKINNING_NEON 1
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define STORM_SKINNING_SSE2 1
#endif

namespace storm::skinning
{
struct Source
{
    float pos[3];
    float weight;
    uint32_t boneid;
    float nrm[3];
    int32_t color;
    float tu0, tv0;
};

struct Target
{
    float pos[3];
    float nrm[3];
    int32_t color;
    float tu, tv;
};

static_assert(sizeof(Source) == 44 && offsetof(Source, nrm) == 20 && offsetof(Source, color) == 32);
static_assert(sizeof(Target) == 36 && offsetof(Target, nrm) == 12 && offsetof(Target, color) == 24);

// `bones` points at the first 4x4 float matrix; consecutive matrices are
// `boneStride` bytes apart (sizeof(CMatrix)).
inline const float *boneMatrix(const void *bones, size_t boneStride, uint32_t index)
{
    return reinterpret_cast<const float *>(static_cast<const unsigned char *>(bones) + boneStride * index);
}

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

inline void skinScalar(const Source *src, Target *dst, const void *bones, size_t boneStride, size_t count)
{
    for (size_t v = 0; v < count; ++v)
    {
        const Source &in = src[v];
        const float *m1 = boneMatrix(bones, boneStride, in.boneid & 0xff);
        const float *m2 = boneMatrix(bones, boneStride, (in.boneid >> 8) & 0xff);
        const float w = in.weight;
        const float wNeg = 1.0f - w;

        float m[16];
        for (int i = 0; i < 16; ++i)
        {
            const float a = m1[i] * w;
            const float b = m2[i] * wNeg;
            m[i] = a + b;
        }
        m[0] = -m[0];
        m[4] = -m[4];
        m[8] = -m[8];
        m[12] = -m[12];

        float pos[3], nrm[3];
        for (int row = 0; row < 3; ++row)
        {
            const float px = m[row] * in.pos[0];
            const float py = m[4 + row] * in.pos[1];
            const float pz = m[8 + row] * in.pos[2];
            const float pxy = px + py;
            const float pzw = pz + m[12 + row];
            pos[row] = pxy + pzw;

            const float nx = m[row] * in.nrm[0];
            const float ny = m[4 + row] * in.nrm[1];
            const float nz = m[8 + row] * in.nrm[2];
            const float nxy = nx + ny;
            const float nzw = nz + m[12 + row];
            nrm[row] = nxy + nzw;
        }

        Target &out = dst[v];
        std::memcpy(out.pos, pos, sizeof(pos));
        std::memcpy(out.nrm, nrm, sizeof(nrm));
        out.color = in.color;
        out.tu = in.tu0;
        out.tv = in.tv0;
    }
}

#if defined(STORM_SKINNING_NEON) || defined(STORM_SKINNING_SSE2)
namespace lanes
{
#if defined(STORM_SKINNING_NEON)
using F4 = float32x4_t;
inline F4 load(const float *p) { return vld1q_f32(p); }
inline void store(float *p, F4 v) { vst1q_f32(p, v); }
inline F4 splat(float x) { return vdupq_n_f32(x); }
inline F4 mul(F4 a, F4 b) { return vmulq_f32(a, b); }
inline F4 add(F4 a, F4 b) { return vaddq_f32(a, b); }
// Flip the sign of lane 0 only (exact negation, including zeros and NaNs).
inline F4 negateFirst(F4 v)
{
    const uint32x4_t mask = {0x80000000u, 0u, 0u, 0u};
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(v), mask));
}
#else
using F4 = __m128;
inline F4 load(const float *p) { return _mm_loadu_ps(p); }
inline void store(float *p, F4 v) { _mm_storeu_ps(p, v); }
inline F4 splat(float x) { return _mm_set1_ps(x); }
inline F4 mul(F4 a, F4 b) { return _mm_mul_ps(a, b); }
inline F4 add(F4 a, F4 b) { return _mm_add_ps(a, b); }
inline F4 negateFirst(F4 v)
{
    return _mm_xor_ps(v, _mm_castsi128_ps(_mm_set_epi32(0, 0, 0, static_cast<int>(0x80000000u))));
}
#endif
} // namespace lanes

inline void skinSimd(const Source *src, Target *dst, const void *bones, size_t boneStride, size_t count)
{
    using namespace lanes;
    for (size_t v = 0; v < count; ++v)
    {
        const Source &in = src[v];
        const float *m1 = boneMatrix(bones, boneStride, in.boneid & 0xff);
        const float *m2 = boneMatrix(bones, boneStride, (in.boneid >> 8) & 0xff);
        const F4 w = splat(in.weight);
        const F4 wNeg = splat(1.0f - in.weight);

        // Each register holds one matrix column: lanes 0..2 feed x, y, z.
        const F4 c0 = negateFirst(add(mul(load(m1), w), mul(load(m2), wNeg)));
        const F4 c1 = negateFirst(add(mul(load(m1 + 4), w), mul(load(m2 + 4), wNeg)));
        const F4 c2 = negateFirst(add(mul(load(m1 + 8), w), mul(load(m2 + 8), wNeg)));
        const F4 c3 = negateFirst(add(mul(load(m1 + 12), w), mul(load(m2 + 12), wNeg)));

        const F4 pos = add(add(mul(c0, splat(in.pos[0])), mul(c1, splat(in.pos[1]))),
                           add(mul(c2, splat(in.pos[2])), c3));
        const F4 nrm = add(add(mul(c0, splat(in.nrm[0])), mul(c1, splat(in.nrm[1]))),
                           add(mul(c2, splat(in.nrm[2])), c3));

        // Read everything still needed from the source before writing: the
        // four-lane stores spill into the next field of the 36-byte target
        // (pos -> nrm.x, nrm -> color) and are then overwritten in order.
        const int32_t color = in.color;
        const float tu = in.tu0, tv = in.tv0;
        Target &out = dst[v];
        store(out.pos, pos);
        store(out.nrm, nrm);
        out.color = color;
        out.tu = tu;
        out.tv = tv;
    }
}
#endif

#if defined(__clang__)
#pragma clang fp contract(on)
#elif defined(__GNUC__)
#pragma GCC pop_options
#elif defined(_MSC_VER)
#pragma fp_contract(on)
#endif

inline void skin(const Source *src, Target *dst, const void *bones, size_t boneStride, size_t count)
{
#if defined(STORM_SKINNING_NEON) || defined(STORM_SKINNING_SSE2)
    skinSimd(src, dst, bones, boneStride, count);
#else
    skinScalar(src, dst, bones, boneStride, count);
#endif
}
} // namespace storm::skinning
