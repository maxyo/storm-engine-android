$input v_uv
// Scene (+ ambient occlusion, bloom) -> tone curve -> colour grade. Variants: FX_BLOOM, FX_TONEMAP, FX_SSAO.
// Alpha receives luma for the FXAA pass.
#include "fx_common.sh"
#include "fx_tonemap.sh"

SAMPLER2D(s_fxScene, 0);
SAMPLER2D(s_fxBloomTex, 1);
SAMPLER3D(s_fxLut, 2);
#ifdef FX_SSAO
SAMPLER2D(s_fxAoTex, 3);
SAMPLER2D(s_fxSceneDepth, 4);
// [0] x: near * far, y: far, z: far - near  [1] w: 1 / far  [2] zw: one occlusion texel in uv (see fs_fx_ssao.sc)
uniform vec4 u_fxAo[3];
#endif
uniform vec4 u_fxTone; // x: exposure * mid-grey bias, y: curve strength, z: bloom intensity, w: exposure
uniform vec4 u_fxLut;  // x: (N - 1) / N, y: 0.5 / N
uniform vec4 u_fxDither; // x: amplitude (0 = off), y: noise seed (changes every frame)

#ifdef FX_SSAO
// x: occlusion * weight, y: weight. A tap counts as far as its depth matches the pixel's depth z (both / far).
vec4 fxAoTap(vec2 uv, float z)
{
    vec3 tap = texture2D(s_fxAoTex, uv).rgb;
    float w = 1.0 / (1e-3 + abs(tap.g + tap.b * (1.0 / 255.0) - z) * 400.0 / max(z, 1e-4));
    return vec4(tap.r * w, w, 0.0, 0.0);
}
#endif

void main()
{
    vec3 c = texture2D(s_fxScene, v_uv).rgb;
#if defined(FX_BLOOM) || defined(FX_TONEMAP) || defined(FX_SSAO)
    vec3 lin = fxDecode(c);
#ifdef FX_SSAO
    // Depth-aware upsample: four half-resolution taps, each trusted as far as its depth matches this pixel's.
    float z = u_fxAo[0].x / (u_fxAo[0].y - texture2D(s_fxSceneDepth, v_uv).r * u_fxAo[0].z) * u_fxAo[1].w;
    vec2 o = u_fxAo[2].zw;
    vec4 ao = fxAoTap(v_uv + vec2(-o.x, -o.y), z) + fxAoTap(v_uv + vec2(o.x, -o.y), z) + fxAoTap(v_uv + vec2(-o.x, o.y), z) +
              fxAoTap(v_uv + o, z);
    lin *= ao.x / ao.y;
#endif
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
    // Dither: keeps smooth gradients free of banding.
    c += vec3_splat((fxNoise(gl_FragCoord.xy + vec2_splat(u_fxDither.y)) - 0.5) * u_fxDither.x);
    gl_FragColor = vec4(c, fxLuma(c));
}
