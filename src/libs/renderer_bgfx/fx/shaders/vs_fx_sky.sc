$input a_position, a_texcoord0
$output v_dir
// Full-screen triangle on the far plane; a_texcoord0 is the world-space view ray of each corner.
#include "fx_common.sh"

void main()
{
    gl_Position = vec4(a_position.xy, 1.0, 1.0);
    v_dir = a_texcoord0;
}
