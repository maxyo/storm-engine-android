$input v_texcoord0, v_texcoord1, v_texcoord2
// fs_ff_lamp_caster for casters whose material cuts holes: where the colour pass's alpha test would discard, nothing
// is cast.
#include "storm_common.sh"
#include "ff_caster_cut.sh"

uniform vec4 u_fxLampCaster; // as fs_ff_lamp_caster

void main()
{
    vec2 tile = fract(gl_FragCoord.xy / u_fxLampCaster.x) * 2.0 - vec2_splat(1.0);
    float reach = sqrt(1.0 + dot(tile, tile) * u_fxLampCaster.w * u_fxLampCaster.w) / gl_FragCoord.w;
    if (reach < u_fxLampCaster.z || ffCasterCut())
        discard;
    gl_FragColor = vec4_splat(0.0);
    gl_FragDepth = clamp(reach * u_fxLampCaster.y, 0.0, 1.0);
}
