$input v_uv
// Dual-filter (Kawase) upsample: eight taps on a diamond.
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxBloom; // xy: half a source texel in uv

void main()
{
    vec2 o = u_fxBloom.xy;
    vec3 c = texture2D(s_fxScene, v_uv + vec2(-2.0 * o.x, 0.0)).rgb;
    c += texture2D(s_fxScene, v_uv + vec2(2.0 * o.x, 0.0)).rgb;
    c += texture2D(s_fxScene, v_uv + vec2(0.0, -2.0 * o.y)).rgb;
    c += texture2D(s_fxScene, v_uv + vec2(0.0, 2.0 * o.y)).rgb;
    c += 2.0 * texture2D(s_fxScene, v_uv + vec2(-o.x, o.y)).rgb;
    c += 2.0 * texture2D(s_fxScene, v_uv + o).rgb;
    c += 2.0 * texture2D(s_fxScene, v_uv + vec2(-o.x, -o.y)).rgb;
    c += 2.0 * texture2D(s_fxScene, v_uv + vec2(o.x, -o.y)).rgb;
    gl_FragColor = vec4(c * (1.0 / 12.0), 1.0);
}
