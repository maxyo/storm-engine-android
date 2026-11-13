$input a_position, a_weight, a_indices, i_data0
// vs_ff_caster for GPU-skinned draws (GEOS::AVERTEX0, bone palette at kBonePaletteUnit as in vs_ff*_skin).
#include "storm_common.sh"
#include "ff_skin.sh"

void main()
{
    vec4 r0 = blendedRow(i_data0.xy, 0, a_indices.x, a_indices.y, a_weight.x);
    vec4 r1 = blendedRow(i_data0.xy, 1, a_indices.x, a_indices.y, a_weight.x);
    vec4 r2 = blendedRow(i_data0.xy, 2, a_indices.x, a_indices.y, a_weight.x);
    gl_Position = mul(u_modelViewProj, vec4(skinPoint(r0, r1, r2, a_position.xyz), 1.0));
}
