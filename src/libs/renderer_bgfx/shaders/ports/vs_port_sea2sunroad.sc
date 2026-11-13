$input a_position, a_normal, a_texcoord0
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader sea_sea2sunroad_vertex_shader (vs_1_1) of src/techniques/weather/sea.fx, one D3D9 instruction per line.
#define PORT_VSC_COUNT 39 // registers c0..c38 (the GL program uploads exactly these)
#include "port_vs.sh"
#include "sea_waves.sh"

void main()
{
    vec4 v0 = a_position; // dcl_position0 v0
    v0.y += seaWavesLift(v0.xz); // waves from objects (sea_waves.sh)
    vec4 v1 = vec4(a_normal, 1.0); // dcl_normal0 v1
    vec4 v2 = a_texcoord0; // dcl_texcoord0 v2
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    vec4 r4 = vec4_splat(0.0);
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
    oPos = r0;                             // mov oPos, r0
    r11.z = r0.z * u_vsc[1].w;             // mul r11.z, r0.zzzz, c1.wwww
    r11.z = exp2(r11.z);                   // expp r11.z, r11.zzzz
    r11.z = portRcp(r11.z);                // rcp r11.z, r11.zzzz
    oFog = r11.z;                          // mov oFog, r11.zzzz
    r4 = v1;                               // mov r4, v1
    r4.w = dot(r4.xyz, r4.xyz);            // dp3 r4.w, r4, r4
    r4.w = portRsq(r4.w);                  // rsq r4.w, r4.wwww
    r4 = r4 * r4.wwww;                     // mul r4, r4, r4.wwww
    r1 = v0 + -u_vsc[23];                  // add r1, v0, -c23
    r1.w = dot(r1.xyz, r1.xyz);            // dp3 r1.w, r1, r1
    r1.w = portRsq(r1.w);                  // rsq r1.w, r1.wwww
    r1 = r1 * r1.wwww;                     // mul r1, r1, r1.wwww
    r3.x = dot(r1.xyz, r4.xyz);            // dp3 r3.x, r1, r4
    r3.x = r3.x + r3.x;                    // add r3.x, r3.xxxx, r3.xxxx
    r2.xyz = r3.xxx * r4.xyz + -r1.xyz;    // mad r2.xyz, r3.xxxx, r4, -r1
    r10 = r4.yzxw * u_vsc[33].zxyw;        // mul r10, r4.yzxw, c33.zxyw
    r10 = -u_vsc[33].yzxw * r4.zxyw + r10; // mad r10, -c33.yzxw, r4.zxyw, r10
    r11 = r4.yzxw * r10.zxyw;              // mul r11, r4.yzxw, r10.zxyw
    r11 = -r10.yzxw * r4.zxyw + r11;       // mad r11, -r10.yzxw, r4.zxyw, r11
    r10.w = dot(r10.xyz, r10.xyz);         // dp3 r10.w, r10, r10
    r10.w = portRsq(r10.w);                // rsq r10.w, r10.wwww
    r10 = r10 * r10.wwww;                  // mul r10, r10, r10.wwww
    r11.w = dot(r11.xyz, r11.xyz);         // dp3 r11.w, r11, r11
    r11.w = portRsq(r11.w);                // rsq r11.w, r11.wwww
    r11 = r11 * r11.wwww;                  // mul r11, r11, r11.wwww
    r9 = r10.zxyw * r11.yzxw;              // mul r9, r10.zxyw, r11.yzxw
    r9 = r10.yzxw * r11.zxyw + -r9;        // mad r9, r10.yzxw, r11.zxyw, -r9
    oT1.x = dot(r10.xyz, u_vsc[36].xyz);   // dp3 oT1.x, r10, c36
    oT1.y = dot(r11.xyz, u_vsc[36].xyz);   // dp3 oT1.y, r11, c36
    oT1.z = dot(r9.xyz, u_vsc[36].xyz);    // dp3 oT1.z, r9, c36
    oT2.x = dot(r10.xyz, u_vsc[37].xyz);   // dp3 oT2.x, r10, c37
    oT2.y = dot(r11.xyz, u_vsc[37].xyz);   // dp3 oT2.y, r11, c37
    oT2.z = dot(r9.xyz, u_vsc[37].xyz);    // dp3 oT2.z, r9, c37
    oT3.x = dot(r10.xyz, u_vsc[38].xyz);   // dp3 oT3.x, r10, c38
    oT3.y = dot(r11.xyz, u_vsc[38].xyz);   // dp3 oT3.y, r11, c38
    oT3.z = dot(r9.xyz, u_vsc[38].xyz);    // dp3 oT3.z, r9, c38
    oT1.w = -r1.x;                         // mov oT1.w, -r1.xxxx
    oT2.w = -r1.y;                         // mov oT2.w, -r1.yyyy
    oT3.w = -r1.z;                         // mov oT3.w, -r1.zzzz
    oT0.z = u_vsc[2].x;                    // mov oT0.z, c2.xxxx
    oT0.xy = v2.xy;                        // mov oT0.xy, v2

    oT4 = vec4(seaWavesUv(v0.xz), v0.xz); // the ripples and wakes of the pixel shader

    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
