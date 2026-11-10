$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of src/techniques/weather/sea_sea3_pixel_shader.pso (ps_1_1, FNV-1a bc1a95c3d1f059a4).
// Stages 0 and 2: animated bump volume texture, stage 1: planar reflection, stage 3: sun road reflection (2D).
#include "port_ps.sh"
SAMPLER3D(s_vol0, 8);
SAMPLER2D(s_tex1, 1);
SAMPLER3D(s_vol2, 10);
SAMPLER2D(s_tex3, 3);

void main()
{
    vec4 v0 = v_color0;
    vec4 v1 = v_color1;
    vec4 r0 = vec4_splat(0.0);

    vec4 t0 = texture3D(s_vol0, v_texcoord0.xyz);                           // tex t0
    vec4 t1 = texture2D(s_tex1, portBem(v_texcoord1.xy, t0.rg, u_psBem[1])); // texbem t1, t0
    vec4 t2 = texture3D(s_vol2, v_texcoord2.xyz);                           // tex t2
    vec4 t3 = texture2D(s_tex3, portBem(v_texcoord3.xy, t2.rg, u_psBem[3])); // texbem t3, t2
    r0.rgb = PS11(v0.rgb * t1.rgb + v1.rgb);                                // mad r0.xyz, v0, t1, v1
    r0.rgb = PS11(r0.rgb + t3.rgb);                                         // add r0.xyz, r0, t3
    r0.a = v0.a;                                                            // mov r0.w, v0.wwww

    PORT_PS_OUTPUT(r0);
}
