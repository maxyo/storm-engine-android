$input v_uv
// Dual-filter (Kawase) downsample: centre + four diagonal taps.
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxBloom; // xy: half a source texel in uv

void main()
{
    vec2 o = u_fxBloom.xy;
    vec3 c = texture2D(s_fxScene, v_uv).rgb * 4.0;
    c += texture2D(s_fxScene, v_uv + vec2(-o.x, -o.y)).rgb;
    c += texture2D(s_fxScene, v_uv + vec2(o.x, -o.y)).rgb;
    c += texture2D(s_fxScene, v_uv + vec2(-o.x, o.y)).rgb;
    c += texture2D(s_fxScene, v_uv + o).rgb;
    gl_FragColor = vec4(c * 0.125, 1.0);
}
