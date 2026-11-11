$input a_position
// Depth-only copy of a lit fixed-function draw into the effects' sun shadow map (src/effects/shadows.hpp): the world
// matrix comes through bgfx::setTransform, the shadow view's view and projection through its view transform.
#include "storm_common.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position.xyz, 1.0));
}
