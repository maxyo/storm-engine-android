$input v_uv, v_smaa0, v_smaa1, v_smaa2
// SMAA pass 2: blending weights from the edges and the reference area and search textures.
#define SMAA_INCLUDE_VS 0
#include "fx_smaa.sh"

SAMPLER2D(s_fxSmaaEdges, 0);
SAMPLER2D(s_fxSmaaArea, 1);
SAMPLER2D(s_fxSmaaSearch, 2);

void main()
{
    vec4 offset[3];
    offset[0] = v_smaa0;
    offset[1] = v_smaa1;
    offset[2] = v_smaa2;
    gl_FragColor = SMAABlendingWeightCalculationPS(v_uv, v_uv * SMAA_RT_METRICS.zw, offset, s_fxSmaaEdges, s_fxSmaaArea,
                                                   s_fxSmaaSearch, vec4_splat(0.0));
}
