$input v_uv
// Scene (+ bloom) -> tone curve -> colour grade. Variants: FX_BLOOM, FX_TONEMAP.
// Alpha receives luma for the FXAA pass.
#include "fx_common.sh"
#include "fx_tonemap.sh"

SAMPLER2D(s_fxScene, 0);
SAMPLER2D(s_fxBloomTex, 1);
SAMPLER3D(s_fxLut, 2);
uniform vec4 u_fxTone; // x: exposure * mid-grey bias, y: curve strength, z: bloom intensity, w: exposure
uniform vec4 u_fxLut;  // x: (N - 1) / N, y: 0.5 / N

void main()
{
    vec3 c = texture2D(s_fxScene, v_uv).rgb;
#if defined(FX_BLOOM) || defined(FX_TONEMAP)
    vec3 lin = fxDecode(c);
    vec3 bloom = vec3_splat(0.0);
#ifdef FX_BLOOM
    bloom = fxBloomDecode(texture2D(s_fxBloomTex, v_uv).rgb) * u_fxTone.z;
#endif
    c = fxEncode(lin * u_fxTone.w + bloom);
#ifdef FX_TONEMAP
    vec3 film = fxAcesFit((fxExpandLdr(lin) + bloom) * u_fxTone.x);
    // The filmic toe would crush the darkest tones (night scenes on a phone in daylight): fade the curve in
    // over the shadows, so it shapes mid-tones and highlights only.
    c = mix(c, fxEncode(film), u_fxTone.y * clamp(fxLuma(lin) * (1.0 / FX_TONE_SHADOW_END), 0.0, 1.0));
#endif
#endif
    c = texture3D(s_fxLut, c * u_fxLut.x + vec3_splat(u_fxLut.y)).rgb;
    gl_FragColor = vec4(c, fxLuma(c));
}
