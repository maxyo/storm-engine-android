$input a_position, a_texcoord0, a_texcoord1, a_texcoord2
$output v_texcoord0, v_texcoord1, v_texcoord2
// vs_ff_caster for casters whose material cuts holes (foliage, nets, gratings, rigging, torn sails): the texture
// coordinates go along; fs_ff_caster_cut / fs_ff_lamp_caster_cut test the textures' alpha (ff_caster_cut.sh).
#include "storm_common.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position.xyz, 1.0));
    v_texcoord0 = vec4(a_texcoord0.xy, 0.0, 1.0);
    v_texcoord1 = vec4(a_texcoord1.xy, 0.0, 1.0);
    v_texcoord2 = vec4(a_texcoord2.xy, 0.0, 1.0);
}
