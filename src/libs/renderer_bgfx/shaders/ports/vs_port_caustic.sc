$input a_position, a_texcoord0, a_color0, a_texcoord1
$output v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of src/techniques/weather/caustic_vertex_shader.vso (vs_1_1, FNV-1a abfb8325bc32d5e2), one bytecode instruction per line.
#include "port_vs.sh"

void main()
{
    vec4 v0 = a_position; // dcl_position0 v0
    vec4 v1 = a_texcoord0; // dcl_texcoord0 v1
    vec4 v2 = D3DCOLOR_TO_RGBA(a_color0); // dcl_color0 v2
    vec4 v3 = a_texcoord1; // dcl_texcoord1 v3
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oD0 = vec4_splat(1.0);
    vec4 oD1 = vec4_splat(0.0);
    vec4 oT0 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT1 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT2 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT3 = vec4(0.0, 0.0, 0.0, 1.0);
    vec4 oT4 = vec4(0.0, 0.0, 0.0, 1.0);
    float oFog = 1.0;

    // m4x4 r3, v0, c0
    r3 = vec4(dot(v0, u_vsc[0]),
              dot(v0, u_vsc[1]),
              dot(v0, u_vsc[2]),
              dot(v0, u_vsc[3]));
    oPos = r3;                         // mov oPos, r3
    // m4x4 r0, v0, c4
    r0 = vec4(dot(v0, u_vsc[4]),
              dot(v0, u_vsc[5]),
              dot(v0, u_vsc[6]),
              dot(v0, u_vsc[7]));
    r1 = r0;                           // mov r1, r0
    // m3x3 r2.xyz, v1, c4
    r2.xyz = vec3(dot(v1.xyz, u_vsc[4].xyz),
                  dot(v1.xyz, u_vsc[5].xyz),
                  dot(v1.xyz, u_vsc[6].xyz));
    r2.x = dot(u_vsc[14].xyz, r2.xyz); // dp3 r2.x, c14, r2
    r2.y = r3.z * u_vsc[15].x;         // mul r2.y, r3.zzzz, c15.xxxx
    r2.y = min(r2.y, u_vsc[15].y);     // min r2.y, r2.yyyy, c15.yyyy
    r2.y = u_vsc[15].y + -r2.y;        // add r2.y, c15.yyyy, -r2.yyyy
    r2.x = r2.x * r2.y;                // mul r2.x, r2.xxxx, r2.yyyy
    oT0.xy = v3.xy;                    // mov oT0.xy, v3
    oT1.xy = r0.xz * u_vsc[10].xx;     // mul oT1.xy, r0.xzzz, c10.xxxx
    oT2.xy = r0.xz * u_vsc[10].xx;     // mul oT2.xy, r0.xzzz, c10.xxxx
    oT3 = u_vsc[10].yyyy;              // mov oT3, c10.yyyy
    oT3.w = r2.x;                      // mov oT3.w, r2.xxxx
    oD0 = u_vsc[11];                   // mov oD0, c11
    r3.z = max(u_vsc[13].x, r3.z);     // max r3.z, c13.xxxx, r3.zzzz
    r3.z = r3.z * u_vsc[12].x;         // mul r3.z, r3.zzzz, c12.xxxx
    r3.z = exp2(r3.z);                 // expp r3.z, r3.zzzz
    oFog = portRcp(r3.z);              // rcp oFog, r3.zzzz

    PORT_VS_OUTPUT(oPos, oD0, oD1, oT0, oT1, oT2, oT3, oT4, oFog);
}
