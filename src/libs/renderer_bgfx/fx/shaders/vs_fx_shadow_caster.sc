$input a_position
// Depth-only pass into the sun shadow map. The caller sets the model matrix (bgfx::setTransform).
#include "fx_common.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
