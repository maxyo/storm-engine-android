// Helpers for the vs_1_1 ports (shaders/ports/vs_port_*.sc). Each port keeps the
// original register names and translates the bytecode one instruction per line.
#ifndef PORT_VS_SH
#define PORT_VS_SH
#include "storm_common.sh"

uniform vec4 u_vsc[96]; // c0..c95 as set through SetVertexShaderConstantF
uniform mat4 u_ffProj;  // D3D clip space -> backend clip space fix-up

#define PORT_FLT_MAX 3.402823466e+38

// rcp: 1/x, +FLT_MAX for a zero source.
float portRcp(float x)
{
    return x == 0.0 ? PORT_FLT_MAX : 1.0 / x;
}

// rsq: 1/sqrt(|x|), +FLT_MAX for a zero source.
float portRsq(float x)
{
    float a = abs(x);
    return a == 0.0 ? PORT_FLT_MAX : inversesqrt(a);
}

// vs_1_1 `mov a0.x, src` converts with floor().
int portAddr(float x)
{
    return int(floor(x));
}

// Writes the standard varyings from the D3D output registers. D3D clamps the
// colour outputs of a vertex shader to [0, 1] before interpolation.
#define PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog) \
    v_color0 = clamp(oD0, 0.0, 1.0); \
    v_color1 = clamp(oD1, 0.0, 1.0); \
    v_texcoord0 = oT0; \
    v_texcoord1 = oT1; \
    v_texcoord2 = oT2; \
    v_texcoord3 = oT3; \
    v_texcoord4 = oT4; \
    v_fog = vec4(oFog, 1.0, 0.0, 0.0); \
    gl_Position = mul(u_ffProj, oPos)

#endif
