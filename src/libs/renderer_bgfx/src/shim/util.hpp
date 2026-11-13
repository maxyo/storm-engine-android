#pragma once
#include "shim.hpp"
#include <bx/math.h>
#include <algorithm>
#include <cmath>
namespace storm::shim
{
template <class T> inline void assign_ref(T *&slot, T *value)
{
    if (value)
        value->AddRef();
    if (slot)
        slot->Release();
    slot = value;
}

inline float as_float(DWORD value)
{
    float f;
    memcpy(&f, &value, sizeof(f));
    return f;
}
inline DWORD as_dword(float value)
{
    DWORD d;
    memcpy(&d, &value, sizeof(d));
    return d;
}

// out = a * b in D3D's row-vector convention (bx's too); `out` may alias `a` or `b`.
inline void multiply(const D3DMATRIX &a, const D3DMATRIX &b, D3DMATRIX &out)
{
    D3DMATRIX r;
    bx::mtxMul(&r.m[0][0], &a.m[0][0], &b.m[0][0]);
    out = r;
}

inline D3DMATRIX identity()
{
    D3DMATRIX m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

inline void color_to_float(D3DCOLOR color, float *out)
{
    out[0] = ((color >> 16) & 255) / 255.0f;
    out[1] = ((color >> 8) & 255) / 255.0f;
    out[2] = (color & 255) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}

inline uint32_t vertex_count_for(D3DPRIMITIVETYPE type, uint32_t primitives)
{
    switch (type)
    {
    case D3DPT_POINTLIST:
        return primitives;
    case D3DPT_LINELIST:
        return primitives * 2;
    case D3DPT_LINESTRIP:
        return primitives + 1;
    case D3DPT_TRIANGLELIST:
        return primitives * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        return primitives + 2;
    default:
        return 0;
    }
}

inline uint64_t blend_factor(DWORD value)
{
    switch (value)
    {
    case D3DBLEND_ZERO:
        return BGFX_STATE_BLEND_ZERO;
    case D3DBLEND_ONE:
        return BGFX_STATE_BLEND_ONE;
    case D3DBLEND_SRCCOLOR:
        return BGFX_STATE_BLEND_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR:
        return BGFX_STATE_BLEND_INV_SRC_COLOR;
    case D3DBLEND_SRCALPHA:
        return BGFX_STATE_BLEND_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA:
        return BGFX_STATE_BLEND_INV_SRC_ALPHA;
    case D3DBLEND_DESTALPHA:
        return BGFX_STATE_BLEND_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA:
        return BGFX_STATE_BLEND_INV_DST_ALPHA;
    case D3DBLEND_DESTCOLOR:
        return BGFX_STATE_BLEND_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR:
        return BGFX_STATE_BLEND_INV_DST_COLOR;
    case D3DBLEND_SRCALPHASAT:
        return BGFX_STATE_BLEND_SRC_ALPHA_SAT;
    case D3DBLEND_BLENDFACTOR:
        return BGFX_STATE_BLEND_FACTOR;
    case D3DBLEND_INVBLENDFACTOR:
        return BGFX_STATE_BLEND_INV_FACTOR;
    default:
        return BGFX_STATE_BLEND_ONE;
    }
}

inline uint64_t depth_func(DWORD value)
{
    switch (value)
    {
    case D3DCMP_NEVER:
        return BGFX_STATE_DEPTH_TEST_NEVER;
    case D3DCMP_LESS:
        return BGFX_STATE_DEPTH_TEST_LESS;
    case D3DCMP_EQUAL:
        return BGFX_STATE_DEPTH_TEST_EQUAL;
    case D3DCMP_LESSEQUAL:
        return BGFX_STATE_DEPTH_TEST_LEQUAL;
    case D3DCMP_GREATER:
        return BGFX_STATE_DEPTH_TEST_GREATER;
    case D3DCMP_NOTEQUAL:
        return BGFX_STATE_DEPTH_TEST_NOTEQUAL;
    case D3DCMP_GREATEREQUAL:
        return BGFX_STATE_DEPTH_TEST_GEQUAL;
    default:
        return BGFX_STATE_DEPTH_TEST_ALWAYS;
    }
}

inline uint32_t stencil_func(DWORD value)
{
    switch (value)
    {
    case D3DCMP_NEVER:
        return BGFX_STENCIL_TEST_NEVER;
    case D3DCMP_LESS:
        return BGFX_STENCIL_TEST_LESS;
    case D3DCMP_EQUAL:
        return BGFX_STENCIL_TEST_EQUAL;
    case D3DCMP_LESSEQUAL:
        return BGFX_STENCIL_TEST_LEQUAL;
    case D3DCMP_GREATER:
        return BGFX_STENCIL_TEST_GREATER;
    case D3DCMP_NOTEQUAL:
        return BGFX_STENCIL_TEST_NOTEQUAL;
    case D3DCMP_GREATEREQUAL:
        return BGFX_STENCIL_TEST_GEQUAL;
    default:
        return BGFX_STENCIL_TEST_ALWAYS;
    }
}

inline uint32_t stencil_op(DWORD value, int shift)
{
    // BGFX_STENCIL_OP_FAIL_S_* values are 0..7 << 16 in D3DSTENCILOP order - 1.
    static const uint32_t table[9] = {1, 1, 0, 2, 4, 6, 7, 3, 5}; // index: D3DSTENCILOP, value: bgfx op
    const uint32_t op = value >= 1 && value <= 8 ? table[value] : 1;
    return op << shift;
}
} // namespace storm::shim
