// Deliberately the first include: geometry headers include this under pack(1).
#pragma pack(push, 1)
#include "c_vector.h"
struct PackedVectors
{
    char prefix;
    CVECTOR f;
    DVECTOR d;
};
struct PackingSentinel { char prefix; double value; };
#pragma pack(pop)

#include <cstdio>
#include <cstdlib>
#include <cstring>

static_assert(alignof(CVECTOR) == 1 && sizeof(CVECTOR) == 12);
static_assert(alignof(DVECTOR) == 1 && sizeof(DVECTOR) == 24);
static_assert(offsetof(PackedVectors, f) == 1 && offsetof(PackedVectors, d) == 13);
static_assert(alignof(PackingSentinel) == 1 && sizeof(PackingSentinel) == 9);
static_assert(alignof(storm::math::GlmVec3) == alignof(float));
static_assert(alignof(storm::math::GlmDVec3) == alignof(double));
static_assert(alignof(storm::math::GlmMat4) == alignof(float));
static_assert(alignof(std::max_align_t) >= alignof(long double));

void check(bool success)
{
    if (!success) std::abort();
}
int main()
{
    // Explicit alignment ensures the packed members really have odd addresses.
    alignas(16) PackedVectors packed{};
    packed.f = CVECTOR(3.0f, 4.0f, 12.0f);
    packed.d = DVECTOR(2.0, 3.0, 6.0);
    check(packed.f.GetLength() == 13.0f);
    check(std::abs(packed.f.GetLength2D() - std::sqrt(153.0f)) < 1e-5f);
    check((packed.f | packed.f) == 169.0f);
    check((packed.d | packed.d) == 49.0);
    auto fn = !packed.f;
    auto dn = !packed.d;
    check(std::abs((fn | fn) - 1.0f) < 1e-6f);
    check(std::abs((dn | dn) - 1.0) < 1e-12);
    packed.f += CVECTOR(1.0f, 2.0f, 3.0f);
    packed.d *= DVECTOR(2.0, 3.0, 4.0);
    check(packed.f.x == 4 && packed.f.y == 6 && packed.f.z == 15);
    check(packed.d.x == 4 && packed.d.y == 9 && packed.d.z == 24);
    auto fc = packed.f ^ CVECTOR(1.0f, 0.0f, 0.0f);
    auto dc = packed.d ^ DVECTOR(1.0, 0.0, 0.0);
    check(fc.x == 0 && fc.y == 15 && fc.z == -6);
    check(dc.x == 0 && dc.y == 24 && dc.z == -9);
    // The matrix bridge also accepts unaligned serialized storage.
    alignas(16) unsigned char bytes[66]{};
    float fixture[16];
    for (int i = 0; i < 16; ++i) fixture[i] = float(i + 1);
    std::memcpy(bytes + 1, fixture, sizeof(fixture));
    auto matrix = storm::math::loadGlmMatrix(bytes + 1);
    check(matrix[2][3] == 12.0f);
    matrix[2][3] = 42.0f;
    storm::math::storeGlmMatrix(bytes + 1, matrix);
    std::memcpy(fixture, bytes + 1, sizeof(fixture));
    check(fixture[11] == 42.0f && bytes[0] == 0 && bytes[65] == 0);
    std::puts("PASS: first include under pack(1), packing restoration and odd-address math");
}
