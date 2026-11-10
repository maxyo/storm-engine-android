$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of the technique shader postprocess_shader (ps_1_1) of src/techniques/postprocess/postprocess.fx.
// Stages 0..3: the same 2D render target sampled with four offset coordinate sets (blur).
#include "port_ps.sh"
SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);

void main()
{
    vec4 c1 = vec4(0.25, 0.25, 0.25, 0.25);                                 // def c1, 0.25, 0.25, 0.25, 0.25
    vec4 r0 = vec4_splat(0.0);                                              // (def c2, 1, 1, 1, 1 is unused)

    vec4 t0 = texture2D(s_tex0, v_texcoord0.xy);                            // tex t0
    vec4 t1 = texture2D(s_tex1, v_texcoord1.xy);                            // tex t1
    vec4 t2 = texture2D(s_tex2, v_texcoord2.xy);                            // tex t2
    vec4 t3 = texture2D(s_tex3, v_texcoord3.xy);                            // tex t3
    r0 = PS11(t0 * c1);                                                     // mul r0, t0, c1
    r0 = PS11(t1 * c1 + r0);                                                // mad r0, t1, c1, r0
    r0 = PS11(t2 * c1 + r0);                                                // mad r0, t2, c1, r0
    r0 = PS11(t3 * c1 + r0);                                                // mad r0, t3, c1, r0

    PORT_PS_OUTPUT(r0);
}
