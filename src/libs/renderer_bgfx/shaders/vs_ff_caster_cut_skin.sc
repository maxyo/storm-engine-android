$input a_position, a_weight, a_indices, a_texcoord0, a_texcoord1, a_texcoord2
$output v_texcoord0, v_texcoord1, v_texcoord2
// vs_ff_caster_cut for GPU-skinned draws (as vs_ff_caster_skin).
#include "storm_common.sh"
#include "ff_skin.sh"

void main()
{
    vec4 r0 = blendedRow(0, a_indices.x, a_indices.y, a_weight.x);
    vec4 r1 = blendedRow(1, a_indices.x, a_indices.y, a_weight.x);
    vec4 r2 = blendedRow(2, a_indices.x, a_indices.y, a_weight.x);
    gl_Position = mul(u_modelViewProj, vec4(skinPoint(r0, r1, r2, a_position.xyz), 1.0));
    v_texcoord0 = vec4(a_texcoord0.xy, 0.0, 1.0);
    v_texcoord1 = vec4(a_texcoord1.xy, 0.0, 1.0);
    v_texcoord2 = vec4(a_texcoord2.xy, 0.0, 1.0);
}
