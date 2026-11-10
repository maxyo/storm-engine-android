// Per-pixel lighting upgrade for the fixed-function emulator.
//
// Compile-time feature flags (the emulator adds them to its variant key; with
// none defined this header adds nothing and the legacy per-vertex path is the
// whole lighting model):
//   FX_LIGHT_PER_PIXEL    sun diffuse evaluated per pixel
//   FX_LIGHT_HEMISPHERE   sky / ground ambient instead of the flat global ambient
//   FX_LIGHT_SPECULAR     Blinn-Phong highlight from the sun
//   FX_LIGHT_TRANSLUCENT  light passing through thin two-sided surfaces (sails, flags)
//
// Contract with the emulator's vertex shader when FX_LIGHT_PER_PIXEL is on:
//   * pass the view-space normal and position to the fragment shader;
//   * leave the sun (the directional light fx::Lighting was given) out of the
//     per-vertex sum, and with FX_LIGHT_HEMISPHERE also the global ambient.
//     Point lights, emissive and material alpha stay per vertex in v_color0.
// The fragment shader then adds, before the texture stages:
//   v_color0.rgb += matDiffuse * fxSunDiffuse(N, V) + matAmbient * fxAmbient(N)
// and after them:
//   color.rgb += matSpecular * fxSunSpecular(N, V)
// Everything is in the gamma space of the legacy pipeline, so a flat surface
// facing the sun keeps exactly its fixed-function brightness.
#ifndef FX_LIGHTING_SH
#define FX_LIGHTING_SH

// [0] xyz: unit vector to the sun in VIEW space, w: specular power
// [1] rgb: sun diffuse colour (D3DLIGHT9.Diffuse), w: specular strength
// [2] xyz: world up in VIEW space, w: translucency 0..1
// [3] rgb: ambient from the sky, w: wrap 0..1 (softens the terminator)
// [4] rgb: ambient from the ground / sea
uniform vec4 u_fxLight[5];

#ifdef FX_LIGHT_PER_PIXEL
// N: unit view-space normal facing the viewer side, V: unit vector to the eye.
vec3 fxSunDiffuse(vec3 N, vec3 V)
{
    float ndl = dot(N, u_fxLight[0].xyz);
    float wrap = u_fxLight[3].w;
    float lit = clamp((ndl + wrap) / (1.0 + wrap), 0.0, 1.0);
#ifdef FX_LIGHT_TRANSLUCENT
    // Light entering the far side glows through, strongest when looking towards the sun.
    float through = clamp(-ndl, 0.0, 1.0) * (0.55 + 0.45 * clamp(-dot(V, u_fxLight[0].xyz), 0.0, 1.0));
    lit += through * u_fxLight[2].w;
#endif
    return u_fxLight[1].rgb * lit;
}

vec3 fxAmbient(vec3 N)
{
#ifdef FX_LIGHT_HEMISPHERE
    float up = 0.5 + 0.5 * dot(N, u_fxLight[2].xyz);
    return mix(u_fxLight[4].rgb, u_fxLight[3].rgb, up);
#else
    return vec3_splat(0.0);
#endif
}

vec3 fxSunSpecular(vec3 N, vec3 V)
{
#ifdef FX_LIGHT_SPECULAR
    vec3 H = normalize(u_fxLight[0].xyz + V);
    float ndl = clamp(dot(N, u_fxLight[0].xyz) * 4.0, 0.0, 1.0);
    return u_fxLight[1].rgb * (pow(max(dot(N, H), 0.0), u_fxLight[0].w) * u_fxLight[1].w * ndl);
#else
    return vec3_splat(0.0);
#endif
}
#endif // FX_LIGHT_PER_PIXEL
#endif
