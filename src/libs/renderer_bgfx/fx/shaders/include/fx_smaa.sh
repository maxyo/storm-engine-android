// SMAA 1x (Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez, 2013; MIT, see
// ../../THIRD_PARTY.md): the reference SMAA.hlsl, fetched at the pin in ../../CMakeLists.txt, through the bgfx
// shader macros. Luma edge detection on the gamma-encoded picture (as the reference asks), the HIGH preset.
// The edges and weights stay in texture space: on a bottom-left-origin backend SMAA runs on the vertically
// mirrored picture, which antialiases it exactly as well.
#ifndef FX_SMAA_SH
#define FX_SMAA_SH
#include "fx_common.sh"

uniform vec4 u_fxSmaa; // 1 / width, 1 / height, width, height of the picture
#define SMAA_RT_METRICS u_fxSmaa
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL 1
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) texture2DLod(tex, coord, 0.0)
#define SMAASampleLevelZeroPoint(tex, coord) texture2DLod(tex, coord, 0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) texture2DLod(tex, (coord) + vec2(offset) * SMAA_RT_METRICS.xy, 0.0)
#define SMAASample(tex, coord) texture2DLod(tex, coord, 0.0)
#define SMAASamplePoint(tex, coord) texture2DLod(tex, coord, 0.0)
#define SMAASampleOffset(tex, coord, offset) texture2DLod(tex, (coord) + vec2(offset) * SMAA_RT_METRICS.xy, 0.0)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#if BGFX_SHADER_LANGUAGE_GLSL
#define lerp(a, b, t) mix(a, b, t)
#define mad(a, b, c) (a * b + c)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4
#endif
#include "SMAA.hlsl"
#endif
