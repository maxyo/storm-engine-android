$input a_position, a_texcoord0
$output v_uv, v_smaa0, v_smaa1, v_smaa2
// Full-screen triangle for the first two SMAA passes; the offsets come from the reference vertex shaders
// (fx_smaa.sh). FX_SMAA_EDGES or FX_SMAA_WEIGHTS picks the pass (the blend pass: vs_fx_smaa_blend.sc).
#define SMAA_INCLUDE_PS 0
#include "fx_smaa.sh"

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_uv = a_texcoord0.xy;
#if defined(FX_SMAA_EDGES)
    vec4 offset[3];
    SMAAEdgeDetectionVS(v_uv, offset);
    v_smaa0 = offset[0];
    v_smaa1 = offset[1];
    v_smaa2 = offset[2];
#else
    vec4 offset[3];
    vec2 pixcoord;
    SMAABlendingWeightCalculationVS(v_uv, pixcoord, offset);
    v_smaa0 = offset[0];
    v_smaa1 = offset[1];
    v_smaa2 = offset[2];
#endif
}
