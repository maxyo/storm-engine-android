$input v_color0, v_texcoord0 // same varyings as vs_ui_quad (bgfx matches the hashes)
// RGB colour matrix in premultiplied space; alpha is kept.
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
uniform mat4 u_ui_color_matrix;
void main()
{
    vec4 texel = texture2D(s_ui_tex, v_texcoord0);
    gl_FragColor = vec4(mul(u_ui_color_matrix, texel).rgb, texel.a);
}
