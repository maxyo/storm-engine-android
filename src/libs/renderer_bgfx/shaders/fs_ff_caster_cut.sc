$input v_texcoord0, v_texcoord1, v_texcoord2
// Depth only, like fs_ff_caster, for casters whose material cuts holes: where the colour pass's alpha test would
// discard, nothing is cast.
#include "storm_common.sh"
#include "ff_caster_cut.sh"

void main()
{
    if (ffCasterCut())
        discard;
    gl_FragColor = vec4_splat(0.0);
}
