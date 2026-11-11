$input v_uv
// Bright pass + first downsample (to 1/2 or 1/4 resolution).
#include "fx_common.sh"
#include "fx_tonemap.sh"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxBloom; // xy: tap offset in uv, z: threshold, w: knee width below the threshold
uniform vec4 u_fxBloomRange; // x: 1 = expand the LDR highlights to HDR (with the tone curve), 0 = glow as they are

vec3 tap(vec2 uv)
{
    return fxDecode(texture2D(s_fxScene, uv).rgb);
}

void main()
{
    vec2 o = u_fxBloom.xy;
    vec3 c = 0.25 * (tap(v_uv + vec2(-o.x, -o.y)) + tap(v_uv + vec2(o.x, -o.y)) + tap(v_uv + vec2(-o.x, o.y)) + tap(v_uv + o));
    // Soft-knee threshold on the brightest channel of the scene's own (LDR) colour, so saturated lights
    // (lanterns, a low sun) glow as readily as white ones.
    float l = max(c.r, max(c.g, c.b));
    float start = u_fxBloom.z - u_fxBloom.w;
    float w = clamp((l - start) / (1.0 - start), 0.0, 1.0);
    w *= w;
    // Without the tone curve the picture stays LDR: white walls, sails or the sea seen from below glow as white,
    // not as three times white (a halo around every bright surface).
    gl_FragColor = vec4(fxBloomEncode(mix(c, fxExpandLdr(c), u_fxBloomRange.x) * w), 1.0);
}
