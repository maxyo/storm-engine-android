// Helpers for the ps_1_x ports (shaders/ports/fs_port_*.sc).
// Sampler units are fixed by the shim: a 2D texture of stage N sits on unit N,
// a cube texture on 5+N (stage 3: 11) and a volume texture on 8+N (stage 4: 12).
#ifndef PORT_PS_SH
#define PORT_PS_SH
#include "ff_finish.sh"

uniform vec4 u_psc[8];  // c0..c7 as set through SetPixelShaderConstantF
uniform vec4 u_psBem[5]; // per stage D3DTSS_BUMPENVMAT00, 01, 10, 11 (texbem)

// Register range: ps_1_1..1_3 hold [-1, 1] (PixelShader1xMaxValue = 1), ps_1_4 [-8, 8].
// This is the one place to change if the reference turns out not to clamp.
#define PS11(x) clamp(x, -1.0, 1.0)
#define PS14(x) clamp(x, -8.0, 8.0)
// Source modifiers.
#define PS_BX2(x) (2.0 * ((x) - 0.5))
// Final colour: oC0 = saturate(r0), then D3D alpha test and fog.
#define PORT_PS_OUTPUT(r0) gl_FragColor = ffFinish(clamp(r0, 0.0, 1.0))

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
