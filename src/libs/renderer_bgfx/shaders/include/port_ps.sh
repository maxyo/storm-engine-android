// Helpers for the ps_1_x ports (shaders/ports/fs_port_*.sc).
// Sampler units are fixed by the shim: a 2D texture of stage N sits on unit N,
// a cube texture on 5+N (stage 3: 11) and a volume texture on 8+N (stage 4: 12).
#ifndef PORT_PS_SH
#define PORT_PS_SH
#include "ff_finish.sh"

uniform vec4 u_psc[8];  // c0..c7 as set through SetPixelShaderConstantF
uniform vec4 u_psBem[5]; // per stage D3DTSS_BUMPENVMAT00, 01, 10, 11 (texbem)

// Register range. The D3D9 reference (DXVK, like every SM2+ GPU, which reports an
// unbounded PixelShader1xMaxValue) does not clamp ps_1_x temporaries; only the
// constants are clamped to [-1, 1]. Every arithmetic result still goes through
// PS11()/PS14(), so a DX8-class [-1, 1] / [-8, 8] register range is a one-line change:
//   #define PS11(x) clamp(x, -1.0, 1.0)
//   #define PS14(x) clamp(x, -8.0, 8.0)
#define PS11(x) (x)
#define PS14(x) (x)
#define PS_CONST(c) clamp(c, -1.0, 1.0)
// Source modifiers.
#define PS_BX2(x) (2.0 * ((x) - 0.5))
// Final colour: oC0 = saturate(r0), then D3D alpha test and fog (and the slope-scaled depth bias of a
// STORM_SLOPE_BIAS port).
#define PORT_PS_OUTPUT(r0) gl_FragColor = ffFinish(clamp(r0, 0.0, 1.0)); FF_SLOPE_DEPTH()

// texbem: stage coordinates perturbed by the (du, dv) of an earlier texel.
vec2 portBem(vec2 uv, vec2 dudv, vec4 m)
{
    return uv + vec2(m.x * dudv.x + m.z * dudv.y, m.y * dudv.x + m.w * dudv.y);
}

// texm3x3vspec: reflect the per-pixel eye vector E about the normal N.
vec3 portReflect(vec3 N, vec3 E)
{
    return 2.0 * (dot(N, E) / dot(N, N)) * N - E;
}

#endif
