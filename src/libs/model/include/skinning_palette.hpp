#pragma once
// Vertex and palette layout of GPU skinning (gpu_skinning.hpp), shared with the renderer test.
//
// Palette: 3 RGBA32F texels per bone, one row per bone; texel k = (m[k], m[4+k], m[8+k], m[12+k])
// of the bone matrix in memory order, texel 0 negated (the CPU kernel, skinning.hpp, negates
// elements 0, 4, 8 and 12 of the blended matrix; the blend is linear, so each bone is negated).
#include "storm_render_types.h"

#include <cstdint>

namespace storm::model
{

inline constexpr uint32_t kSkinnedSourceFvf =
    D3DFVF_XYZB2 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
inline constexpr uint32_t kSkinnedSourceStride = 44; // sizeof(GEOS::AVERTEX0)
inline constexpr uint32_t kPaletteTexelsPerBone = 3;

// Writes one bone (16 floats) into a palette row (3 RGBA32F texels).
inline void writePaletteRow(const float *bone, float *row)
{
    for (int k = 0; k < 3; ++k)
    {
        const float sign = k == 0 ? -1.0f : 1.0f;
        for (int c = 0; c < 4; ++c)
            row[k * 4 + c] = sign * bone[c * 4 + k];
    }
}

} // namespace storm::model
