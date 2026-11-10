$input a_position, a_texcoord0
$output v_uv
// Full-screen triangle; positions are already in clip space.
#include "fx_common.sh"

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_uv = a_texcoord0.xy;
}
