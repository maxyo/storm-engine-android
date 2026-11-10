$input v_uv
// Sun shafts: radial blur of the blurred bright pass towards the sun, added
// into the bloom chain (additive blend state) at low resolution.
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxShafts; // xy: sun position in uv, z: intensity, w: length (fraction of the way to the sun)

void main()
{
    vec2 delta = (u_fxShafts.xy - v_uv) * (u_fxShafts.w / 12.0);
    vec2 uv = v_uv;
    vec3 sum = vec3_splat(0.0);
    float weight = 1.0;
    for (int i = 0; i < 12; ++i)
    {
        uv += delta;
        sum += texture2D(s_fxScene, uv).rgb * weight;
        weight *= 0.88;
    }
    // Fade with the distance from the sun so the effect stays a halo of rays.
    float falloff = clamp(1.0 - length((u_fxShafts.xy - v_uv) * vec2(1.0, 0.6)), 0.0, 1.0);
    gl_FragColor = vec4(sum * (u_fxShafts.z * falloff * falloff / 6.4), 1.0);
}
