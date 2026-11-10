$input v_color0, v_texcoord0 // same varyings as vs_ui_quad (bgfx matches the hashes)
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
SAMPLER2D(s_ui_mask, 1);
void main()
{
    gl_FragColor = texture2D(s_ui_tex, v_texcoord0) * texture2D(s_ui_mask, v_texcoord0).a;
}
