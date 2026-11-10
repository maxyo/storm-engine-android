$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of src/techniques/weather/sea_sea2foam_pixel_shader.pso (ps_1_4, FNV-1a 62cad3c5d353e242).
// Stage 0: foam texture (2D), stage 4: animated bump volume texture.
#include "port_ps.sh"
SAMPLER2D(s_tex0, 0);
SAMPLER3D(s_vol4, 12);

void main()
{
    vec4 c0 = PS_CONST(u_psc[0]);
    vec4 r0 = vec4_splat(0.0);
    vec4 r1 = vec4_splat(0.0);
    vec4 r2 = vec4_splat(0.0);
    vec4 r3 = vec4_splat(0.0);
    vec4 r4 = vec4_splat(0.0);
    vec4 r5 = vec4_splat(0.0);

    r0.rgb = PS14(v_texcoord0.xyz);                 // texcrd r0.rgb, t0
    r1.rgb = PS14(v_texcoord1.xyz);                 // texcrd r1.rgb, t1
    r2.rgb = PS14(v_texcoord2.xyz);                 // texcrd r2.rgb, t2
    r3.rgb = PS14(v_texcoord3.xyz);                 // texcrd r3.rgb, t3
    r4 = texture3D(s_vol4, v_texcoord4.xyz);        // texld r4, t4
    r5.r = PS14(dot(r0.rgb, PS_BX2(r4.rgb)));       // dp3 r5.r, r0, r4_bx2
    r5.g = PS14(dot(r1.rgb, PS_BX2(r4.rgb)));       // dp3 r5.g, r1, r4_bx2
    r5.b = PS14(dot(r2.rgb, PS_BX2(r4.rgb)));       // dp3 r5.b, r2, r4_bx2
    r5.rgb = PS14(r5.rgb * c0.rrr);                 // mul r5.rgb, r5, c0.rrr   (r5 is never read again)
    r1.rgb = r3.rgb;                                // mov r1.rgb, r3
                                                    // phase
    r0 = texture2D(s_tex0, r1.xy);                  // texld r0, r1
    r0.a = r3.b;                                    // mov r0.a, r3.b

    PORT_PS_OUTPUT(r0);
}
