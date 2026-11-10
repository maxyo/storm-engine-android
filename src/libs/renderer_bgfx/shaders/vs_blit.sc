$input a_position, a_texcoord0
$output v_texcoord0
#include "storm_common.sh"
uniform vec4 u_blitUv; // xy: texture coordinate at clip (-1, +1), zw: its change to clip (+1, -1)
void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texcoord0 = vec4(u_blitUv.xy + a_texcoord0.xy * u_blitUv.zw, 0.0, 1.0);
}
