$input v_color0, v_texcoord0
// Textures with straight alpha (game .tx pictures from the frame owner).
#include <bgfx_shader.sh>
SAMPLER2D(s_ui_tex, 0);
void main()
{
    vec4 texel = texture2D(s_ui_tex, v_texcoord0);
    gl_FragColor = v_color0 * vec4(texel.rgb * texel.a, texel.a);
}
