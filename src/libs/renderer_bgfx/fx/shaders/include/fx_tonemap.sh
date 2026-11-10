// Tone curve of the composite pass. Mirrored on the CPU in src/color_lut.cpp
// (ExpandLdr / AcesFit); change both together.
#ifndef FX_TONEMAP_SH
#define FX_TONEMAP_SH

// The legacy scene is LDR. Give it highlight headroom before bloom is added:
// inverse of an extended Reinhard shoulder, 1.0 -> 3.0.
vec3 fxExpandLdr(vec3 c)
{
    return c / (vec3_splat(1.0) - 0.6666667 * min(c, vec3_splat(1.0)));
}

// ACES RRT+ODT fit by Stephen Hill, from Baking Lab (ACES.hlsl).
// Copyright (c) 2016 MJP, MIT License; see ../../THIRD_PARTY.md.
vec3 fxAcesFit(vec3 c)
{
    vec3 v = vec3(dot(vec3(0.59719, 0.35458, 0.04823), c), dot(vec3(0.07600, 0.90834, 0.01566), c),
                  dot(vec3(0.02840, 0.13383, 0.83777), c));
    vec3 a = v * (v + vec3_splat(0.0245786)) - vec3_splat(0.000090537);
    vec3 b = v * (0.983729 * v + vec3_splat(0.4329510)) + vec3_splat(0.238081);
    v = a / b;
    return clamp(vec3(dot(vec3(1.60475, -0.53108, -0.07367), v), dot(vec3(-0.10208, 1.10813, -0.00605), v),
                      dot(vec3(-0.00327, -0.07276, 1.07602), v)),
                 0.0, 1.0);
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
