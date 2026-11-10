$input v_color0, v_texcoord0 // same varyings as vs_ui_quad (bgfx matches the hashes)
// Copies a layer texture; u_ui_factor scales all channels (opacity filter).
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
uniform vec4 u_ui_factor;
void main()
{
    gl_FragColor = texture2D(s_ui_tex, v_texcoord0) * u_ui_factor;
}
