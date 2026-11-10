$input a_position, a_color0, a_texcoord0, a_texcoord1, a_tangent, a_weight, a_texcoord2, a_texcoord3
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader particles_vertex_shader (vs_1_1) of src/techniques/particles/particles.fx, one D3D9 instruction per line.
#define PORT_VSC_COUNT 14 // registers c0..c13 (the GL program uploads exactly these)
#include "port_vs.sh"

void main()
{
    vec4 v0 = a_position; // dcl_position0 v0
    vec4 v1 = D3DCOLOR_TO_RGBA(a_color0); // dcl_color0 v1
    vec4 v2 = a_texcoord0; // dcl_texcoord0 v2
    vec4 v3 = a_texcoord1; // dcl_texcoord1 v3
    vec4 v4 = vec4(a_tangent, 1.0); // dcl_tangent0 v4
    vec4 v5 = a_weight; // dcl_blendweight0 v5
    vec4 v6 = a_texcoord2; // dcl_texcoord2 v6
    vec4 v7 = a_texcoord3; // dcl_texcoord3 v7
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    vec4 r4 = vec4_splat(0.0);
    vec4 r5 = vec4_splat(0.0);
    vec4 r7 = vec4_splat(0.0);
    vec4 r9 = vec4_splat(0.0);
    vec4 r11 = vec4_splat(0.0);
    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oD0 = vec4_splat(1.0);
    vec4 oD1 = vec4_splat(0.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT1 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT3 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT4 = vec4(0.0, 0.0, 0.0, 1.0);
    float oFog = 1.0;

    oD0 = v1;                                   // mov oD0, v1
    oT0 = v2;                                   // mov oT0, v2
    oT1 = v3;                                   // mov oT1, v3
    oT3 = v2;                                   // mov oT3, v2
    oT2 = u_vsc[13].xxxx;                       // mov oT2, c13.xxxx
    oT2.x = v7.x;                               // mov oT2.x, v7.xxxx
    oD1 = v5.xxxx;                              // mov oD1, v5.xxxx
    r7.xy = v4.xx * u_vsc[1].xx + u_vsc[1].yz;  // mad r7.xy, v4.xxxx, c1.xxxx, c1.yzzw
    r2.xy = fract(r7.xy);                       // frc r2.xy, r7.xyyy
    r9.xy = r2.xy * u_vsc[1].ww;                // mul r9.xy, r2, c1.wwww
    r4.xy = r9.xy + u_vsc[2].xx;                // add r4.xy, r9, c2.xxxx
    r11.xy = r4.xy * r4.xy;                     // mul r11.xy, r4, r4
    r1.xy = r11.xy * u_vsc[2].ww + u_vsc[2].yy; // mad r1.xy, r11.xyyy, c2.wwww, c2.yyyy
    r3.xy = r11.xy * r1.xy + u_vsc[2].zz;       // mad r3.xy, r11.xyyy, r1, c2.zzzz
    r5.xy = r11.xy * r3.xy + u_vsc[0].xx;       // mad r5.xy, r11.xyyy, r3, c0.xxxx
    r7.xy = r11.xy * r5.xy + u_vsc[0].ww;       // mad r7.xy, r11.xyyy, r5, c0.wwww
    r0.xy = r11.xy * r7.xy + u_vsc[0].yy;       // mad r0.xy, r11.xyyy, r7, c0.yyyy
    oD1.x = -r0.y * u_vsc[13].z + u_vsc[13].z;  // mad oD1.x, -r0.yyyy, c13.zzzz, c13.zzzz
    oD1.y = -r0.x * u_vsc[13].z + u_vsc[13].z;  // mad oD1.y, -r0.xxxx, c13.zzzz, c13.zzzz
    oD1.z = u_vsc[13].z;                        // mov oD1.z, c13.zzzz
    r1 = v0;                                    // mov r1, v0
    r3.x = r1.y * r0.y;                         // mul r3.x, r1.yyyy, r0.yyyy
    r3.y = r1.x * r0.x;                         // mul r3.y, r1.xxxx, r0.xxxx
    r2.x = r3.y + r3.x;                         // add r2.x, r3.yyyy, r3.xxxx
    r3.x = r1.x * r0.y;                         // mul r3.x, r1.xxxx, r0.yyyy
    r3.y = r1.y * r0.x;                         // mul r3.y, r1.yyyy, r0.xxxx
    r2.y = r3.y + -r3.x;                        // add r2.y, r3.yyyy, -r3.xxxx
    r2.z = u_vsc[13].x;                         // mov r2.z, c13.xxxx
    r2.w = u_vsc[13].x;                         // mov r2.w, c13.xxxx
    // m4x4 r3, v6, c3
    r3 = vec4(dot(v6, u_vsc[3]),
              dot(v6, u_vsc[4]),
              dot(v6, u_vsc[5]),
              dot(v6, u_vsc[6]));
    r2 = r2 + r3;                               // add r2, r2, r3
    // m4x4 oPos, r2, c7
    oPos = vec4(dot(r2, u_vsc[7]),
                dot(r2, u_vsc[8]),
                dot(r2, u_vsc[9]),
                dot(r2, u_vsc[10]));

    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
