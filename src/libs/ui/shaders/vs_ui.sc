$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0
// RmlUi geometry: pixel positions, translation, then projection * element transform.
#include <bgfx_shader.sh>
uniform mat4 u_ui_transform;
uniform vec4 u_ui_translate;
void main()
{
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    gl_Position = mul(u_ui_transform, vec4(a_position + u_ui_translate.xy, 0.0, 1.0));
}
