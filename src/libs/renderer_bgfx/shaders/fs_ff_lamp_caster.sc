// Lamp shadow faces (src/effects/lamp_shadows.hpp, vs_ff_caster*): the depth is the distance to the lamp divided by
// the lamp's radius, the same in every face and linear, so 16 bits resolve a fraction of a millimetre in a room and
// the receiver's bias is in metres. The distance comes from the face's own projection: w is the distance along the
// face axis, the position inside the tile gives the other two.
#include "storm_common.sh"

// x: face size in pixels (the atlas is a grid of faces), y: 1 / lamp radius, z: metres around the lamp in which
// nothing casts (its own holder), w: fx::kLampFaceGuard
uniform vec4 u_fxLampCaster;

void main()
{
    vec2 tile = fract(gl_FragCoord.xy / u_fxLampCaster.x) * 2.0 - vec2_splat(1.0);
    float reach = sqrt(1.0 + dot(tile, tile) * u_fxLampCaster.w * u_fxLampCaster.w) / gl_FragCoord.w;
    if (reach < u_fxLampCaster.z)
        discard;
    gl_FragColor = vec4_splat(0.0);
    gl_FragDepth = clamp(reach * u_fxLampCaster.y, 0.0, 1.0);
}
