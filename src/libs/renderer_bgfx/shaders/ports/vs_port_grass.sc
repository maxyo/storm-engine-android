$input a_position, a_color0, a_color1, a_texcoord0
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader grass_main (vs_1_1) of src/techniques/effects/grass.fx, one D3D9 instruction per line.
#include "port_vs.sh"

void main()
{
    vec4 v0 = a_position; // dcl_position0 v0
    vec4 v1 = D3DCOLOR_TO_RGBA(a_color0); // dcl_color0 v1
    vec4 v2 = D3DCOLOR_TO_RGBA(a_color1); // dcl_color1 v2
    vec4 v3 = a_texcoord0; // dcl_texcoord0 v3
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    int a0 = 0;
    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oD0 = vec4_splat(1.0);
    vec4 oD1 = vec4_splat(0.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT1 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT3 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT4 = vec4(0.0, 0.0, 0.0, 1.0);
    float oFog = 1.0;

    r0.xy = v3.xy * u_vsc[36].xy;                // mul r0.xy, v3, c36
    r0.w = r0.y + r0.x;                          // add r0.w, r0.yyyy, r0.xxxx
    r0.w = max(-r0.w, u_vsc[40].y);              // max r0.w, -r0.wwww, c40.yyyy
    r2.xw = v1.zw * u_vsc[40].xx;                // mul r2.xw, v1.zyzw, c40.xxxx
    r0.w = min(r0.w, u_vsc[40].z);               // min r0.w, r0.wwww, c40.zzzz
    a0 = portAddr(r2.x);                         // mov a0.x, r2.xxxx
    r0.y = u_vsc[0 + a0].z;                      // mov r0.y, c0[a0.x].zzzz
    r1.w = r0.y * u_vsc[40].w;                   // mul r1.w, r0.yyyy, c40.wwww
    r0.w = r0.w * u_vsc[39].x + r1.w;            // mad r0.w, r0.wwww, c39.xxxx, r1.wwww
    r1.w = r0.w * v2.y;                          // mul r1.w, r0.wwww, v2.yyyy
    r0.xy = v3.xy * v3.xy;                       // mul r0.xy, v3, v3
    r0.w = r0.y + r0.x;                          // add r0.w, r0.yyyy, r0.xxxx
    r0.xy = v1.xy * u_vsc[41].xy + u_vsc[41].zw; // mad r0.xy, v1, c41, c41.zwzw
    r1.w = r1.w * u_vsc[39].y;                   // mul r1.w, r1.wwww, c39.yyyy
    r0.w = -r0.w + r0.y;                         // add r0.w, -r0.wwww, r0.yyyy
    r0.xy = r0.xx * u_vsc[0 + a0].xy;            // mul r0.xy, r0.xxxx, c0[a0.x]
    r0.w = portRsq(r0.w);                        // rsq r0.w, r0.wwww
    r0.w = portRcp(r0.w);                        // rcp r0.w, r0.wwww
    r3.w = v2.x + u_vsc[40].y;                   // add r3.w, v2.xxxx, c40.yyyy
    r3.xy = v2.xy;                               // mov r3.xy, v2
    r0.w = r0.w * r3.y;                          // mul r0.w, r0.wwww, r3.yyyy
    r0.w = v0.y * u_vsc[38].w + r0.w;            // mad r0.w, v0.yyyy, c38.wwww, r0.wwww
    r1.xy = r0.xy * -r3.ww;                      // mul r1.xy, r0, -r3.wwww
    r1.xy = v0.xz * u_vsc[38].ww + r1.xy;        // mad r1.xy, v0.xzzw, c38.wwww, r1.xyyy
    r0 = r0.wwww * u_vsc[33];                    // mul r0, r0.wwww, c33
    r2.xy = v3.xy * r3.yy + r1.xy;               // mad r2.xy, v3, r3.yyyy, r1
    r1.xyz = u_vsc[38].xyz;                      // mov r1.xyz, c38
    oD0.xyz = r1.xyz * r1.www + u_vsc[37].xyz;   // mad oD0.xyz, r1, r1.wwww, c37
    r0 = u_vsc[32] * r2.xxxx + r0;               // mad r0, c32, r2.xxxx, r0
    r1.zw = u_vsc[39].zw;                        // mov r1.zw, c39
    a0 = portAddr(r2.w);                         // mov a0.x, r2.wwww
    oT0.xy = r3.xy * r1.zw + u_vsc[16 + a0].xy;  // mad oT0.xy, r3, r1.zwzw, c16[a0.x]
    r0 = u_vsc[34] * r2.yyyy + r0;               // mad r0, c34, r2.yyyy, r0
    oPos = r0 + u_vsc[35];                       // add oPos, r0, c35
    oD0.w = v3.z;                                // mov oD0.w, v3.zzzz

    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
