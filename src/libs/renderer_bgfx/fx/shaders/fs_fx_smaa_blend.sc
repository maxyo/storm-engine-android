$input v_uv, v_smaa0
// SMAA pass 3: the picture blended with its neighbours by the weights (in gamma space: the targets are RGBA8).
#define SMAA_INCLUDE_VS 0
#include "fx_smaa.sh"

SAMPLER2D(s_fxScene, 0);
SAMPLER2D(s_fxSmaaWeights, 1);

void main()
{
    gl_FragColor = vec4(SMAANeighborhoodBlendingPS(v_uv, v_smaa0, s_fxScene, s_fxSmaaWeights).rgb, 1.0);
}
