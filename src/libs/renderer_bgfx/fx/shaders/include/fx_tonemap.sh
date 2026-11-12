// Tone curve of the composite pass. Mirrored on the CPU in src/color_lut.cpp
// (ExpandLdr / AgxCurve); change both together.
#ifndef FX_TONEMAP_SH
#define FX_TONEMAP_SH

// The legacy scene is LDR. Give it highlight headroom before bloom is added: inverse of an extended Reinhard
// shoulder, 1.0 -> 3.0, on the luminance (per channel it would saturate bright colours).
vec3 fxExpandLdr(vec3 c)
{
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return c * (1.0 / (1.0 - 0.6666667 * min(y, 1.0)));
}

// AgX tone curve on a grey value (linear in, linear out): the log2 encoding between -12.47 and +4.03 EV around
// middle grey of Blender's AgX (Troy Sobotka) and its sigmoid, the polynomial fit of Benjamin Wrensch's "Minimal
// AgX" (iolite-engine.com), as in three.js (MIT, Copyright (c) 2010-2023 three.js authors; see ../../THIRD_PARTY.md).
// On grey the AgX inset and outset matrices are the identity, so the curve of the luminance is exact.
float fxAgx(float y)
{
    float x = clamp((log2(max(y, 1e-10)) + 12.47393) * (1.0 / 16.5), 0.0, 1.0);
    float x2 = x * x;
    float x4 = x2 * x2;
    float s = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
    return pow(max(s, 0.0), 2.2);
}

// The tone curve on luminance only: hue and saturation stay the game's (a per-channel curve saturates colours,
// against the restrained grade). A colour pushed past white is pulled towards its grey instead of clipped.
vec3 fxToneLuma(vec3 c)
{
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float t = fxAgx(y);
    c *= t / max(y, 1e-6);
    float peak = max(c.r, max(c.g, c.b));
    float keep = peak > 1.0 ? (1.0 - t) / max(peak - t, 1e-6) : 1.0;
    return vec3_splat(t) + (c - vec3_splat(t)) * keep;
}

// Linear luminance below which the tone curve fades out (see fs_fx_composite.sc).
#define FX_TONE_SHADOW_END 0.12

// Bloom targets are RGBA8: sqrt(colour / range).
#define FX_BLOOM_RANGE 4.0
vec3 fxBloomEncode(vec3 c)
{
    return sqrt(clamp(c * (1.0 / FX_BLOOM_RANGE), 0.0, 1.0));
}
vec3 fxBloomDecode(vec3 c)
{
    return c * c * FX_BLOOM_RANGE;
}
#endif
