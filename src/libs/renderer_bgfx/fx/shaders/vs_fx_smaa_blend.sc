$input a_position, a_texcoord0
$output v_uv, v_smaa0
// Full-screen triangle for the SMAA neighbourhood blending pass (its outputs must match fs_fx_smaa_blend's inputs).
#define SMAA_INCLUDE_PS 0
#include "fx_smaa.sh"

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_uv = a_texcoord0.xy;
    SMAANeighborhoodBlendingVS(v_uv, v_smaa0);
}
