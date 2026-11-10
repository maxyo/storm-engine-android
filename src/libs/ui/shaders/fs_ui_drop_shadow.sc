$input v_color0, v_texcoord0 // same varyings as vs_ui_quad (bgfx matches the hashes)
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
uniform vec4 u_ui_region; // xy: min texcoord, zw: max texcoord
uniform vec4 u_ui_shadow_color;
void main()
{
    vec2 in_region = step(u_ui_region.xy, v_texcoord0) * step(v_texcoord0, u_ui_region.zw);
    gl_FragColor = texture2D(s_ui_tex, v_texcoord0).a * in_region.x * in_region.y * u_ui_shadow_color;
}
