// Per-pixel lighting upgrade for the fixed-function emulator.
//
// Compile-time feature flags (the emulator adds them to its variant key; with
// none defined this header adds nothing and the legacy per-vertex path is the
// whole lighting model). fx::LightingDefines(settings) returns the set to use.
//   FX_LIGHT_PER_PIXEL    sun diffuse evaluated per pixel
//   FX_LIGHT_HEMISPHERE   sky / ground ambient instead of the flat global ambient
//   FX_LIGHT_SPECULAR     Blinn-Phong highlights
//   FX_LIGHT_TRANSLUCENT  light passing through thin two-sided surfaces (sails, flags)
//   FX_LIGHT_POINT        the enabled D3D point / spot lights evaluated per pixel;
//                         FX_LIGHT_POINT_MAX = 4 or 8 lights per draw call
//   FX_LIGHT_SHADOW       sun shadow map; FX_LIGHT_SHADOW_SOFT = four taps instead of one
//
// Contract with the emulator's vertex shader when FX_LIGHT_PER_PIXEL is on:
//   * pass the view-space normal and position to the fragment shader;
//   * leave the sun (the directional light fx::Lighting was given) out of the
//     per-vertex sum, with FX_LIGHT_HEMISPHERE also the global ambient, and
//     with FX_LIGHT_POINT every point and spot light. Emissive, material alpha
//     and the lights' own ambient terms stay per vertex in v_color0.
// The fragment shader then adds, before the texture stages:
//   v_color0.rgb += matDiffuse * (fxSunDiffuse(N, V) * fxSunShadow(P, N) + fxPointDiffuse(N, P))
//                 + matAmbient * fxAmbient(N)
// and after them:
//   color.rgb += matSpecular * (fxSunSpecular(N, V) * fxSunShadow(P, N) + fxPointSpecular(N, P, V))
// Everything is in the gamma space of the legacy pipeline and uses the D3D9
// formulas, so a surface keeps its fixed-function brightness; only the
// interpolation changes (per pixel instead of per vertex).
//
// Geometry drawn with D3DRS_LIGHTING off and baked vertex colours (static
// location models, see src/libs/lighter) already contains the location's
// lights. For it use FX_LIGHT_POINT alone, select lights with
// include_baked = false (flashes and carried lights only) and add
//   current.rgb += texture.rgb * fxPointDiffuse(N, P)
// so nothing is lit twice and the baked night look is kept.
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

#ifdef FX_LIGHT_POINT
#ifndef FX_LIGHT_POINT_MAX
#define FX_LIGHT_POINT_MAX 4
#endif
// Fraction of the range over which the D3D range cut-off is faded (a hard edge would show per pixel).
#define FX_LIGHT_RANGE_FADE 0.15

// Per light i (fx::PackPointLights), view space:
//   [4i]   xyz: position, w: range
//   [4i+1] rgb: diffuse colour, w: attenuation0
//   [4i+2] x: attenuation1, y: attenuation2, z, w: spot cone as saturate(rho * z + w)
//   [4i+3] xyz: spot direction, w: spot falloff exponent
uniform vec4 u_fxPoint[32];
uniform vec4 u_fxPointCount; // x: number of lights

// Light i at P: xyz = unit vector to the light, w = attenuation * spot factor (D3D9 fixed-function formulas).
vec4 fxPointLight(int i, vec3 P)
{
    vec4 position = u_fxPoint[4 * i];
    vec4 params = u_fxPoint[4 * i + 2];
    vec4 spot = u_fxPoint[4 * i + 3];
    vec3 toLight = position.xyz - P;
    float dist = length(toLight);
    vec3 L = toLight / max(dist, 1e-5);
    float attenuation = 1.0 / max(u_fxPoint[4 * i + 1].w + params.x * dist + params.y * dist * dist, 1e-6);
    attenuation *= clamp((position.w - dist) / (FX_LIGHT_RANGE_FADE * position.w), 0.0, 1.0);
    float cone = clamp(dot(spot.xyz, -L) * params.z + params.w, 0.0, 1.0);
    return vec4(L, attenuation * pow(cone, spot.w));
}

// Sum of the point / spot lights' diffuse terms at P with unit normal N (to be multiplied by the material diffuse).
vec3 fxPointDiffuse(vec3 N, vec3 P)
{
    vec3 sum = vec3_splat(0.0);
    for (int i = 0; i < FX_LIGHT_POINT_MAX; ++i)
    {
        if (float(i) >= u_fxPointCount.x)
            break;
        vec4 light = fxPointLight(i, P);
        sum += u_fxPoint[4 * i + 1].rgb * (max(dot(N, light.xyz), 0.0) * light.w);
    }
    return sum;
}

vec3 fxPointSpecular(vec3 N, vec3 P, vec3 V)
{
    vec3 sum = vec3_splat(0.0);
#ifdef FX_LIGHT_SPECULAR
    for (int i = 0; i < FX_LIGHT_POINT_MAX; ++i)
    {
        if (float(i) >= u_fxPointCount.x)
            break;
        vec4 light = fxPointLight(i, P);
        float ndl = clamp(dot(N, light.xyz) * 4.0, 0.0, 1.0);
        vec3 H = normalize(light.xyz + V);
        sum += u_fxPoint[4 * i + 1].rgb * (pow(max(dot(N, H), 0.0), u_fxLight[0].w) * u_fxLight[1].w * ndl * light.w);
    }
#endif
    return sum;
}
#endif // FX_LIGHT_POINT

#ifdef FX_LIGHT_SHADOW
// Sampler stage 10 is reserved for the shadow map in every shader that defines FX_LIGHT_SHADOW.
SAMPLER2DSHADOW(s_fxShadowMap, 10);
uniform mat4 u_fxShadowMtx; // view space -> shadow map uv and depth
uniform vec4 u_fxShadow;    // x: one texel in uv, y: shadow strength, z: normal offset in view units, w: depth bias

// 1 = fully lit by the sun, 1 - strength = fully shadowed. P and N in view space.
float fxSunShadow(vec3 P, vec3 N)
{
    vec4 c = mul(u_fxShadowMtx, vec4(P + N * u_fxShadow.z, 1.0));
    vec3 coord = vec3(c.xy, c.z - u_fxShadow.w);
#ifdef FX_LIGHT_SHADOW_SOFT
    // Four hardware-filtered taps on a rotated square: a 4x4 texel soft edge.
    vec2 o = u_fxShadow.xx;
    float lit = shadow2D(s_fxShadowMap, coord + vec3(-1.1 * o.x, -0.4 * o.y, 0.0));
    lit += shadow2D(s_fxShadowMap, coord + vec3(0.4 * o.x, -1.1 * o.y, 0.0));
    lit += shadow2D(s_fxShadowMap, coord + vec3(1.1 * o.x, 0.4 * o.y, 0.0));
    lit += shadow2D(s_fxShadowMap, coord + vec3(-0.4 * o.x, 1.1 * o.y, 0.0));
    lit *= 0.25;
#else
    float lit = shadow2D(s_fxShadowMap, coord);
#endif
    // Outside the map (and beyond its far plane) everything is lit; fade out over the last tenth.
    vec2 edge = abs(coord.xy - vec2_splat(0.5)) * 2.0;
    float inside = clamp((1.0 - max(max(edge.x, edge.y), coord.z)) * 10.0, 0.0, 1.0);
    return 1.0 - (1.0 - lit) * u_fxShadow.y * inside;
}
#else
#define fxSunShadow(P, N) 1.0
#endif
#endif
