$input v_uv
// fx::DepthCopy: the scene depth into another depth target (nearest; the colour is not written).
#include "fx_common.sh"

SAMPLER2D(s_fxSceneDepth, 1);

void main()
{
    gl_FragDepth = texture2D(s_fxSceneDepth, v_uv).r;
}
