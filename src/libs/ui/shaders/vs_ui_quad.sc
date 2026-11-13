$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0
// Full-target passes over layer textures: positions are already in clip space.
#include <bgfx_shader.sh>
uniform vec4 u_ui_clip; // x: 1, or -1 to draw the target upside down (backend_bgfx.cpp, Executor::SetClip)
void main()
{
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    gl_Position = vec4(a_position.x, a_position.y * u_ui_clip.x, 0.0, 1.0);
}
