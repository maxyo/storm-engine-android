$input a_position, a_normal, a_texcoord0
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader sea_sea3_vertex_shader (vs_1_1) of src/techniques/weather/sea.fx, one D3D9 instruction per line.
#define PORT_VSC_COUNT 59 // registers c0..c58 (the GL program uploads exactly these)
#include "port_vs.sh"

void main()
{
    vec4 v0 = a_position; // dcl_position0 v0
    vec4 v1 = vec4(a_normal, 1.0); // dcl_normal0 v1
    vec4 v2 = a_texcoord0; // dcl_texcoord0 v2
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    vec4 r4 = vec4_splat(0.0);
    vec4 r5 = vec4_splat(0.0);
    vec4 r7 = vec4_splat(0.0);
    vec4 r9 = vec4_splat(0.0);
    vec4 r10 = vec4_splat(0.0);
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

    // m4x4 r0, v0, c24
    r0 = vec4(dot(v0, u_vsc[24]),
              dot(v0, u_vsc[25]),
              dot(v0, u_vsc[26]),
              dot(v0, u_vsc[27]));
    oPos = r0;                         // mov oPos, r0
    r11.z = r0.z * u_vsc[1].w;         // mul r11.z, r0.zzzz, c1.wwww
    r11.z = exp2(r11.z);               // expp r11.z, r11.zzzz
    r11.z = portRcp(r11.z);            // rcp r11.z, r11.zzzz
    oFog = r11.z;                      // mov oFog, r11.zzzz
    r4 = v1;                           // mov r4, v1
    r4.w = dot(r4.xyz, r4.xyz);        // dp3 r4.w, r4, r4
    r4.w = portRsq(r4.w);              // rsq r4.w, r4.wwww
    r4 = r4 * r4.wwww;                 // mul r4, r4, r4.wwww
    r1 = v0 + -u_vsc[23];              // add r1, v0, -c23
    r1.w = dot(r1.xyz, r1.xyz);        // dp3 r1.w, r1, r1
    r1.w = portRsq(r1.w);              // rsq r1.w, r1.wwww
    r1 = r1 * r1.wwww;                 // mul r1, r1, r1.wwww
    r7.x = dot(r1.xyz, r4.xyz);        // dp3 r7.x, r1, r4
    r5.x = -r7.x;                      // mov r5.x, -r7.xxxx
    r5.yz = u_vsc[34].xy;              // mov r5.yz, c34.xxyy
    r10.xy = r5.xy * r5.xy;            // mul r10.xy, r5.xyyy, r5.xyyy
    r10.z = r10.y * r10.x + -r10.y;    // mad r10.z, r10.yyyy, r10.xxxx, -r10.yyyy
    r10.z = r10.z + u_vsc[0].y;        // add r10.z, r10.zzzz, c0.yyyy
    r9.x = portRsq(r10.z);             // rsq r9.x, r10.zzzz
    r9.x = r10.z * r9.x;               // mul r9.x, r10.zzzz, r9.xxxx
    r10.y = r5.y * r9.x + r5.x;        // mad r10.y, r5.yyyy, r9.xxxx, r5.xxxx
    r10.x = -r5.y * r9.x + r5.x;       // mad r10.x, -r5.yyyy, r9.xxxx, r5.xxxx
    r9.y = portRcp(r10.y);             // rcp r9.y, r10.yyyy
    r10.w = r5.y * r5.x + r9.x;        // mad r10.w, r5.yyyy, r5.xxxx, r9.xxxx
    r10.z = r5.y * r5.x + -r9.x;       // mad r10.z, r5.yyyy, r5.xxxx, -r9.xxxx
    r9.z = portRcp(r10.w);             // rcp r9.z, r10.wwww
    r9.yz = r9.yz * r10.xz;            // mul r9.yz, r9.xyzz, r10.xxzz
    r9.xw = r9.yz * r9.yz;             // mul r9.xw, r9.yyyz, r9.yyyz
    r7.y = r9.x + r9.w;                // add r7.y, r9.xxxx, r9.wwww
    r7.y = r7.y * u_vsc[0].z;          // mul r7.y, r7.yyyy, c0.zzzz
    r7.y = min(r7.y, r5.z);            // min r7.y, r7.yyyy, r5.zzzz
    r2.x = portRcp(r1.y);              // rcp r2.x, r1.yyyy
    r2.x = r2.x * u_vsc[28].x;         // mul r2.x, r2.xxxx, c28.xxxx
    r2.x = min(r2.x, -r2.x);           // min r2.x, r2.xxxx, -r2.xxxx
    r7.z = exp2(r2.x);                 // expp r7.z, r2.xxxx
    r2.y = u_vsc[28].y * r7.y;         // mul r2.y, c28.yyyy, r7.yyyy
    r3.x = dot(u_vsc[58].xyz, r4.xyz); // dp3 r3.x, c58, r4
    r3.y = r3.x * r2.y;                // mul r3.y, r3.xxxx, r2.yyyy
    oD0.xyz = r3.yyy * u_vsc[30].xyz;  // mul oD0.xyz, r3.yyyy, c30
    r2.x = -r7.y * r7.z + r7.z;        // mad r2.x, -r7.yyyy, r7.zzzz, r7.zzzz
    oD0.w = r2.x * u_vsc[28].z;        // mul oD0.w, r2.xxxx, c28.zzzz
    r2.xy = u_vsc[0].yy + -r7.yz;      // add r2.xy, c0.yyyy, -r7.yzzz
    r2.z = r2.x * r2.y;                // mul r2.z, r2.xxxx, r2.yyyy
    oD1.xyz = r2.zzz * u_vsc[29].xyz;  // mul oD1.xyz, r2.zzzz, c29
    oD1.w = u_vsc[0].y;                // mov oD1.w, c0.yyyy
    r1 = v0;                           // mov r1, v0
    // m4x4 r0, r1, c24
    r0 = vec4(dot(r1, u_vsc[24]),
              dot(r1, u_vsc[25]),
              dot(r1, u_vsc[26]),
              dot(r1, u_vsc[27]));
    r1.w = portRcp(r0.w);              // rcp r1.w, r0.wwww
    r0.xyz = r0.xyz * r1.www;          // mul r0.xyz, r0.xyzz, r1.wwww
    r0.xyz = r0.xyz * u_vsc[0].zzz;    // mul r0.xyz, r0.xyzz, c0.zzzz
    r0.y = r0.y * u_vsc[1].y;          // mul r0.y, r0.yyyy, c1.yyyy
    r0.xy = r0.xy + u_vsc[0].zz;       // add r0.xy, r0.xyyy, c0.zzzz
    r0.xy = r0.xy + u_vsc[0].ww;       // add r0.xy, r0.xyyy, c0.wwww
    oT3 = r0;                          // mov oT3, r0
    r0.xy = r0.xy + v1.xz;             // add r0.xy, r0.xyyy, v1.xzzz
    oT1 = r0;                          // mov oT1, r0
    oT0.z = u_vsc[2].x;                // mov oT0.z, c2.xxxx
    oT0.xy = v2.xy;                    // mov oT0.xy, v2
    oT2.z = u_vsc[2].x;                // mov oT2.z, c2.xxxx
    oT2.xy = v2.xy;                    // mov oT2.xy, v2

    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
