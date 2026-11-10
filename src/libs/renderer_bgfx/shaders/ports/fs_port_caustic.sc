$input v_color0, v_color1, v_texcoord0, v_texcoord1, v_texcoord2, v_texcoord3, v_texcoord4, v_fog
// Port of src/techniques/weather/caustic_pixel_shader.pso (ps_1_1, FNV-1a f1f78a40cf8ffd66).
// Stages 0, 1, 2: 2D textures (model texture alpha, two caustic frames).
#include "port_ps.sh"
SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);

void main()
{
    vec4 c0 = vec4(0.5, 0.5, 0.5, 0.0);                                     // def c0, 0.5, 0.5, 0.5, 0
    vec4 c1 = vec4(1.0, 1.0, 1.0, 0.0);                                     // def c1, 1, 1, 1, 0
    vec4 v0 = v_color0;
    vec4 r0 = vec4_splat(0.0);

    vec4 t0 = texture2D(s_tex0, v_texcoord0.xy);                            // tex t0
    vec4 t1 = texture2D(s_tex1, v_texcoord1.xy);                            // tex t1
    vec4 t2 = texture2D(s_tex2, v_texcoord2.xy);                            // tex t2
    // ps_1_1 texcoord yields (u, v, w, 1) clamped to [0, 1]: the w written by the vertex shader is not visible.
    vec4 t3 = vec4(clamp(v_texcoord3.xyz, 0.0, 1.0), 1.0);                  // texcoord t3
    r0 = t3;                                                                // mov r0, t3
    r0 = PS11(r0 * t2 + (vec4_splat(1.0) - r0) * t1);                       // lrp r0, r0, t2, t1
    r0 = PS11(r0 * v0);                                                     // mul r0, r0, v0
    r0.rgb = PS11(r0.rgb - c0.rgb);                                         // sub r0.rgb, r0, c0
    r0.rgb = PS11(r0.rgb * c1.rgb);                                         // mul r0.rgb, r0, c1
    r0.a = PS11(t0.a * t3.a);                                               // mul r0.a, t0.a, t3.a

    PORT_PS_OUTPUT(r0);
}
