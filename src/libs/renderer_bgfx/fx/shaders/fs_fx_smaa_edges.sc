$input v_uv, v_smaa0, v_smaa1, v_smaa2
// SMAA pass 1: luma edges of the picture into an RG target cleared to zero (pixels without an edge discard).
#define SMAA_INCLUDE_VS 0
#include "fx_smaa.sh"

SAMPLER2D(s_fxScene, 0);

void main()
{
    vec4 offset[3];
    offset[0] = v_smaa0;
    offset[1] = v_smaa1;
    offset[2] = v_smaa2;
    gl_FragColor = vec4(SMAALumaEdgeDetectionPS(v_uv, offset, s_fxScene), 0.0, 0.0);
}
