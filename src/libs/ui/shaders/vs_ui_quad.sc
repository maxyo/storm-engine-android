$input a_position, a_color0, a_texcoord0
$output v_color0, v_texcoord0
// Full-target passes over layer textures: positions are already in clip space.
#include <bgfx_shader.sh>
void main()
{
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
