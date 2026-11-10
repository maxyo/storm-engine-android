// Sky panorama, sun, clouds and matching fog. The panorama is baked on the CPU
// by fx::ComputeSky (single scattering) and bound by fx::Sky::Bind().
// Sampler stages 5 and 6 are reserved for it in every shader that includes this.
#ifndef FX_ATMOSPHERE_SH
#define FX_ATMOSPHERE_SH
#include "fx_common.sh"

SAMPLER2D(s_fxSkyLut, 5);
SAMPLER2D(s_fxCloudNoise, 6);

// [0] xyz: unit vector to the sun (world), w: cosine of the solar disc radius
// [1] xy: unit horizontal direction to the sun, z: fog density, w: cloud cover
// [2] rgb: halo colour around the sun, w: cloud layer scale (uv per unit of d.xz / d.y)
// [3] rgb: solar disc colour, w: dither amplitude (0 = off)
// [4] rgb: sun-lit cloud colour, w: cloud drift u
// [5] rgb: shaded cloud colour, w: cloud drift v
uniform vec4 u_fxSky[6];

// Linear sky colour in the unit direction d (clear sky, no sun disc).
vec3 fxSkyColor(vec3 d)
{
    vec2 h = d.xz * inversesqrt(max(dot(d.xz, d.xz), 1e-6));
    float u = 0.5 - 0.5 * dot(h, u_fxSky[1].xy);
    float v = sqrt(max(d.y, 0.0));
    vec2 uv = vec2(u * (63.0 / 64.0) + 0.5 / 64.0, v * (31.0 / 32.0) + 0.5 / 32.0);
    return fxDecode(texture2DLod(s_fxSkyLut, uv, 0.0).rgb);
}

// Forward-scattering halo: a tight aureole plus a wide warm veil.
vec3 fxSunHalo(vec3 d)
{
    float mu = max(dot(d, u_fxSky[0].xyz), 0.0);
    float mu8 = mu * mu;
    mu8 *= mu8;
    mu8 *= mu8;
    float mu64 = mu8 * mu8;
    mu64 *= mu64;
    mu64 *= mu64;
    return u_fxSky[2].rgb * (0.10 * mu8 + 0.45 * mu64 * mu64);
}

// Blends a procedural cloud deck over `sky`. Three fetches.
vec3 fxClouds(vec3 d, vec3 sky)
{
    float up = max(d.y, 0.0);
    vec2 plane = d.xz / (up + 0.12);
    vec2 uv = plane * u_fxSky[2].w + vec2(u_fxSky[4].w, u_fxSky[5].w);
    vec4 big = texture2D(s_fxCloudNoise, uv);
    vec4 detail = texture2D(s_fxCloudNoise, uv * 3.1 + vec2(u_fxSky[5].w, u_fxSky[4].w) * 1.7);
    float shape = big.r * 0.70 + big.g * 0.18 + detail.g * 0.12 + (detail.b - 0.5) * 0.06;
    float edge = 0.80 - 0.66 * u_fxSky[1].w;
    float density = smoothstep(edge, edge + 0.22, shape);
    // Self shadowing: compare with the coverage a little further towards the sun.
    float towardsSun = texture2D(s_fxCloudNoise, uv + u_fxSky[1].xy * 0.035).a;
    float lit = clamp(0.62 + (big.a - towardsSun) * 5.0 + (1.0 - density) * 0.45, 0.0, 1.0);
    vec3 cloud = mix(u_fxSky[5].rgb, u_fxSky[4].rgb, lit);
    // Silver lining next to the sun, haze towards the horizon.
    cloud += fxSunHalo(d) * (1.0 - density) * 1.5;
    float alpha = density * smoothstep(0.0, 0.22, up);
    return mix(sky, cloud, alpha);
}

// Fog that matches the sky: geometry fades into the sky colour of the horizon
// in the viewing direction. `toEye` is the unit vector from the surface to the camera.
vec3 fxApplyFog(vec3 color, vec3 toEye, float dist)
{
    float f = exp(-u_fxSky[1].z * dist);
    vec3 fog = fxSkyColor(vec3(-toEye.x, 0.0, -toEye.z));
    return mix(fog, color, f);
}
#endif
