$input a_position, a_color0, a_texcoord0
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of src/techniques/worldmap/worldmap_shader.vso (vs_1_1, FNV-1a 64d4d2edd7ef1bcd), one bytecode instruction per line.
#include "port_vs.sh"

void main()
{
    vec4 v0 = a_position; // dcl_position0 v0
    vec4 v1 = D3DCOLOR_TO_RGBA(a_color0); // dcl_color0 v1
    vec4 v2 = a_texcoord0; // dcl_texcoord0 v2
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oD0 = vec4_splat(1.0);
    vec4 oD1 = vec4_splat(0.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT1 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT3 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT4 = vec4(0.0, 0.0, 0.0, 1.0);
    float oFog = 1.0;

    r0 = v0;                            // mov r0, v0
    r1 = r0.xxxx * u_vsc[0];            // mul r1, r0.xxxx, c0
    r1 = r0.yyyy * u_vsc[1] + r1;       // mad r1, r0.yyyy, c1, r1
    r1 = r0.zzzz * u_vsc[2] + r1;       // mad r1, r0.zzzz, c2, r1
    oPos = r1 + u_vsc[3];               // add oPos, r1, c3
    oD0 = v1.xxxw;                      // mov oD0, v1.xxxw
    oD1 = v1.yyyw * u_vsc[6];           // mul oD1, v1.yyyw, c6
    oT0 = v2;                           // mov oT0, v2
    r2 = u_vsc[5];                      // mov r2, c5
    r2.w = v1.z * r2.z + r2.x;          // mad r2.w, v1.zzzz, r2.zzzz, r2.xxxx
    r1 = vec4_splat(dot(r0, u_vsc[4])); // dp4 r1, r0, c4
    r2.w = portRcp(r2.w);               // rcp r2.w, r2.wwww
    r1.w = r1.w * r2.w + u_vsc[5].w;    // mad r1.w, r1.wwww, r2.wwww, c5.wwww
    oT1 = r1.wwww;                      // mov oT1, r1.wwww

    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
