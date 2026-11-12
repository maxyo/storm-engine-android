$input v_uv
// Contrast adaptive sharpening, five-tap cross variant; with a bilinear
// source it also upscales a smaller render (tools/fx-preview render_scale).
// Derived from AMD FidelityFX CAS (ffx_cas.h).
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc., MIT License; see ../THIRD_PARTY.md.
#include "fx_common.sh"

SAMPLER2D(s_fxScene, 0);
uniform vec4 u_fxTexel; // xy: one source texel in uv, z: sharpness 0..1

void main()
{
    vec2 t = u_fxTexel.xy;
    vec3 e = texture2D(s_fxScene, v_uv).rgb;
    vec3 b = texture2D(s_fxScene, v_uv + vec2(0.0, -t.y)).rgb;
    vec3 d = texture2D(s_fxScene, v_uv + vec2(-t.x, 0.0)).rgb;
    vec3 f = texture2D(s_fxScene, v_uv + vec2(t.x, 0.0)).rgb;
    vec3 h = texture2D(s_fxScene, v_uv + vec2(0.0, t.y)).rgb;
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    vec3 mx = max(max(max(d, e), max(f, b)), h);
    // Sharpen less where the local contrast is already high.
    vec3 amp = sqrt(clamp(min(mn, vec3_splat(1.0) - mx) / max(mx, vec3_splat(1e-4)), 0.0, 1.0));
    float peak = -1.0 / mix(8.0, 5.0, u_fxTexel.z);
    vec3 w = amp * peak;
    vec3 c = ((b + d + f + h) * w + e) / (vec3_splat(1.0) + 4.0 * w);
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
