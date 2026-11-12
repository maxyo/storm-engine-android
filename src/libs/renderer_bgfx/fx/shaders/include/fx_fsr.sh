// AMD FidelityFX Super Resolution 1 (ffx_a.h, ffx_fsr1.h; Copyright (c) 2021 Advanced Micro Devices, Inc., MIT, see
// ../../THIRD_PARTY.md), fetched at the pin in ../../CMakeLists.txt, in its 32-bit pixel-shader form: every ESSL
// blob is highp (no A_HALF). The constants come from FsrEasuCon / FsrRcasCon on the CPU (post_chain.cpp); they are
// ordinary floats, carried as vec4 uniforms and read back as the uint bit patterns the reference expects.
#ifndef FX_FSR_SH
#define FX_FSR_SH
#include "fx_common.sh"

#define A_GPU 1
#if BGFX_SHADER_LANGUAGE_GLSL
#define A_GLSL 1
#define A_SKIP_EXT 1
#else
#define A_HLSL 1
#endif
#include "ffx_a.h"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxFsr[4]; // EASU: con0..con3; RCAS: con0 in [0]; [3].zw: input size in pixels (both passes)

AU4 fxFsrCon(int i)
{
    return AU4_AF4(u_fxFsr[i]);
}

// One texel of the input, clamped to its edge like the reference's clamp sampler.
AF4 fxFsrTexel(ASU2 p)
{
    ASU2 last = ASU2(u_fxFsr[3].zw) - ASU2(1, 1);
    return texelFetch(s_fxScene, clamp(p, ASU2(0, 0), last), 0);
}

// The output pixel from the screen triangle's uv: also right when the caller mirrors the image.
AU2 fxFsrPixel(vec2 uv, vec2 size)
{
    return AU2(floor(uv * size));
}
#endif
