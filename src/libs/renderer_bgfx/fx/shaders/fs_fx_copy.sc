$input v_uv
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);

void main()
{
    gl_FragColor = vec4(texture2D(s_fxScene, v_uv).rgb, 1.0);
}
