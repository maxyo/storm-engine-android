// Depth-only pass into the sun shadow map: colour writes are disabled by the render state.
#include "fx_common.sh"

void main()
{
    gl_FragColor = vec4_splat(0.0);
}
