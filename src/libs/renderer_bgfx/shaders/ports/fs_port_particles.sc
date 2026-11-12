$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader particles_pixel_shader (ps_1_1) of src/techniques/particles/particles.fx.
// Stages 0 and 1: particle atlas (two animation frames), stage 3: atlas normal map; all 2D.
#include "port_ps.sh"
SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex3, 3);

void main()
{
    vec4 c0 = vec4(0.6, 0.6, 0.6, 0.6);                                     // def c0, 0.6, 0.6, 0.6, 0.6
    vec4 v0 = v_color0;
    vec4 v1 = v_color1;
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);

    vec4 t0 = texture2D(s_tex0, v_texcoord0.xy);                            // tex t0
    vec4 t1 = texture2D(s_tex1, v_texcoord1.xy);                            // tex t1
    vec4 t2 = clamp(v_texcoord2, 0.0, 1.0);                                 // texcoord t2 (all four, see fs_port_caustic)
    vec4 t3 = texture2D(s_tex3, v_texcoord3.xy);                            // tex t3
    r0 = PS11(v1.aaaa * t0 + (vec4_splat(1.0) - v1.aaaa) * t1);             // lrp r0, v1.aaaa, t0, t1
    r1 = PS11(r0 * v0);                                                     // mul r1, r0, v0
    r0.rgb = PS11(r1.rgb * r1.aaa);                                         // mul r0.rgb, r1, r1.aaaa
    r1 = vec4_splat(PS11(dot(t2.rgb, r1.aaa)));                             // dp3 r1.rgba, t2, r1.aaaa
    r0.a = r1.a;                                                            // mov r0.a, r1.a
    r1 = vec4_splat(PS11(dot(PS_BX2(t3.rgb), PS_BX2(v1.rgb))));             // dp3 r1, t3_bx2, v1_bx2
    r1 = PS11(r1 + c0);                                                     // add r1, r1, c0
    r0.rgb = PS11(r0.rgb * r1.rgb);                                         // mul r0.rgb, r0, r1

    PORT_PS_OUTPUT(r0);
}
