$input a_position, a_normal, a_texcoord0
$output v_wpos, v_misc
// Procedural sprites: lens flare elements (clip-space quads) and contact shadow blobs (world-space quads,
// variant FX_SPRITE_WORLD). a_normal carries the colour, a_texcoord0 = (u, v in -1..1, shape).
#include "fx_common.sh"

void main()
{
#ifdef FX_SPRITE_WORLD
    gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
#else
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
#endif
    v_wpos = a_normal;
    v_misc = vec4(a_texcoord0, 0.0);
}
